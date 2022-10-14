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
#include "dbg_i2c.h"

/* revise date : 2022/9/7
 * revise content : The code adds support for 64-bit addresses while being
 * compatible with existing Settings
 */

//#if 1
#define LOGV(args...) printf(args)
//#else
//#define LOGV(args...)
//#endif

#define DBG_I2C_DATA_LEN 128

static int i2c_file; // I2C device fd
/* 0x20 is for bm1684x_evb */
/* 0x56 if for mango FPGA MINI A53 */
static unsigned char dbg_i2c_addr;

enum {
	DATALEN_1BYTES,
	DATALEN_2BYTES,
	DATALEN_4BYTES,
	DATALEN_ANYBYTES,
};

void set_dbg_i2c_addr(unsigned char addr)
{
	dbg_i2c_addr = addr;
}

void set_i2c_file(int file_name)
{
	i2c_file = file_name;
}

int dbgi2c_set_addrwidth(int addr_width)
{
	struct i2c_rdwr_ioctl_data *i2c_data;
	struct i2c_msg *msgs0;
	char cmd;
	int ret;

	i2c_data = (struct i2c_rdwr_ioctl_data *)malloc(sizeof(struct i2c_rdwr_ioctl_data));
	i2c_data->msgs = (struct i2c_msg *)malloc(2 * sizeof(struct i2c_msg));

	switch (addr_width) {
	case ADDRW_8:
		cmd = 0x20;
		break;
	case ADDRW_16:
		cmd = 0x21;
		break;
	case ADDRW_24:
		cmd = 0x22;
		break;
	case ADDRW_32:
		cmd = 0x23;
		break;
	case ADDRW_40:
		cmd = 0x24;
		break;
	case ADDRW_48:
		cmd = 0x25;
		break;
	default:
		cmd = 0x24;
		break;
	}

	msgs0 = &i2c_data->msgs[0];
	i2c_data->nmsgs  = 1;
	msgs0->len = 1;
	msgs0->flags = 0; // write
	msgs0->addr = dbg_i2c_addr;
	msgs0->buf = &cmd;

	ret = ioctl(i2c_file, I2C_RDWR, (unsigned long)i2c_data);
	if (ret < 0)
		printf("%s failed: %d %d\n", __func__, ret, errno);

	free(i2c_data->msgs);
	free(i2c_data);
	return ret;
}

int dbgi2c_write32(unsigned long long addr, unsigned int value)
{
	struct i2c_rdwr_ioctl_data *i2c_data;
	struct i2c_msg *msgs0;
	int header_size = 0;
	char header_body[32] = {0};
	int ret = 0;

	i2c_data = (struct i2c_rdwr_ioctl_data *)malloc(sizeof(struct i2c_rdwr_ioctl_data));
	i2c_data->msgs = (struct i2c_msg *)malloc(sizeof(struct i2c_msg));

	msgs0 = &i2c_data->msgs[0];
	i2c_data->nmsgs = 1;
	header_body[0] = 0x12; // four byte per trans
	header_body[1] = addr & 0xff;
	header_body[2] = (addr >> 8) & 0xff;
	header_body[3] = (addr >> 16) & 0xff;
	header_body[4] = (addr >> 24) & 0xff;
	if (addr & 0xff00000000) {
		header_body[5] = addr >> 32;
		header_size  = 6;
		//dbgi2c_set_addrwidth(ADDRW_40);
	} else {
		header_size  = 5;
		//dbgi2c_set_addrwidth(ADDRW_32);
	}
	*(unsigned int *)(header_body + header_size) = value;

	msgs0->len = header_size + 4;
	msgs0->flags = 0; // write
	msgs0->addr = dbg_i2c_addr;
	msgs0->buf = header_body;

	ret = ioctl(i2c_file, I2C_RDWR, (unsigned long)i2c_data);
	if (ret < 0)
		printf("dbgi2c_wrte32 failed: %d %d\n", ret, errno);

	free(i2c_data->msgs);
	free(i2c_data);
	return ret;
}

int dbgi2c_write8(unsigned long long addr, unsigned char value)
{
	struct i2c_rdwr_ioctl_data *i2c_data;
	struct i2c_msg *msgs0;
	int header_size = 0;
	char header_body[32] = {0};
	int ret = 0;

	i2c_data = (struct i2c_rdwr_ioctl_data *)malloc(sizeof(struct i2c_rdwr_ioctl_data));
	i2c_data->msgs = (struct i2c_msg *)malloc(sizeof(struct i2c_msg));

	msgs0 = &i2c_data->msgs[0];
	i2c_data->nmsgs = 1;
	header_body[0] = 0x10; // one byte per trans
	header_body[1] = addr & 0xff;
	header_body[2] = (addr >> 8) & 0xff;
	header_body[3] = (addr >> 16) & 0xff;
	header_body[4] = (addr >> 24) & 0xff;
	if (addr & 0xff00000000) {
		header_body[5] = addr >> 32;
		header_size  = 6;
		//dbgi2c_set_addrwidth(ADDRW_40);
	} else {
		header_size  = 5;
		//dbgi2c_set_addrwidth(ADDRW_32);
	}
	*(unsigned char *)(header_body + header_size) = value;

	msgs0->len = header_size + 1;
	msgs0->flags = 0; // write
	msgs0->addr = dbg_i2c_addr;
	msgs0->buf = header_body;

	ret = ioctl(i2c_file, I2C_RDWR, (unsigned long)i2c_data);
	if (ret < 0)
		printf("%s failed: %d %d\n", __func__, ret, errno);

	free(i2c_data->msgs);
	free(i2c_data);
	return ret;
}

uint32_t dbgi2c_read32(unsigned long long addr)
{
	struct i2c_rdwr_ioctl_data *i2c_data;
	struct i2c_msg *msgs0;
	struct i2c_msg *msgs1;
	int header_size = 0;
	char header_body[32] = {0};
	uint32_t ret = 0;
	uint32_t *value;
	uint32_t ret_value = 0;

	value = &ret_value;

	i2c_data = (struct i2c_rdwr_ioctl_data *)malloc(sizeof(struct i2c_rdwr_ioctl_data));
	i2c_data->msgs = (struct i2c_msg *)malloc(2 * sizeof(struct i2c_msg));

	msgs0 = &i2c_data->msgs[0];
	msgs1 = &i2c_data->msgs[1];
	i2c_data->nmsgs = 2;
	header_body[0] = 0x12; // four byte per trans
	header_body[1] = addr & 0xff;
	header_body[2] = (addr >> 8) & 0xff;
	header_body[3] = (addr >> 16) & 0xff;
	header_body[4] = (addr >> 24) & 0xff;
	if (addr & 0xff00000000) {
		header_body[5] = addr >> 32;
		header_size  = 6;
		//dbgi2c_set_addrwidth(ADDRW_40);
	} else {
		header_size  = 5;
		//dbgi2c_set_addrwidth(ADDRW_32);
	}

	msgs0->len = header_size;
	msgs0->flags = 0; // write
	msgs0->addr = dbg_i2c_addr;
	msgs0->buf = header_body;

	msgs1->len = 4;
	msgs1->flags = I2C_M_RD;
	msgs1->addr = dbg_i2c_addr;
	msgs1->buf = (char *)value;

	ret = ioctl(i2c_file, I2C_RDWR, (unsigned long)i2c_data);
	if (ret < 0)
		printf("%s failed: %d %d\n", __func__, ret, errno);

	free(i2c_data->msgs);
	free(i2c_data);
	return ret_value;
}

uint8_t dbgi2c_read8(unsigned long long addr)
{
	struct i2c_rdwr_ioctl_data *i2c_data;
	struct i2c_msg *msgs0;
	struct i2c_msg *msgs1;
	int header_size = 0;
	char header_body[32] = {0};
	uint32_t ret = 0;
	uint8_t *value;
	uint8_t ret_value = 0;

	value = &ret_value;

	i2c_data = (struct i2c_rdwr_ioctl_data *)malloc(sizeof(struct i2c_rdwr_ioctl_data));
	i2c_data->msgs = (struct i2c_msg *)malloc(2 * sizeof(struct i2c_msg));

	msgs0 = &i2c_data->msgs[0];
	msgs1 = &i2c_data->msgs[1];
	i2c_data->nmsgs = 2;
	header_body[0] = 0x10; // one byte per trans
	header_body[1] = addr & 0xff;
	header_body[2] = (addr >> 8) & 0xff;
	header_body[3] = (addr >> 16) & 0xff;
	header_body[4] = (addr >> 24) & 0xff;
	if (addr & 0xff00000000) {
		header_body[5] = addr >> 32;
		header_size  = 6;
		//dbgi2c_set_addrwidth(ADDRW_40);
	} else {
		header_size  = 5;
		//dbgi2c_set_addrwidth(ADDRW_32);
	}

	msgs0->len = header_size;
	msgs0->flags = 0; // write
	msgs0->addr = dbg_i2c_addr;
	msgs0->buf = header_body;

	msgs1->len = 1;
	msgs1->flags = I2C_M_RD;
	msgs1->addr = dbg_i2c_addr;
	msgs1->buf = (char *)value;

	ret = ioctl(i2c_file, I2C_RDWR, (unsigned long)i2c_data);
	if (ret < 0)
		printf("%s failed: %d %d\n", __func__, ret, errno);

	free(i2c_data->msgs);
	free(i2c_data);
	return ret_value;
}

int dbgi2c_write(unsigned long long addr, char *buf, int len)
{
	struct i2c_rdwr_ioctl_data *i2c_data;
	struct i2c_msg *msgs0;
	int header_size = 0;
	char header_body[DBG_I2C_DATA_LEN + 32] = {0};
	int i, j, k;
	int ret = 0;

	i2c_data = (struct i2c_rdwr_ioctl_data *)malloc(sizeof(struct i2c_rdwr_ioctl_data));
	i2c_data->msgs = (struct i2c_msg *)malloc(sizeof(struct i2c_msg));

	msgs0 = &i2c_data->msgs[0];
	i2c_data->nmsgs = 1;
	header_body[0] = 0x18; //any byte per trans
	header_body[1] = addr & 0xff;
	header_body[2] = (addr >> 8) & 0xff;
	header_body[3] = (addr >> 16) & 0xff;
	header_body[4] = (addr >> 24) & 0xff;
	if (addr & 0xff00000000) {
		header_body[5] = addr >> 32;
		header_size  = 6;
		dbgi2c_set_addrwidth(ADDRW_40);
	} else {
		header_size  = 5;
		dbgi2c_set_addrwidth(ADDRW_32);
	}
	msgs0->len = DBG_I2C_DATA_LEN + header_size;
	msgs0->flags = 0; // write
	msgs0->addr = dbg_i2c_addr;
	msgs0->buf = header_body;

	if (len > DBG_I2C_DATA_LEN) {
		j =  len / DBG_I2C_DATA_LEN;
		k =  len % DBG_I2C_DATA_LEN;
		for (i = 0; i < j; i++) {
			memcpy(header_body + header_size, buf + DBG_I2C_DATA_LEN * i, DBG_I2C_DATA_LEN);
			ret = ioctl(i2c_file, I2C_RDWR, (unsigned long)i2c_data);
			if (ret < 0) {
				printf("%s %d/%d failed: %d %d\n", __func__, i, j, ret, errno);
				goto out;
			}
			// TODO: cross 4GB boundary is not supported
			*(unsigned int *)(header_body + 1) += DBG_I2C_DATA_LEN;
		}
		if (k) {
			memcpy(header_body + header_size, buf + DBG_I2C_DATA_LEN*i, k);

			// the length should be multiple of 4, otherwise the last several bytes won't send
			if (k % 4)
				k += (4 - k % 4);

			msgs0->len = k + header_size;
			ret = ioctl(i2c_file, I2C_RDWR, (unsigned long)i2c_data);
			if (ret < 0) {
				printf("%s last block failed %d %d\n", __func__, ret, errno);
				goto out;
			}
		}
	} else {
		memcpy(header_body + header_size, buf, len);
		if (len % 4)
			len += (4 - len % 4);
		msgs0->len = len + header_size;
		ret = ioctl(i2c_file, I2C_RDWR, (unsigned long)i2c_data);
		if (ret < 0) {
			printf("%s failed %d %d\n", __func__, ret, errno);
			goto out;
		}
	}

out:
	free(i2c_data->msgs);
	free(i2c_data);
	return ret;
}

int dbgi2c_read(unsigned long long addr, char *buf, int len)
{
	struct i2c_rdwr_ioctl_data *i2c_data;
	struct i2c_msg *msgs0;
	struct i2c_msg *msgs1;
	int header_size = 0;
	char header_body[DBG_I2C_DATA_LEN + 32] = {0};
	int i, j, k;
	int ret = 0;

	i2c_data = (struct i2c_rdwr_ioctl_data *)malloc(sizeof(struct i2c_rdwr_ioctl_data));
	i2c_data->msgs = (struct i2c_msg *)malloc(2 * sizeof(struct i2c_msg));

	msgs0 = &i2c_data->msgs[0];
	msgs1 = &i2c_data->msgs[1];
	i2c_data->nmsgs = 2;
	header_body[0] = 0x18; // any byte per trans
	header_body[1] = addr & 0xff;
	header_body[2] = (addr >> 8) & 0xff;
	header_body[3] = (addr >> 16) & 0xff;
	header_body[4] = (addr >> 24) & 0xff;
	if (addr & 0xff00000000) {
		header_body[5] = addr >> 32;
		header_size  = 6;
		dbgi2c_set_addrwidth(ADDRW_40);
	} else {
		header_size  = 5;
		dbgi2c_set_addrwidth(ADDRW_32);
	}

	msgs0->len = header_size;
	msgs0->flags = 0; // write
	msgs0->addr = dbg_i2c_addr;
	msgs0->buf = header_body;

	msgs1->flags = I2C_M_RD;
	msgs1->addr = dbg_i2c_addr;

	if (len > DBG_I2C_DATA_LEN) {
		j =  len / DBG_I2C_DATA_LEN;
		k =  len % DBG_I2C_DATA_LEN;

		msgs1->len = DBG_I2C_DATA_LEN;
		for (i = 0; i < j; i++) {
			msgs1->buf = buf + i * DBG_I2C_DATA_LEN;
			ret = ioctl(i2c_file, I2C_RDWR, (unsigned long)i2c_data);
			if (ret < 0) {
				printf("%s %d/%d failed: %d %d\n", __func__, i, j, ret, errno);
				goto out;
			}
			// TODO: cross 4GB boundary is not supported
			*(unsigned int *)(header_body+1) += DBG_I2C_DATA_LEN;
		}
		if (k) {
			msgs1->len = k;
			msgs1->buf = buf + i * DBG_I2C_DATA_LEN;
			ret = ioctl(i2c_file, I2C_RDWR, (unsigned long)i2c_data);
			if (ret < 0) {
				printf("%s last block failed %d %d\n", __func__, ret, errno);
				goto out;
			}
		}
	} else {
		msgs1->buf = buf;
		msgs1->len = len;
		ret = ioctl(i2c_file, I2C_RDWR, (unsigned long)i2c_data);
		if (ret < 0) {
			printf("%s failed %d %d\n", __func__, ret, errno);
			goto out;
		}
	}

out:
	free(i2c_data->msgs);
	free(i2c_data);
	return ret;
}
