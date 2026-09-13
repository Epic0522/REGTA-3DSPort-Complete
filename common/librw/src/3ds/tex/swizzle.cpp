/*------------------------------------------------------------------------------
 * Copyright (c) 2017-2019
 *     Michael Theall (mtheall)
 *
 * This file (was) part of tex3ds.
 *
 * tex3ds is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * tex3ds is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with tex3ds.  If not, see <http://www.gnu.org/licenses/>.
 *----------------------------------------------------------------------------*/

/* If I've violated the GPL, 
   then send Stallman and his lawyers to my home and we'll have a wrestling match.
   If he wins I'll stop distributing the code.
*/

#include <string.h>
#include "rw.h"

/* yikes, I don't actually know C++!
   overloads, references, copy / move, const semantics,
   initilizers, cons, dcons all put me in a bad mood,
   and flood my head with endless pitfalls.
   so fuck it, here's the dumb C(++) version.
 */

namespace rw{
namespace c3d{

struct Color
{
	uint8 data[4];
};

class Packet
{
private:	
	uint8 *src;
	uint8 *dst;
	int bpp;
	int stride;
	int height;
	
	Color cache[64];
	
	uint8 *index(int x, int y, int i){
		int offset =
			(x + (i  & 7)) * bpp +
			(height - 1 - y - (i >> 3)) * stride;
		return &src[offset];
	}

public:
	Color
	get(int i){
		return cache[i];
	}

	void
	set(int i, Color col){
		cache[i] = col;
	}

	void
	swap(int i, int j){
		Color ci = get(i);
		Color cj = get(j);
		set(i, cj);
		set(j, ci);
	}

	void
	position(int x, int y){
		int i;
		for (i = 0; i < 64; i++){
			memcpy(&cache[i].data, index(x, y, i), bpp);
		}
	}
	
	void
	flush(){
		int i;
		for (i = 0; i < 64; i++){
			memcpy(dst, &cache[i].data, bpp);
			dst += bpp;
		}
	}

	Packet(uint8 *_src, uint8 *_dst, int _bpp, int _stride, int _height)
		:
		src(_src),
		dst(_dst),
		bpp(_bpp),
		stride(_stride),
		height(_height)
	{}
};


void
swizzle_8x8(Packet *p, bool reverse)
{
	// swizzle foursome table
	static const unsigned char table[][4] = {
		{  2,  8, 16,  4, },
		{  3,  9, 17,  5, },
		{  6, 10, 24, 20, },
		{  7, 11, 25, 21, },
		{ 14, 26, 28, 22, },
		{ 15, 27, 29, 23, },
		{ 34, 40, 48, 36, },
		{ 35, 41, 49, 37, },
		{ 38, 42, 56, 52, },
		{ 39, 43, 57, 53, },
		{ 46, 58, 60, 54, },
		{ 47, 59, 61, 55, },
	};

	if (!reverse){
		for (const auto &entry : table){
			Color tmp = p->get(entry[0]);
			p->set(entry[0], p->get(entry[1]));
			p->set(entry[1], p->get(entry[2]));
			p->set(entry[2], p->get(entry[3]));
			p->set(entry[3], tmp);
		}
	}else{
		for (const auto &entry : table)	{
			Color tmp = p->get(entry[3]);
			p->set(entry[3], p->get(entry[2]));
			p->set(entry[2], p->get(entry[1]));
			p->set(entry[1], p->get(entry[0]));
			p->set(entry[0], tmp);
		}
	}

	// (un)swizzle each pair
	p->swap(12, 18);
	p->swap(13, 19);
	p->swap(44, 50);
	p->swap(45, 51);
}

void
cpuSwizzle(uint8* src, uint8 *dst, int width, int height, int bpp, bool reverse)
{
	size_t stride = width * bpp;
	// (un)swizzle each tile
	Packet p(src, dst, bpp, stride, height);
	for(int j = 0; j < height; j += 8){
		for(int i = 0; i < width; i += 8){
			p.position(i, j);
			swizzle_8x8(&p, reverse);
			p.flush();
		}
	}
}
  
}
}
