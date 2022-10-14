/**
 * memory access control
 *
 * memory regions added to this module may not be mapped to userland
 */

/* #define DEBUG */

#define pr_fmt(fmt) "kmemac: " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "kmemac.h"

struct kmemac_region {
	phys_addr_t base, size;
	struct list_head head;
};

static LIST_HEAD(region_list);
static DEFINE_SPINLOCK(list_lock);

struct list_head *kmemac_add_protected_region(phys_addr_t base,
					      phys_addr_t size)
{
	unsigned long flags;
	struct kmemac_region *region =
		kzalloc(sizeof(struct kmemac_region), GFP_KERNEL);

	if (!region)
		return NULL;

	INIT_LIST_HEAD(&region->head);
	region->base = base;
	region->size = size;

	spin_lock_irqsave(&list_lock, flags);
	list_add_tail(&region->head, &region_list);
	spin_unlock_irqrestore(&list_lock, flags);

	return &region->head;
}
EXPORT_SYMBOL_GPL(kmemac_add_protected_region);

void kmemac_remove_protected_region(struct list_head *head)
{
	unsigned long flags;

	if (!head)
		return;

	spin_lock_irqsave(&list_lock, flags);
	list_del(head);
	spin_unlock_irqrestore(&list_lock, flags);

	kfree(list_entry(head, struct kmemac_region, head));
}
EXPORT_SYMBOL_GPL(kmemac_remove_protected_region);

static int is_intersect(phys_addr_t b0, phys_addr_t s0,
			phys_addr_t b1, phys_addr_t s1)
{
	int result;
	phys_addr_t e0 = b0 + s0;
	phys_addr_t e1 = b1 + s1;

	if (e0 <= b1 || b0 >= e1)
		result = false;
	else
		result = true;

	pr_debug("[%016llx - %016llx] and [%016llx - %016llx] are %s\n",
		 b0, e0, b1, e1, result ? "intersected" : "not intersected");

	return result;
}

/* called by drivers/char/mem */
int phys_mem_access_prot_allowed(struct file *file,
				 unsigned long pfn, unsigned long size,
				 pgprot_t *vma_prot)
{
	unsigned long flags;
	struct kmemac_region *region;
	int err;

	err = true;

	spin_lock_irqsave(&list_lock, flags);

	if (list_empty(&region_list))
		goto out;

	list_for_each_entry(region, &region_list, head) {
		if (is_intersect(pfn << PAGE_SHIFT, size,
				 region->base, region->size)) {
			err = false;
			goto out;
		}
	}

out:
	spin_unlock_irqrestore(&list_lock, flags);
	return err;
}
