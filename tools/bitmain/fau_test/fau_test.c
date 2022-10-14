#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../../../drivers/soc/bitmain/uapi/ion_bitmain.h"
#include <getopt.h>
#include <sys/time.h>
#include <time.h>

#define FAU_OP_PHY   _IOWR('F', 0x01, unsigned long)
#define debug(...)					\
	do {						\
		if (1)					\
			fprintf(stderr, __VA_ARGS__);	\
	} while (0)

#define error(...)	fprintf(stderr, __VA_ARGS__)

typedef unsigned char       uint8_t;
typedef   signed char        int8_t;
typedef unsigned short     uint16_t;
typedef   signed short      int16_t;
typedef unsigned int       uint32_t;
typedef   signed int        int32_t;
typedef unsigned long long uint64_t;

static int ion_fd;
static int mem_fd;

struct timespec tp;
struct timeval tv_start;
struct timeval tv_end;
struct timeval timediff;
unsigned long consume;

enum fau_type {
	MERKEL = 0,
	SDR,
};

struct fau_batch {
	enum fau_type fau_type;
	uint8_t index;
	union {
		struct fau_merkel {
			void *addr;
			uint64_t sector_size;
		} fau_merkel_t;
		struct fau_sdr {
			void *base;
			void *exp;
			void *coord;
			uint8_t repid[32];
			uint8_t layerid;
			uint32_t nodeid;
			uint32_t loops;
		} fau_sdr_t;
	} u;
};

uint8_t repid[32] = {
	0x0D,	0x93,	0xB5,	0x9E,	0xB9,	0xC7,	0x56,	0x37,
	0xAA,	0x7F,	0x72,	0x0C,	0x83,	0xCB,	0xBB,	0xFE,
	0x1D,	0xBA,	0x68,	0xEC,	0xD7,	0x5C,	0xC1,	0xFC,
	0xDE,	0xC8,	0x51,	0xC2,	0x7A,	0xD8,	0x9C,	0x31,
};

static const uint32_t K256[] = {
		0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
		0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
		0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
		0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
		0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
		0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
		0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
		0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
		0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
		0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
		0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
		0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
		0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
		0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
		0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
		0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2};

#define ROTATE(x, y) (((x) >> (y)) | ((x) << (32 - (y))))
#define Sigma0(x) (ROTATE((x), 2) ^ ROTATE((x), 13) ^ ROTATE((x), 22))
#define Sigma1(x) (ROTATE((x), 6) ^ ROTATE((x), 11) ^ ROTATE((x), 25))
#define sigma0(x) (ROTATE((x), 7) ^ ROTATE((x), 18) ^ ((x) >> 3))
#define sigma1(x) (ROTATE((x), 17) ^ ROTATE((x), 19) ^ ((x) >> 10))

#define Ch(x, y, z) (((x) & (y)) ^ ((~(x)) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

#define BIG_LITTLE_SWAP32(x) ((((*(uint32_t *)&(x)) & 0xff000000) >> 24) | \
							  (((*(uint32_t *)&(x)) & 0x00ff0000) >> 8) |  \
							  (((*(uint32_t *)&(x)) & 0x0000ff00) << 8) |  \
							  (((*(uint32_t *)&(x)) & 0x000000ff) << 24))

/* Avoid undefined behavior                    */
/* https://stackoverflow.com/q/29538935/608639 */
uint32_t B2U32(uint8_t val, uint8_t sh)
{
	return ((uint32_t)val) << sh;
}

/* Process multiple blocks. The caller is responsible for setting the initial */
/*  state, and the caller is responsible for padding the final block.        */
void sha256_process(uint32_t state[8], const uint8_t data[], uint32_t length)
{
	uint32_t a, b, c, d, e, f, g, h, s0, s1, T1, T2;
	uint32_t X[16], i;

	size_t blocks = length / 64;

	while (blocks--) {
		a = state[0];
		b = state[1];
		c = state[2];
		d = state[3];
		e = state[4];
		f = state[5];
		g = state[6];
		h = state[7];

		for (i = 0; i < 16; i++) {
			X[i] = B2U32(data[0], 24) | B2U32(data[1], 16) | B2U32(data[2], 8) | B2U32(data[3], 0);
			data += 4;

			T1 = h;
			T1 += Sigma1(e);
			T1 += Ch(e, f, g);
			T1 += K256[i];
			T1 += X[i];

			T2 = Sigma0(a);
			T2 += Maj(a, b, c);

			h = g;
			g = f;
			f = e;
			e = d + T1;
			d = c;
			c = b;
			b = a;
			a = T1 + T2;
		}

		for (; i < 64; i++)	{
			s0 = X[(i + 1) & 0x0f];
			s0 = sigma0(s0);
			s1 = X[(i + 14) & 0x0f];
			s1 = sigma1(s1);

			T1 = X[i & 0xf] += s0 + s1 + X[(i + 9) & 0xf];
			T1 += h + Sigma1(e) + Ch(e, f, g) + K256[i];
			T2 = Sigma0(a) + Maj(a, b, c);
			h = g;
			g = f;
			f = e;
			e = d + T1;
			d = c;
			c = b;
			b = a;
			a = T1 + T2;
		}

		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
		state[5] += f;
		state[6] += g;
		state[7] += h;
	}
}

int SDR_calc_node0(uint8_t *addr, uint8_t *repid, uint8_t layerid)
{
	uint8_t message[128];
	int i = 0;
	/* initial state */
	uint32_t state[8] = {
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
		0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

	memset(message, 0x00, sizeof(message));
	memcpy(message, repid, 32);
	message[35] = layerid;
	message[64] = 0x80;
	message[126] = 0x02;
	sha256_process(state, message, sizeof(message));
	for (i = 0; i < 8; i++)
		state[i] = BIG_LITTLE_SWAP32(state[i]);

	state[7] &= 0x3fffffff;
	memcpy(addr, (uint8_t *)state, 32);

	return 0;
}

void dma_free(void)
{
	if (mem_fd >= 0)
		close(mem_fd);
}

void *dma_alloc(unsigned long len, unsigned long *dma_addr)
{
	void *virt_addr = NULL;
	struct ion_heap_query heap_query;
	struct ion_heap_data heap_data[8];
	struct ion_allocation_data ion_data;
	int err, i;
	unsigned long pa;
	unsigned long size;
	void *va;

	ion_fd = open("/dev/ion", O_RDWR);
	if (ion_fd < 0) {
		error("open ion device failed\n");
		return NULL;
	}

	memset(&heap_query, 0x00, sizeof(heap_query));
	memset(heap_data, 0x00, sizeof(heap_data));
	err = ioctl(ion_fd, ION_IOC_HEAP_QUERY, &heap_query);
	if (err < 0 || heap_query.cnt == 0) {
		error("ion query heap count failed\n");
		goto close_ion_fd;
	}
	heap_query.heaps = (unsigned long long)heap_data;
	err = ioctl(ion_fd, ION_IOC_HEAP_QUERY, &heap_query);
	if (err < 0) {
		error("ion query heap data failed\n");
		goto close_ion_fd;
	}

	for (i = 0; i < heap_query.cnt; ++i) {
		debug("heap %d, type %d, name %s, id %u\n",
		       i, heap_data[i].type, heap_data[i].name,
		       heap_data[i].heap_id);
	}
	memset(&ion_data, 0x00, sizeof(ion_data));

	ion_data.len = len;
	/* 0 means allocate cache choerent memory */
	/* 1 means allocate none cache choerent memory */
	ion_data.flags = 0;
	/* select first heap */
	ion_data.heap_id_mask = 1 << heap_data[0].heap_id;

	err = ioctl(ion_fd, ION_IOC_ALLOC, &ion_data);
	if (err < 0) {
		error("failed allocate buffer from ion\n");
		goto close_ion_fd;
	}

	mem_fd = ion_data.fd;
	pa = ion_data.paddr;
	size = ion_data.len;
	/* map memory */
	va = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED,
			mem_fd, 0);
	if (va == MAP_FAILED) {
		error("map allocated memory failed\n");
		close(mem_fd);
		mem_fd = -1;
		goto close_ion_fd;
	}

	debug("physicall address %p mapped on virtual address %p\n"
	       "with %lu bytes long\n",
	       (void *)pa, va, size);
	virt_addr = va;
	*dma_addr = pa;
close_ion_fd:
	close(ion_fd);
	return virt_addr;
}

int fau_op_phy(struct fau_batch *fau_batch)
{
	int err;
	int fau_fd;

	fau_fd = open("/dev/mango-fau", O_RDWR);
	if (fau_fd < 0) {
		error("cannot open fau device node\n");
		err = fau_fd;
		return err;
	}

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp);
	srand(tp.tv_nsec);
	gettimeofday(&tv_start, NULL);

	err = ioctl(fau_fd, FAU_OP_PHY, fau_batch);

	gettimeofday(&tv_end, NULL);
	timersub(&tv_end, &tv_start, &timediff);
	consume = timediff.tv_sec * 1000000 + timediff.tv_usec;

	printf("FAU_OP_PHY spend time= %ld\n", consume);
	if (err < 0) {
		error("fau ioctl operation on physicall address failed\n");
		goto close_fau_fd;
	}
	err = 0;
close_fau_fd:
	close(fau_fd);
	return err;
}

long get_file_size(char *file_name)
{
	struct stat st;

	if (stat(file_name, &st)) {
		error("cannot get file status\n");
		return -1;
	}

	return st.st_size;
}

int load_file(char *file_name, void *va)
{
	int err;
	long file_size;
	int fd;

	file_size = get_file_size(file_name);
	if (file_size < 0)
		return -1;

	fd = open(file_name, O_RDONLY);
	if (fd < 0) {
		error("cannot open file %s\n", file_name);
		return -1;
	}

	if (read(fd, va, file_size) != file_size) {
		error("cannot read file\n");
		err = -1;
		goto close_fd;
	}

	err = 0;
close_fd:
	close(fd);
	return err;
}

int mango_fau_module_test(int index)
{
	int err;
	long file_size;
	unsigned long total_size;
	unsigned char *src_va;
	unsigned char *dst_va;
	unsigned long src_pa;
	struct fau_batch fau_batch;

	file_size = get_file_size("/usr/staged-file");
	if (file_size < 0)
		return 1;

	total_size = file_size * 4; //merkel:4k, sdr:2k+2k+4k
	src_va = dma_alloc(total_size, &src_pa);
	if (src_va == NULL)
		return 1;
	dst_va = malloc(total_size);//for ref value
	if (dst_va == NULL)
		return 1;

	//calc merkel tree
	if (load_file("/usr/staged-file", src_va) < 0) {
		err = 1;
		printf("open staged-file failed\n");
		goto free_mem;
	}

	fau_batch.fau_type = 0;
	fau_batch.index = index;
	fau_batch.u.fau_merkel_t.addr = (void *)src_pa;
	fau_batch.u.fau_merkel_t.sector_size = 2048;

	printf("start merkel\n");
	err = fau_op_phy(&fau_batch);
	if (err) {
		error("merkel calc failed\n");
		err = 1;
		goto free_mem;
	}

	file_size = get_file_size("/usr/sc-02-data-tree-d.dat");
	if (file_size < 0) {
		printf("tree-d size is %ld\n", file_size);
		err = 1;
		goto free_mem;
	} else
		printf("tree-d size is %ld\n", file_size);

	if (load_file("/usr/sc-02-data-tree-d.dat", dst_va) < 0) {
		err = 1;
		goto free_mem;
	}
	if (memcmp(dst_va, src_va, file_size))
		printf("merkel cmp failed\n");
	else
		printf("merkel cmp success\n");

	//calc sdr1
	if (load_file("/usr/coord.cache", src_va + 4096) < 0) {
		err = 1;
		printf("open coord.cache failed\n");
		goto free_mem;
	}
	SDR_calc_node0(src_va, repid, 1);

	fau_batch.fau_type = 1;
	fau_batch.index = index;
	fau_batch.u.fau_sdr_t.base = (void *)src_pa;
	fau_batch.u.fau_sdr_t.exp = NULL;
	fau_batch.u.fau_sdr_t.coord = (void *)(src_pa + 4096);
	memcpy(fau_batch.u.fau_sdr_t.repid, repid, 32);
	fau_batch.u.fau_sdr_t.layerid = 1;
	fau_batch.u.fau_sdr_t.nodeid = 1;
	fau_batch.u.fau_sdr_t.loops = 63;
	printf("start sdr1 calc\n");
	err = fau_op_phy(&fau_batch);
	if (err) {
		error("sdr1 calc error\n");
		err = 1;
		goto free_mem;
	}

	file_size = get_file_size("/usr/sc-02-data-layer-1.dat");
	if (file_size < 0) {
		err = 1;
		goto free_mem;
	} else
		printf("sdr1 size is %ld\n", file_size);


	if (load_file("/usr/sc-02-data-layer-1.dat", dst_va) < 0) {
		err = 1;
		goto free_mem;
	}
	if (memcmp(dst_va, src_va, file_size))
		printf("sdr1 cmp failed\n");
	else
		printf("sdr1 cmp success\n");

	//calc sdr2
	SDR_calc_node0(src_va + 2048, repid, 2);
	//fau_batch.fau_type = 1;
	//fau_batch.index = index;
	fau_batch.u.fau_sdr_t.base = (void *)(src_pa + 2048);
	fau_batch.u.fau_sdr_t.exp = (void *)src_pa;
	//fau_batch.u.fau_sdr_t.coord = src_pa + 4096;
	//memcpy(fau_batch.u.fau_sdr_t.repid ,repid, 32);
	fau_batch.u.fau_sdr_t.layerid = 2;
	//fau_batch.u.fau_sdr_t.nodeid = 1;
	//fau_batch.u.fau_sdr_t.loops = 63;
	printf("start sdr2\n");
	err = fau_op_phy(&fau_batch);
	if (err) {
		error("sdr2 calc error\n");
		err = 1;
		goto free_mem;
	}

	file_size = get_file_size("/usr/sc-02-data-layer-2.dat");
	if (file_size < 0) {
		err = 1;
		goto free_mem;
	} else
		printf("sdr2 size is %ld\n", file_size);

	if (load_file("/usr/sc-02-data-layer-2.dat", dst_va) < 0) {
		err = 1;
		goto free_mem;
	}
	if (memcmp(dst_va, src_va+2048, file_size))
		printf("sdr2 cmp failed\n");
	else
		printf("sdr2 cmp success\n");

	err = 0;
free_mem:
	dma_free();
	free(dst_va);
	return err;
}

int mango_fau_test(void)
{
	int ret = 0;

	printf("fau_test start\n");
	for (int i = 0; i < 3; i++)
		ret |= mango_fau_module_test(i);

	if (ret)
		error("fau_test failed\n");
	else
		printf("fau_test success\n");
	return ret;
}


int main(int argc, char * const argv[])
{
	return mango_fau_test();
}

