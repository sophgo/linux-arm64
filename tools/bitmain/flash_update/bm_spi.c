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

static int devmem_fd;
static struct bm_memory_ops mem_rw_ops;

#define NOTICE(...)	printf("NOTICE:  " __VA_ARGS__)
#define INFO(...)	printf("INFO:  " __VA_ARGS__)
#define ERROR(...)	printf("ERROR:  " __VA_ARGS__)
#define VERBOSE(...)

#define udelay(a) usleep(a)
#define mdelay(a) usleep(a * 1000)

void set_memory_rw_dbgi2c(void)
{
	mem_rw_ops.rw_type = DBG_I2C_RW;
}

int get_memory_rw_type(void)
{
	return mem_rw_ops.rw_type;
}

uint32_t bm_get_chip_id(void)
{
	unsigned long addr = 0x50010000;
	uint32_t val;
	void *virt_addr;

	virt_addr = devm_map(addr, 4);
	if (virt_addr == NULL) {
		ERROR("addr map failed");
		return 0;
	}

	val = *(uint32_t *)virt_addr;

	devm_unmap(virt_addr, 4);
	return val;
}

static int spi_data_in_tran(uint64_t spi_base, uint8_t *dst_buf,
		uint8_t *cmd_buf, int with_cmd, int addr_bytes, int data_bytes)
{
	uint32_t *p_data = (uint32_t *)cmd_buf;
	uint32_t tran_csr = 0;
	int cmd_bytes = addr_bytes + ((with_cmd) ? 1 : 0);
	int i, xfer_size, off;

	if (data_bytes > 65535) {
		ERROR("SPI data in overflow, should be less than 65535 bytes(%d)\n", data_bytes);
		return -1;
	}

	/* init tran_csr */
	tran_csr = mem_rw_ops.read32(spi_base + REG_SPI_TRAN_CSR);
	tran_csr &= ~(BIT_SPI_TRAN_CSR_TRAN_MODE_MASK
			| BIT_SPI_TRAN_CSR_ADDR_BYTES_MASK
			| BIT_SPI_TRAN_CSR_FIFO_TRG_LVL_MASK
			| BIT_SPI_TRAN_CSR_WITH_CMD);
	tran_csr |= (addr_bytes << SPI_TRAN_CSR_ADDR_BYTES_SHIFT);
	tran_csr |= (with_cmd) ? BIT_SPI_TRAN_CSR_WITH_CMD : 0;
	tran_csr |= BIT_SPI_TRAN_CSR_FIFO_TRG_LVL_8_BYTE;
	tran_csr |= BIT_SPI_TRAN_CSR_TRAN_MODE_RX;

	mem_rw_ops.write32(spi_base + REG_SPI_FIFO_PT, 0); // flush FIFO before filling fifo
	if (with_cmd) {
		for (i = 0; i < ((cmd_bytes - 1) / 4 + 1); i++)
			mem_rw_ops.write32(spi_base + REG_SPI_FIFO_PORT, p_data[i]);
	}

	/* issue tran */
	mem_rw_ops.write32(spi_base + REG_SPI_INT_STS, 0); // clear all int
	mem_rw_ops.write32(spi_base + REG_SPI_TRAN_NUM, data_bytes);
	tran_csr |= BIT_SPI_TRAN_CSR_GO_BUSY;
	mem_rw_ops.write32(spi_base + REG_SPI_TRAN_CSR, tran_csr);

	/* check rd int to make sure data out done and in data started */
	while ((mem_rw_ops.read32(spi_base + REG_SPI_INT_STS) & BIT_SPI_INT_RD_FIFO) == 0)
		;

	/* get data */
	p_data = (uint32_t *)dst_buf;
	off = 0;
	while (off < data_bytes) {
		if (data_bytes - off >= SPI_MAX_FIFO_DEPTH)
			xfer_size = SPI_MAX_FIFO_DEPTH;
		else
			xfer_size = data_bytes - off;

		while ((mem_rw_ops.read32(spi_base + REG_SPI_FIFO_PT) & 0xF) != xfer_size)
			;
		for (i = 0; i < ((xfer_size - 1) / 4 + 1); i++)
			p_data[off / 4 + i] = mem_rw_ops.read32(spi_base + REG_SPI_FIFO_PORT);
		off += xfer_size;
	}

	/* wait tran done */
	while ((mem_rw_ops.read32(spi_base + REG_SPI_INT_STS) & BIT_SPI_INT_TRAN_DONE) == 0)
		;
	mem_rw_ops.write32(spi_base + REG_SPI_FIFO_PT, 0);    //should flush FIFO after tran
	return 0;
}

static int spi_data_read(uint64_t spi_base, uint8_t *dst_buf, int addr, int size)
{
	uint8_t cmd_buf[4];

	cmd_buf[0] = SPI_CMD_READ;
	cmd_buf[1] = ((addr) >> 16) & 0xFF;
	cmd_buf[2] = ((addr) >> 8) & 0xFF;
	cmd_buf[3] = (addr) & 0xFF;
	spi_data_in_tran(spi_base, dst_buf, cmd_buf, 1, 3, size);

	return 0;
}

void bm_spi_flash_read_sector(uint64_t spi_base, uint32_t addr, uint8_t *buf)
{
	spi_data_read(spi_base, buf, (int)addr, 256);
}

size_t spi_flash_read_blocks(int lba, uintptr_t buf, size_t size)
{
	spi_data_read(mem_rw_ops.ctrl_base, (uint8_t *)buf, lba * SPI_FLASH_BLOCK_SIZE, size);
	return size;
}

size_t spi_flash_write_blocks(int lba, const uintptr_t buf, size_t size)
{
	ERROR("SPI flash writing is not supported\n");
	return -ENODEV;
}

void bm_spi_init(uint64_t base)
{
	uint32_t tran_csr = 0;

	if (mem_rw_ops.rw_type == DBG_I2C_RW) {
		mem_rw_ops.init = dbgi2c_init;
		mem_rw_ops.relase = dbgi2c_close;
		mem_rw_ops.write32 = dbgi2c_write32;
		mem_rw_ops.read32 = dbgi2c_read32;
		mem_rw_ops.write8 = dbgi2c_write8;
		mem_rw_ops.read8 = dbgi2c_read8;
		mem_rw_ops.ctrl_base = base;
		mem_rw_ops.init(base);
	} else if (mem_rw_ops.rw_type == MEM_IO_RW) {
		mem_rw_ops.init = mmio_init;
		mem_rw_ops.relase = mmio_relase;
		mem_rw_ops.write32 = mmio_write_32;
		mem_rw_ops.read32 = mmio_read_32;
		mem_rw_ops.write8 = mmio_write_8;
		mem_rw_ops.read8 = mmio_read_8;
		mem_rw_ops.ctrl_base = (uint64_t)mem_rw_ops.init(base);
	}

	// disable DMMR (direct memory mapping read)
	mem_rw_ops.write32(mem_rw_ops.ctrl_base + REG_SPI_DMMR, 0);
	// soft reset
	mem_rw_ops.write32(mem_rw_ops.ctrl_base + REG_SPI_CTRL,
			mem_rw_ops.read32(mem_rw_ops.ctrl_base + REG_SPI_CTRL) | BIT_SPI_CTRL_SRST | 0x3);
	// hardware CE control, soft reset won't change this reg...
	mem_rw_ops.write32(mem_rw_ops.ctrl_base + REG_SPI_CE_CTRL, 0x0);

	tran_csr |= (0x03 << SPI_TRAN_CSR_ADDR_BYTES_SHIFT);
	tran_csr |= BIT_SPI_TRAN_CSR_FIFO_TRG_LVL_4_BYTE;
	tran_csr |= BIT_SPI_TRAN_CSR_WITH_CMD;
	mem_rw_ops.write32(mem_rw_ops.ctrl_base + REG_SPI_TRAN_CSR, tran_csr);
}

void bm_spi_uninit(void)
{
	mem_rw_ops.relase((void *)mem_rw_ops.ctrl_base);
}

/* here are APIs for SPI flash programming */

static uint8_t spi_non_data_tran(uint64_t spi_base, uint8_t *cmd_buf,
	uint32_t with_cmd, uint32_t addr_bytes)
{
	uint32_t *p_data = (uint32_t *)cmd_buf;
	uint32_t tran_csr = 0;

	if (addr_bytes > 3) {
		ERROR("non-data: addr bytes should be less than 3 (%d)\n", addr_bytes);
		return -1;
	}

	/* init tran_csr */
	tran_csr = mem_rw_ops.read32(spi_base + REG_SPI_TRAN_CSR);
	tran_csr &= ~(BIT_SPI_TRAN_CSR_TRAN_MODE_MASK
		  | BIT_SPI_TRAN_CSR_ADDR_BYTES_MASK
		  | BIT_SPI_TRAN_CSR_FIFO_TRG_LVL_MASK
		  | BIT_SPI_TRAN_CSR_WITH_CMD);
	tran_csr |= (addr_bytes << SPI_TRAN_CSR_ADDR_BYTES_SHIFT);
	tran_csr |= BIT_SPI_TRAN_CSR_FIFO_TRG_LVL_1_BYTE;
	tran_csr |= (with_cmd ? BIT_SPI_TRAN_CSR_WITH_CMD : 0);

	mem_rw_ops.write32(spi_base + REG_SPI_FIFO_PT, 0); //do flush FIFO before filling fifo

	mem_rw_ops.write32(spi_base + REG_SPI_FIFO_PORT, p_data[0]);

	/* issue tran */
	mem_rw_ops.write32(spi_base + REG_SPI_INT_STS, 0); //clear all int
	tran_csr |= BIT_SPI_TRAN_CSR_GO_BUSY;
	mem_rw_ops.write32(spi_base + REG_SPI_TRAN_CSR, tran_csr);

	/* wait tran done */
	while ((mem_rw_ops.read32(spi_base + REG_SPI_INT_STS) & BIT_SPI_INT_TRAN_DONE) == 0)
		;
	mem_rw_ops.write32(spi_base + REG_SPI_FIFO_PT, 0); //should flush FIFO after tran
	return 0;
}

static uint8_t spi_data_out_tran(uint64_t spi_base, uint8_t *src_buf, uint8_t *cmd_buf,
	uint32_t with_cmd, uint32_t addr_bytes, uint32_t data_bytes)
{
	uint32_t *p_data = (uint32_t *)cmd_buf;
	uint32_t tran_csr = 0;
	uint32_t cmd_bytes = addr_bytes + (with_cmd ? 1 : 0);
	uint32_t xfer_size, off;
	uint32_t wait = 0;
	int i;

	if (data_bytes > 65535) {
		ERROR("data out overflow, should be less than 65535 bytes(%d)\n", data_bytes);
		return -1;
	}

	/* init tran_csr */
	tran_csr = mem_rw_ops.read32(spi_base + REG_SPI_TRAN_CSR);
	tran_csr &= ~(BIT_SPI_TRAN_CSR_TRAN_MODE_MASK
		  | BIT_SPI_TRAN_CSR_ADDR_BYTES_MASK
		  | BIT_SPI_TRAN_CSR_FIFO_TRG_LVL_MASK
		  | BIT_SPI_TRAN_CSR_WITH_CMD);
	tran_csr |= (addr_bytes << SPI_TRAN_CSR_ADDR_BYTES_SHIFT);
	tran_csr |= (with_cmd ? BIT_SPI_TRAN_CSR_WITH_CMD : 0);
	tran_csr |= BIT_SPI_TRAN_CSR_FIFO_TRG_LVL_8_BYTE;
	tran_csr |= BIT_SPI_TRAN_CSR_TRAN_MODE_TX;

	mem_rw_ops.write32(spi_base + REG_SPI_FIFO_PT, 0); //do flush FIFO before filling fifo
	if (with_cmd)
		for (i = 0; i < ((cmd_bytes - 1) / 4 + 1); i++)
			mem_rw_ops.write32(spi_base + REG_SPI_FIFO_PORT, p_data[i]);

	/* issue tran */
	mem_rw_ops.write32(spi_base + REG_SPI_INT_STS, 0); //clear all int
	mem_rw_ops.write32(spi_base + REG_SPI_TRAN_NUM, data_bytes);
	tran_csr |= BIT_SPI_TRAN_CSR_GO_BUSY;
	mem_rw_ops.write32(spi_base + REG_SPI_TRAN_CSR, tran_csr);
	while ((mem_rw_ops.read32(spi_base + REG_SPI_FIFO_PT) & 0xF) != 0)
		; //wait for cmd issued

	/* fill data */
	p_data = (uint32_t *)src_buf;
	off = 0;
	while (off < data_bytes) {
		if (data_bytes - off >= SPI_MAX_FIFO_DEPTH)
			xfer_size = SPI_MAX_FIFO_DEPTH;
		else
			xfer_size = data_bytes - off;

		wait = 0;
		while ((mem_rw_ops.read32(spi_base + REG_SPI_FIFO_PT) & 0xF) != 0) {
			wait++;
			udelay(10);
			if (wait > 30000) { // 300ms
				ERROR("wait to write FIFO timeout\n");
				return -1;
			}
		}

		for (i = 0; i < ((xfer_size - 1) / 4 + 1); i++)
			mem_rw_ops.write32(spi_base + REG_SPI_FIFO_PORT, p_data[off / 4 + i]);

		off += xfer_size;
	}

	/* wait tran done */
	while ((mem_rw_ops.read32(spi_base + REG_SPI_INT_STS) & BIT_SPI_INT_TRAN_DONE) == 0)
		;
	mem_rw_ops.write32(spi_base + REG_SPI_FIFO_PT, 0); //should flush FIFO after tran
	return 0;
}

/*
 * spi_in_out_tran is a workaround function for current 32-bit access to spic fifo:
 * AHB bus could only do 32-bit access to spic fifo, so cmd without 3-bytes addr will leave 3-byte
 * data in fifo, so set tx to mark that these 3-bytes data would be sent out.
 * So send_bytes should be 3 (write 1 dw into fifo) or 7(write 2 dw), get_bytes sould be the same value.
 * software would mask out useless data in get_bytes.
 */
static uint8_t spi_in_out_tran(uint64_t spi_base, uint8_t *dst_buf, uint8_t *src_buf,
	uint32_t with_cmd, uint32_t addr_bytes, uint32_t send_bytes, uint32_t get_bytes)
{
	uint8_t *p_data = (uint8_t *)src_buf;
	uint32_t total_out_bytes;
	uint32_t tran_csr = 0;
	int i;

	if (send_bytes != get_bytes) {
		ERROR("data in&out: get_bytes should be the same as send_bytes\n");
		return -1;
	}

	if ((send_bytes > SPI_MAX_FIFO_DEPTH) || (get_bytes > SPI_MAX_FIFO_DEPTH)) {
		ERROR("data in&out: FIFO will overflow\n");
		return -1;
	}

	/* init tran_csr */
	tran_csr = mem_rw_ops.read32(spi_base + REG_SPI_TRAN_CSR);
	tran_csr &= ~(BIT_SPI_TRAN_CSR_TRAN_MODE_MASK
		  | BIT_SPI_TRAN_CSR_ADDR_BYTES_MASK
		  | BIT_SPI_TRAN_CSR_FIFO_TRG_LVL_MASK
		  | BIT_SPI_TRAN_CSR_WITH_CMD);
	tran_csr |= (addr_bytes << SPI_TRAN_CSR_ADDR_BYTES_SHIFT);
	tran_csr |= BIT_SPI_TRAN_CSR_FIFO_TRG_LVL_1_BYTE;
	tran_csr |= BIT_SPI_TRAN_CSR_WITH_CMD;
	tran_csr |= BIT_SPI_TRAN_CSR_TRAN_MODE_TX;
	tran_csr |= BIT_SPI_TRAN_CSR_TRAN_MODE_RX;

	mem_rw_ops.write32(spi_base + REG_SPI_FIFO_PT, 0); //do flush FIFO before filling fifo
	total_out_bytes = addr_bytes + send_bytes + (with_cmd ? 1 : 0);
	for (i = 0; i < total_out_bytes; i++)
		mem_rw_ops.write8(spi_base + REG_SPI_FIFO_PORT, p_data[i]);

	/* issue tran */
	mem_rw_ops.write32(spi_base + REG_SPI_INT_STS, 0); //clear all int
	mem_rw_ops.write32(spi_base + REG_SPI_TRAN_NUM, get_bytes);
	tran_csr |= BIT_SPI_TRAN_CSR_GO_BUSY;
	mem_rw_ops.write32(spi_base + REG_SPI_TRAN_CSR, tran_csr);

	/* wait tran done and get data */
	while ((mem_rw_ops.read32(spi_base + REG_SPI_INT_STS) & BIT_SPI_INT_TRAN_DONE) == 0)
		;

	p_data = (uint8_t *)dst_buf;
	for (i = 0; i < get_bytes; i++)
		p_data[i] = mem_rw_ops.read8(spi_base + REG_SPI_FIFO_PORT);

	mem_rw_ops.write32(spi_base + REG_SPI_FIFO_PT, 0); //should flush FIFO after tran
	return 0;
}

static uint8_t spi_write_en(uint64_t spi_base)
{
	uint8_t cmd_buf[4];

	memset(cmd_buf, 0, sizeof(cmd_buf));

	cmd_buf[0] = SPI_CMD_WREN;
	spi_non_data_tran(spi_base, cmd_buf, 1, 0);
	return 0;
}

#if 0
static void spi_bulk_erase(unsigned long spi_base)
{
	uint8_t cmd_buf[4];

	memset(cmd_buf, 0, sizeof(cmd_buf));

	cmd_buf[0] = SPI_CMD_BE;
	spi_non_data_tran(spi_base, cmd_buf, 1, 0);
}
#endif

static uint8_t spi_read_status(uint64_t spi_base)
{
	uint8_t cmd_buf[4];
	uint8_t data_buf[4];

	memset(cmd_buf, 0, sizeof(cmd_buf));
	memset(data_buf, 0, sizeof(data_buf));

	cmd_buf[0] = SPI_CMD_RDSR;
	spi_in_out_tran(spi_base, data_buf, cmd_buf, 1, 0, 1, 1);

	VERBOSE("read status low: 0x%x\n", data_buf[0]);
	return data_buf[0];
}

static uint8_t spi_read_status_high(uint64_t spi_base)
{
	uint8_t cmd_buf[4];
	uint8_t data_buf[4];

	memset(cmd_buf, 0, sizeof(cmd_buf));
	memset(data_buf, 0, sizeof(data_buf));

	cmd_buf[0] = SPI_CMD_RDSR_H;
	spi_in_out_tran(spi_base, data_buf, cmd_buf, 1, 0, 1, 1);

	VERBOSE("read status high: 0x%x\n", data_buf[0]);
	return data_buf[0];
}

static void spi_write_status(uint64_t spi_base, uint16_t value)
{
	uint8_t cmd_buf[4];
	uint8_t data_buf[4];

	memset(cmd_buf, 0, sizeof(cmd_buf));
	memset(data_buf, 0, sizeof(data_buf));

	cmd_buf[0] = SPI_CMD_WRSR;
	cmd_buf[1] = value & 0xFF;
	cmd_buf[2] = (value >> 8) & 0xFF;
	spi_in_out_tran(spi_base, data_buf, cmd_buf, 1, 0, 2, 2);

	INFO("write status: 0x%x\n", value);
}

uint32_t bm_spi_read_id(uint64_t spi_base)
{
	uint8_t cmd_buf[4];
	uint8_t data_buf[4];
	uint32_t read_id = 0;

	memset(cmd_buf, 0, sizeof(cmd_buf));
	memset(data_buf, 0, sizeof(data_buf));

	cmd_buf[0] = SPI_CMD_RDID;
	spi_in_out_tran(spi_base, data_buf, cmd_buf, 1, 0, 3, 3);
	read_id = (data_buf[2] << 16) | (data_buf[1] << 8) | (data_buf[0]);

	return read_id;
}

static uint8_t spi_page_program(uint64_t spi_base, uint8_t *src_buf, uint32_t addr, uint32_t size)
{
	uint8_t cmd_buf[4];

	memset(cmd_buf, 0, sizeof(cmd_buf));

	cmd_buf[0] = SPI_CMD_PP;
	cmd_buf[1] = (addr >> 16) & 0xFF;
	cmd_buf[2] = (addr >> 8) & 0xFF;
	cmd_buf[3] = addr & 0xFF;

	spi_data_out_tran(spi_base, src_buf, cmd_buf, 1, 3, size);
	return 0;
}

static void spi_sector_erase(uint64_t spi_base, uint32_t addr)
{
	uint8_t cmd_buf[4];

	memset(cmd_buf, 0, sizeof(cmd_buf));

	cmd_buf[0] = SPI_CMD_SE;
	cmd_buf[1] = (addr >> 16) & 0xFF;
	cmd_buf[2] = (addr >> 8) & 0xFF;
	cmd_buf[3] = addr & 0xFF;

	spi_non_data_tran(spi_base, cmd_buf, 1, 3);
}

int bm_spi_flash_erase_sector(uint64_t spi_base, uint32_t addr)
{
	uint32_t spi_status, wait = 0;

	spi_write_en(spi_base);
	spi_status = spi_read_status(spi_base);
	printf("erasing one sector.....\r\n");
	if ((spi_status & SPI_STATUS_WEL) == 0) {
		ERROR("write en failed, get status: 0x%x\n", spi_status);
		return -1;
	}

	spi_sector_erase(spi_base, addr);

	while (1) {
		mdelay(100);
		spi_status = spi_read_status(spi_base);
		if (((spi_status & SPI_STATUS_WIP) == 0) || (wait > 30)) { // 3s, spec 0.15~1s
			VERBOSE("sector erase done, get status: 0x%x, wait: %d\n", spi_status, wait);
			break;
		}
		wait++;
		VERBOSE("device busy, get status: 0x%x\n", spi_status);
	}

	return 0;
}

#if 0
static int do_bulk_erase(unsigned long spi_base)
{
	uint8_t spi_status;
	uint32_t wait = 0;

	VERBOSE("do bulkd erase\n");

	spi_write_en(spi_base);
	spi_status = spi_read_status(spi_base);
	if ((spi_status & SPI_STATUS_WEL) == 0) {
		ERROR("write en failed, get status: 0x%x\n", spi_status);
		return -1;
	}

	spi_bulk_erase(spi_base);

	while (1) {
		mdelay(1000);
		spi_status = spi_read_status(spi_base);
		if (((spi_status & SPI_STATUS_WIP) == 0) || (wait > 300)) { // 300s, spec 38~114s
			VERBOSE("bulk erase done, get status: 0x%x, wait: %d\n", spi_status, wait);
			break;
		}
		wait++;
		VERBOSE("device busy, get status: 0x%x\n", spi_status);
	}

	return 0;
}
#endif

int bm_spi_flash_program_sector(uint64_t spi_base, uint32_t addr)
{
	uint8_t cmd_buf[256], spi_status;
	uint32_t wait = 0;

	memset(cmd_buf, 0x5A, sizeof(cmd_buf));
	spi_write_en(spi_base);

	spi_status = spi_read_status(spi_base);
	if ((spi_status & SPI_STATUS_WEL) == 0) {
		ERROR("spi status check failed, get status: 0x%x\n", spi_status);
		return -1;
	}

	spi_page_program(spi_base, cmd_buf, addr, sizeof(cmd_buf));

	while (1) {
		udelay(100);
		spi_status = spi_read_status(spi_base);
		if (((spi_status & SPI_STATUS_WIP) == 0) || (wait > 600)) { // 60ms, spec 120~2800us
			VERBOSE("sector prog done, get status: 0x%x\n", spi_status);
			break;
		}
		wait++;
		VERBOSE("device busy, get status: 0x%x\n", spi_status);
	}
	return 0;
}

static int do_page_program(uint64_t spi_base, uint8_t *src_buf, uint32_t addr, uint32_t size)
{
	uint8_t spi_status;
	uint32_t wait = 0;

	VERBOSE("do page prog @ 0x%x, size: %d\n", addr, size);

	if (size > SPI_FLASH_BLOCK_SIZE) {
		ERROR("size larger than a page\n");
		return -1;
	}
	if ((addr % SPI_FLASH_BLOCK_SIZE) != 0) {
		ERROR("addr not alignned to page\n");
		return -1;
	}

	spi_write_en(spi_base);

	spi_status = spi_read_status(spi_base);
	if ((spi_status & SPI_STATUS_WEL) == 0) {
		ERROR("spi status check failed, get status: 0x%x\n", spi_status);
		return -1;
	}

	spi_page_program(spi_base, src_buf, addr, size);

	while (1) {
		udelay(100);
		spi_status = spi_read_status(spi_base);
		if (((spi_status & SPI_STATUS_WIP) == 0) || (wait > 600)) { // 60ms, spec 120~2800us
			VERBOSE("page prog done, get status: 0x%x\n", spi_status);
			break;
		}
		wait++;
		VERBOSE("device busy, get status: 0x%x\n", spi_status);
	}
	return 0;
}

static void dump_hex(char *desc, void *addr, int len)
{
	int i;
	unsigned char buff[17];
	unsigned char *pc = (unsigned char *)addr;

	/* Output description if given. */
	if (desc != NULL)
		printf("%s:\n", desc);

	/* Process every byte in the data. */
	for (i = 0; i < len; i++) {
		/* Multiple of 16 means new line (with line offset). */
		if ((i % 16) == 0) {
			/* Just don't print ASCII for the zeroth line. */
			if (i != 0)
				printf("  %s\n", buff);

			/* Output the offset. */
			printf("  %x ", i);
		}

		/* Now the hex code for the specific character. */
		printf(" %x", pc[i]);

		/* And store a printable ASCII character for later. */
		if ((pc[i] < 0x20) || (pc[i] > 0x7e))
			buff[i % 16] = '.';
		else
			buff[i % 16] = pc[i];
		buff[(i % 16) + 1] = '\0';
	}

	/* Pad out last line if not exactly 16 characters. */
	while ((i % 16) != 0) {
		printf("   ");
		i++;
	}

	/* And print the final ASCII bit. */
	printf("  %s\n", buff);
}

static void fw_sp_read_test(uint64_t spi_base, uint32_t addr)
{
	unsigned char cmp_buf[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

	spi_data_read(spi_base, cmp_buf, addr, sizeof(cmp_buf));
	dump_hex((char *)"read flash", (void *)cmp_buf, sizeof(cmp_buf));
}

int bm_spi_flash_program(uint8_t *src_buf, uint32_t base, uint32_t size)
{
	uint32_t xfer_size, off, cmp_ret;
	uint8_t cmp_buf[SPI_FLASH_BLOCK_SIZE], erased_sectors, i;
	uint32_t id, sector_size;

	id = bm_spi_read_id(mem_rw_ops.ctrl_base);
	if (id == SPI_ID_M25P128) {
		sector_size = 256 * 1024;
	} else if (id == SPI_ID_N25Q128 || id == SPI_ID_GD25LQ128 ||
			id == SPI_ID_GD25LB512ME || id == SPI_ID_XM25QU128C ||
			id == SPI_ID_W25Q128FW) {
		sector_size = 64 * 1024; /* each block size = 64KB */
	} else {
		ERROR("unrecognized flash ID 0x%x\n", id);
		return -EINVAL;
	}

	if ((base % sector_size) != 0) {
		ERROR("<flash offset addr> is not aligned erase sector size (0x%x)!\n", sector_size);
		return -EINVAL;
	}

	erased_sectors = (size + sector_size) / sector_size;
	INFO("Start erasing %d sectors, each %d bytes...\n", erased_sectors, sector_size);

	for (i = 0; i < erased_sectors; i++)
		bm_spi_flash_erase_sector(mem_rw_ops.ctrl_base, base + i * sector_size);

	fw_sp_read_test(mem_rw_ops.ctrl_base, base);
	INFO("--program boot fw, page size %d\n", SPI_FLASH_BLOCK_SIZE);

	off = 0;
	i = 0;
	printf("progress:    ");
	while (off < size) {
		if ((size - off) >= SPI_FLASH_BLOCK_SIZE)
			xfer_size = SPI_FLASH_BLOCK_SIZE;
		else
			xfer_size = size - off;
		if (xfer_size < SPI_MAX_FIFO_DEPTH)
			xfer_size = SPI_MAX_FIFO_DEPTH;

		if (do_page_program(mem_rw_ops.ctrl_base, src_buf + off, base + off, xfer_size) != 0) {
			ERROR("page prog failed @ 0x%x\n", base + off);
			return -1;
		}

		spi_data_read(mem_rw_ops.ctrl_base, cmp_buf, base + off, xfer_size);
		cmp_ret = memcmp(src_buf + off, cmp_buf, xfer_size);
		if (cmp_ret != 0) {
			ERROR("memcmp failed\n");
			dump_hex((char *)"src_buf", (void *)(src_buf + off), xfer_size);
			dump_hex((char *)"cmp_buf", (void *)cmp_buf, xfer_size);
			return cmp_ret;
		}
		VERBOSE("page read compare ok @ %d\n", off);
		off += xfer_size;

		printf("\b\b\b%2d%%", off * 100 / size);
	}
	printf("\n");

	INFO("--program boot fw success\n");
	fw_sp_read_test(mem_rw_ops.ctrl_base, base);

	return 0;
}

int bm_spi_flash_write_status(uint16_t value)
{
	uint32_t id, spi_status, wait = 0;

	id = bm_spi_read_id(mem_rw_ops.ctrl_base);
	if ((id != SPI_ID_GD25LQ128) &&
			(id != SPI_ID_M25P128) &&
			(id != SPI_ID_N25Q128) &&
			(id != SPI_ID_GD25LB512ME) &&
			(id != SPI_ID_XM25QU128C) &&
			(id != SPI_ID_W25Q128FW)) {
		ERROR("unrecognized flash ID 0x%x\n", id);
		return -EINVAL;
	}
	spi_status = spi_read_status(mem_rw_ops.ctrl_base) |
		(spi_read_status_high(mem_rw_ops.ctrl_base) << 8);
	INFO("status before: 0x%x\n", spi_status);
	spi_write_en(mem_rw_ops.ctrl_base);
	spi_status = spi_read_status(mem_rw_ops.ctrl_base);
	if ((spi_status & SPI_STATUS_WEL) == 0) {
		ERROR("write en failed, get status: 0x%x\n", spi_status);
		return -1;
	}
	spi_write_status(mem_rw_ops.ctrl_base, value);

	while (1) {
		mdelay(100);
		spi_status = spi_read_status(mem_rw_ops.ctrl_base) |
			(spi_read_status_high(mem_rw_ops.ctrl_base) << 8);
		if (((spi_status & SPI_STATUS_WIP) == 0) || (wait > 30)) { // 3s, spec 0.15~1s
			VERBOSE("sector erase done, get status: 0x%x, wait: %d\n", spi_status, wait);
			break;
		}
		wait++;
		VERBOSE("device busy, get status: 0x%x\n", spi_status);
	}
	printf("status after: 0x%x\n", spi_status);
	return 0;
}

