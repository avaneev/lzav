/**
 * @file lzav_d0.h
 *
 * @version 1.5.2
 *
 * @brief Optional header file for deprecated LZAV stream format 0
 * decompression.
 *
 * This header file is not required unless LZAV_FMT_MIN defined in "lzav.h" is
 * below 1 - in this case this header will be auto-included by "lzav.h".
 *
 * Description is available at https://github.com/avaneev/lzav
 * E-mail: aleksey.vaneev@gmail.com or info@voxengo.com
 *
 * License
 *
 * Copyright (c) 2023-2024 Aleksey Vaneev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef LZAV_D0_INCLUDED
#define LZAV_D0_INCLUDED

/**
 * Function decompresses "raw" data previously compressed into the LZAV stream
 * format 0.
 *
 * This function should not be called directly since it does not check the
 * format identifier.
 *
 * @param[in] src Source (compressed) data pointer.
 * @param[out] dst Destination (decompressed data) buffer pointer.
 * @param srcl Source data length, in bytes.
 * @param dstl Expected destination data length, in bytes.
 * @return The length of decompressed data, in bytes, or any negative value if
 * some error happened.
 */

static inline int lzav_decompress_0( const void* const src, void* const dst,
	const int srcl, const int dstl )
{
	const uint8_t* ip = (const uint8_t*) src; // Compressed data pointer.
	const uint8_t* const ipe = ip + srcl; // End pointer.
	const uint8_t* const ipet = ipe - 5; // Header read threshold LZAV_LIT_FIN.
	uint8_t* op = (uint8_t*) dst; // Destination (decompressed data) pointer.
	uint8_t* const ope = op + dstl; // Destination boundary pointer.
	uint8_t* const opet = ope - 63; // Threshold for fast copy to destination.
	const int mref = *ip & 15; // Minimal reference length in use.
	size_t cv = 0; // Reference offset carry value.
	int csh = 0; // Reference offset carry shift.
	int bh = 0; // Current block header, updated in each branch.

	#define LZAV_MEMMOVE( d, s, c ) \
		{ uint8_t tmp[ c ]; memcpy( tmp, s, c ); memcpy( d, tmp, c ); }

	#define LZAV_SET_IPD( x ) \
		const size_t d = ( x ) << csh | cv; \
		ipd = op - d; \
		if( LZAV_UNLIKELY( (uint8_t*) dst + d > op )) \
			return( LZAV_E_REFOOB ); \
		cv = 0; \
		csh = 0;

	ip++; // Advance beyond prefix byte.

	if( LZAV_LIKELY( ip < ipet ))
	{
		bh = *ip;
	}

	while( LZAV_LIKELY( ip < ipet ))
	{
		const uint8_t* ipd; // Reference or source data pointer.
		int cc = ( bh >> 2 ) & 15; // Byte copy count.

		if( LZAV_LIKELY( cc != 15 )) // True, if no additional length byte.
		{
			if(( bh & 2 ) != 0 ) // True, if block types 2 and 3.
			{
				cc += mref;

				if( LZAV_LIKELY(( bh & 1 ) == 0 )) // True, if block type 2.
				{
					LZAV_SET_IPD( bh >> 6 | ip[ 1 ] << 2 | ip[ 2 ] << 10 );
					bh = ip[ 3 ];
					ip += 3;

					if( LZAV_LIKELY( op < opet ))
					{
						LZAV_MEMMOVE( op, ipd, 16 );
						LZAV_MEMMOVE( op + 16, ipd + 16, 4 );
						op += cc;
						continue;
					}
				}
				else // Block type 3.
				{
					LZAV_SET_IPD( ip[ 1 ] | ip[ 2 ] << 8 | ip[ 3 ] << 16 );
					cv = bh >> 6;
					csh = 2;
					bh = ip[ 4 ];
					ip += 4;

					if( LZAV_LIKELY( op < opet ))
					{
						LZAV_MEMMOVE( op, ipd, 16 );
						LZAV_MEMMOVE( op + 16, ipd + 16, 4 );
						op += cc;
						continue;
					}
				}
			}
			else
			{
				if(( bh & 1 ) == 0 ) // True, if block type 0.
				{
					cv = bh >> 6;
					csh = 2;

					cc++;
					ip++;
					ipd = ip;
					ip += cc;

					if( LZAV_LIKELY( ip < ipe ))
					{
						bh = *ip;
					}
					else
					if( ip > ipe )
					{
						return( LZAV_E_SRCOOB );
					}

					if( LZAV_LIKELY(( op < opet ) & ( ipe - ipd >= 20 )))
					{
						memcpy( op, ipd, 16 );
						op += cc;
						continue;
					}
				}
				else // Block type 1.
				{
					cc += mref;
					LZAV_SET_IPD( bh >> 6 | ip[ 1 ] << 2 );
					bh = ip[ 2 ];
					ip += 2;

					if( LZAV_LIKELY( op < opet ))
					{
						LZAV_MEMMOVE( op, ipd, 16 );
						LZAV_MEMMOVE( op + 16, ipd + 16, 4 );
						op += cc;
						continue;
					}
				}
			}

			if( LZAV_UNLIKELY( op + cc > ope ))
			{
				return( LZAV_E_DSTOOB );
			}

			// This and other alike copy-blocks are transformed into fast SIMD
			// instructions, by a modern compiler. Direct use of "memcpy" is
			// slower due to shortness of data remaining to copy, on average.

			while( cc > 0 )
			{
				*op = *ipd;
				ipd++;
				op++;
				cc--;
			}

			continue;
		}

		// Handle large copy count blocks.

		const int bt = bh & 3; // Block type.

		if( LZAV_LIKELY( bt != 0 )) // True, if not type 0.
		{
			cc += mref;

			if( LZAV_LIKELY( bt == 2 ))
			{
				cc += ip[ 1 ];
				LZAV_SET_IPD( bh >> 6 | ip[ 2 ] << 2 | ip[ 3 ] << 10 );
				bh = ip[ 4 ];
				ip += 4;
			}
			else
			if( LZAV_LIKELY( bt == 1 ))
			{
				cc += ip[ 1 ];
				LZAV_SET_IPD( bh >> 6 | ip[ 2 ] << 2 );
				bh = ip[ 3 ];
				ip += 3;
			}
			else
			{
				uint32_t bv;
				memcpy( &bv, ip + 1, 4 );
				LZAV_IEC32( bv );

				cc += bv & 0xFF;
				LZAV_SET_IPD( bv >> 8 );
				cv = bh >> 6;
				csh = 2;
				bh = ip[ 5 ];
				ip += 5;
			}

			if( LZAV_LIKELY( op < opet ))
			{
				LZAV_MEMMOVE( op, ipd, 16 );
				LZAV_MEMMOVE( op + 16, ipd + 16, 16 );
				LZAV_MEMMOVE( op + 32, ipd + 32, 16 );
				LZAV_MEMMOVE( op + 48, ipd + 48, 16 );

				if( LZAV_LIKELY( cc <= 64 ))
				{
					op += cc;
					continue;
				}

				ipd += 64;
				op += 64;
				cc -= 64;
			}
		}
		else // Block type 0.
		{
			cv = bh >> 6;
			csh = 2;

			const int l2 = ip[ 1 ];
			const int lb = ( l2 == 255 );
			cc += 1 + l2 + ( ip[ 2 ] & ( 0x100 - lb ));
			ip += 2 + lb;
			ipd = ip;
			ip += cc;

			if( LZAV_LIKELY( ip < ipe ))
			{
				bh = *ip;
			}
			else
			if( ip > ipe )
			{
				return( LZAV_E_SRCOOB );
			}

			if( LZAV_LIKELY(( op < opet ) & ( ipe - ipd >= 48 )))
			{
				memcpy( op, ipd, 16 );
				memcpy( op + 16, ipd + 16, 16 );
				memcpy( op + 32, ipd + 32, 16 );

				if( LZAV_LIKELY( cc <= 48 ))
				{
					op += cc;
					continue;
				}

				ipd += 48;
				op += 48;
				cc -= 48;
			}
		}

		if( LZAV_UNLIKELY( op + cc > ope ))
		{
			return( LZAV_E_DSTOOB );
		}

		while( cc > 0 )
		{
			*op = *ipd;
			ipd++;
			op++;
			cc--;
		}
	}

	if( LZAV_UNLIKELY( op != ope ))
	{
		return( LZAV_E_DSTLEN );
	}

	return( (int) ( op - (uint8_t*) dst ));

	#undef LZAV_MEMMOVE
	#undef LZAV_SET_IPD
}

#endif // LZAV_D0_INCLUDED
