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
#include "bm_spi.h"
#include "dbg_i2c.h"

#define ONE_FIP_SIZE  2097152 // 2MB
#define ONE_CHIP_SIZE 4194304 // 4MB

#define ALIGN(x, mask)  (((x) + ((mask)-1)) & ~((mask)-1))

long xstrtol(const char *s)
{
	long tmp;

	errno = 0;
	tmp = strtol(s, NULL, 0);
	if (!errno)
		return tmp;

	if (errno == ERANGE)
		fprintf(stderr, "Bad integer format: %s\n",  s);
	else
		fprintf(stderr, "Error while parsing %s: %s\n", s,
				strerror(errno));

	exit(EXIT_FAILURE);
}

unsigned long long addrxstrtol(const char *s)
{
	unsigned long long tmp;

	errno = 0;
	tmp = strtoll(s, NULL, 0);
	if (!errno)
		return tmp;

	if (errno == ERANGE)
		fprintf(stderr, "Bad integer format: %s\n",  s);
	else
		fprintf(stderr, "Error while parsing %s: %s\n", s,
				strerror(errno));

	exit(EXIT_FAILURE);
}

int write_bin_to_flash(char *buf, unsigned long long spi_addr_phy, uint32_t offset, int len)
{
	int ret = 0;
	//void *spi_base;

	printf("write 0x%x bytes to flash @ 0x%llx + 0x%x\n", len, spi_addr_phy, offset);
	//spi_base = devm_map(spi_addr_phy, 0x30);
	bm_spi_init(spi_addr_phy);
	ret = bm_spi_flash_program(buf, offset, len);
	//devm_unmap(spi_base, 0x30);

	return ret;
}

int read_bin_from_flash(char *buf, unsigned long spi_addr_phy, uint32_t offset, int len)
{
	int ret = 0;
	void *spi_base;

	printf("read 0x%x bytes from flash @ 0x%lx + 0x%x\n", len, spi_addr_phy, offset);
	spi_base = devm_map(spi_addr_phy, len);
	if (!spi_base)
		return -ENOMEM;
	memcpy(buf, spi_base + offset, len);
	devm_unmap(spi_base, len);

	return ret;
}


int set_write_protection(unsigned long spi_addr_phy, int enable)
{
	int ret = 0;
	void *spi_base;

	printf("set write protection\n");
	spi_base = devm_map(spi_addr_phy, 0x30);
	if (!spi_base)
		return -ENOMEM;
	bm_spi_init((unsigned long)spi_base);
	if (enable)
		ret = bm_spi_flash_write_status(SPI_STATUS_SRP0 | SPI_STATUS_CMP);
	else
		ret = bm_spi_flash_write_status(0);
	devm_unmap(spi_base, 0x30);

	return ret;
}

int do_flash_update(unsigned long long spi_addr_phy, const char *input_str, int is_combo, uint32_t flash_offset)
{
	int input_fd = -1, ret = 0, i, do_len;
	unsigned char *input_raw;
	struct stat statbuf;
	uint32_t chip_id, file_offset = 0;

	if (is_combo) {
		chip_id = bm_get_chip_id();
		switch (chip_id) {
		case BM1684:
			file_offset = ONE_CHIP_SIZE;
			printf("chip is bm1684, file offset 0x%x\n", file_offset);
			break;
		case BM1684X:
			file_offset = 0;
			printf("chip is bm1684x, file offset 0x%x\n", file_offset);
			break;
		default:
			printf("unknown chip 0x%x\n", chip_id);
			return -EFAULT;
		}
	}

	if (input_str) {
		printf("input file is: %s\n", input_str);

		input_fd = open(input_str, O_RDONLY);
		if (input_fd < 0) {
			printf("input file open failed (-%d)\n", errno);
			goto update_out;
		}
		stat(input_str, &statbuf);

		input_raw = malloc((statbuf.st_size / SPI_MAX_FIFO_DEPTH + 1) * SPI_MAX_FIFO_DEPTH);
		if (!input_raw) {
			printf("input buffer alloc failed\n");
			ret = -ENOMEM;
			goto update_out;
		}

		ret = read(input_fd, input_raw, statbuf.st_size);
		if (ret != statbuf.st_size) {
			printf("input file read failed (%d) (-%d)\n", ret, errno);
			ret = -EIO;
			goto update_out;
		}

		if (is_combo) {
			/*
			 * FIXME:
			 * as no length info in spi_flash.bin, we use this workaround
			 * to decide how much data writing to flash. here we only support
			 * 2 chips packed in one spi_flash.bin.
			 */
			if (file_offset != 0)
				do_len = statbuf.st_size - file_offset;
			else if (*(input_raw + ONE_FIP_SIZE) == 0)
				do_len = ONE_FIP_SIZE;
			else
				do_len = ONE_CHIP_SIZE;
		} else {
			do_len = statbuf.st_size;
		}

		ret = write_bin_to_flash(input_raw + file_offset,
				spi_addr_phy, flash_offset, do_len);

update_out:
		if (input_raw)
			free(input_raw);
		if (input_fd >= 0)
			close(input_fd);
	}

	return ret;
}

int do_flash_dump(unsigned long spi_addr_phy, const char *output_str, uint32_t flash_offset, uint32_t dump_len)
{
	int output_fd = -1, ret = 0;
	unsigned char *output_raw;

	if (output_str && dump_len) {
		printf("output file is: %s\n", output_str);

		output_fd = open(output_str, O_CREAT | O_WRONLY, 0664);
		if (output_fd < 0) {
			printf("output file open failed (-%d)\n", errno);
			goto dump_out;
		}

		output_raw = malloc(dump_len);
		if (!output_raw) {
			printf("output buffer alloc failed\n");
			ret = -ENOMEM;
			goto dump_out;
		}

		ret = read_bin_from_flash(output_raw, spi_addr_phy, flash_offset, ALIGN(dump_len, 4096));

		ret = write(output_fd, output_raw, dump_len);
		if (ret != dump_len) {
			printf("output file write failed (%d) (-%d)\n", ret, errno);
			ret = -EFAULT;
			goto dump_out;
		}

dump_out:
		if (output_raw)
			free(output_raw);
		if (output_fd >= 0)
			close(output_fd);
	}

	return ret;
}

int main(int argc, char **argv)
{
	int option, ret, is_combo = 0, i2c_port = -1, i2c_file;
	unsigned long long spi_base = 0;
	const char *input_str = NULL;
	const char *output_str = NULL;
	uint32_t flash_offset = 0, wp = 0, dump_len = 0;
	char i2c_file_name[20];
	unsigned char dbg_i2c_addr = 0;

	if (argc == 1) {
		printf("argv usage: -i-f:input_file -d:output_file -b:spi_flash_base\r\n"
				"-o:offset -s:i2c_slave_addr -m:mater_i2c_port -l:dump_len\r\n");
		printf("\n");

		printf("./dbgi2c_flash_update -f spi_flash.bin -b 0x6000000 -o 0x0 -s 0x20 -m 1\r\n");
		printf("\n");

		printf("BM1684/BM1684X update combo spi_flash.bin:\n\t"
			"sudo flash_update -i spi_flash.bin -b 0x6000000 -s 0x20\n");
		printf("\n");

		printf("BM1684 update fip.bin\n\t"
			"sudo flash_update -f fip.bin -b 0x6000000 -o 0x40000 -s 0x20\n");
		printf("BM1684 update spi_flash_bm1684.bin\n\t"
			"sudo flash_update -f spi_flash_bm1684.bin -b 0x6000000 -s 0x20\n");
		printf("\n");

		printf("BM1684X update fip.bin\n\t"
			"sudo flash_update -f fip.bin -b 0x6000000 -o 0x30000 -s 0x20\n");
		printf("BM1684X update spi_flash_bm1684.bin\n\t"
	"sudo flash_update -f spi_flash_bm1684x.bin -b 0x6000000 -s 0x20\n");
		printf("\n");

		printf("BM1684/BM1686X dump flash:\n\t"
			"sudo flash_update -d spi_flash_dump.bin -b 0x6000000 -o 0 -l 0x200000 -s 0x20\n");
		printf("\n");
		printf("BM1684/BM1686X write protection:\n\t"
			"sudo flash_update -p -b 0x6000000\n");

		return -EINVAL;
	}

	while ((option = getopt(argc, argv, "i:f:d:b:o:s:m:l:pq")) != -1) {
		switch (option) {
		case 'i':
			input_str = strdup(optarg);
			if (!input_str) {
				printf("please give input file name\n");
				return -EFAULT;
			}
			is_combo = 1;
			break;
		case 'f':
			input_str = strdup(optarg);
			if (!input_str) {
				printf("please give input file name\n");
				return -EFAULT;
			}
			break;
		case 'd':
			output_str = strdup(optarg);
			if (!output_str) {
				printf("please give output file name\n");
				return -EFAULT;
			}
			break;
		case 'b':
			spi_base = addrxstrtol(optarg);
			break;
		case 'o':
			flash_offset = xstrtol(optarg);
			break;
		case 's':
			dbg_i2c_addr = xstrtol(optarg);
			set_dbg_i2c_addr(dbg_i2c_addr);
			break;
		case 'm':
			i2c_port = xstrtol(optarg);
			break;
		case 'l':
			dump_len = xstrtol(optarg);
			break;
		case 'p':
			wp = 1;
			break;
		case 'q':
			wp = 2;
			break;
		}
	}

	if (!spi_base || !dbg_i2c_addr || i2c_port == -1 ||
			(!wp && !output_str && !input_str) || (output_str && !dump_len)) {
		printf("please give all arguments we need\n");
		return -EFAULT;
	}

	if (access("/sys/kernel/debug/top/clock", F_OK) == 0)
		system("echo 'en clk_gate_ahb_sf 1' > /sys/kernel/debug/top/clock");

	// deal with non-flash-update function first
	if (wp) {
		if (wp == 1)
			ret = set_write_protection(spi_base, 1);
		else if (wp == 2)
			ret = set_write_protection(spi_base, 0);
		goto all_out;
	}

	if (output_str) {
		ret = do_flash_dump(spi_base, output_str, flash_offset, dump_len);
		goto all_out;
	}

	// i2c master init
	snprintf(i2c_file_name, sizeof(i2c_file_name), "/dev/i2c-%d", i2c_port);
	i2c_file = open(i2c_file_name, O_RDWR);
	set_i2c_file(i2c_file);
	if (i2c_file < 0) {
		printf("open I2C device node failed: %d\n", errno);
		return -ENODEV;
	}
	if (spi_base & 0xff00000000)
		dbgi2c_set_addrwidth(ADDRW_40);
	else
		dbgi2c_set_addrwidth(ADDRW_32);
	// here starts flash update routine
	ret = do_flash_update(spi_base, input_str, is_combo, flash_offset);
	close(i2c_file);

all_out:
	if (access("/sys/kernel/debug/top/clock", F_OK) == 0)
		system("echo 'en clk_gate_ahb_sf 0' > /sys/kernel/debug/top/clock");
	return ret;
}
