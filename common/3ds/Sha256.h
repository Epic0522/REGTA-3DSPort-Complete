#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace RegtaInstall
{
struct Sha256 {
	uint32_t state[8];
	uint64_t bytes;
	unsigned char block[64];
	unsigned used;

	static uint32_t rotate(uint32_t value, unsigned bits) { return value >> bits | value << (32 - bits); }
	static uint32_t load(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
	static void store(unsigned char *p, uint32_t value)
	{
		p[0] = value >> 24;
		p[1] = value >> 16;
		p[2] = value >> 8;
		p[3] = value;
	}
	Sha256() : bytes(0), used(0)
	{
		const uint32_t initial[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
		memcpy(state, initial, sizeof(state));
	}
	void transform(const unsigned char *data)
	{
		static const uint32_t k[64] = {0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
		                               0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
		                               0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
		                               0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
		                               0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
		                               0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
		                               0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
		                               0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
		uint32_t w[64];
		for(unsigned i = 0; i < 16; ++i) w[i] = load(data + i * 4);
		for(unsigned i = 16; i < 64; ++i) {
			uint32_t x = w[i - 15], y = w[i - 2];
			w[i] = w[i - 16] + (rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3)) + w[i - 7] + (rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10));
		}
		uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6], h = state[7];
		for(unsigned i = 0; i < 64; ++i) {
			uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
			uint32_t ch = (e & f) ^ (~e & g);
			uint32_t t1 = h + s1 + ch + k[i] + w[i];
			uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
			uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
			uint32_t t2 = s0 + maj;
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
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
	void update(const void *input, size_t size)
	{
		const unsigned char *data = (const unsigned char *)input;
		bytes += size;
		while(size) {
			unsigned take = 64 - used;
			if(take > size) take = (unsigned)size;
			memcpy(block + used, data, take);
			used += take;
			data += take;
			size -= take;
			if(used == 64) {
				transform(block);
				used = 0;
			}
		}
	}
	void finish(unsigned char digest[32])
	{
		const uint64_t bits = bytes * 8;
		block[used++] = 0x80;
		if(used > 56) {
			while(used < 64) block[used++] = 0;
			transform(block);
			used = 0;
		}
		while(used < 56) block[used++] = 0;
		for(unsigned i = 0; i < 8; ++i) block[63 - i] = (unsigned char)(bits >> (i * 8));
		transform(block);
		for(unsigned i = 0; i < 8; ++i) store(digest + i * 4, state[i]);
	}
};
} // namespace RegtaInstall
