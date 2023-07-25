/**
 * lzav.h version 2.1
 *
 * The inclusion file for the "LZAV" in-memory data compression and
 * decompression algorithms.
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

#define LZAV_API_VER 0x101 // API version, unrelated to code's version.

// Decompression error codes:

#define LZAV_E_PARAMS -1 // Incorrect function parameters.
#define LZAV_E_SRCOOB -2 // Source buffer OOB.
#define LZAV_E_DSTOOB -3 // Destination buffer OOB.
#define LZAV_E_REFOOB -4 // Back-reference OOB.
#define LZAV_E_DSTLEN -5 // Decompressed length mismatch.
#define LZAV_E_UNKFMT -6 // Unknown stream format.

// NOTE: all macros defined below are for internal use, do not change.

#define LZAV_WIN_LEN ( 1 << 24 ) // LZ77 window length, in bytes.
#define LZAV_LIT_LEN ( 1 + 15 + 255 + 255 ) // Max literal length, in bytes.
#define LZAV_REF_MIN 6 // Min reference length, in bytes.
#define LZAV_REF_LEN ( LZAV_REF_MIN + 15 + 255 ) // Max reference length.
#define LZAV_LIT_FIN 5 // The number of literals required at finish.
#define LZAV_FMT_CUR 1 // Stream format identifier used by the compressor.

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

	#define LZAV_IEC16( x )
	#define LZAV_IEC32( x )

#else // LZAV_LITTLE_ENDIAN

	#if defined( __GNUC__ ) || defined( __clang__ )

		#define LZAV_IEC16( x ) x = __builtin_bswap16( x )
		#define LZAV_IEC32( x ) x = __builtin_bswap32( x )

	#elif defined( _MSC_VER )

		#define LZAV_IEC16( x ) x = _byteswap_ushort( x )
		#define LZAV_IEC32( x ) x = _byteswap_ulong( x )

	#else // defined( _MSC_VER )

		#define LZAV_IEC16( x ) x = (uint16_t) ( x >> 8 | x << 8 )
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

	#define LZAV_LIKELY( x ) __builtin_expect( x, 1 )
	#define LZAV_UNLIKELY( x ) __builtin_expect( x, 0 )

#else // likelihood macros

	#define LZAV_LIKELY( x ) ( x )
	#define LZAV_UNLIKELY( x ) ( x )

#endif // likelihood macros

#if defined( _MSC_VER ) && !defined( __clang__ )
	#include <intrin.h> // For _BitScanForwardX.
#endif // defined( _MSC_VER ) && !defined( __clang__ )

/**
 * Function finds the number of continuously-matching leading bytes between
 * two buffers. This function is well-optimized for a wide variety of
 * compilers and platforms.
 *
 * @param p1 Pointer to buffer 1.
 * @param p2 Pointer to buffer 2.
 * @param ml Maximal number of bytes to match.
 * @return The number of matching leading bytes.
 */

static inline size_t lzav_match_len( const uint8_t* p1, const uint8_t* p2,
	const size_t ml )
{
#if defined( _MSC_VER ) || defined( __GNUC__ ) || defined( __clang__ )

	const uint8_t* const p1s = p1;
	const uint8_t* const p1e = p1 + ml;

	#if defined( _WIN64 ) || defined( __x86_64__ ) || defined( __ppc64__ ) || \
		defined( __aarch64__ ) || defined( __arm64__ )

		while( LZAV_LIKELY( p1 + 7 < p1e ))
		{
			uint64_t v1, v2, vd;
			memcpy( &v1, p1, 8 );
			memcpy( &v2, p2, 8 );
			vd = v1 ^ v2;

			if( vd != 0 )
			{
			#if defined( _MSC_VER ) && !defined( __clang__ )
				unsigned long i = 0;
				_BitScanForward64( &i, (unsigned __int64) vd );
				return( p1 - p1s + ( i >> 3 ));
			#elif LZAV_LITTLE_ENDIAN
				return( p1 - p1s + ( __builtin_ctzll( vd ) >> 3 ));
			#else // LZAV_LITTLE_ENDIAN
				return( p1 - p1s + ( __builtin_clzll( vd ) >> 3 ));
			#endif // LZAV_LITTLE_ENDIAN
			}

			p1 += 8;
			p2 += 8;
		}

		// At most 7 bytes left.

	if( LZAV_LIKELY( p1 + 3 < p1e ))
	{

	#else // 64-bit availability check

	while( LZAV_LIKELY( p1 + 3 < p1e ))
	{

	#endif // 64-bit availability check

		uint32_t v1, v2, vd;
		memcpy( &v1, p1, 4 );
		memcpy( &v2, p2, 4 );
		vd = v1 ^ v2;

		if( vd != 0 )
		{
		#if defined( _MSC_VER ) && !defined( __clang__ )
			unsigned long i = 0;
			_BitScanForward( &i, (unsigned long) vd );
			return( p1 - p1s + ( i >> 3 ));
		#elif LZAV_LITTLE_ENDIAN
			return( p1 - p1s + ( __builtin_ctz( vd ) >> 3 ));
		#else // LZAV_LITTLE_ENDIAN
			return( p1 - p1s + ( __builtin_clz( vd ) >> 3 ));
		#endif // LZAV_LITTLE_ENDIAN
		}

		p1 += 4;
		p2 += 4;
	}

	// At most 3 bytes left.

	if( p1 < p1e )
	{
		if( *p1 != p2[ 0 ])
		{
			return( p1 - p1s );
		}

		if( ++p1 < p1e )
		{
			if( *p1 != p2[ 1 ])
			{
				return( p1 - p1s );
			}

			if( ++p1 < p1e )
			{
				if( *p1 != p2[ 2 ])
				{
					return( p1 - p1s );
				}
			}
		}
	}

	return( ml );

#else // defined( _MSC_VER ) || defined( __GNUC__ ) || defined( __clang__ )

	size_t l = 0;

	if( LZAV_LIKELY( ml > 7 ))
	{
		if( p1[ 0 ] != p2[ 0 ]) return( l ); l++;
		if( p1[ 1 ] != p2[ 1 ]) return( l ); l++;
		if( p1[ 2 ] != p2[ 2 ]) return( l ); l++;
		if( p1[ 3 ] != p2[ 3 ]) return( l ); l++;
		if( p1[ 4 ] != p2[ 4 ]) return( l ); l++;
		if( p1[ 5 ] != p2[ 5 ]) return( l ); l++;
		if( p1[ 6 ] != p2[ 6 ]) return( l ); l++;
		if( p1[ 7 ] != p2[ 7 ]) return( l ); l++;
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

#endif // defined( _MSC_VER ) || defined( __GNUC__ ) || defined( __clang__ )
}

/**
 * Internal function writes a block to the output buffer. This function can be
 * used in custom compression algorithms.
 *
 * Stream format 1.
 *
 * Block starts with a header byte, followed by several optional bytes.
 * Bits 4-5 of the header specify block's type.
 *
 * CC00LLLL: literal block (1-3 bytes). LLLL is literal length.
 * OO01RRRR: 10-bit offset block (2-3 bytes). RRRR is reference length.
 * OO10RRRR: 18-bit offset block (3-4 bytes).
 * CC11RRRR: 24-bit offset block (4-5 bytes).
 *
 * If LLLL==0 or RRRR==0, a value of 16 is assumed, and an additional length
 * byte follows. If in a literal block this additional byte is equal to 255,
 * one more length byte follows. In a reference block, an additional length
 * byte follows the offset bytes. CC is a reference offset carry value
 * (additional 2 lowest bits of offset of the next reference block).
 *
 * The overall compressed data is prefixed with a byte whose lower 4 bits
 * contain minimal reference length (mref), and the highest 4 bits contain
 * stream format identifier.
 *
 * @param op Output buffer pointer.
 * @param lc Literal length, in bytes.
 * @param rc Reference length, in bytes, not lesser than mref.
 * @param d Reference offset, in bytes. Should be lesser than LZAV_WIN_LEN.
 * @param ipa Literals anchor pointer.
 * @param cbpp Pointer to the pointer to the latest offset carry block header.
 * Cannot be 0, but the contained pointer can be 0.
 * @param mref Minimal reference length, in bytes, used by the compression
 * algorithm.
 * @return Incremented output buffer pointer.
 */

static inline uint8_t* lzav_write_blk_1( uint8_t* op, size_t lc, size_t rc,
	size_t d, const uint8_t* ipa, uint8_t** const cbpp, const size_t mref )
{
	while( LZAV_UNLIKELY( lc > LZAV_LIT_LEN ))
	{
		// Write literals due to literal length overflow.

		op[ 0 ] = 0;
		op[ 1 ] = 255;
		op[ 2 ] = 255;
		op += 3;

		memcpy( op, ipa, LZAV_LIT_LEN );
		op += LZAV_LIT_LEN;
		ipa += LZAV_LIT_LEN;
		lc -= LZAV_LIT_LEN;
	}

	uint8_t* cbp = *cbpp;

	if( LZAV_UNLIKELY( lc != 0 ))
	{
		// Write literal block.

		const size_t cv = ( d & 3 ) << 6; // Offset carry value.
		d >>= 2;
		cbp = 0;
		*cbpp = 0;

		if( LZAV_LIKELY( lc < 9 ))
		{
			*op = (uint8_t) ( cv | lc );
			op++;

			memcpy( op, ipa, 8 );
			op += lc;
		}
		else
		if( lc < 1 + 15 )
		{
			*op = (uint8_t) ( cv | lc );
			op++;

			memcpy( op, ipa, 16 );
			op += lc;
		}
		else
		if( lc < 1 + 15 + 255 )
		{
		#if LZAV_LITTLE_ENDIAN
			uint16_t ov = (uint16_t) (( lc - 1 - 15 ) << 8 | cv );
		#else // LZAV_LITTLE_ENDIAN
			uint16_t ov = (uint16_t) ( cv << 8 | ( lc - 1 - 15 ));
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
			op[ 0 ] = (uint8_t) cv;
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

	rc = rc + 1 - mref;

	if( LZAV_UNLIKELY( d < ( 1 << 10 )))
	{
		if( LZAV_LIKELY( rc < 16 ))
		{
			uint16_t ov = (uint16_t) ( d << 6 | 1 << 4 | rc );

			LZAV_IEC16( ov );
			memcpy( op, &ov, 2 );
			return( op + 2 );
		}

		*op = (uint8_t) ( d << 6 | 1 << 4 );

	#if LZAV_LITTLE_ENDIAN
		uint16_t ov = (uint16_t) (( rc - 16 ) << 8 | d >> 2 );
	#else // LZAV_LITTLE_ENDIAN
		uint16_t ov = (uint16_t) (( d & ~3 ) << 6 | ( rc - 16 ));
	#endif // LZAV_LITTLE_ENDIAN

		memcpy( op + 1, &ov, 2 );
		return( op + 3 );
	}

	if( LZAV_LIKELY( d < ( 1 << 18 )))
	{
		if( LZAV_LIKELY( rc < 16 ))
		{
			*op = (uint8_t) ( d << 6 | 2 << 4 | rc );

			uint16_t ov = (uint16_t) ( d >> 2 );

			LZAV_IEC16( ov );
			memcpy( op + 1, &ov, 2 );
			return( op + 3 );
		}

		uint32_t ov = (uint32_t) (( rc - 16 ) << 24 | d << 6 | 2 << 4 );

		LZAV_IEC32( ov );
		memcpy( op, &ov, 4 );
		return( op + 4 );
	}

	*cbpp = op;

	if( LZAV_LIKELY( rc < 16 ))
	{
		uint32_t ov = (uint32_t) ( d << 8 | 3 << 4 | rc );

		LZAV_IEC32( ov );
		memcpy( op, &ov, 4 );
		return( op + 4 );
	}

	*op = (uint8_t) ( 3 << 4 );
	uint32_t ov = (uint32_t) (( rc - 16 ) << 24 | d );

	LZAV_IEC32( ov );
	memcpy( op + 1, &ov, 4 );
	return( op + 5 );
}

/**
 * Internal function writes finishing literal block(s) to the output buffer.
 * This function can be used in custom compression algorithms.
 *
 * Stream format 1.
 *
 * @param op Output buffer pointer.
 * @param lc Literal length, in bytes. Not less than LZAV_LIT_FIN.
 * @param ipa Literals anchor pointer.
 * @return Incremented output buffer pointer.
 */

static inline uint8_t* lzav_write_fin_1( uint8_t* op, size_t lc,
	const uint8_t* ipa )
{
	while( lc > 15 )
	{
		size_t wc = lc - LZAV_LIT_FIN; // Leave literals for the final block.

		if( wc < 1 + 15 )
		{
			*op = (uint8_t) wc;
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
				op[ 0 ] = 0;
				op[ 1 ] = (uint8_t) ( wc - 1 - 15 );
				op += 2;
			}
			else
			{
				op[ 0 ] = 0;
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

	*op = (uint8_t) lc;
	op++;

	memcpy( op, ipa, lc );

	return( op + lc );
}

/**
 * @param srcl The length of the source data to be compressed.
 * @return The required allocation size for destination compression buffer.
 * Always a positive value.
 */

static inline int lzav_compress_bound( const int srcl )
{
	if( srcl <= 0 )
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
 * alignment is unimportant. Should be different to src.
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
	const int srcl, const int dstl, void* const ext_buf, const int ext_bufl )
{
	if( srcl <= 0 || src == 0 || dst == 0 || src == dst ||
		dstl < lzav_compress_bound( srcl ))
	{
		return( 0 );
	}

	*(uint8_t*) dst = LZAV_FMT_CUR << 4 | LZAV_REF_MIN; // Write prefix byte.

	if( srcl <= LZAV_LIT_FIN )
	{
		// Handle a very short source data.

		*( (uint8_t*) dst + 1 ) = (uint8_t) srcl;

		memcpy( (uint8_t*) dst + 2, src, srcl );
		memset( (uint8_t*) dst + 2 + srcl, 0, LZAV_LIT_FIN - srcl );

		return( 2 + LZAV_LIT_FIN );
	}

	uint32_t stack_buf[ 2048 ]; // On-stack hash-table.
	void* alloc_buf = 0; // Hash-table allocated on heap.
	uint8_t* ht = (uint8_t*) stack_buf; // The actual hash-table pointer.

	int htcap = 1 << 8; // Hash-table's capacity (power-of-2).

	while( htcap != ( 1 << 17 ) && htcap * 5 < srcl )
	{
		htcap <<= 1;
	}

	const size_t htsize = htcap * sizeof( uint32_t );

	if( htsize > sizeof( stack_buf ))
	{
		if( ext_buf != 0 && ext_bufl >= (int) htsize )
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

	uint32_t* const ht32 = (uint32_t*) ht;
	int i;

	for( i = 0; i < htcap; i++ )
	{
		ht32[ i ] = LZAV_REF_MIN; // Set item offset to avoid rc2 OOB below.
	}

	const uint32_t hmask = (uint32_t) (( htsize - 1 ) ^ 3 ); // Hash mask.
	const uint8_t* ip = (const uint8_t*) src; // Source data pointer.
	const uint8_t* const ipe = ip + srcl - LZAV_LIT_FIN; // End pointer.
	const uint8_t* const ipet = ipe - LZAV_REF_MIN + 1; // Hashing threshold.
	const uint8_t* ipa = ip; // Literals anchor pointer.
	uint8_t* op = (uint8_t*) dst; // Destination (compressed data) pointer.
	uint8_t* cbp = 0; // Pointer to the latest offset carry block header.
	int mavg = 50 << 16; // Running average of hash match percentage (*2^16).
	int rndb = 0; // PRNG bit derived from the non-matching offset.

	op++; // Advance beyond prefix byte.
	ip += LZAV_REF_MIN; // Skip source bytes, to avoid OOB in rc2 match below.

	while( LZAV_LIKELY( ip < ipet ))
	{
		// Hash source data (endianness is unimportant for compression
		// efficiency). Hash is based on the "komihash" math construct, see
		// https://github.com/avaneev/komihash for details.

		uint32_t iw1, ww1, hval;
		uint16_t iw2, ww2;
		memcpy( &iw1, ip, 4 );
		const uint32_t Seed1 = 0x243F6A88 ^ iw1;
		memcpy( &iw2, ip + 4, 2 );
		const uint64_t hm = (uint64_t) Seed1 * (uint32_t) ( 0x85A308D3 ^ iw2 );
		hval = (uint32_t) hm ^ (uint32_t) ( hm >> 32 );

		// Hash-table access.

		const uint32_t ipo = (uint32_t) ( ip - (const uint8_t*) src );
		uint32_t* const hp = (uint32_t*) ( ht + ( hval & hmask ));
		uint32_t wpo = *hp; // Source data position associated with hash.
		const uint32_t wpo0 = wpo; // The highest bit is a "match" flag.
		wpo &= 0x7FFFFFFF; // Obtain the offset.
		const uint8_t* const wp = (const uint8_t*) src + wpo; // At window ptr.

		if(( wpo0 & 0x80000000 ) == 0 ) // Replace item, if no "match" flag.
		{
			wpo = ipo;
		}

		const size_t d = ip - wp; // Reference offset.

		if( LZAV_UNLIKELY( d < 8 )) // Small offsets may be inefficient.
		{
			ip++;
			continue;
		}

		if( LZAV_UNLIKELY( d >= LZAV_WIN_LEN ))
		{
			*hp = ipo;
			ip++;
			continue;
		}

		memcpy( &ww1, wp, 4 );
		memcpy( &ww2, wp + 4, 2 );

		if( LZAV_UNLIKELY( iw1 == ww1 && iw2 == ww2 ))
		{
			// Source data and hash-table entry match.

			if( LZAV_LIKELY( d > LZAV_REF_LEN ))
			{
				// Update a matching entry which is not inside max reference
				// length's range. Otherwise, source data consisting of
				// same-byte runs won't compress well. Set the "match" flag so
				// that this "useful" offset remains in the hash-table longer.

				*hp = ipo | 0x80000000;
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

			if( lc != 0 && lc < LZAV_REF_MIN )
			{
				// Try to consume literals by finding a match at an
				// earlier position.

				const size_t rc2 = lzav_match_len( ip - lc, wp - lc, ml );

				if( rc2 >= LZAV_REF_MIN )
				{
					rc = rc2;
					ip -= lc;
					lc = 0;
				}
			}

			if( LZAV_LIKELY( rc == 0 ))
			{
				rc = LZAV_REF_MIN + lzav_match_len( ip + LZAV_REF_MIN,
					wp + LZAV_REF_MIN, ml - LZAV_REF_MIN );
			}

			op = lzav_write_blk_1( op, lc, rc, d, ipa, &cbp, LZAV_REF_MIN );
			ip += rc;
			ipa = ip;
			mavg += (( 100 << 16 ) - mavg ) >> 8; // Increase match rate.
			continue;
		}

		mavg -= mavg >> 8; // Decrease match rate.

		if( mavg < ( 12 << 16 ) && ip != ipa ) // 12% match rate threshold.
		{
			// Compression speed-up technique that keeps the number of hash
			// evaluations around 45% of compressed data length. In some cases
			// reduces the number of blocks by several percent.

			ip += 2 | rndb; // Use PRNG bit to dither match positions.
			rndb = ipo & 1; // Delay to decorrelate from current match.
			*hp = wpo;

			if( mavg < 7 << 16 )
			{
				ip += ( 7 - ( mavg >> 16 )) * 2; // Gradually faster.
			}

			continue;
		}

		*hp = wpo;
		ip++;
	}

	if( alloc_buf != 0 )
	{
		free( alloc_buf );
	}

	return( (int) ( lzav_write_fin_1( op, ipe - ipa + LZAV_LIT_FIN, ipa ) -
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
 * Function decompresses "raw" data previously compressed into the LZAV stream
 * format. Stream format 1.
 *
 * Note that while the function does perform checks to avoid OOB memory
 * accesses, and checks for decompressed data length equality, this is not a
 * strict guarantee of a valid decompression. In cases when the compressed
 * data is stored in a long-term storage without embedded data integrity
 * mechanisms (e.g., a database without RAID 0 guarantee, a binary container
 * without a digital signature nor CRC), a checksum (hash) of the original
 * uncompressed data should be stored, and then evaluated against that of the
 * decompressed data. Also, a separate checksum (hash) of application-defined
 * header, which contains uncompressed and compressed data lengths, should be
 * checked before decompression. A high-performance "komihash" hash function
 * can be used to obtain a hash value of the data.
 *
 * @param[in] src Source (compressed) data pointer, can be 0 if srcl==0.
 * Address alignment is unimportant.
 * @param[out] dst Destination (decompressed data) buffer pointer. Address
 * alignment is unimportant. Should be different to src.
 * @param srcl Source data length, in bytes, can be 0.
 * @param dstl Expected destination data length, in bytes, can be 0.
 * @return The length of decompressed data, in bytes, or any negative value if
 * some error happened. Always returns a negative value if the resulting
 * decompressed data length differs from dstl. This means that error result
 * handling requires just a check for a negative return value (see the
 * LZAV_E_x macros for possible values).
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

	if( src == 0 || dst == 0 || src == dst || dstl <= 0 )
	{
		return( LZAV_E_PARAMS );
	}

	if(( *(const uint8_t*) src >> 4 ) != 1 )
	{
		return( LZAV_E_UNKFMT );
	}

	const uint8_t* ip = (const uint8_t*) src; // Compressed data pointer.
	const uint8_t* const ipe = ip + srcl; // End pointer.
	const uint8_t* const ipet = ipe - 5; // Header read threshold LZAV_LIT_FIN.
	uint8_t* op = (uint8_t*) dst; // Destination (decompressed data) pointer.
	uint8_t* const ope = op + dstl; // Destination boundary pointer.
	uint8_t* const opet = ope - 63; // Threshold for fast copy to destination.
	const int mref1 = ( *ip & 15 ) - 1; // Minimal reference length in use - 1.
	size_t cv = 0; // Reference offset carry value.
	int csh = 0; // Reference offset carry shift.
	int bh = 0; // Current block header, updated in each branch.

	#define LZAV_LOAD16( a ) \
		uint16_t bv; \
		memcpy( &bv, a, 2 ); \
		LZAV_IEC16( bv );

	#define LZAV_LOAD32( a ) \
		uint32_t bv; \
		memcpy( &bv, a, 4 ); \
		LZAV_IEC32( bv );

	#define LZAV_SET_IPD_CV( x, v, sh ) \
		const size_t d = ( x ) << csh | cv; \
		ipd = op - d; \
		if( LZAV_UNLIKELY( (uint8_t*) dst + d > op )) return( LZAV_E_REFOOB ); \
		cv = ( v ); \
		csh = ( sh );

	#define LZAV_SET_IPD( x ) \
		LZAV_SET_IPD_CV( x, 0, 0 )

	ip++; // Advance beyond prefix byte.

	if( LZAV_LIKELY( ip < ipet ))
	{
		bh = *ip;
	}

	while( LZAV_LIKELY( ip < ipet ))
	{
		const uint8_t* ipd; // Reference or source data pointer.
		int cc = bh & 15; // Byte copy count.

		if( LZAV_LIKELY( cc != 0 )) // True, if no additional length byte.
		{
			if(( bh & 32 ) != 0 ) // True, if block type 2 or 3.
			{
				cc += mref1;

				if( LZAV_LIKELY(( bh & 16 ) == 0 )) // True, if block type 2.
				{
					LZAV_LOAD16( ip + 1 );
					LZAV_SET_IPD( bh >> 6 | bv << 2 );
					ip += 3;
					bh = *ip;

					if( LZAV_LIKELY( op < opet ))
					{
						memmove( op, ipd, 16 );
						memmove( op + 16, ipd + 16, 4 );
						op += cc;
						continue;
					}
				}
				else // Block type 3.
				{
					LZAV_SET_IPD_CV( ip[ 1 ] | ip[ 2 ] << 8 | ip[ 3 ] << 16,
						bh >> 6, 2 );

					ip += 4;
					bh = *ip;

					if( LZAV_LIKELY( op < opet ))
					{
						memmove( op, ipd, 16 );
						memmove( op + 16, ipd + 16, 4 );
						op += cc;
						continue;
					}
				}
			}
			else
			{
				if(( bh & 16 ) == 0 ) // True, if block type 0.
				{
					cv = bh >> 6;
					csh = 2;

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

					if( LZAV_LIKELY(( op < opet ) & ( ipe - ipd >= 16 )))
					{
						memcpy( op, ipd, 16 );
						op += cc;
						continue;
					}
				}
				else // Block type 1.
				{
					cc += mref1;
					LZAV_SET_IPD( bh >> 6 | ip[ 1 ] << 2 );
					ip += 2;
					bh = *ip;

					if( LZAV_LIKELY( op < opet ))
					{
						memmove( op, ipd, 16 );
						memmove( op + 16, ipd + 16, 4 );
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

		const int bt = bh & 0x30; // Block type (bits 4-5).

		if( LZAV_LIKELY( bt != 0 )) // True, if not type 0.
		{
			cc = 16 + mref1;

			if( LZAV_LIKELY( bt == 32 )) // Block type 2.
			{
				LZAV_LOAD32( ip + 1 );
				LZAV_SET_IPD( bh >> 6 | ( bv & 0xFFFF ) << 2 );
				cc += ( bv >> 16 ) & 0xFF;
				ip += 4;
				bh = bv >> 24;
			}
			else
			if( LZAV_LIKELY( bt == 16 )) // Block type 1.
			{
				LZAV_SET_IPD( bh >> 6 | ip[ 1 ] << 2 );
				cc += ip[ 2 ];
				ip += 3;
				bh = *ip;
			}
			else
			{
				LZAV_LOAD32( ip + 1 );
				LZAV_SET_IPD_CV(( bv & 0xFFFFFF ), bh >> 6, 2 );
				cc += bv >> 24;
				ip += 5;
				bh = *ip;
			}

			if( LZAV_LIKELY( op < opet ))
			{
				memmove( op, ipd, 16 );
				memmove( op + 16, ipd + 16, 16 );
				memmove( op + 32, ipd + 32, 16 );
				memmove( op + 48, ipd + 48, 16 );

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
			LZAV_LOAD16( ip + 1 );
			cv = bh >> 6;
			csh = 2;

			const int l2 = bv & 0xFF;
			const int lb = ( l2 == 255 );
			cc = 16 + l2 + (( bv >> 8 ) & ( 0x100 - lb ));
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
}

#endif // LZAV_INCLUDED
