#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/hwspinlock.h>
#include <linux/cpufreq.h>

struct bm1684_cooling_device {
	struct thermal_cooling_device *cdev;
	struct mutex lock; /* lock to protect the content of this struct */
	unsigned long clock_state;
	unsigned long max_clock_state;
	struct clk *tpu_div0; // from tpll
	struct clk *tpu_div1; // from fpll
	struct clk *tpu_mux;
	struct clk *tpu_clk;
	struct clk *cpu_clk;
	unsigned long tpu_init_rate;
	struct hwspinlock *hwlock;
};

#define MAX_TPU_CLK 550000000UL			// 550M
#define MIN_TPU_CLK 75000000UL			// 75M
#define MAX_CPU_CLK 2300000000UL		// 2.3G
#define MIN_CPU_CLK 1150000000UL		// 1.15G
#define HWLOCK_TPU_TIMEOUT 200			// 200ms

/* cooling device thermal callback functions are defined below */

/**
 * bm1684_cooling_get_max_state - callback function to get the max cooling state.
 * @cdev: thermal cooling device pointer.
 * @state: fill this variable with the max cooling state.
 *
 * Callback for the thermal cooling device to return the
 * max cooling state.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int bm1684_cooling_get_max_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct bm1684_cooling_device *bmcdev = cdev->devdata;

	mutex_lock(&bmcdev->lock);
	*state = bmcdev->max_clock_state;
	mutex_unlock(&bmcdev->lock);

	return 0;
}

/**
 * bm1684_cooling_get_cur_state - function to get the current cooling state.
 * @cdev: thermal cooling device pointer.
 * @state: fill this variable with the current cooling state.
 *
 * Callback for the thermal cooling device to return the current cooling
 * state.
 *
 * Return: 0 (success)
 */
static int bm1684_cooling_get_cur_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct bm1684_cooling_device *bmcdev = cdev->devdata;

	mutex_lock(&bmcdev->lock);
	*state = bmcdev->clock_state;
	mutex_unlock(&bmcdev->lock);

	return 0;
}

/**
 * bm1684_cooling_set_cur_state - function to set the current cooling state.
 * @cdev: thermal cooling device pointer.
 * @state: set this variable to the current cooling state.
 *
 * Callback for the thermal cooling device to change the bm1684 cooling
 * current cooling state.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int bm1684_cooling_set_cur_state(struct thermal_cooling_device *cdev,
					unsigned long state)
{
	struct bm1684_cooling_device *bmcdev = cdev->devdata;
	int ret = 0;

	mutex_lock(&bmcdev->lock);

	if (state > bmcdev->max_clock_state) {
		ret = -EINVAL;
		goto out;
	}

	ret =  hwspin_lock_timeout(bmcdev->hwlock, HWLOCK_TPU_TIMEOUT);
	if (ret)
		goto out;

	switch (state) {
	case 0:
		ret = clk_set_rate(bmcdev->tpu_clk, bmcdev->tpu_init_rate);
		ret |= clk_set_rate(bmcdev->cpu_clk, MAX_CPU_CLK);
		break;
	case 1:
		ret = clk_set_rate(bmcdev->tpu_clk, (bmcdev->tpu_init_rate * 4)/5); // 80% of max performance
		ret |= clk_set_rate(bmcdev->cpu_clk, MIN_CPU_CLK);
		break;
	case 2:
		ret = clk_set_rate(bmcdev->tpu_clk, MIN_TPU_CLK);
		break;
	}

	hwspin_unlock(bmcdev->hwlock);
	if (ret < 0)
		goto out;

	dev_info(&cdev->device, "bm1684 cooling device set state=%ld rate=%ld/%ld\n",
				 state, clk_get_rate(bmcdev->tpu_clk),
				 clk_get_rate(bmcdev->cpu_clk));
	bmcdev->clock_state = state;

out:
	mutex_unlock(&bmcdev->lock);
	return ret;
}

/* Bind clock callbacks to thermal cooling device ops */
static struct thermal_cooling_device_ops const bm1684_cooling_ops = {
	.get_max_state = bm1684_cooling_get_max_state,
	.get_cur_state = bm1684_cooling_get_cur_state,
	.set_cur_state = bm1684_cooling_set_cur_state,
};

static struct bm1684_cooling_device *
bm1684_cooling_device_register(struct device *dev)
{
	struct thermal_cooling_device *cdev;
	struct bm1684_cooling_device *bmcdev = NULL;
	int hwlock_id;

	bmcdev = devm_kzalloc(dev, sizeof(*bmcdev), GFP_KERNEL);
	if (!bmcdev)
		return ERR_PTR(-ENOMEM);

	mutex_init(&bmcdev->lock);

	bmcdev->tpu_div0 = __clk_lookup("clk_div_tpu_0");
	bmcdev->tpu_div1 = __clk_lookup("clk_div_tpu_1");
	bmcdev->tpu_mux = __clk_lookup("clk_mux_tpu");
	bmcdev->tpu_clk = __clk_lookup("tpll_clock");
	bmcdev->cpu_clk = __clk_lookup("clk_div_a53_1");

	if (!bmcdev->tpu_div0 || !bmcdev->tpu_div1 || !bmcdev->tpu_mux ||
		!bmcdev->tpu_clk || !bmcdev->cpu_clk) {
		dev_err(dev, "failed to get all clocks\n");
		return ERR_PTR(-ENOTSUPP);
	}

	/**
	 * In the process of switching frequency and switching mux(clk_set_rate, clk_set_parent),
	 * there will be a moment to write illegal values to the registers of configured mux,
	 * although this wrong value will be corrected by CCF framework later,
	 * multiple switching will cause TPU to stop working due to unavailability of clock,
	 * turning on tpu_div0 and tpu_div1 in advance can avoid writing illegal values to the mux registers.
	 */
	clk_prepare(bmcdev->tpu_div0);
	clk_prepare(bmcdev->tpu_div1);
	clk_enable(bmcdev->tpu_div0);
	clk_enable(bmcdev->tpu_div1);

	bmcdev->tpu_init_rate = clk_get_rate(bmcdev->tpu_clk);
	dev_info(dev, "TPU init rate %ld\n", bmcdev->tpu_init_rate);

	bmcdev->clock_state = 0;
	bmcdev->max_clock_state = 2;

	hwlock_id = of_hwspin_lock_get_id(dev->of_node, 0);
	if (hwlock_id < 0) {
		if (hwlock_id != -EPROBE_DEFER)
			dev_err(dev, "failed to retrieve hwlock\n");
		return ERR_PTR(hwlock_id);
	}

	bmcdev->hwlock = hwspin_lock_request_specific(hwlock_id);
	if (!bmcdev->hwlock)
		return ERR_PTR(-ENXIO);

	cdev = thermal_of_cooling_device_register(dev->of_node,
			"bm1684_cooling", bmcdev,
			&bm1684_cooling_ops);
	if (IS_ERR(cdev))
		return ERR_PTR(-EINVAL);

	bmcdev->cdev = cdev;

	return bmcdev;
}

static void bm1684_cooling_device_unregister(struct bm1684_cooling_device
		*bmcdev)
{
	hwspin_lock_free(bmcdev->hwlock);
	thermal_cooling_device_unregister(bmcdev->cdev);
}

static int bm1684_cooling_probe(struct platform_device *pdev)
{
	struct bm1684_cooling_device *bmcdev;

	bmcdev = bm1684_cooling_device_register(&pdev->dev);
	if (IS_ERR(bmcdev)) {
		int ret = PTR_ERR(bmcdev);

		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"Failed to register cooling device %d\n",
				ret);

		return ret;
	}

	platform_set_drvdata(pdev, bmcdev);
	dev_info(&pdev->dev, "Cooling device registered: %s\n", bmcdev->cdev->type);

	return 0;
}

static int bm1684_cooling_remove(struct platform_device *pdev)
{
	struct bm1684_cooling_device *bmcdev = platform_get_drvdata(pdev);

	if (!IS_ERR(bmcdev))
		bm1684_cooling_device_unregister(bmcdev);

	return 0;
}

static const struct of_device_id bm1684_cooling_match[] = {
	{ .compatible = "bitmain,bm1684-cooling" },
	{},
};
MODULE_DEVICE_TABLE(of, bm1684_cooling_match);

static struct platform_driver bm1684_cooling_driver = {
	.driver = {
		.name = "bm1684-cooling",
		.of_match_table = of_match_ptr(bm1684_cooling_match),
	},
	.probe = bm1684_cooling_probe,
	.remove = bm1684_cooling_remove,
};

module_platform_driver(bm1684_cooling_driver);

MODULE_AUTHOR("yaxun.chen@bitmain.com");
MODULE_DESCRIPTION("BM1684 cooling driver");
MODULE_LICENSE("GPL");
