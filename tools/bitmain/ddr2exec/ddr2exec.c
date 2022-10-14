#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	int fd = 0;
	FILE *fp = NULL;
	void *map_base = NULL;
	off_t target_addr = 0;
	size_t write_size = 0;
	size_t mapped_size = 0;

	if (argc != 3) {
		printf("usage: ddr2exec 0x80f0000000(target addr) 0x201e(target size)\n");
		return -1;
	}

	target_addr = strtoull(argv[1], NULL, 0);
	mapped_size = strtoull(argv[2], NULL, 0);
	printf("target addr = 0x%lx\n", target_addr);
	printf("target size = 0x%lx(%ld)\n", mapped_size, mapped_size);

	fd = open("/dev/mem",  (O_RDONLY | O_SYNC));
	if (fd == -1) {
		printf("open /dev/mem error\n");
		fclose(fp);
		return -1;
	}

	fp = fopen("test_exec", "w+");
	if (fp == NULL) {
		printf("creat file error\n");
		return -1;
	}

	map_base = mmap(NULL, mapped_size, PROT_READ, MAP_SHARED, fd, target_addr);
	if (map_base == MAP_FAILED) {
		printf("mmap failed\n");
		close(fd);
		fclose(fp);
		return -1;
	}

	write_size = fwrite(map_base, 1, mapped_size, fp);
	if (write_size != mapped_size)
		printf("file size = 0x%lx, copy size = 0x%lx\n", mapped_size, write_size);
	else
		printf("ddr2exec done, file name is test_exec\n");

	munmap(map_base, mapped_size);

	close(fd);
	fclose(fp);

	return 0;
}

