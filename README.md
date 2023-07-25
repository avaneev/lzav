# LZAV - Fast Data Compression Algorithm (in C) #

## Introduction ##

LZAV is a fast general-purpose in-memory data compression algorithm based on
now-classic [LZ77](https://wikipedia.org/wiki/LZ77_and_LZ78) lossless data
compression method. LZAV holds a good position on the Pareto landscape of
factors, among many similar in-memory (non-streaming) compression algorithms.

LZAV algorithm's code is portable, scalar, header-only, inlineable C
(C++ compatible). It supports little- and big-endian platforms, and any memory
alignment models. The algorithm is efficient on both 32- and 64-bit platforms.
Incompressible data expands by no more than 0.58%.

LZAV does not sacrifice internal out-of-bounds (OOB) checks for decompression
speed. This means that LZAV can be used in strict conditions where OOB memory
writes (and especially reads) that lead to a trap, are unacceptable (e.g.,
real-time, system, server software). LZAV can be safely used to decompress
malformed or damaged compressed data. Which means that LZAV does not require
calculation of a checksum (or hash) of the compressed data. Only a checksum
of uncompressed data may be required, depending on application's guarantees.

The internal functions available in the `lzav.h` file allow you to easily
implement, and experiment with, your own compression algorithms. LZAV stream
format and decompressor have a potential of very high decompression speeds,
which depends on the way data is compressed.

## Usage Information ##

To compress data:

    #include "lzav.h"

    int max_len = lzav_compress_bound( src_len );
    void* comp_buf = malloc( max_len ); // Or similar.
    int comp_len = lzav_compress_default( src_buf, comp_buf, src_len, max_len );

    if( comp_len == 0 && src_len != 0 )
    {
        // Error handling.
    }

To decompress data:

    #include "lzav.h"

    void* decomp_buf = malloc( src_len ); // Or similar.
    int l = lzav_decompress( comp_buf, decomp_buf, comp_len, src_len );

    if( l < 0 )
    {
        // Error handling.
    }

## Comparisons ##

The tables below present performance ballpark numbers of LZAV algorithm
(based on Silesia dataset).

While there LZ4 seems to be compressing faster, LZAV comparably provides 14%
memory storage cost savings. This is a significant benefit in database and
file system use cases since CPUs rarely run at their maximum capacity anyway.
In general, LZAV holds a very strong position in this class of data
compression algorithms, if one considers all factors: compression and
decompression speeds, compression ratio, and not less important - code
maintainability: LZAV is maximally portable and has a rather small independent
codebase.

Performance of LZAV is not limited to the presented ballpark numbers.
Depending on the data being compressed, LZAV can achieve 850 MB/s compression
and 4500 MB/s decompression speeds. Incompressible data decompresses at 9300
MB/s rate, which is not far from the "memcpy". There are cases like the
[enwik9 dataset](https://mattmahoney.net/dc/textdata.html) where LZAV
provides 23% higher memory storage savings compared to LZ4. However, on very
small files, compression ratio difference between LZAV and LZ4 diminishes.

LZAV algorithm's geomean performance on a variety of datasets is 540 +/- 210
MB/s compression and 3000 +/- 1000 MB/s decompression speeds, on 4+ GHz 64-bit
processors released after 2019. Note that the algorithm exhibits adaptive
qualities, and its actual performance depends on the data being compressed.
LZAV may show an exceptional performance on your specific datasets.

For a more comprehensive in-memory compression algorithms benchmark you may
visit [lzbench](https://github.com/inikep/lzbench).

### Apple clang 12.0.0 64-bit, macOS 13.3.1, Apple M1, 3.5 GHz ###

Silesia compression corpus

|Compressor      |Compression    |Decompression  |Ratio          |
|----            |----           |----           |----           |
|**LZAV 2.1**    |500 MB/s       |2820 MB/s      |41.63          |
|LZ4 1.9.2       |670 MB/s       |3950 MB/s      |47.60          |
|LZF 3.6         |390 MB/s       |810 MB/s       |48.15          |

### LLVM clang-cl 8.0.1 64-bit, Windows 10, Ryzen 3700X (Zen2), 4.2 GHz ###

Silesia compression corpus

|Compressor      |Compression    |Decompression  |Ratio          |
|----            |----           |----           |----           |
|**LZAV 2.1**    |430 MB/s       |2540 MB/s      |41.63          |
|LZ4 1.9.2       |660 MB/s       |4200 MB/s      |47.60          |
|LZF 3.6         |350 MB/s       |700 MB/s       |48.15          |

### LLVM clang 12.0.1 64-bit, CentOS 8, Xeon E-2176G (CoffeeLake), 4.5 GHz ###

Silesia compression corpus

|Compressor      |Compression    |Decompression  |Ratio          |
|----            |----           |----           |----           |
|**LZAV 2.1**    |380 MB/s       |2100 MB/s      |41.63          |
|LZ4 1.9.2       |620 MB/s       |4300 MB/s      |47.60          |
|LZF 3.6         |370 MB/s       |880 MB/s       |48.15          |
