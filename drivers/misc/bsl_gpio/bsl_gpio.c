#include <linux/module.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#define GPIO_NUM_MAX 40

#define GPIO_FUNCTION_OUTPUT_ACTIVE 0
#define GPIO_FUNCTION_INPUT 1
#define GPIO_FUNCTION_IRQ 2
#define GPIO_FUNCTION_FLASH 3
#define GPIO_FUNCTION_OUTPUT_INACTIVE 4
#define GPIO_FUNCTION_OUTPUT_RESET 5

static int flash_flag;

struct bsl_gpio {
	int gpio_num;		//gpui num
	int gpio_irq;
	int action;			//gpio flag
	int gpio_event;		//input only
	int send_mode;		//input only
	int gpio_function;	//gpio function,i/o
	int gpio_ctrl;
	char *gpio_name;
	u32 reset_time_ms[3];
	struct gpio_desc *gpiodesc;
};

struct bsl_gpio_data {
	struct bsl_gpio bsl_gpio_num[GPIO_NUM_MAX];
	struct input_dev *input;
	struct timer_list mytimer;
	int gpio_dts_num;
};

static struct bsl_gpio_data *gpio_data;
// static int open_now = 0;
static char *file_name;

struct workqueue_struct *g_ak_src_wq;
struct delayed_work g_ak_src_work;

static int gpio_open(struct inode *inode, struct file *file)
{
	return 0;
}


static ssize_t gpio_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	char buf[2] = {0};
	char s1[] = "1";

	int open_now = 0;
	struct dentry *dent = file->f_path.dentry;
	int i = 0;

	file_name = (char *)(dent->d_name.name);

	for (i = 0; i < gpio_data->gpio_dts_num; i++)
		if (!strcmp(file_name, gpio_data->bsl_gpio_num[i].gpio_name)) {
			open_now = i;
			break;
		}

	if (copy_from_user(&buf[0], buffer, 1)) {
		pr_err("failed to copy data to kernel space\n");
		return -EFAULT;
	}

	if (!strcmp(buf, s1)) {
		gpiod_set_value(gpio_data->bsl_gpio_num[open_now].gpiodesc, 1);
		pr_info("%s write 1 succeed\n", gpio_data->bsl_gpio_num[open_now].gpio_name);
	} else {
		gpiod_set_value(gpio_data->bsl_gpio_num[open_now].gpiodesc, 0);
		pr_info("%s write 0 succeed\n", gpio_data->bsl_gpio_num[open_now].gpio_name);
	}
	return count;
}


static ssize_t gpio_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	int gpio_val = 0;
	int len = 0;
	char s[10] = {0};

	int open_now = 0;
	struct dentry *dent = file->f_path.dentry;
	int i = 0;

	file_name = (char *)(dent->d_name.name);

	for (i = 0; i < gpio_data->gpio_dts_num; i++) {
		if (!strcmp(file_name, gpio_data->bsl_gpio_num[i].gpio_name)) {
			open_now = i;
			break;
		}
	}

	if (*ppos)
		return 0;

	gpio_val = gpiod_get_value(gpio_data->bsl_gpio_num[open_now].gpiodesc);
	pr_info("get %s value %d\n", gpio_data->bsl_gpio_num[open_now].gpio_name, gpio_val);

	len = sprintf(s+len, "%d\n", gpio_val);

	return simple_read_from_buffer(buffer, count, ppos, s, 2);
}

static const struct file_operations gpio_ops = {
	.open    = gpio_open,
	.read    = gpio_read,
	.write   = gpio_write,
};

static void send_event(struct timer_list *list)
{
	int gpio_value = 0;
	int i = 0;

	for (i = 0; i < gpio_data->gpio_dts_num; i++) {
		switch (gpio_data->bsl_gpio_num[i].gpio_function) {
		case GPIO_FUNCTION_INPUT:
			gpio_value  = gpiod_get_value(gpio_data->bsl_gpio_num[i].gpiodesc);

				if (gpio_value == 1) {
					input_report_key(gpio_data->input, gpio_data->bsl_gpio_num[i].gpio_event, 1);
					input_sync(gpio_data->input);
				}
				if (gpio_value == 0) {
					input_report_key(gpio_data->input, gpio_data->bsl_gpio_num[i].gpio_event, 0);
					input_sync(gpio_data->input);
				}

			//printk("\n%s gpio num %d  %d\n",__func__,i,gpio_value);
			//printk("\n%s send event %d\n",__func__,gpio_data->bsl_gpio_num[i].gpio_event);
			break;
		case GPIO_FUNCTION_FLASH:
			gpiod_set_value(gpio_data->bsl_gpio_num[i].gpiodesc, !flash_flag);
			flash_flag = !flash_flag;
			break;
		}
	}

	mod_timer(&(gpio_data->mytimer), jiffies + msecs_to_jiffies(2000));
}

static void ak_src_work(struct work_struct *work)
{
	int i = 0;
	int timeout = 0;

	while (timeout <= 30000) {
		for (i = 0; i < gpio_data->gpio_dts_num; i++) {
			switch (gpio_data->bsl_gpio_num[i].gpio_function) {
			case GPIO_FUNCTION_OUTPUT_RESET:
				if (timeout > gpio_data->bsl_gpio_num[i].reset_time_ms[2]) {
					if (gpiod_get_value(gpio_data->bsl_gpio_num[i].gpiodesc) == 1) {
						pr_info("reset %s gpio unassert\n",
							gpio_data->bsl_gpio_num[i].gpio_name);
						gpiod_set_value(gpio_data->bsl_gpio_num[i].gpiodesc, 0);
					}
				} else if (timeout > gpio_data->bsl_gpio_num[i].reset_time_ms[1]) {
					if (gpiod_get_value(gpio_data->bsl_gpio_num[i].gpiodesc) == 0) {
						pr_info("reset %s gpio assert\n",
							gpio_data->bsl_gpio_num[i].gpio_name);
						gpiod_set_value(gpio_data->bsl_gpio_num[i].gpiodesc, 1);
					}
				} else if (timeout > gpio_data->bsl_gpio_num[i].reset_time_ms[0]) {
					if (gpiod_get_value(gpio_data->bsl_gpio_num[i].gpiodesc) == 1) {
						pr_info("reset %s gpio pre_unassert\n",
							gpio_data->bsl_gpio_num[i].gpio_name);
						gpiod_set_value(gpio_data->bsl_gpio_num[i].gpiodesc, 0);
					}
				}
				break;
			}
		}
		timeout += 100;
		msleep(100);
	}
	pr_err("## bsl_gpio reset done\n");
}

static int bsl_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child_np;
	struct device *dev = &pdev->dev;
	static struct proc_dir_entry *root_entry_gpio;
	int ret = 0;
	int gpio_cnt = 0;
	char gpio_name_num[GPIO_NUM_MAX];
	int gpio_in_cnt = 0;
	int cnt = 0;

	gpio_data = devm_kzalloc(&pdev->dev, sizeof(struct bsl_gpio_data), GFP_KERNEL);
	if (!gpio_data) {
		// dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	gpio_data->gpio_dts_num = of_get_child_count(np);
	pr_info("bsl gpio prepare build %d gpio\n", gpio_data->gpio_dts_num);

	if (gpio_data->gpio_dts_num == 0)
		dev_info(&pdev->dev, "no gpio defined\n");

	/* create node */
	root_entry_gpio = proc_mkdir("bsl_gpio", NULL);

	for_each_child_of_node(np, child_np) {
		gpio_data->bsl_gpio_num[gpio_cnt].gpio_name = (char *)child_np->name;

		/* parse dts */
		gpio_data->bsl_gpio_num[gpio_cnt].gpiodesc = fwnode_get_named_gpiod(&child_np->fwnode,
			"gpio_num-gpio", 0, GPIOD_ASIS, gpio_data->bsl_gpio_num[gpio_cnt].gpio_name);
		if (IS_ERR(gpio_data->bsl_gpio_num[gpio_cnt].gpiodesc)) {
			dev_err(&pdev->dev, "failed to request GPIO %s: %ld\n",
				gpio_data->bsl_gpio_num[gpio_cnt].gpio_name,
				PTR_ERR(gpio_data->bsl_gpio_num[gpio_cnt].gpiodesc));
				continue;
		}

		gpio_data->bsl_gpio_num[gpio_cnt].gpio_ctrl = gpio_cnt;
		of_property_read_u32(child_np, "gpio_function", &(gpio_data->bsl_gpio_num[gpio_cnt].gpio_function));

		pr_info("bsl gpio request %s with function %d\n",
			gpio_data->bsl_gpio_num[gpio_cnt].gpio_name,
			gpio_data->bsl_gpio_num[gpio_cnt].gpio_function);

		switch (gpio_data->bsl_gpio_num[gpio_cnt].gpio_function) {
		case GPIO_FUNCTION_INPUT:		/* init input gpio */
			gpiod_direction_input(gpio_data->bsl_gpio_num[gpio_cnt].gpiodesc);

			of_property_read_u32(child_np, "send_mode", &(gpio_data->bsl_gpio_num[gpio_cnt].send_mode));
			of_property_read_u32(child_np, "gpio_event", &(gpio_data->bsl_gpio_num[gpio_cnt].gpio_event));
			gpio_in_cnt++;
			break;

		case GPIO_FUNCTION_OUTPUT_ACTIVE:		/* init output gpio */
			gpiod_direction_output(gpio_data->bsl_gpio_num[gpio_cnt].gpiodesc, 1);
			break;

		case GPIO_FUNCTION_OUTPUT_RESET:		/* init output reset gpio */
			ret = of_property_read_u32_array(child_np, "reset_time_ms",
				gpio_data->bsl_gpio_num[gpio_cnt].reset_time_ms, 3);
			if (ret != 0) {
				pr_info("no reset_time_ms for %s\n",
					gpio_data->bsl_gpio_num[gpio_cnt].gpio_name);
				gpio_data->bsl_gpio_num[gpio_cnt].reset_time_ms[0] = 0;
				gpio_data->bsl_gpio_num[gpio_cnt].reset_time_ms[1] = 0;
				gpio_data->bsl_gpio_num[gpio_cnt].reset_time_ms[2] = 0;
				gpiod_direction_output(gpio_data->bsl_gpio_num[gpio_cnt].gpiodesc, 0);
			} else {
				pr_info("reset_time_ms for %s: %d, %d, %d\n",
					gpio_data->bsl_gpio_num[gpio_cnt].gpio_name,
					gpio_data->bsl_gpio_num[gpio_cnt].reset_time_ms[0],
					gpio_data->bsl_gpio_num[gpio_cnt].reset_time_ms[1],
					gpio_data->bsl_gpio_num[gpio_cnt].reset_time_ms[2]);
				gpiod_direction_output(gpio_data->bsl_gpio_num[gpio_cnt].gpiodesc, 1);
			}
			break;

		case GPIO_FUNCTION_OUTPUT_INACTIVE:		/* init output gpio */
			gpiod_direction_output(gpio_data->bsl_gpio_num[gpio_cnt].gpiodesc, 0);
			break;

		case GPIO_FUNCTION_FLASH:
			gpiod_direction_output(gpio_data->bsl_gpio_num[gpio_cnt].gpiodesc, 1);
			gpio_in_cnt++;
			break;
		}

		sprintf(gpio_name_num, gpio_data->bsl_gpio_num[gpio_cnt].gpio_name, gpio_cnt);
		proc_create(gpio_name_num, 0644, root_entry_gpio, &gpio_ops);
		gpio_cnt++;
	}

	if (gpio_in_cnt > 0) {
		/* init timer */
		/* old linux version timer api
		 * init_timer(&(gpio_data->mytimer));
		 * gpio_data->mytimer.expires = jiffies + msecs_to_jiffies(10000);
		 * gpio_data->mytimer.function = send_event;
		 * add_timer(&(gpio_data->mytimer));
		 */

		timer_setup(&(gpio_data->mytimer), send_event, 0);
		gpio_data->mytimer.expires = jiffies + msecs_to_jiffies(10000);
		add_timer(&(gpio_data->mytimer));


		/* init struct input_dev */
		gpio_data->input = devm_input_allocate_device(dev);
		gpio_data->input->name = "gpio_event";  /* pdev->name; */
		gpio_data->input->phys = "gpio_event/input1";
		gpio_data->input->dev.parent = dev;
		gpio_data->input->id.bustype = BUS_HOST;
		gpio_data->input->id.vendor = 0x0001;
		gpio_data->input->id.product = 0x0001;
		gpio_data->input->id.version = 0x0100;
		for (cnt = 0; cnt < gpio_cnt; cnt++)
			if (gpio_data->bsl_gpio_num[cnt].gpio_function == GPIO_FUNCTION_INPUT)
				input_set_capability(gpio_data->input, EV_KEY, gpio_data->bsl_gpio_num[cnt].gpio_event);

		ret = input_register_device(gpio_data->input);
	}

	platform_set_drvdata(pdev, gpio_data);

	g_ak_src_wq =  create_singlethread_workqueue("ak_src_wq");
	INIT_DELAYED_WORK(&g_ak_src_work, ak_src_work);
	queue_delayed_work(g_ak_src_wq, &g_ak_src_work, 0);
	return 0;
}

static int bsl_gpio_remove(struct platform_device *pdev)
{
	return 0;
}


static const struct of_device_id bsl_gpio_of_match[] = {
	{ .compatible = "bsl_gpio" },
	{ }
};

static struct platform_driver bsl_gpio_driver = {
	.probe = bsl_gpio_probe,
	.remove = bsl_gpio_remove,
	.driver = {
				.name           = "bsl_gpio",
				.of_match_table = of_match_ptr(bsl_gpio_of_match),
		},
};

module_platform_driver(bsl_gpio_driver);
MODULE_LICENSE("GPL");
