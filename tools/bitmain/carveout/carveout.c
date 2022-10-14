#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "../../../include/uapi/linux/dma-buf.h"
#include "../../../include/uapi/linux/dma-heap.h"

#define DEVPATH "/dev/dma_heap/carveout_npu"
#define ONE_MEG (1 * 1024 * 1024)

static int dmabuf_heap_alloc_fdflags(int fd, size_t len, unsigned int fd_flags,
				     unsigned int heap_flags, int *dmabuf_fd)
{
	struct dma_heap_allocation_data data = {
		.len = len,
		.fd = 0,
		.fd_flags = fd_flags,
		.heap_flags = heap_flags,
	};
	int ret;

	if (!dmabuf_fd)
		return -EINVAL;

	ret = ioctl(fd, DMA_HEAP_IOCTL_ALLOC, &data);
	if (ret < 0)
		return ret;
	*dmabuf_fd = (int)data.fd;
	return ret;
}

static int dmabuf_heap_alloc(int fd, size_t len, unsigned int flags,
			     int *dmabuf_fd)
{
	return dmabuf_heap_alloc_fdflags(fd, len, O_RDWR | O_CLOEXEC, flags,
					 dmabuf_fd);
}

static void dmabuf_sync(int fd, int start_stop)
{
	struct dma_buf_sync sync = {
		.flags = start_stop | DMA_BUF_SYNC_RW,
	};
	int ret;

	ret = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
	if (ret)
		printf("sync failed %d\n", errno);
}

int main(void)
{
	int heap_fd, dmabuf_fd = -1, ret;
	void *p = NULL;

	heap_fd = open(DEVPATH, O_RDWR);
	if (heap_fd < 0) {
		printf("failed to open carveout node: %d\n", errno);
		return -1;
	}

	ret = dmabuf_heap_alloc(heap_fd, ONE_MEG, 0, &dmabuf_fd);
	if (ret) {
		printf("failed to alloc from heap: %d\n", ret);
		ret = -1;
		goto out;
	}

	/* mmap and write a simple pattern */
	p = mmap(NULL,
		 ONE_MEG,
		 PROT_READ | PROT_WRITE,
		 MAP_SHARED,
		 dmabuf_fd,
		 0);
	if (p == MAP_FAILED) {
		printf("mmap() failed: %m\n");
		ret = -1;
		goto out;
	}
	printf("mmap passed\n");

	dmabuf_sync(dmabuf_fd, DMA_BUF_SYNC_START);
	memset(p, 1, ONE_MEG / 2);
	memset((char *)p + ONE_MEG / 2, 0, ONE_MEG / 2);
	dmabuf_sync(dmabuf_fd, DMA_BUF_SYNC_END);

	printf("memset passed\n");
	printf("press Enter to continue...\n");
	getchar();

out:
	if (p)
		munmap(p, ONE_MEG);
	if (dmabuf_fd >= 0)
		close(dmabuf_fd);
	if (heap_fd >= 0)
		close(heap_fd);
	return ret;
}
