/**
 * lzav.h version 2.15
 *
 * The inclusion file for the "LZAV" in-memory data compression and
 * decompression algorithms.
 *
 * Description is available at https://github.com/avaneev/lzav
 * E-mail: aleksey.vaneev@gmail.com or info@voxengo.com
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

// Endianness definition macro, can be used as a logical constant.

#if defined( __LITTLE_ENDIAN__ ) || defined( __LITTLE_ENDIAN ) || \
	defined( _LITTLE_ENDIAN ) || defined( _WIN32 ) || defined( i386 ) || \
	defined( __i386 ) || defined( __i386__ ) || defined( __x86_64__ ) || \
	( defined( __BYTE_ORDER__ ) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ )

	#define LZAV_LITTLE_ENDIAN 1

#elif defined( __BIG_ENDIAN__ ) || defined( __BIG_ENDIAN ) || \
	defined( _BIG_ENDIAN ) || defined( __SYSC_ZARCH__ ) || \
	defined( __zarch__ ) || defined( __s390x__ ) || defined( __sparc ) || \
	defined( __sparc__ ) || \
	( defined( __BYTE_ORDER__ ) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ )

	#define LZAV_LITTLE_ENDIAN 0

#else // defined( __BIG_ENDIAN__ )

	#warning LZAV: cannot determine endianness, assuming little-endian.

	#define LZAV_LITTLE_ENDIAN 1

#endif // defined( __BIG_ENDIAN__ )

// Macro that denotes availability of required GCC-style built-in functions.

#if defined( __GNUC__ ) || defined( __clang__ ) || \
	defined( __IBMC__ ) || defined( __IBMCPP__ ) || defined( __COMPCERT__ )

	#define LZAV_GCC_BUILTINS

#endif // GCC built-ins.

// In-place endianness-correction macros, for singular variables of matching
// bit-size.

#if LZAV_LITTLE_ENDIAN

	#define LZAV_IEC16( x )
	#define LZAV_IEC32( x )

#else // LZAV_LITTLE_ENDIAN

	#if defined( LZAV_GCC_BUILTINS )

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

#if defined( LZAV_GCC_BUILTINS ) && \
	!( defined( __aarch64__ ) && defined( __APPLE__ ))

	#define LZAV_LIKELY( x ) __builtin_expect( x, 1 )
	#define LZAV_UNLIKELY( x ) __builtin_expect( x, 0 )

#else // likelihood macros

	#define LZAV_LIKELY( x ) ( x )
	#define LZAV_UNLIKELY( x ) ( x )

#endif // likelihood macros

#if defined( _MSC_VER ) && !defined( __clang__ )
	#include <intrin.h> // For _BitScanForwardX and _byteswap_X.
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
#if defined( LZAV_GCC_BUILTINS ) || defined( _MSC_VER )

	const uint8_t* const p1s = p1;
	const uint8_t* const p1e = p1 + ml;

	#if defined( _WIN64 ) || defined( __x86_64__ ) || defined( __ia64__ ) || \
		defined( __aarch64__ ) || defined( __arm64 ) || \
		defined( __PPC64__ ) || defined( __powerpc64__ ) || defined( __LP64__ )

		while( LZAV_LIKELY( p1 + 7 < p1e ))
		{
			uint64_t v1, v2, vd;
			memcpy( &v1, p1, 8 );
			memcpy( &v2, p2, 8 );
			vd = v1 ^ v2;

			if( vd != 0 )
			{
			#if defined( _MSC_VER ) && !defined( __clang__ )
				unsigned long i;
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
			unsigned long i;
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

#else // defined( LZAV_GCC_BUILTINS ) || defined( _MSC_VER )

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

#endif // defined( LZAV_GCC_BUILTINS ) || defined( _MSC_VER )
}

/**
 * Internal function writes a block to the output buffer. This function can be
 * used in custom compression algorithms.
 *
 * Stream format 1.
 *
 * "Raw" compressed stream consists of any quantity of unnumerated "blocks".
 * A block starts with a header byte, followed by several optional bytes.
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
 * stream format identifier. Compressed data always finishes with
 * LZAV_LIT_LEN literals. The lzav_write_fin_1() function should be used to
 * finalize compression.
 *
 * @param op Output buffer pointer.
 * @param lc Literal length, in bytes.
 * @param rc Reference length, in bytes, not lesser than mref.
 * @param d Reference offset, in bytes. Should be lesser than LZAV_WIN_LEN,
 * and not lesser than rc since fast copy on decompression cannot provide
 * consistency of copying of data that is not in the output yet.
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

	if( LZAV_UNLIKELY( lc != 0 ))
	{
		// Write literal block.

		const size_t cv = ( d & 3 ) << 6; // Offset carry value.
		d >>= 2;
		*cbpp = 0;

		if( LZAV_LIKELY( lc < 9 ))
		{
			*op = (uint8_t) ( cv | lc );
			op++;

			memcpy( op, ipa, 8 );
			op += lc;
		}
		else
		if( LZAV_LIKELY( lc < 1 + 15 ))
		{
			*op = (uint8_t) ( cv | lc );
			op++;

			memcpy( op, ipa, 16 );
			op += lc;
		}
		else
		if( LZAV_LIKELY( lc < 1 + 15 + 255 ))
		{
		#if LZAV_LITTLE_ENDIAN
			uint16_t ov = (uint16_t) (( lc - 1 - 15 ) << 8 | cv );
		#else // LZAV_LITTLE_ENDIAN
			uint16_t ov = (uint16_t) ( cv << 8 | ( lc - 1 - 15 ));
		#endif // LZAV_LITTLE_ENDIAN

			memcpy( op, &ov, 2 );
			op += 2;

			memcpy( op, ipa, 16 );
			memcpy( op + 16, ipa + 16, 8 );

			if( lc < 25 )
			{
				op += lc;
			}
			else
			{
				ipa += 24;
				op += 24;
				lc -= 24;

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
	else
	{
		uint8_t* const cbp = *cbpp;

		if( cbp != 0 )
		{
			// Perform offset carry to a previous block header.

			*cbp |= (uint8_t) ( d << 6 );
			d >>= 2;
			*cbpp = 0;
		}
	}

	// Write reference block.

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
 * lesser than 4*srcl, not lesser than 256, but not greater than 1
 * MiB. Same ext_bufl value can be used for any smaller sources.
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

	if( srcl <= LZAV_LIT_FIN )
	{
		// Handle a very short source data.

		*(uint8_t*) dst = LZAV_FMT_CUR << 4 | LZAV_REF_MIN;
		*( (uint8_t*) dst + 1 ) = (uint8_t) srcl;

		memcpy( (uint8_t*) dst + 2, src, srcl );
		memset( (uint8_t*) dst + 2 + srcl, 0, LZAV_LIT_FIN - srcl );

		return( 2 + LZAV_LIT_FIN );
	}

	uint32_t stack_buf[ 4096 ]; // On-stack hash-table.
	void* alloc_buf = 0; // Hash-table allocated on heap.
	uint8_t* ht = (uint8_t*) stack_buf; // The actual hash-table pointer.

	int htcap = 1 << 8; // Hash-table's capacity (power-of-2).

	while( htcap != ( 1 << 16 ) && htcap * 4 < srcl )
	{
		htcap <<= 1;
	}

	const size_t htsize = htcap * sizeof( uint32_t ) * 4;

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

	const uint32_t hmask = (uint32_t) (( htsize - 1 ) ^ 15 ); // Hash mask.
	const uint8_t* ip = (const uint8_t*) src; // Source data pointer.
	const uint8_t* const ipe = ip + srcl - LZAV_LIT_FIN; // End pointer.
	const uint8_t* const ipet = ipe - LZAV_REF_MIN + 1; // Hashing threshold.
	const uint8_t* ipa = ip; // Literals anchor pointer.
	uint8_t* op = (uint8_t*) dst; // Destination (compressed data) pointer.
	uint8_t* cbp = 0; // Pointer to the latest offset carry block header.
	int mavg = 100 << 22; // Running average of hash match rate (*2^16).
		// Two-factor average: success (0-64) by average reference length.
	int rndb = 0; // PRNG bit derived from the non-matching offset.

	ip += LZAV_REF_MIN; // Skip source bytes, to avoid OOB in rc2 match below.
	*op = LZAV_FMT_CUR << 4 | LZAV_REF_MIN; // Write prefix byte.
	op++;

	// Initialize the hash-table. Each hash-table item consists of 2 tuples
	// (4 initial match bytes; 32-bit source data offset). Set source data
	// offset to avoid rc2 OOB below.

	uint32_t initv[ 4 ] = { 0, LZAV_REF_MIN, 0, LZAV_REF_MIN };

	if( LZAV_LIKELY( ip < ipet ))
	{
		memcpy( initv, ip, 4 );
		memcpy( initv + 2, ip, 4 );
	}

	uint32_t* ht32 = (uint32_t*) ht;
	int i;

	for( i = 0; i < htcap; i++ )
	{
		ht32[ 0 ] = initv[ 0 ];
		ht32[ 1 ] = initv[ 1 ];
		ht32[ 2 ] = initv[ 2 ];
		ht32[ 3 ] = initv[ 3 ];
		ht32 += 4;
	}

	while( LZAV_LIKELY( ip < ipet ))
	{
		// Hash source data (endianness is unimportant for compression
		// efficiency). Hash is based on the "komihash" math construct, see
		// https://github.com/avaneev/komihash for details.

		uint32_t iw1, hval;
		uint16_t iw2, ww2;
		memcpy( &iw1, ip, 4 );
		const uint32_t Seed1 = 0x243F6A88 ^ iw1;
		memcpy( &iw2, ip + 4, 2 );
		const uint64_t hm = (uint64_t) Seed1 * (uint32_t) ( 0x85A308D3 ^ iw2 );
		hval = (uint32_t) hm ^ (uint32_t) ( hm >> 32 );

		// Hash-table access.

		const uint32_t ipo = (uint32_t) ( ip - (const uint8_t*) src );
		uint32_t* const hp = (uint32_t*) ( ht + ( hval & hmask ));

		// Find source data in hash-table tuples.

		uint32_t wpo; // Source data position associated with hash.
		const uint8_t* wp; // At window pointer, 0-not found.

		if( LZAV_UNLIKELY( iw1 == hp[ 0 ]))
		{
			wpo = hp[ 1 ];
			wp = (const uint8_t*) src + wpo;
			memcpy( &ww2, wp + 4, 2 );
			const uint32_t hp2 = hp[ 2 ];

			if( LZAV_UNLIKELY( iw2 != ww2 ))
			{
				if( LZAV_LIKELY( iw1 != hp2 ))
				{
					hp[ 2 ] = iw1;
					hp[ 3 ] = ipo;
					wp = 0;
				}
				else
				{
					wpo = hp[ 3 ];
					wp = (const uint8_t*) src + wpo;
					memcpy( &ww2, wp + 4, 2 );

					if( LZAV_UNLIKELY( iw2 != ww2 ))
					{
						hp[ 0 ] = iw1;
						hp[ 1 ] = ipo;
						wp = 0;
					}
				}
			}
		}
		else
		{
			if( LZAV_LIKELY( iw1 != hp[ 2 ]))
			{
				hp[ 2 ] = iw1;
				hp[ 3 ] = ipo;
				wp = 0;
			}
			else
			{
				wpo = hp[ 3 ];
				wp = (const uint8_t*) src + wpo;
				memcpy( &ww2, wp + 4, 2 );

				if( LZAV_UNLIKELY( iw2 != ww2 ))
				{
					hp[ 0 ] = iw1;
					hp[ 1 ] = ipo;
					wp = 0;
				}
			}
		}

		if( LZAV_LIKELY( wp == 0 ))
		{
			mavg -= mavg >> 11;

			if( mavg < ( 200 << 15 ) && ip != ipa ) // Speed-up threshold.
			{
				// Compression speed-up technique that keeps the number of
				// hash evaluations around 45% of compressed data length.
				// In some cases reduces the number of blocks by several
				// percent.

				ip += 2 | rndb; // Use PRNG bit to dither match positions.
				rndb = ipo & 1; // Delay to decorrelate from current match.

				if( LZAV_UNLIKELY( mavg < 130 << 15 ))
				{
					ip++;

					if( LZAV_UNLIKELY( mavg < 100 << 15 ))
					{
						ip += 100 - ( mavg >> 15 ); // Gradually faster.
					}
				}

				continue;
			}

			ip++;
			continue;
		}

		const size_t d = ipo - wpo; // Reference offset.

		if( LZAV_UNLIKELY(( d <= 7 ) | ( d >= LZAV_WIN_LEN )))
		{
			// Small offsets may be inefficient.

			if( LZAV_UNLIKELY( d >= LZAV_WIN_LEN ))
			{
				hp[ 1 + ( iw1 != hp[ 0 ]) * 2 ] = ipo;
			}

			ip++;
			continue;
		}

		// Source data and hash-table entry match.

		if( LZAV_LIKELY( d > LZAV_REF_LEN ))
		{
			// Update a matching entry which is not inside max reference
			// length's range. Otherwise, source data consisting of same-byte
			// runs won't compress well.

			const uint32_t hp0 = hp[ 0 ];

			if( LZAV_UNLIKELY( iw1 != hp0 )) // Insert tuple, or replace.
			{
				hp[ 2 ] = hp0;
				hp[ 3 ] = hp[ 1 ];
				hp[ 0 ] = iw1;
			}

			hp[ 1 ] = ipo;
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
			// Try to consume literals by finding a match at an earlier
			// position.

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
		mavg += (( (int) rc << 22 ) - mavg ) >> 10;
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
	const size_t mref1 = ( *ip & 15 ) - 1; // Minimal reference length - 1.
	size_t bh = 0; // Current block header, updated in each branch.
	size_t cv = 0; // Reference offset carry value.
	int csh = 0; // Reference offset carry shift.

	#define LZAV_LOAD16( a ) \
		uint16_t bv; \
		memcpy( &bv, a, 2 ); \
		LZAV_IEC16( bv );

	#define LZAV_LOAD32( a ) \
		uint32_t bv; \
		memcpy( &bv, a, 4 ); \
		LZAV_IEC32( bv );

	#define LZAV_MEMMOVE( d, s, c ) \
		{ uint8_t tmp[ c ]; memcpy( tmp, s, c ); memcpy( d, tmp, c ); }

	#define LZAV_SET_IPD_CV( x, v, sh ) \
		const size_t d = ( x ) << csh | cv; \
		ipd = op - d; \
		if( LZAV_UNLIKELY( (uint8_t*) dst + d > op )) \
			return( LZAV_E_REFOOB ); \
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
		if( LZAV_UNLIKELY(( bh & 0x30 ) == 0 )) // Block type 0.
		{
			const uint8_t* ipd; // Source data pointer.
			cv = bh >> 6;
			csh = 2;
			size_t cc = bh & 15; // Byte copy count.

			if( LZAV_LIKELY( cc != 0 )) // True, if no additional length byte.
			{
				ip++;
				ipd = ip;
				ip += cc;

				if( LZAV_LIKELY( ip < ipe ))
				{
					bh = *ip;

					if( LZAV_LIKELY(( op < opet ) & ( ipd + 15 < ipe )))
					{
						memcpy( op, ipd, 16 );
						op += cc;
						continue;
					}
				}
				else
				{
					if( LZAV_UNLIKELY( ip > ipe ))
					{
						return( LZAV_E_SRCOOB );
					}

					if( LZAV_LIKELY(( op < opet ) & ( ipd + 15 < ipe )))
					{
						memcpy( op, ipd, 16 );
						op += cc;
						continue;
					}
				}
			}
			else
			{
				LZAV_LOAD16( ip + 1 );

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
				if( LZAV_UNLIKELY( ip > ipe ))
				{
					return( LZAV_E_SRCOOB );
				}

				if( LZAV_LIKELY(( op < opet ) & ( ipd + 63 < ipe )))
				{
				#if defined( __AVX__ )
					memcpy( op, ipd, 32 );
					memcpy( op + 32, ipd + 32, 32 );
				#else // defined( __AVX__ )
					memcpy( op, ipd, 16 );
					memcpy( op + 16, ipd + 16, 16 );
					memcpy( op + 32, ipd + 32, 16 );
					memcpy( op + 48, ipd + 48, 16 );
				#endif // defined( __AVX__ )

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

		size_t cc = bh & 15; // Byte copy count.

		if( LZAV_LIKELY( cc != 0 )) // True, if no additional length byte.
		{
			const uint8_t* ipd; // Reference data pointer.
			cc += mref1;

			if( LZAV_LIKELY(( bh & 32 ) != 0 )) // True, if block type 2 or 3.
			{
				if( LZAV_LIKELY(( bh & 16 ) == 0 )) // True, if block type 2.
				{
					LZAV_LOAD16( ip + 1 );
					LZAV_SET_IPD( bh >> 6 | bv << 2 );
					ip += 3;
					bh = *ip;

					if( LZAV_LIKELY( op < opet ))
					{
					#if defined( __AVX__ )
						LZAV_MEMMOVE( op, ipd, 32 );
					#else // defined( __AVX__ )
						LZAV_MEMMOVE( op, ipd, 16 );
						LZAV_MEMMOVE( op + 16, ipd + 16, 4 );
					#endif // defined( __AVX__ )

						op += cc;
						continue;
					}
				}
				else // Block type 3.
				{
					LZAV_LOAD32( ip + 1 );
					LZAV_SET_IPD_CV( bv & 0xFFFFFF, bh >> 6, 2 );
					ip += 4;
					bh = bv >> 24;

					if( LZAV_LIKELY( op < opet ))
					{
					#if defined( __AVX__ )
						LZAV_MEMMOVE( op, ipd, 32 );
					#else // defined( __AVX__ )
						LZAV_MEMMOVE( op, ipd, 16 );
						LZAV_MEMMOVE( op + 16, ipd + 16, 4 );
					#endif // defined( __AVX__ )

						op += cc;
						continue;
					}
				}
			}
			else // Block type 1.
			{
				LZAV_SET_IPD( bh >> 6 | ip[ 1 ] << 2 );
				ip += 2;
				bh = *ip;

				if( LZAV_LIKELY( op < opet ))
				{
				#if defined( __AVX__ )
					LZAV_MEMMOVE( op, ipd, 32 );
				#else // defined( __AVX__ )
					LZAV_MEMMOVE( op, ipd, 16 );
					LZAV_MEMMOVE( op + 16, ipd + 16, 4 );
				#endif // defined( __AVX__ )

					op += cc;
					continue;
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

			continue;
		}

		// Handle large copy count blocks.

		const uint8_t* ipd; // Reference data pointer.
		cc = 16 + mref1;

		if( LZAV_LIKELY(( bh & 32 ) != 0 )) // True, if block type 2 or 3.
		{
			if( LZAV_LIKELY(( bh & 16 ) == 0 )) // True, if block type 2.
			{
				LZAV_LOAD32( ip + 1 );
				LZAV_SET_IPD( bh >> 6 | ( bv & 0xFFFF ) << 2 );
				cc += ( bv >> 16 ) & 0xFF;
				ip += 4;
				bh = bv >> 24;
			}
			else // Block type 3.
			{
				LZAV_LOAD32( ip + 1 );
				LZAV_SET_IPD_CV( bv & 0xFFFFFF, bh >> 6, 2 );
				cc += bv >> 24;
				ip += 5;
				bh = *ip;
			}
		}
		else // Block type 1.
		{
			LZAV_SET_IPD( bh >> 6 | ip[ 1 ] << 2 );
			cc += ip[ 2 ];
			ip += 3;
			bh = *ip;
		}

		if( LZAV_LIKELY( op < opet ))
		{
		#if defined( __AVX__ )
			LZAV_MEMMOVE( op, ipd, 32 );
			LZAV_MEMMOVE( op + 32, ipd + 32, 32 );
		#else // defined( __AVX__ )
			LZAV_MEMMOVE( op, ipd, 16 );
			LZAV_MEMMOVE( op + 16, ipd + 16, 16 );
			LZAV_MEMMOVE( op + 32, ipd + 32, 16 );
			LZAV_MEMMOVE( op + 48, ipd + 48, 16 );
		#endif // defined( __AVX__ )

			if( LZAV_LIKELY( cc <= 64 ))
			{
				op += cc;
				continue;
			}

			ipd += 64;
			op += 64;
			cc -= 64;
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
