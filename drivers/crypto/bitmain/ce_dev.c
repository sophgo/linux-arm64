#include <ce_inst.h>
#include <ce_cipher.h>
#include <ce_hash.h>
#include <ce_base.h>

static struct ce_inst *inst;

static const struct alg_attribute ce_ver_attr[] = {
	[0] = {
		{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
		{1, 1, 0},
		16,
		16,
	},
	[1] = {
		{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
		{1, 1, 1},
		15,
		15,
	},
};

static const struct of_device_id ce_match[] = {
	{ .compatible = "bitmain,crypto-engine", .data = &ce_ver_attr[0]},
	{ .compatible = "bitmain,spacc", .data = &ce_ver_attr[0]},
	{ .compatible = "bitmain,ce", .data = &ce_ver_attr[0]},
	{ .compatible = "bitmain,crypto-engine,v2", .data = &ce_ver_attr[1]},
	{ },
};

int ce_enqueue_request(struct crypto_async_request *req)
{
	return ce_inst_enqueue_request(inst, req);
}

static int ce_probe(struct platform_device *dev)
{
	int err = -EINVAL;
	struct device *ce_dev = &dev->dev;
	struct device_node *np = ce_dev->of_node;
	const struct of_device_id *dev_id;
	struct alg_attribute *alg_attribute;

	dev_id = of_match_node(ce_match, np);
	alg_attribute = (struct alg_attribute *)dev_id->data;
	err = ce_inst_new(&inst, dev, alg_attribute);

	if (err)
		return err;

	err = register_all_alg(inst->alg_attribute->cipher_map) |
		register_all_hash(inst->alg_attribute->hash_map) |
		register_all_base(&dev->dev);
	if (err) {
		ce_err("register algrithm failed\n");
		goto err_nop;
	}
	ce_info("bitmain crypto engine prob success\n");
	return 0;
err_nop:
	return err;
}
static int ce_remove(struct platform_device *dev)
{
	unregister_all_alg(inst->alg_attribute->cipher_map);
	unregister_all_hash(inst->alg_attribute->hash_map);
	unregister_all_base(&dev->dev);
	return 0;
}
static void ce_shutdown(struct platform_device *dev)
{
}
static int ce_suspend(struct device *dev)
{
	return 0;
}
static int ce_resume(struct device *dev)
{
	return 0;
}


static SIMPLE_DEV_PM_OPS(ce_pm, ce_suspend, ce_resume);
static struct platform_driver ce_driver = {
	.probe		= ce_probe,
	.remove		= ce_remove,
	.shutdown	= ce_shutdown,
	.driver		= {
		.name		= "bce",
		.of_match_table	= ce_match,
		.pm		= &ce_pm,
	},
};

module_platform_driver(ce_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("<chao.wei@bitmain.com>");
MODULE_DESCRIPTION("Bitmain Crypto Engine");
MODULE_VERSION("0.1");
