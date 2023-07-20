/**
 * lzav.h version 1.2
 *
 * The inclusion file for the "LZAV" in-memory data compression and
 * decompression algorithm.
 *
 * Description is available at https://github.com/avaneev/lzav
 * E-mail: aleksey.vaneev@gmail.com
 *
 * License
 *
 * Copyright (c) 2023 Aleksey Vaneev
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

#ifndef LZAV_INCLUDED
#define LZAV_INCLUDED

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define LZAV_API_VER 0x100 // API version, unrelated to code version.

// Decompression error codes:

#define LZAV_E_PARAMS -1 // Incorrect function parameters.
#define LZAV_E_SRCOOB -2 // Source buffer OOB.
#define LZAV_E_DSTOOB -3 // Destination buffer OOB.
#define LZAV_E_REFOOB -4 // Back-reference OOB.
#define LZAV_E_DSTLEN -5 // Uncompressed length mismatch.
#define LZAV_E_UNKFMT -6 // Unknown stream format.

// NOTE: all macros defined below are for internal use, do not change.

#define LZAV_WIN_LEN ( 1 << 24 ) // LZ77 window length, in bytes.
#define LZAV_LIT_LEN ( 1 + 15 + 255 + 255 ) // Max literal length, in bytes.
#define LZAV_REF_MIN 6 // Min reference length, in bytes.
#define LZAV_REF_LEN ( LZAV_REF_MIN + 15 + 255 ) // Max reference length.
#define LZAV_LIT_FIN 5 // The number of literals required at finish.

// Endianness-definition macro.

#if defined( _WIN32 ) || defined( __LITTLE_ENDIAN__ ) || \
	( defined( __BYTE_ORDER__ ) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ )

	#define LZAV_LITTLE_ENDIAN 1

#elif defined( __BIG_ENDIAN__ ) || \
	( defined( __BYTE_ORDER__ ) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ )

	#define LZAV_LITTLE_ENDIAN 0

#else // defined( __BIG_ENDIAN__ )

	#warning LZAV: cannot determine endianness, assuming little-endian.

	#define LZAV_LITTLE_ENDIAN 1

#endif // defined( __BIG_ENDIAN__ )

// In-place endianness-correction macros, for singular variables of matching
// bit-size.

#if LZAV_LITTLE_ENDIAN

	#define LZAV_IEC32( x )

#else // LZAV_LITTLE_ENDIAN

	#if defined( __GNUC__ ) || defined( __clang__ )

		#define LZAV_IEC32( x ) x = __builtin_bswap32( x )

	#elif defined( _MSC_VER )

		#define LZAV_IEC32( x ) x = _byteswap_ulong( x )

	#else // defined( _MSC_VER )

		#define LZAV_IEC32( x ) x = ( \
			( x & 0xFF000000 ) >> 24 | \
			( x & 0x00FF0000 ) >> 8 | \
			( x & 0x0000FF00 ) << 8 | \
			( x & 0x000000FF ) << 24 )

	#endif // defined( _MSC_VER )

#endif // LZAV_LITTLE_ENDIAN

// Likelihood macros that are used for manually-guided micro-optimization
// (on Apple Silicon these guidances are slightly counter-productive).

#if ( defined( __GNUC__ ) || defined( __clang__ )) && \
	!( defined( __aarch64__ ) && defined( __APPLE__ ))

	#define LZAV_LIKELY( x )  __builtin_expect( x, 1 )
	#define LZAV_UNLIKELY( x )  __builtin_expect( x, 0 )

#else // likelihood macros

	#define LZAV_LIKELY( x ) ( x )
	#define LZAV_UNLIKELY( x ) ( x )

#endif // likelihood macros

/**
 * Function finds the number of continuously-matching leading bytes between
 * two buffers.
 *
 * @param p1 Pointer to buffer 1.
 * @param p2 Pointer to buffer 2.
 * @param ml Maximal number of bytes to match.
 * @return The number of matching leading bytes.
 */

static inline size_t lzav_match_len( const uint8_t* p1, const uint8_t* p2,
	const size_t ml )
{
#if defined( __GNUC__ ) || defined( __clang__ )

	const uint8_t* const p1s = p1;
	const uint8_t* const p1e = p1 + ml;

	#if defined( __x86_64__ ) || defined( __ppc64__ ) || \
		defined( __aarch64__ ) || defined( __arm64__ )

		while( LZAV_LIKELY( p1 + 7 < p1e ))
		{
			uint64_t v1, v2, vd;
			memcpy( &v1, p1, 8 );
			memcpy( &v2, p2, 8 );
			vd = v1 ^ v2;

			if( vd != 0 )
			{
			#if LZAV_LITTLE_ENDIAN
				return( p1 - p1s + ( __builtin_ctzll( vd ) >> 3 ));
			#else // LZAV_LITTLE_ENDIAN
				return( p1 - p1s + ( __builtin_clzll( vd ) >> 3 ));
			#endif // LZAV_LITTLE_ENDIAN
			}

			p1 += 8;
			p2 += 8;
		}

	#endif // 64-bit availability check

	while( LZAV_LIKELY( p1 + 3 < p1e ))
	{
		uint32_t v1, v2, vd;
		memcpy( &v1, p1, 4 );
		memcpy( &v2, p2, 4 );
		vd = v1 ^ v2;

		if( vd != 0 )
		{
		#if LZAV_LITTLE_ENDIAN
			return( p1 - p1s + ( __builtin_ctz( vd ) >> 3 ));
		#else // LZAV_LITTLE_ENDIAN
			return( p1 - p1s + ( __builtin_clz( vd ) >> 3 ));
		#endif // LZAV_LITTLE_ENDIAN
		}

		p1 += 4;
		p2 += 4;
	}

	while( p1 < p1e )
	{
		if( *p1 != *p2 )
		{
			return( p1 - p1s );
		}

		p1++;
		p2++;
	}

	return( ml );

#else // defined( __GNUC__ ) || defined( __clang__ )

	size_t l = 0;

	while( 1 )
	{
		if( LZAV_LIKELY( ml > 7 ))
		{
			if( p1[ 0 ] != p2[ 0 ]) break; l++;
			if( p1[ 1 ] != p2[ 1 ]) break; l++;
			if( p1[ 2 ] != p2[ 2 ]) break; l++;
			if( p1[ 3 ] != p2[ 3 ]) break; l++;
			if( p1[ 4 ] != p2[ 4 ]) break; l++;
			if( p1[ 5 ] != p2[ 5 ]) break; l++;
			if( p1[ 6 ] != p2[ 6 ]) break; l++;
			if( p1[ 7 ] != p2[ 7 ]) break; l++;
		}

		const uint8_t* const p1s = p1;
		const uint8_t* const p1e = p1 + ml;
		p1 += l;
		p2 += l;

		while( LZAV_LIKELY( p1 < p1e ))
		{
			if( *p1 != *p2 )
			{
				return( p1 - p1s );
			}

			p1++;
			p2++;
		}

		return( ml );
	}

	return( l );

#endif // defined( __GNUC__ ) || defined( __clang__ )
}

/**
 * Internal function writes a block to the output buffer. This function can be
 * used in custom compression algorithms.
 *
 * Block starts with a header byte, followed by several optional bytes. The
 * lowest 2 bits of the header specify block's type.
 *
 * CCLLLL00: literal block (1-2 bytes). LLLL is literal length.
 * OORRRR01: 10-bit offset block (2-3 bytes). RRRR is reference length.
 * OORRRR10: 18-bit offset block (3-4 bytes).
 * CCRRRR11: 24-bit offset block (4-5 bytes).
 *
 * If LLLL==15 or RRRR==15, an additional length byte follows. If, in literal
 * block, this additional byte is equal to 255, one more length byte follows.
 * CC is a reference offset carry value (additional 2 lowest bits of offset of
 * the next reference block).
 *
 * The overall compressed data is prefixed with a byte whose lower 4 bits
 * contain minimal reference length (mref), and the highest 4 bits contain
 * stream format identifier.
 *
 * @param op Output buffer pointer.
 * @param lc Literal length, in bytes.
 * @param rc Reference length, in bytes, not lesser than mref.
 * @param d Reference offset, in bytes.
 * @param ipa Literals anchor pointer.
 * @param cbpp Pointer to the pointer to the latest offset carry block header.
 * Cannot be 0, but the contained pointer can be 0.
 * @param mref Minimal reference length, in bytes, used by the compression
 * algorithm.
 * @return Incremented output buffer pointer.
 */

static inline uint8_t* lzav_write_blk( uint8_t* op, size_t lc, size_t rc,
	size_t d, const uint8_t* ipa, uint8_t** const cbpp, const size_t mref )
{
	uint8_t* cbp = *cbpp;

	while( LZAV_UNLIKELY( lc >= LZAV_LIT_LEN ))
	{
		// Write literals due to literal length overflow.

		cbp = op;
		*cbpp = op;

		op[ 0 ] = 15 << 2 | 0;
		op[ 1 ] = 255;
		op[ 2 ] = 255;
		op += 3;

		memcpy( op, ipa, LZAV_LIT_LEN );
		op += LZAV_LIT_LEN;
		ipa += LZAV_LIT_LEN;
		lc -= LZAV_LIT_LEN;
	}

	if( LZAV_UNLIKELY( lc != 0 ))
	{
		// Write literal block.

		cbp = op;
		*cbpp = op;

		if( LZAV_LIKELY( lc < 9 ))
		{
			*op = (uint8_t) (( lc - 1 ) << 2 | 0 );
			op++;

			memcpy( op, ipa, 8 );
			op += lc;
		}
		else
		if( lc < 1 + 15 )
		{
			*op = (uint8_t) (( lc - 1 ) << 2 | 0 );
			op++;

			memcpy( op, ipa, 16 );
			op += lc;
		}
		else
		if( lc < 1 + 15 + 255 )
		{
		#if LZAV_LITTLE_ENDIAN
			uint16_t ov = (uint16_t) (( lc - 1 - 15 ) << 8 | 15 << 2 | 0 );
		#else // LZAV_LITTLE_ENDIAN
			uint16_t ov = (uint16_t) (( lc - 1 - 15 ) | 15 << ( 2 + 8 ));
		#endif // LZAV_LITTLE_ENDIAN

			memcpy( op, &ov, 2 );
			op += 2;

			memcpy( op, ipa, 16 );

			if( lc < 17 )
			{
				op += lc;
			}
			else
			{
				ipa += 16;
				op += 16;
				lc -= 16;

				do
				{
					*op = *ipa;
					ipa++;
					op++;
				} while( --lc != 0 );
			}
		}
		else
		{
			op[ 0 ] = 15 << 2 | 0;
			op[ 1 ] = 255;
			op[ 2 ] = (uint8_t) ( lc - 1 - 15 - 255 );
			op += 3;

			memcpy( op, ipa, lc );
			op += lc;
		}
	}

	// Write reference block.

	if( cbp != 0 )
	{
		// Perform offset carry to a previous block header.

		*cbp |= (uint8_t) ( d << 6 );
		d >>= 2;
		*cbpp = 0;
	}

	rc -= mref;

	if( d < ( 1 << 10 ))
	{
		if( rc < 15 )
		{
		#if LZAV_LITTLE_ENDIAN
			uint16_t ov = (uint16_t) ( d << 6 | rc << 2 | 1 );
		#else // LZAV_LITTLE_ENDIAN
			uint16_t ov = (uint16_t) ( d >> 2 | d << 14 | rc << 10 | 1 << 8 );
		#endif // LZAV_LITTLE_ENDIAN

			memcpy( op, &ov, 2 );
			return( op + 2 );
		}

		*op = (uint8_t) ( d << 6 | 15 << 2 | 1 );

	#if LZAV_LITTLE_ENDIAN
		uint16_t ov = (uint16_t) (( d & ~3 ) << 6 | ( rc - 15 ));
	#else // LZAV_LITTLE_ENDIAN
		uint16_t ov = (uint16_t) ( d >> 2 | ( rc - 15 ) << 8 );
	#endif // LZAV_LITTLE_ENDIAN

		memcpy( op + 1, &ov, 2 );
		return( op + 3 );
	}

	if( d < ( 1 << 18 ))
	{
		if( rc < 15 )
		{
			*op = (uint8_t) ( d << 6 | rc << 2 | 2 );

		#if LZAV_LITTLE_ENDIAN
			uint16_t ov = (uint16_t) ( d >> 2 );
		#else // LZAV_LITTLE_ENDIAN
			uint16_t ov = (uint16_t) ( d >> 10 | ( d & ~3 ) << 6 );
		#endif // LZAV_LITTLE_ENDIAN

			memcpy( op + 1, &ov, 2 );
			return( op + 3 );
		}

		uint32_t ov = (uint32_t) (( d & ~3 ) << 14 | ( rc - 15 ) << 8 |
			( d & 3 ) << 6 | 15 << 2 | 2 );

		LZAV_IEC32( ov );
		memcpy( op, &ov, 4 );
		return( op + 4 );
	}

	*cbpp = op;

	if( rc < 15 )
	{
		uint32_t ov = (uint32_t) ( d << 8 | rc << 2 | 3 );

		LZAV_IEC32( ov );
		memcpy( op, &ov, 4 );
		return( op + 4 );
	}

	*op = (uint8_t) ( 15 << 2 | 3 );
	uint32_t ov = (uint32_t) ( d << 8 | ( rc - 15 ));

	LZAV_IEC32( ov );
	memcpy( op + 1, &ov, 4 );
	return( op + 5 );
}

/**
 * Internal function writes finishing literal block(s) to the output buffer.
 * This function can be used in custom compression algorithms.
 *
 * @param op Output buffer pointer.
 * @param lc Literal length, in bytes. Not less than LZAV_LIT_FIN.
 * @param ipa Literals anchor pointer.
 * @return Incremented output buffer pointer.
 */

static inline uint8_t* lzav_write_fin( uint8_t* op, size_t lc,
	const uint8_t* ipa )
{
	while( lc > 15 )
	{
		size_t wc = lc - LZAV_LIT_FIN; // Leave literals for the final block.

		if( wc < 1 + 15 )
		{
			*op = (uint8_t) (( wc - 1 ) << 2 | 0 );
			op++;
		}
		else
		{
			if( wc > LZAV_LIT_LEN )
			{
				wc = LZAV_LIT_LEN;
			}

			if( wc < 1 + 15 + 255 )
			{
				op[ 0 ] = 15 << 2 | 0;
				op[ 1 ] = (uint8_t) ( wc - 1 - 15 );
				op += 2;
			}
			else
			{
				op[ 0 ] = 15 << 2 | 0;
				op[ 1 ] = 255;
				op[ 2 ] = (uint8_t) ( wc - 1 - 15 - 255 );
				op += 3;
			}
		}

		memcpy( op, ipa, wc );
		op += wc;
		ipa += wc;
		lc -= wc;
	}

	*op = (uint8_t) (( lc - 1 ) << 2 | 0 );
	op++;

	memcpy( op, ipa, lc );

	return( op + lc );
}

/**
 * @param srcl The length of the source data to be compressed.
 * @return The required allocation size for destination compression buffer.
 */

static inline int lzav_compress_bound( const int srcl )
{
	if( srcl < 0 )
	{
		return( 8 );
	}

	return( srcl + srcl * 3 / LZAV_LIT_LEN + 8 );
}

/**
 * Function performs in-memory data compression using the LZAV compression
 * algorithm and stream format. The function produces a "raw" compressed data,
 * without a header containing data length nor identifier nor checksum.
 *
 * Note that compression algorithm and its output on the same source data may
 * differ between LZAV versions, and may differ on little- and big-endian
 * systems. However, the decompression of a compressed data produced by any
 * prior compressor version will stay possible.
 *
 * @param[in] src Source (uncompressed) data pointer, can be 0 if srcl==0.
 * Address alignment is unimportant.
 * @param[out] dst Destination (compressed data) buffer pointer. The allocated
 * size should be at least lzav_compress_bound() bytes large. Address
 * alignment is unimportant.
 * @param srcl Source data length, in bytes, can be 0: in this case the
 * compressed length is assumed to be 0 as well.
 * @param dstl Destination buffer's capacity, in bytes.
 * @param ext_buf External buffer to use for hash-table, set to 0 for the
 * function to manage memory itself (via std malloc). Supplying a
 * pre-allocated buffer is useful if compression is performed during
 * application's operation often: this reduces memory allocation overhead and
 * fragmentation. Note that the access to the supplied buffer is not
 * implicitly thread-safe.
 * @param ext_bufl The capacity of the ext_buf, in bytes, should be a
 * power-of-2 value. Set to 0 if ext_buf is 0. The capacity should not be
 * lesser than srcl, not lesser than 256, but not greater than 512 KiB. Same
 * ext_bufl value can be used for any smaller sources.
 * @return The length of compressed data, in bytes. Returns 0 if srcl<=0, or
 * if dstl is too small, or if not enough memory.
 */

static inline int lzav_compress( const void* const src, void* const dst,
	const int srcl, const int dstl, void* const ext_buf,
	const int ext_bufl )
{
	if( srcl <= 0 || src == 0 || dst == 0 ||
		dstl < lzav_compress_bound( srcl ))
	{
		return( 0 );
	}

	if( srcl <= LZAV_LIT_FIN )
	{
		// Handle very short source data.

		*(uint8_t*) dst = LZAV_REF_MIN;
		*( (uint8_t*) dst + 1 ) = (uint8_t) (( srcl - 1 ) << 2 | 0 );
		memset( (uint8_t*) dst + 2, 0, LZAV_LIT_FIN );
		memcpy( (uint8_t*) dst + 2, src, srcl );

		return( 2 + LZAV_LIT_FIN );
	}

	uint32_t stack_buf[ 2048 ]; // On-stack hash-table.
	void* alloc_buf = 0; // Hash-table allocated on heap.
	uint8_t* ht = (uint8_t*) stack_buf; // The actual hash-table pointer.
	size_t hcap = 1 << 8; // Hash-table's capacity (power-of-2).

	while( hcap != ( 1 << 17 ) && hcap * 5 < (size_t) srcl )
	{
		hcap <<= 1;
	}

	const size_t htsize = hcap * sizeof( uint32_t );

	if( htsize > sizeof( stack_buf ))
	{
		if( ext_buf != 0 && (size_t) ext_bufl >= htsize )
		{
			ht = (uint8_t*) ext_buf;
		}
		else
		{
			alloc_buf = malloc( htsize );

			if( alloc_buf == 0 )
			{
				return( 0 );
			}

			ht = (uint8_t*) alloc_buf;
		}
	}

	memset( ht, 0, htsize );

	const uint32_t hmask = (uint32_t) (( htsize - 1 ) ^ 3 ); // Hash mask.
	const uint8_t* ip = (const uint8_t*) src; // Source data pointer.
	const uint8_t* const ipe = ip + srcl - LZAV_LIT_FIN; // End pointer.
	const uint8_t* const ipet = ipe - ( LZAV_REF_MIN - 1 ); // Hash threshold.
	const uint8_t* ipa = ip; // Literals anchor pointer.
	uint8_t* op = (uint8_t*) dst; // Destination data pointer.
	uint8_t* cbp = 0; // Pointer to the latest offset carry block header.
	int mavg = 500000; // Running average of match success percentage (*10000).
	int rndb = 0; // PRNG bit derived from the non-matching offset.

	*op = LZAV_REF_MIN;
	op++;

	while( LZAV_LIKELY( ip < ipet ))
	{
		// Hash source data (endianness is unimportant for compression
		// efficiency). Hash is based on the "komihash" math construct, see
		// https://github.com/avaneev/komihash for details.

		uint32_t iw1, ww1, hval, io;
		uint16_t iw2, ww2;
		memcpy( &iw1, ip, 4 );
		const uint64_t Seed1 = 0x243F6A88 ^ iw1;
		memcpy( &iw2, ip + 4, 2 );
		const uint64_t hm64 = Seed1 * (uint32_t) ( 0x85A308D3 ^ iw2 );
		hval = (uint32_t) hm64 ^ ( 0x85A308D3 + (uint32_t) ( hm64 >> 32 ));

		// Hash-table access.

		uint32_t* const hp = (uint32_t*) ( ht + ( hval & hmask ));
		const size_t wo = *hp; // Window back-offset.

		if( LZAV_LIKELY( wo != 0 ))
		{
			const uint8_t* const wp = (const uint8_t*) src + wo;
			size_t d = ip - wp; // Reference offset.

			if( LZAV_UNLIKELY( d < 8 ))
			{
				ip++;
				continue;
			}

			if( LZAV_LIKELY( d < LZAV_WIN_LEN ))
			{
				memcpy( &ww1, wp, 4 );
				memcpy( &ww2, wp + 4, 2 );

				if( LZAV_UNLIKELY( iw1 == ww1 && iw2 == ww2 ))
				{
					// Source data and hash-table entry match.

					if( d > 127 ) // Do not update close matching entries.
					{
						*hp = (uint32_t) ( ip - (const uint8_t*) src );
					}

					// Disallow overlapped reference copy.

					size_t ml = ( d > LZAV_REF_LEN ? LZAV_REF_LEN : d );

					if( LZAV_UNLIKELY( ip + ml > ipe ))
					{
						// Make sure LZAV_LIT_FIN literals remain on finish.

						ml = ipe - ip;
					}

					size_t lc = ip - ipa;
					size_t rc = 0;

					if( lc != 0 && lc < LZAV_REF_MIN && lc < wo )
					{
						// Try to skip literals by finding a local match.

						const uint8_t* const ipl = ip - lc;
						const uint8_t* const wpl = wp - lc;

						if( *ipl == *wpl )
						{
							const size_t rc2 = 1 + lzav_match_len( ipl + 1,
								wpl + 1, ml - 1 );

							if( rc2 >= LZAV_REF_MIN )
							{
								rc = rc2;
								ip -= lc;
								lc = 0;
							}
						}
					}

					if( LZAV_LIKELY( rc == 0 ))
					{
						rc = LZAV_REF_MIN + lzav_match_len( ip + LZAV_REF_MIN,
							wp + LZAV_REF_MIN, ml - LZAV_REF_MIN );
					}

					op = lzav_write_blk( op, lc, rc, d, ipa, &cbp,
						LZAV_REF_MIN );

					ip += rc;
					ipa = ip;
					mavg += ( 1000000 - mavg ) >> 8;
					continue;
				}

				mavg -= mavg >> 8;

				if( mavg < 120000 && ip != ipa ) // 12% match rate threshold.
				{
					// Compression speed-up technique that keeps the number of
					// hash evaluations around 45% of compressed data length.
					// In some cases reduces the number of blocks by several
					// percent.

					io = (uint32_t) ( ip - (const uint8_t*) src );
					ip += 2 | rndb; // Use PRNG bit to dither match positions.
					rndb = io & 1; // Delay to decorrelate from current match.
					ip += ( mavg < 80000 ) + ( mavg < 40000 ) * 2; // Faster.
					*hp = io;
					continue;
				}
			}
		}

		*hp = (uint32_t) ( ip - (const uint8_t*) src );
		ip++;
	}

	if( alloc_buf != 0 )
	{
		free( alloc_buf );
	}

	return( (int) ( lzav_write_fin( op, ipe - ipa + LZAV_LIT_FIN, ipa ) -
		(uint8_t*) dst ));
}

/**
 * Function performs in-memory data compression using the LZAV compression
 * algorithm, with the default settings.
 *
 * See the lzav_compress() function for more detailed description.
 *
 * @param[in] src Source (uncompressed) data pointer.
 * @param[out] dst Destination (compressed data) buffer pointer. The allocated
 * size should be at least lzav_compress_bound() bytes large.
 * @param srcl Source data length, in bytes.
 * @param dstl Destination buffer's capacity, in bytes.
 * @return The length of compressed data, in bytes. Returns 0 if srcl<=0, or
 * if dstl is too small, or if not enough memory.
 */

static inline int lzav_compress_default( const void* const src,
	void* const dst, const int srcl, const int dstl )
{
	return( lzav_compress( src, dst, srcl, dstl, 0, 0 ));
}

/**
 * Function decompresses "raw" data previously compressed in the LZAV stream
 * format.
 *
 * Note that while the function does perform checks to avoid OOB memory
 * accesses, and checks for decompressed data length equality, this is not a
 * strict guarantee of a valid decompression. In cases when the compressed
 * data is stored in a long-term storage without embedded data integrity
 * mechanisms (e.g., a database without RAID 0 guarantee, a binary container
 * without digital signature nor CRC), a checksum (hash) of the original
 * uncompressed data should be stored, and then evaluated against that of the
 * decompressed data. Also, a separate checksum (hash) of application-defined
 * header, which contains uncompressed and compressed data lengths, should be
 * checked before decompression. A high-performance "komihash" hash function
 * can be used to obtain a hash value of the data.
 *
 * @param[in] src Source (compressed) data pointer, can be 0 if srcl==0.
 * Address alignment is unimportant.
 * @param[out] dst Destination (uncompressed data) buffer pointer. Address
 * alignment is unimportant.
 * @param srcl Source data length, in bytes, can be 0.
 * @param dstl Expected destination data length, in bytes, can be 0.
 * @return The length of decompressed data, in bytes, or any negative value if
 * some error happened, including buffer overrun. Always returns a negative
 * value if the resulting decompressed data length differs from dstl. This
 * means that error result handling requires just a check for a negative
 * return value (see the LZAV_E_x macros for possible values).
 */

static inline int lzav_decompress( const void* const src, void* const dst,
	const int srcl, const int dstl )
{
	if( srcl < 0 )
	{
		return( LZAV_E_PARAMS );
	}

	if( srcl == 0 )
	{
		return( dstl == 0 ? 0 : LZAV_E_PARAMS );
	}

	if( src == 0 || dst == 0 || dstl <= 0 )
	{
		return( LZAV_E_PARAMS );
	}

	if(( *(const uint8_t*) src & 0xF0 ) != 0 )
	{
		return( LZAV_E_UNKFMT );
	}

	const uint8_t* ip = (const uint8_t*) src;
	const uint8_t* const ipe = ip + srcl;
	const uint8_t* const ipet = ipe - LZAV_LIT_FIN;
	uint8_t* op = (uint8_t*) dst;
	uint8_t* const ope = op + dstl;
	uint8_t* const opet = ope - 63; // Threshold for fast copy.
	const int mref = *ip & 15; // Minimal reference length in use.
	size_t cv = 0; // Reference offset carry value.
	int csh = 0; // Reference offset carry shift.
	int bh = 0; // Current block header, updated in each branch.
	ip++;

	if( LZAV_LIKELY( ip < ipet ))
	{
		bh = *ip;
	}

	#define LZAV_SET_IPD( x ) \
		const size_t d = ( x ) << csh | cv; \
		ipd = op - d; \
		if( LZAV_UNLIKELY( (uint8_t*) dst + d > op )) return( LZAV_E_REFOOB ); \
		cv = 0; \
		csh = 0;

	while( LZAV_LIKELY( ip < ipet ))
	{
		const uint8_t* ipd;
		int cc = ( bh >> 2 ) & 15;

		if( LZAV_LIKELY( cc != 15 ))
		{
			if(( bh & 2 ) != 0 )
			{
				cc += mref;

				if( LZAV_LIKELY(( bh & 1 ) == 0 ))
				{
					LZAV_SET_IPD( bh >> 6 | ip[ 1 ] << 2 | ip[ 2 ] << 10 );
					bh = ip[ 3 ];
					ip += 3;

					if( LZAV_LIKELY( op < opet ))
					{
						memcpy( op, ipd, 16 );
						memcpy( op + 16, ipd + 16, 4 );
						op += cc;
						continue;
					}
				}
				else
				{
					LZAV_SET_IPD( ip[ 1 ] | ip[ 2 ] << 8 | ip[ 3 ] << 16 );
					cv = bh >> 6;
					csh = 2;
					bh = ip[ 4 ];
					ip += 4;

					if( LZAV_LIKELY( op < opet ))
					{
						memcpy( op, ipd, 16 );
						memcpy( op + 16, ipd + 16, 4 );
						op += cc;
						continue;
					}
				}
			}
			else
			{
				if(( bh & 1 ) == 0 )
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
						memcpy( op + 16, ipd + 16, 4 );
						op += cc;
						continue;
					}
				}
				else
				{
					LZAV_SET_IPD( bh >> 6 | ip[ 1 ] << 2 );
					bh = ip[ 2 ];
					ip += 2;
					cc += mref;

					if( LZAV_LIKELY( op < opet ))
					{
						memcpy( op, ipd, 16 );
						memcpy( op + 16, ipd + 16, 4 );
						op += cc;
						continue;
					}
				}
			}
		}
		else
		{
			int bt;

			if( LZAV_LIKELY(( bt = bh & 3 ) != 0 ))
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
					memcpy( op, ipd, 16 );
					memcpy( op + 16, ipd + 16, 16 );
					memcpy( op + 32, ipd + 32, 16 );
					memcpy( op + 48, ipd + 48, 16 );

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
			else
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
		}

		if( LZAV_UNLIKELY( op + cc > ope ))
		{
			return( LZAV_E_DSTOOB );
		}

		// This copy-block is transformed into memcpy-alike instructions, by a
		// modern compiler. Direct use of memcpy is slower, on average.

		while( LZAV_LIKELY( cc > 0 ))
		{
			*op = *ipd;
			ipd++;
			op++;
			cc--;
		}
	}

	if( op != ope )
	{
		return( LZAV_E_DSTLEN );
	}

	return( (int) ( op - (uint8_t*) dst ));
}

#endif // LZAV_INCLUDED
