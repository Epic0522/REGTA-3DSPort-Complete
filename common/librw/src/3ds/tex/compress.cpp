/*------------------------------------------------------------------------------
 * Copyright (c) 2017-2019
 *     Michael Theall (mtheall)
 *
 * This file is part of tex3ds.
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

// this file has obviously been modified.
// here's the source code lmao.
// you know were to find the GPL.

#include <stdint.h>

#include "rg_etc1.h"

/*
  compress image using etc1.
  input must be 32-bit 4ByPP.
  use Image::convertTo32(void).
*/

namespace rw {
namespace c3d {

int gTexQuality = rg_etc1::cLowQuality;
  
void
etc1_common(uint8_t *src, uint8_t **dst, int stride, bool alpha)
{
	static bool ready = false;

	if (!ready){
		ready = true;
		rg_etc1::pack_etc1_block_init();
	}
	
	rg_etc1::etc1_pack_params params;

	params.clear();
	params.m_quality = gTexQuality; 

	for (int j = 0; j < 8; j += 4)
	{
		for (int i = 0; i < 8; i += 4)
		{
			uint8_t in_block[4 * 4 * 4];
			uint8_t out_block[8];
			uint8_t out_alpha[8] = {0, 0, 0, 0, 0, 0, 0, 0};

			// iterate each 4x4 subblock
			for (int y = 0; y < 4; ++y)
			{
				for (int x = 0; x < 4; ++x)
				{
					uint8_t *pix = src +
						((7 - (j + y)) * stride) + /* my code is beautiful */
						((i + x) * 4); /* good thing I'm not a professional */
				
					in_block[y * 16 + x * 4 + 0] = pix[0];
					in_block[y * 16 + x * 4 + 1] = pix[1];
					in_block[y * 16 + x * 4 + 2] = pix[2];
					in_block[y * 16 + x * 4 + 3] = 0xFF;

					if (alpha)
					{
						uint32_t a4 = (1<<4) * pix[3] / 256;
						// encode 4bpp alpha; X/Y axes are swapped
						if (y & 1){
							out_alpha[2 * x + y / 2] |= (a4 << 4);
						}else{
							out_alpha[2 * x + y / 2] |= (a4 << 0);
						}
					}
				}
			}

			// encode etc1 block
			rg_etc1::pack_etc1_block
				(out_block, reinterpret_cast<unsigned int*> (in_block), params);

			// alpha block precedes etc1 block
			if(alpha){
				for (int i = 0; i < 8; ++i){
					*((*dst)++) = out_alpha[i]; // intentionally hard to read
				}
			}
		
			// rg_etc1 outputs in big-endian; convert to little-endian
			for (int i = 0; i < 8; i++){
				*((*dst)++) = out_block[8 - i - 1]; // lol
			}
		}
	}
}

}
}
