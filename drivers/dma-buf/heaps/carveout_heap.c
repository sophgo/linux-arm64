// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF CMA heap exporter
 *
 * Copyright (C) 2012, 2019 Linaro Ltd.
 * Author: <benjamin.gaignard@linaro.org> for ST-Ericsson.
 */

#include <linux/cma.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-contiguous.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/genalloc.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/sched/signal.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/rbtree.h>

#include "heap-helpers.h"

struct carveout_heap {
	unsigned int id;
	const char *name;
	phys_addr_t base;
	size_t size;
	struct gen_pool *pool;
	struct dma_heap *dheap;
	struct platform_device *pdev;
#ifdef CONFIG_DMABUF_HEAPS_CARVEOUT_DEBUG
	struct rb_root buffers;
	size_t alloced;
	struct mutex buffer_lock;
	struct dentry *debugfs_dir;
#endif
};

struct carveout_buffer {
	struct sg_table *table;
	phys_addr_t paddr;
	size_t size;
	struct carveout_heap *heap;
#ifdef CONFIG_DMABUF_HEAPS_CARVEOUT_DEBUG
	char *alloced_by;
	struct rb_node node;
#endif
};

struct carveout_list {
	struct carveout_heap *heaps; // if heap is not usable, set its pdev to NULL
	int heap_num;
	struct platform_device *pdev;
#ifdef CONFIG_DMABUF_HEAPS_CARVEOUT_DEBUG
	struct dentry *debugfs_root;
#endif
};

#ifdef CONFIG_DMABUF_HEAPS_CARVEOUT_DEBUG
/* these functions should only be called while carveout_heap->buffer_lock is held */
static void carveout_buffer_add(struct carveout_buffer *buffer)
{
	struct rb_node **p = &buffer->heap->buffers.rb_node;
	struct rb_node *parent = NULL;
	struct carveout_buffer *entry;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct carveout_buffer, node);

		if (buffer < entry) {
			p = &(*p)->rb_left;
		} else if (buffer > entry) {
			p = &(*p)->rb_right;
		} else {
			pr_err("%s: buffer already found.", __func__);
			WARN_ON(1);
		}
	}

	rb_link_node(&buffer->node, parent, p);
	rb_insert_color(&buffer->node, &buffer->heap->buffers);

	buffer->heap->alloced += buffer->size;
	buffer->alloced_by = current->comm;
}

static void carveout_buffer_remove(struct carveout_buffer *buffer)
{
	rb_erase(&buffer->node, &buffer->heap->buffers);
	buffer->heap->alloced -= buffer->size;
}
#endif

static void carveout_heap_free(struct heap_helper_buffer *helper_buffer)
{
	struct carveout_buffer *buffer = (struct carveout_buffer *)helper_buffer->priv_virt;

#ifdef CONFIG_DMABUF_HEAPS_CARVEOUT_DEBUG
	mutex_lock(&buffer->heap->buffer_lock);
	carveout_buffer_remove(buffer);
	mutex_unlock(&buffer->heap->buffer_lock);
#endif
	gen_pool_free(buffer->heap->pool, buffer->paddr, buffer->size);
	sg_free_table(buffer->table);
	kfree(buffer->table);
	kfree(buffer);
	pr_info("%s free %pa size %ld\n", buffer->heap->name, &buffer->paddr, (unsigned long)buffer->size);
	kfree(helper_buffer->pages);
	kfree(helper_buffer);
}

static int carveout_heap_allocate(struct dma_heap *dheap,
				  unsigned long len,
				  unsigned long fd_flags,
				  unsigned long heap_flags)
{
	struct carveout_heap *heap = (struct carveout_heap *)dma_heap_get_drvdata(dheap);
	struct carveout_buffer *buffer = NULL;
	struct heap_helper_buffer *helper_buffer = NULL;
	struct sg_table *table = NULL;
	size_t size = PAGE_ALIGN(len);
	unsigned long nr_pages = size >> PAGE_SHIFT;
	struct dma_buf *dmabuf;
	phys_addr_t paddr;
	struct scatterlist *sg;
	struct page **tmp;
	int ret, i, j;

	helper_buffer = kzalloc(sizeof(*helper_buffer), GFP_KERNEL);
	if (!helper_buffer)
		goto err_kmem_out;

	init_heap_helper_buffer(helper_buffer, carveout_heap_free);
	helper_buffer->heap = dheap;
	helper_buffer->size = len;
	helper_buffer->pagecount = nr_pages;
	helper_buffer->pages = kmalloc_array(helper_buffer->pagecount,
					     sizeof(*helper_buffer->pages),
					     GFP_KERNEL);
	if (!helper_buffer->pages)
		goto err_kmem_out;
	tmp = helper_buffer->pages;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		goto err_kmem_out;

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		goto err_kmem_out;

	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret)
		goto err_kmem_out;

	paddr = gen_pool_alloc(heap->pool, size);
	if (!paddr)
		goto err_pool_out;

	sg_set_page(table->sgl, pfn_to_page(PFN_DOWN(paddr)), size, 0);

	buffer->heap = heap;
	buffer->paddr = paddr;
	buffer->size = size;
	buffer->table = table;

	// get pages
	for_each_sg(table->sgl, sg, table->nents, i) {
		int npages_this_entry = PAGE_ALIGN(sg->length) / PAGE_SIZE;
		struct page *page = sg_page(sg);

		WARN_ON(i >= nr_pages);
		for (j = 0; j < npages_this_entry; j++)
			*(tmp++) = page++;
	}

	// create dmabuf
	dmabuf = heap_helper_export_dmabuf(helper_buffer, fd_flags);
	if (IS_ERR(dmabuf))
		goto err_pool_out;

	helper_buffer->dmabuf = dmabuf;
	helper_buffer->priv_virt = buffer;

	ret = dma_buf_fd(dmabuf, fd_flags);
	if (ret < 0) {
		dma_buf_put(dmabuf);
		/* just return, as put will call release and that will free */
		return ret;
	}

#ifdef CONFIG_DMABUF_HEAPS_CARVEOUT_DEBUG
	mutex_lock(&heap->buffer_lock);
	carveout_buffer_add(buffer);
	mutex_unlock(&heap->buffer_lock);
#endif
	pr_info("%s alloc %pa size %ld\n", heap->name, &paddr, (unsigned long)size);
	return ret;

err_pool_out:
	sg_free_table(table);
err_kmem_out:
	kfree(helper_buffer->pages);
	kfree(helper_buffer);
	kfree(buffer);
	kfree(table);
	return -ENOMEM;
}

static const struct dma_heap_ops carveout_heap_ops = {
	.allocate = carveout_heap_allocate,
};

static int __add_carveout_heap(struct carveout_heap *heap)
{
	struct dma_heap_export_info exp_info;

	exp_info.name = heap->name;
	exp_info.ops = &carveout_heap_ops;
	exp_info.priv = heap;

	heap->dheap = dma_heap_add(&exp_info);
	if (IS_ERR(heap->dheap))
		return PTR_ERR(heap->dheap);

	return 0;
}

static void add_carveout_heaps(struct carveout_list *heap_list)
{
	int i, ret;

	for (i = 0; i < heap_list->heap_num; i++) {
		if (heap_list->heaps[i].pdev == NULL)
			continue;

		heap_list->heaps[i].pool = gen_pool_create(PAGE_SHIFT, -1);
		if (!heap_list->heaps[i].pool)
			continue;

		ret = gen_pool_add(heap_list->heaps[i].pool, heap_list->heaps[i].base, heap_list->heaps[i].size, -1);
		if (ret != 0)
			continue;

		ret = __add_carveout_heap(heap_list->heaps + i);
		pr_info("carveout heap registered %s %d\n", heap_list->heaps[i].name, ret);
	}
}

#ifdef CONFIG_DMABUF_HEAPS_CARVEOUT_DEBUG
static int debugfs_carveout_summary_show(struct seq_file *s, void *unused)
{
	struct carveout_heap *heap = s->private;
	struct carveout_buffer *pos, *n;
	size_t total_size = heap->size;
	size_t alloc_size = heap->alloced;
	int usage_rate = 0;

	seq_puts(s, "Summary:\n");

	usage_rate = alloc_size * 100 / total_size;
	if ((alloc_size * 100) % total_size)
		usage_rate += 1;

	seq_printf(s, "%s size:%lu bytes, used:%lu bytes, usage rate: %d%%\n",
		   heap->name, total_size, alloc_size, usage_rate);

	seq_printf(s, "\nDetails:\n%16s %16s %16s\n",
		   "size", "paddr", "by");
	mutex_lock(&heap->buffer_lock);
	rbtree_postorder_for_each_entry_safe(pos, n, &heap->buffers, node) {
		seq_printf(s, "%16zu %16llx %16s\n",
			   pos->size, pos->paddr, pos->alloced_by);
	}
	mutex_unlock(&heap->buffer_lock);
	seq_puts(s, "\n");

	return 0;
}

static int debugfs_carveout_open(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_carveout_summary_show, inode->i_private);
}

static const struct file_operations debugfs_carveout_summary_fops = {
	.open = debugfs_carveout_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void add_debugfs_node(struct carveout_list *heap_list)
{
	struct dentry *debug_file;
	int i;

	heap_list->debugfs_root = debugfs_create_dir("carveout", NULL);
	if (!heap_list->debugfs_root) {
		pr_err("carveout heap failed to create debugfs root directory\n");
		return;
	}

	for (i = 0; i < heap_list->heap_num; i++) {
		if (heap_list->heaps[i].pdev == NULL)
			continue;

		heap_list->heaps[i].debugfs_dir = debugfs_create_dir(heap_list->heaps[i].name,
								     heap_list->debugfs_root);
		if (!heap_list->heaps[i].debugfs_dir) {
			pr_err("%s failed to create debugfs directory\n", heap_list->heaps[i].name);
			continue;
		}

		debug_file = debugfs_create_file("summary", 0644,
						 heap_list->heaps[i].debugfs_dir,
						 &heap_list->heaps[i],
						 &debugfs_carveout_summary_fops);
		if (!debug_file)
			pr_err("%s failed to create debugfs summary node\n", heap_list->heaps[i].name);
	}
}
#endif

static int carveout_heap_probe(struct platform_device *pdev)
{
	const struct device_node *dt_node = pdev->dev.of_node;
	int i = 0, heap_num = of_get_available_child_count(dt_node);
	struct device_node *node;
	struct carveout_list *heap_list;

	heap_list = devm_kzalloc(&pdev->dev, sizeof(struct carveout_list), GFP_KERNEL);
	if (!heap_list)
		return -ENOMEM;

	heap_list->pdev = pdev;
	heap_list->heap_num = heap_num;
	pdev->dev.platform_data = heap_list;

	heap_list->heaps = devm_kzalloc(&pdev->dev, sizeof(struct carveout_heap) * heap_num, GFP_KERNEL);
	if (!heap_list->heaps) {
		kfree(heap_list);
		return -ENOMEM;
	}

	for_each_available_child_of_node(dt_node, node) {
		struct platform_device *heap_pdev;

		heap_pdev = of_platform_device_create(node, node->full_name, &pdev->dev);
		if (!heap_pdev)
			continue;

		heap_pdev->dev.platform_data = &heap_list->heaps[i];
		heap_list->heaps[i].pdev = heap_pdev;
		heap_list->heaps[i].name = dev_name(&heap_pdev->dev);
#ifdef CONFIG_DMABUF_HEAPS_CARVEOUT_DEBUG
		heap_list->heaps[i].buffers = RB_ROOT;
		mutex_init(&heap_list->heaps[i].buffer_lock);
#endif

		of_reserved_mem_device_init(&heap_pdev->dev);
		if (heap_list->heaps[i].base == 0 || heap_list->heaps[i].size == 0) {
			of_device_unregister(heap_pdev);
			heap_list->heaps[i].pdev = NULL;
			continue;
		}
		pr_info("carveout heap created device %s\n", dev_name(&heap_pdev->dev));
		i++;
	}

	add_carveout_heaps(heap_list);
#ifdef CONFIG_DMABUF_HEAPS_CARVEOUT_DEBUG
	add_debugfs_node(heap_list);
#endif
	return 0;
}

static int carveout_heap_remove(struct platform_device *pdev)
{
	// unexpected
	return 0;
}

static const struct of_device_id carveout_heap_match_table[] = {
	{.compatible = "bitmain,carveout_heap"},
	{},
};

static struct platform_driver carveout_heap_driver = {
	.probe = carveout_heap_probe,
	.remove = carveout_heap_remove,
	.driver = {
		.name = "carveout-heap",
		.of_match_table = carveout_heap_match_table,
	},
};

static int __init carveout_heap_init(void)
{
	return platform_driver_register(&carveout_heap_driver);
}

subsys_initcall(carveout_heap_init);

static int caveout_heap_rmem_device_init(struct reserved_mem *rmem, struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct carveout_heap *heap = pdev->dev.platform_data;

	heap->base = rmem->base;
	heap->size = rmem->size;
	pr_info("carveout heap %s got %pa size %pa\n",
		heap->name, &rmem->base, &rmem->size);
	return 0;
}

static void caveout_heap_rmem_device_release(struct reserved_mem *rmem, struct device *dev)
{
	// unexpected
}

static const struct reserved_mem_ops rmem_dev_ops = {
	.device_init	= caveout_heap_rmem_device_init,
	.device_release	= caveout_heap_rmem_device_release,
};

static int __init caveout_heap_rmem_setup(struct reserved_mem *rmem)
{
	phys_addr_t size = rmem->size;

	size = size / 1024 / 1024;

	pr_info("carveout heap reserved memory at %pa size %ld MiB\n",
		&rmem->base, (unsigned long)size);
	rmem->ops = &rmem_dev_ops;
	return 0;
}

RESERVEDMEM_OF_DECLARE(npu, "npu-reserved", caveout_heap_rmem_setup);

MODULE_AUTHOR("xiao.wang@bitmain.com");
MODULE_DESCRIPTION("DMA-BUF Carveout Heap");
MODULE_LICENSE("GPL v2");
