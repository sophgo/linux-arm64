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
#include <sys/types.h>

void dbgi2c_init(uint64_t target_base_addr);
void dbgi2c_close(void);
int dbgi2c_set_addrwidth(int addr_width);
void dbgi2c_write32(uint64_t addr, unsigned int value);
void dbgi2c_write8(uint64_t addr, unsigned char value);
uint32_t dbgi2c_read32(uint64_t addr);
uint8_t dbgi2c_read8(uint64_t addr);
int dbgi2c_write(uint64_t addr, char *buf, int len);
int dbgi2c_read(uint64_t addr, char *buf, int len);
void set_dbgi2c_slave_addr(unsigned char set_val);
void set_i2c_file(int set_val);
void set_i2c_port(int set_val);

#endif
