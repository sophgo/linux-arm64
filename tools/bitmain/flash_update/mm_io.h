#ifndef __MM_IO__
#define __MM_IO__

void *devm_map(uint64_t addr, int len);
void devm_unmap(void *virt_addr, int len);
void *mmio_init(uint64_t addr);
void mmio_relase(void *virt_addr);
uint32_t mmio_read_32(uint64_t addr);
void mmio_write_32(uint64_t addr, uint32_t val);
uint8_t mmio_read_8(uint64_t addr);
void mmio_write_8(uint64_t addr, uint8_t val);


#endif
