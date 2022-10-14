#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "../../../drivers/soc/bitmain/uapi/ion.h"
#include "../../../drivers/soc/bitmain/uapi/ion_bitmain.h"

#define TEST_SIZE  (128 * 1024 * 1024UL) //((4 * 1024 * 1024 - 0) * 1024UL)

int main(int argc, char const *argv[])
{
	int fd, ret = 0, *p;
	unsigned int heap_id;
	struct ion_heap_query query;
	struct ion_heap_data heap_data[ION_NUM_HEAP_IDS];
	struct ion_allocation_data test_alloc;
	struct ion_custom_data custom_data;
	struct bitmain_heap_info heap_info;
	struct bitmain_cache_range cache_range;

	fd = open("/dev/ion", O_RDONLY);
	if (fd < 0) {
		printf("open ION dev failed %d\n", errno);
		return -1;
	}

	memset(&query, 0, sizeof(query));
	query.cnt = ION_NUM_HEAP_IDS;
	query.heaps = (unsigned long)&heap_data[0];
	ret = ioctl(fd, ION_IOC_HEAP_QUERY, &query);
	if (ret != 0) {
		printf("ion query failed %d %d\n", ret, errno);
		return ret;
	}

	for (int i = 0; i < query.cnt; i++) {
		custom_data.cmd = ION_IOC_BITMAIN_GET_HEAP_INFO;
		custom_data.arg = (unsigned long)&heap_info;
		heap_info.id = i;
		ret = ioctl(fd, ION_IOC_CUSTOM, &custom_data);
		if (ret < 0) {
			printf("get heap info failed %d %d\n", ret, errno);
			goto out1;
		}
		printf("heap %d: total %ld available %ld\n",
			heap_info.id, heap_info.total_size, heap_info.avail_size);
	}

	heap_id = ION_NUM_HEAP_IDS + 1;
	for (int i = 0; i < query.cnt; i++) {
		if (heap_data[i].type == ION_HEAP_TYPE_CARVEOUT) {
			heap_id = heap_data[i].heap_id;
			break;
		}
	}
	if (heap_id > ION_NUM_HEAP_IDS) {
		printf("carveout heap not found\n");
		return -2;
	}
	printf("carveout heap id %d\n", heap_id);

	printf("alloc for 0x%lx bytes\n", TEST_SIZE);
	test_alloc.len = TEST_SIZE;
	test_alloc.heap_id_mask = (1 << heap_id);
	test_alloc.flags = 0; // ION_FLAG_CACHED;
	ret = ioctl(fd, ION_IOC_ALLOC, &test_alloc);
	if (ret < 0) {
		printf("carveout alloc failed %d %d\n", ret, errno);
		goto out;
	}

	printf("mapping paddr 0x%llx\n", test_alloc.paddr);
	p = mmap(NULL, TEST_SIZE, PROT_WRITE, MAP_SHARED, test_alloc.fd, 0);
	if (p == MAP_FAILED) {
		printf("carveout mmap failed %d\n", errno);
		goto out1;
	}
	printf("got vaddr %p\n", p);

	printf("original: %08x %08x %08x %08x\n", *p, *(p+1), *(p+2), *(p+3));
	*p = 0x12812800;
	printf("change to:%08x %08x %08x %08x\n", *p, *(p+1), *(p+2), *(p+3));

	custom_data.cmd = ION_IOC_BITMAIN_FLUSH_RANGE;
	custom_data.arg = (unsigned long)&cache_range;
	cache_range.start = p;
	cache_range.size = 16;
	ret = ioctl(fd, ION_IOC_CUSTOM, &custom_data);
	if (ret < 0) {
		printf("flush range failed %d %d\n", ret, errno);
		goto out1;
	}
	printf("flushed cache\n");
	printf("now use JTAG to check addr 0x%llx\n", test_alloc.paddr);
	sleep(10);

	printf("now use JTAG to change addr 0x%llx\n", test_alloc.paddr);
	sleep(10);
	custom_data.cmd = ION_IOC_BITMAIN_INVALIDATE_RANGE;
	custom_data.arg = (unsigned long)&cache_range;
	cache_range.start = p;
	cache_range.size = 16;
	ret = ioctl(fd, ION_IOC_CUSTOM, &custom_data);
	if (ret < 0) {
		printf("invalidate range failed %d %d\n", ret, errno);
		goto out1;
	}
	printf("invalidated cache\n");
	printf("we got: %08x %08x %08x %08x\n", *p, *(p+1), *(p+2), *(p+3));

	munmap(p, TEST_SIZE);
out1:
	close(test_alloc.fd);
out:
	close(fd);
	return ret;
}
