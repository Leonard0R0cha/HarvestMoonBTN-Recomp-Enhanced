/* CRC-32 (zlib's polynomial) for parallel-psx's texture tracker hashes. Written for this port. */
#pragma once
#include <stddef.h>
#include <stdint.h>

static inline uint32_t crc32(uint32_t crc, const unsigned char *p, size_t n)
{
	crc = ~crc;
	while (n--)
	{
		crc ^= *p++;
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}
