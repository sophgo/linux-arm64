#include <crypto/sha.h>
#include <crypto/sha256_base.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <linux/cryptohash.h>
#include <crypto/sm3.h>
#include <crypto/sm3_base.h>
#include <linux/kernel.h>


static inline u32 Ch(u32 x, u32 y, u32 z)
{
	return z ^ (x & (y ^ z));
}

static inline u32 Maj(u32 x, u32 y, u32 z)
{
	return (x & y) | (z & (x | y));
}

#define e0(x)       (ror32(x, 2) ^ ror32(x, 13) ^ ror32(x, 22))
#define e1(x)       (ror32(x, 6) ^ ror32(x, 11) ^ ror32(x, 25))
#define s0(x)       (ror32(x, 7) ^ ror32(x, 18) ^ (x >> 3))
#define s1(x)       (ror32(x, 17) ^ ror32(x, 19) ^ (x >> 10))

static inline void LOAD_OP(int I, u32 *W, const u8 *input)
{
	W[I] = get_unaligned_be32((__u32 *)input + I);
}

static inline void BLEND_OP(int I, u32 *W)
{
	W[I] = s1(W[I-2]) + W[I-7] + s0(W[I-15]) + W[I-16];
}

void ce_sha256_transform(u32 *state, const u8 *input)
{
	u32 a, b, c, d, e, f, g, h, t1, t2;
	u32 W[64];
	int i;

	/* load the input */
	for (i = 0; i < 16; i++)
		LOAD_OP(i, W, input);

	/* now blend */
	for (i = 16; i < 64; i++)
		BLEND_OP(i, W);

	/* load the state into our registers */
	a = state[0];  b = state[1];  c = state[2];  d = state[3];
	e = state[4];  f = state[5];  g = state[6];  h = state[7];

	/* now iterate */
	t1 = h + e1(e) + Ch(e, f, g) + 0x428a2f98 + W[0];
	t2 = e0(a) + Maj(a, b, c);    d += t1;    h = t1 + t2;
	t1 = g + e1(d) + Ch(d, e, f) + 0x71374491 + W[1];
	t2 = e0(h) + Maj(h, a, b);    c += t1;    g = t1 + t2;
	t1 = f + e1(c) + Ch(c, d, e) + 0xb5c0fbcf + W[2];
	t2 = e0(g) + Maj(g, h, a);    b += t1;    f = t1 + t2;
	t1 = e + e1(b) + Ch(b, c, d) + 0xe9b5dba5 + W[3];
	t2 = e0(f) + Maj(f, g, h);    a += t1;    e = t1 + t2;
	t1 = d + e1(a) + Ch(a, b, c) + 0x3956c25b + W[4];
	t2 = e0(e) + Maj(e, f, g);    h += t1;    d = t1 + t2;
	t1 = c + e1(h) + Ch(h, a, b) + 0x59f111f1 + W[5];
	t2 = e0(d) + Maj(d, e, f);    g += t1;    c = t1 + t2;
	t1 = b + e1(g) + Ch(g, h, a) + 0x923f82a4 + W[6];
	t2 = e0(c) + Maj(c, d, e);    f += t1;    b = t1 + t2;
	t1 = a + e1(f) + Ch(f, g, h) + 0xab1c5ed5 + W[7];
	t2 = e0(b) + Maj(b, c, d);    e += t1;    a = t1 + t2;

	t1 = h + e1(e) + Ch(e, f, g) + 0xd807aa98 + W[8];
	t2 = e0(a) + Maj(a, b, c);    d += t1;    h = t1 + t2;
	t1 = g + e1(d) + Ch(d, e, f) + 0x12835b01 + W[9];
	t2 = e0(h) + Maj(h, a, b);    c += t1;    g = t1 + t2;
	t1 = f + e1(c) + Ch(c, d, e) + 0x243185be + W[10];
	t2 = e0(g) + Maj(g, h, a);    b += t1;    f = t1 + t2;
	t1 = e + e1(b) + Ch(b, c, d) + 0x550c7dc3 + W[11];
	t2 = e0(f) + Maj(f, g, h);    a += t1;    e = t1 + t2;
	t1 = d + e1(a) + Ch(a, b, c) + 0x72be5d74 + W[12];
	t2 = e0(e) + Maj(e, f, g);    h += t1;    d = t1 + t2;
	t1 = c + e1(h) + Ch(h, a, b) + 0x80deb1fe + W[13];
	t2 = e0(d) + Maj(d, e, f);    g += t1;    c = t1 + t2;
	t1 = b + e1(g) + Ch(g, h, a) + 0x9bdc06a7 + W[14];
	t2 = e0(c) + Maj(c, d, e);    f += t1;    b = t1 + t2;
	t1 = a + e1(f) + Ch(f, g, h) + 0xc19bf174 + W[15];
	t2 = e0(b) + Maj(b, c, d);    e += t1;    a = t1 + t2;

	t1 = h + e1(e) + Ch(e, f, g) + 0xe49b69c1 + W[16];
	t2 = e0(a) + Maj(a, b, c);    d += t1;    h = t1 + t2;
	t1 = g + e1(d) + Ch(d, e, f) + 0xefbe4786 + W[17];
	t2 = e0(h) + Maj(h, a, b);    c += t1;    g = t1 + t2;
	t1 = f + e1(c) + Ch(c, d, e) + 0x0fc19dc6 + W[18];
	t2 = e0(g) + Maj(g, h, a);    b += t1;    f = t1 + t2;
	t1 = e + e1(b) + Ch(b, c, d) + 0x240ca1cc + W[19];
	t2 = e0(f) + Maj(f, g, h);    a += t1;    e = t1 + t2;
	t1 = d + e1(a) + Ch(a, b, c) + 0x2de92c6f + W[20];
	t2 = e0(e) + Maj(e, f, g);    h += t1;    d = t1 + t2;
	t1 = c + e1(h) + Ch(h, a, b) + 0x4a7484aa + W[21];
	t2 = e0(d) + Maj(d, e, f);    g += t1;    c = t1 + t2;
	t1 = b + e1(g) + Ch(g, h, a) + 0x5cb0a9dc + W[22];
	t2 = e0(c) + Maj(c, d, e);    f += t1;    b = t1 + t2;
	t1 = a + e1(f) + Ch(f, g, h) + 0x76f988da + W[23];
	t2 = e0(b) + Maj(b, c, d);    e += t1;    a = t1 + t2;

	t1 = h + e1(e) + Ch(e, f, g) + 0x983e5152 + W[24];
	t2 = e0(a) + Maj(a, b, c);    d += t1;    h = t1 + t2;
	t1 = g + e1(d) + Ch(d, e, f) + 0xa831c66d + W[25];
	t2 = e0(h) + Maj(h, a, b);    c += t1;    g = t1 + t2;
	t1 = f + e1(c) + Ch(c, d, e) + 0xb00327c8 + W[26];
	t2 = e0(g) + Maj(g, h, a);    b += t1;    f = t1 + t2;
	t1 = e + e1(b) + Ch(b, c, d) + 0xbf597fc7 + W[27];
	t2 = e0(f) + Maj(f, g, h);    a += t1;    e = t1 + t2;
	t1 = d + e1(a) + Ch(a, b, c) + 0xc6e00bf3 + W[28];
	t2 = e0(e) + Maj(e, f, g);    h += t1;    d = t1 + t2;
	t1 = c + e1(h) + Ch(h, a, b) + 0xd5a79147 + W[29];
	t2 = e0(d) + Maj(d, e, f);    g += t1;    c = t1 + t2;
	t1 = b + e1(g) + Ch(g, h, a) + 0x06ca6351 + W[30];
	t2 = e0(c) + Maj(c, d, e);    f += t1;    b = t1 + t2;
	t1 = a + e1(f) + Ch(f, g, h) + 0x14292967 + W[31];
	t2 = e0(b) + Maj(b, c, d);    e += t1;    a = t1 + t2;

	t1 = h + e1(e) + Ch(e, f, g) + 0x27b70a85 + W[32];
	t2 = e0(a) + Maj(a, b, c);    d += t1;    h = t1 + t2;
	t1 = g + e1(d) + Ch(d, e, f) + 0x2e1b2138 + W[33];
	t2 = e0(h) + Maj(h, a, b);    c += t1;    g = t1 + t2;
	t1 = f + e1(c) + Ch(c, d, e) + 0x4d2c6dfc + W[34];
	t2 = e0(g) + Maj(g, h, a);    b += t1;    f = t1 + t2;
	t1 = e + e1(b) + Ch(b, c, d) + 0x53380d13 + W[35];
	t2 = e0(f) + Maj(f, g, h);    a += t1;    e = t1 + t2;
	t1 = d + e1(a) + Ch(a, b, c) + 0x650a7354 + W[36];
	t2 = e0(e) + Maj(e, f, g);    h += t1;    d = t1 + t2;
	t1 = c + e1(h) + Ch(h, a, b) + 0x766a0abb + W[37];
	t2 = e0(d) + Maj(d, e, f);    g += t1;    c = t1 + t2;
	t1 = b + e1(g) + Ch(g, h, a) + 0x81c2c92e + W[38];
	t2 = e0(c) + Maj(c, d, e);    f += t1;    b = t1 + t2;
	t1 = a + e1(f) + Ch(f, g, h) + 0x92722c85 + W[39];
	t2 = e0(b) + Maj(b, c, d);    e += t1;    a = t1 + t2;

	t1 = h + e1(e) + Ch(e, f, g) + 0xa2bfe8a1 + W[40];
	t2 = e0(a) + Maj(a, b, c);    d += t1;    h = t1 + t2;
	t1 = g + e1(d) + Ch(d, e, f) + 0xa81a664b + W[41];
	t2 = e0(h) + Maj(h, a, b);    c += t1;    g = t1 + t2;
	t1 = f + e1(c) + Ch(c, d, e) + 0xc24b8b70 + W[42];
	t2 = e0(g) + Maj(g, h, a);    b += t1;    f = t1 + t2;
	t1 = e + e1(b) + Ch(b, c, d) + 0xc76c51a3 + W[43];
	t2 = e0(f) + Maj(f, g, h);    a += t1;    e = t1 + t2;
	t1 = d + e1(a) + Ch(a, b, c) + 0xd192e819 + W[44];
	t2 = e0(e) + Maj(e, f, g);    h += t1;    d = t1 + t2;
	t1 = c + e1(h) + Ch(h, a, b) + 0xd6990624 + W[45];
	t2 = e0(d) + Maj(d, e, f);    g += t1;    c = t1 + t2;
	t1 = b + e1(g) + Ch(g, h, a) + 0xf40e3585 + W[46];
	t2 = e0(c) + Maj(c, d, e);    f += t1;    b = t1 + t2;
	t1 = a + e1(f) + Ch(f, g, h) + 0x106aa070 + W[47];
	t2 = e0(b) + Maj(b, c, d);    e += t1;    a = t1 + t2;

	t1 = h + e1(e) + Ch(e, f, g) + 0x19a4c116 + W[48];
	t2 = e0(a) + Maj(a, b, c);    d += t1;    h = t1 + t2;
	t1 = g + e1(d) + Ch(d, e, f) + 0x1e376c08 + W[49];
	t2 = e0(h) + Maj(h, a, b);    c += t1;    g = t1 + t2;
	t1 = f + e1(c) + Ch(c, d, e) + 0x2748774c + W[50];
	t2 = e0(g) + Maj(g, h, a);    b += t1;    f = t1 + t2;
	t1 = e + e1(b) + Ch(b, c, d) + 0x34b0bcb5 + W[51];
	t2 = e0(f) + Maj(f, g, h);    a += t1;    e = t1 + t2;
	t1 = d + e1(a) + Ch(a, b, c) + 0x391c0cb3 + W[52];
	t2 = e0(e) + Maj(e, f, g);    h += t1;    d = t1 + t2;
	t1 = c + e1(h) + Ch(h, a, b) + 0x4ed8aa4a + W[53];
	t2 = e0(d) + Maj(d, e, f);    g += t1;    c = t1 + t2;
	t1 = b + e1(g) + Ch(g, h, a) + 0x5b9cca4f + W[54];
	t2 = e0(c) + Maj(c, d, e);    f += t1;    b = t1 + t2;
	t1 = a + e1(f) + Ch(f, g, h) + 0x682e6ff3 + W[55];
	t2 = e0(b) + Maj(b, c, d);    e += t1;    a = t1 + t2;

	t1 = h + e1(e) + Ch(e, f, g) + 0x748f82ee + W[56];
	t2 = e0(a) + Maj(a, b, c);    d += t1;    h = t1 + t2;
	t1 = g + e1(d) + Ch(d, e, f) + 0x78a5636f + W[57];
	t2 = e0(h) + Maj(h, a, b);    c += t1;    g = t1 + t2;
	t1 = f + e1(c) + Ch(c, d, e) + 0x84c87814 + W[58];
	t2 = e0(g) + Maj(g, h, a);    b += t1;    f = t1 + t2;
	t1 = e + e1(b) + Ch(b, c, d) + 0x8cc70208 + W[59];
	t2 = e0(f) + Maj(f, g, h);    a += t1;    e = t1 + t2;
	t1 = d + e1(a) + Ch(a, b, c) + 0x90befffa + W[60];
	t2 = e0(e) + Maj(e, f, g);    h += t1;    d = t1 + t2;
	t1 = c + e1(h) + Ch(h, a, b) + 0xa4506ceb + W[61];
	t2 = e0(d) + Maj(d, e, f);    g += t1;    c = t1 + t2;
	t1 = b + e1(g) + Ch(g, h, a) + 0xbef9a3f7 + W[62];
	t2 = e0(c) + Maj(c, d, e);    f += t1;    b = t1 + t2;
	t1 = a + e1(f) + Ch(f, g, h) + 0xc67178f2 + W[63];
	t2 = e0(b) + Maj(b, c, d);    e += t1;    a = t1 + t2;

	state[0] += a; state[1] += b; state[2] += c; state[3] += d;
	state[4] += e; state[5] += f; state[6] += g; state[7] += h;

	/* clear any sensitive info... */
	a = b = c = d = e = f = g = h = t1 = t2 = 0;
	memzero_explicit(W, 64 * sizeof(u32));
}

void ce_sha1_transform(u32 *state, const u8 *input)
{
	u32 temp[16];

	sha_transform(state, input, temp);
	memzero_explicit(temp, sizeof(temp)); /* secure consideration */
}

static inline u32 p0(u32 x)
{
	return x ^ rol32(x, 9) ^ rol32(x, 17);
}

static inline u32 p1(u32 x)
{
	return x ^ rol32(x, 15) ^ rol32(x, 23);
}
static inline u32 ff(unsigned int n, u32 a, u32 b, u32 c)
{
	return (n < 16) ? (a ^ b ^ c) : ((a & b) | (a & c) | (b & c));
}

static inline u32 gg(unsigned int n, u32 e, u32 f, u32 g)
{
	return (n < 16) ? (e ^ f ^ g) : ((e & f) | ((~e) & g));
}

static inline u32 t(unsigned int n)
{
	return (n < 16) ? SM3_T1 : SM3_T2;
}

static void sm3_expand(u32 *t, u32 *w, u32 *wt)
{
	int i;
	unsigned int tmp;

	/* load the input */
	for (i = 0; i <= 15; i++)
		w[i] = get_unaligned_be32((__u32 *)t + i);

	for (i = 16; i <= 67; i++) {
		tmp = w[i - 16] ^ w[i - 9] ^ rol32(w[i - 3], 15);
		w[i] = p1(tmp) ^ (rol32(w[i - 13], 7)) ^ w[i - 6];
	}

	for (i = 0; i <= 63; i++)
		wt[i] = w[i] ^ w[i + 4];
}

static void sm3_compress(u32 *w, u32 *wt, u32 *state)
{
	u32 ss1;
	u32 ss2;
	u32 tt1;
	u32 tt2;
	u32 a, b, c, d, e, f, g, h;
	int i;

	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];
	f = state[5];
	g = state[6];
	h = state[7];

	for (i = 0; i <= 63; i++) {

		ss1 = rol32((rol32(a, 12) + e + rol32(t(i), i & 31)), 7);

		ss2 = ss1 ^ rol32(a, 12);

		tt1 = ff(i, a, b, c) + d + ss2 + *wt;
		wt++;

		tt2 = gg(i, e, f, g) + h + ss1 + *w;
		w++;

		d = c;
		c = rol32(b, 9);
		b = a;
		a = tt1;
		h = g;
		g = rol32(f, 19);
		f = e;
		e = p0(tt2);
	}

	state[0] = a ^ state[0];
	state[1] = b ^ state[1];
	state[2] = c ^ state[2];
	state[3] = d ^ state[3];
	state[4] = e ^ state[4];
	state[5] = f ^ state[5];
	state[6] = g ^ state[6];
	state[7] = h ^ state[7];

	a = b = c = d = e = f = g = h = ss1 = ss2 = tt1 = tt2 = 0;
}

void ce_sm3_transform(u32 *state, u8 const *input)
{
	unsigned int w[68];
	unsigned int wt[64];

	sm3_expand((u32 *)input, w, wt);
	sm3_compress(w, wt, state);

	memzero_explicit(w, sizeof(w));
	memzero_explicit(wt, sizeof(wt));
}
