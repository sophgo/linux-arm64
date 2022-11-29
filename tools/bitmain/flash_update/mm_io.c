#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "bm_spi.h"
#include "dbg_i2c.h"
#include "mm_io.h"

#define NOTICE(...)	printf("NOTICE:  " __VA_ARGS__)
#define INFO(...)	printf("INFO:  " __VA_ARGS__)
#define ERROR(...)	printf("ERROR:  " __VA_ARGS__)
#define VERBOSE(...)

static int devmem_fd;


void *devm_map(uint64_t addr, int len)
{
	off_t offset;
	void *map_base = NULL;

	devmem_fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (devmem_fd == -1) {
		ERROR("cannot open '/dev/mem' (-%d)\n", errno);
		goto open_err;
	}

	offset = addr & ~(sysconf(_SC_PAGE_SIZE) - 1);

	map_base = mmap(NULL, len + addr - offset, PROT_READ | PROT_WRITE,
	MAP_SHARED, devmem_fd, offset);
	if (map_base == MAP_FAILED) {
		ERROR("mmap 0x%lx failed\n", addr);
		goto mmap_err;
	}

	printf("%p mapped at address %p\n", (void *)addr, map_base);

mmap_err:
	close(devmem_fd);
open_err:
	return map_base;
}

void devm_unmap(void *virt_addr, int len)
{
	unsigned long addr;

	if (devmem_fd == -1) {
		ERROR(" '/dev/mem' is closed\n");
		return;
	}

	/* page align */
	addr = (((unsigned long)virt_addr) & ~(sysconf(_SC_PAGE_SIZE) - 1));
	munmap((void *)addr, len + (unsigned long)virt_addr - addr);
	close(devmem_fd);
	printf("unmapped %p\n", virt_addr);
}

void *mmio_init(uint64_t addr)
{
	return devm_map(addr, 0x30);
}

void mmio_relase(void *virt_addr)
{
	return devm_unmap((void *)virt_addr, 0x30);
}

uint32_t mmio_read_32(uint64_t addr)
{
	uint32_t val = *(uint32_t *)addr;

	VERBOSE("0x%lx <- 0x%x\n", addr, val);
	return val;
}

void mmio_write_32(uint64_t addr, uint32_t val)
{
	*(uint32_t *)addr = val;
	VERBOSE("0x%lx -> 0x%x\n", addr, val);
}

uint8_t mmio_read_8(uint64_t addr)
{
	uint8_t val = *(uint8_t *)addr;

	VERBOSE("0x%lx <- 0x%x\n", addr, val);
	return val;
}

void mmio_write_8(uint64_t addr, uint8_t val)
{
	*(uint8_t *)addr = val;
	VERBOSE("0x%lx -> 0x%x\n", addr, val);
}

