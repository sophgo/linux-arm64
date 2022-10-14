#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <asm/sysreg.h>
#include "bmcpu_k.h"

const static struct file_operations g_bmcpu_ops = {
	.owner   = THIS_MODULE,
	.read    = bmcpu_read,
	.write    = bmcpu_write,
	.open    = bmcpu_open,
	.release = bmcpu_release,
	.mmap    = bmcpu_mmap,
	.compat_ioctl = bmcpu_ioctl,
	.unlocked_ioctl = bmcpu_ioctl
};

static int g_bmcpu_dev_major;
static dev_t bmcpu_devno;
static struct cdev g_bmcpu_cdev;
static struct class *bmcpu_dev_class;
static struct device *bmcpu_device;

static int __init bmcpu_init(void)
{
	int ret;
	struct device_node *np;
	const char *bmcpu_enable;

	np = of_find_node_by_name(of_root, "bmcpu");
	if (!np) {
		pr_err("get bmcpu from of_root failed!\n");
		return -EINVAL;
	}

	if (!of_property_read_string(np, "status", &bmcpu_enable)) {
		if (!strcmp("okay", bmcpu_enable))
			pr_info("bmcpu enable in dts, start init /dev/bmcpu!\n");
		else
			return -EINVAL;
	}
	else
		return -EINVAL;

	if (g_bmcpu_dev_major == 0) {
		ret = alloc_chrdev_region(&bmcpu_devno, 0, 1, BMCPU_DEV_NAME);
		if (ret < 0) {
			pr_info("bmcpu: alloc_chrdev_region error! errno = %d\n", ret);
			return ret;
		}

		g_bmcpu_dev_major = MAJOR(bmcpu_devno);

		cdev_init(&g_bmcpu_cdev, &g_bmcpu_ops);

		g_bmcpu_cdev.owner = THIS_MODULE;
		g_bmcpu_cdev.ops = &g_bmcpu_ops;

		ret = cdev_add(&g_bmcpu_cdev, bmcpu_devno, 1);
		if (ret < 0) {
			pr_info("bmcpu: cdev_add error! errno = %d", ret);
			goto cdev_add_fail;
		}

		bmcpu_dev_class = class_create(THIS_MODULE, BMCPU_CLASS_NAME);
		if (IS_ERR(bmcpu_dev_class)) {
			pr_info("bmcpu: class_create error! errno = %ld", PTR_ERR(bmcpu_dev_class));
			ret = PTR_ERR(bmcpu_dev_class);
			goto class_create_fail;
		}

		bmcpu_device = device_create(bmcpu_dev_class, NULL, MKDEV(g_bmcpu_dev_major, 0), NULL, BMCPU_DEV_NAME);
		if (IS_ERR(bmcpu_device)) {
			pr_info("bmcpu: device_create error! errno = %ld", PTR_ERR(bmcpu_device));
			ret = PTR_ERR(bmcpu_device);
			goto device_create_fail;
		}

		sysreg_clear_set(sctlr_el1, SCTLR_EL1_UCI, 0);
		sysreg_clear_set(sctlr_el1, SCTLR_EL1_UCT, 0);

		pr_info("bmcpu module init success!\n");

		return 0;

device_create_fail:
		class_destroy(bmcpu_dev_class);
class_create_fail:
		cdev_del(&g_bmcpu_cdev);
cdev_add_fail:
		unregister_chrdev_region(bmcpu_devno, 1);
		g_bmcpu_dev_major = 0;

		return ret;

	} else {
		pr_info("bmcpu module already init!\n");
		return -EBUSY;
	}
}

static void bmcpu_exit(void)
{
	if (g_bmcpu_dev_major > 0) {
		device_destroy(bmcpu_dev_class, bmcpu_devno);
		class_destroy(bmcpu_dev_class);
		cdev_del(&g_bmcpu_cdev);
		unregister_chrdev_region(MKDEV(g_bmcpu_dev_major, 0), 1);
		g_bmcpu_dev_major = 0;
		pr_info("bmcpu module exit!\n");
	}
}

int bmcpu_open(struct inode *inode, struct file *filp)
{
	return 0;
}

int bmcpu_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t bmcpu_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	return 0;
}

ssize_t bmcpu_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	return 0;
}

long bmcpu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return 0;
}

int bmcpu_mmap(struct file *filp, struct vm_area_struct *vma)
{
	if (filp->f_flags & O_DSYNC)
		vma->vm_page_prot =  __pgprot_modify(vma->vm_page_prot, PTE_ATTRINDX_MASK,
											 PTE_ATTRINDX(MT_NORMAL_WT) |
											 PTE_PXN | PTE_UXN);
	else
		vma->vm_page_prot =  __pgprot_modify(vma->vm_page_prot, PTE_ATTRINDX_MASK,
											 PTE_ATTRINDX(MT_NORMAL) |
											 PTE_PXN | PTE_UXN);

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, vma->vm_end-vma->vm_start,
						vma->vm_page_prot) < 0)
		return -EAGAIN;

	return 0;
}

module_init(bmcpu_init);
module_exit(bmcpu_exit);

MODULE_AUTHOR("Da Yu");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_INFO(intree, "Y");
