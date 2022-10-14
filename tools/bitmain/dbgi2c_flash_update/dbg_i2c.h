#ifndef __DBG_I2C_H__
#define __DBG_I2C_H__
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

enum {
	ADDRW_8,
	ADDRW_16,
	ADDRW_24,
	ADDRW_32,
	ADDRW_40,
	ADDRW_48,
};

int dbgi2c_set_addrwidth(int addr_width);
int dbgi2c_write32(unsigned long long addr, unsigned int value);
int dbgi2c_write8(unsigned long long addr, unsigned char value);
uint32_t dbgi2c_read32(unsigned long long addr);
uint8_t dbgi2c_read8(unsigned long long addr);
void set_dbg_i2c_addr(unsigned char addr);
void set_i2c_file(int file_name);

#endif
