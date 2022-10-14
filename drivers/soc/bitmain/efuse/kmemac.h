#ifndef __KMEMAC_H__
#define __KMEMAC_H__

#include <linux/list.h>

struct list_head *kmemac_add_protected_region(phys_addr_t base,
					      phys_addr_t size);

void kmemac_remove_protected_region(struct list_head *head);

#endif
