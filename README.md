# LZAV - Fast In-Memory Data Compression Algorithm (in C) #

## Introduction ##

LZAV is a fast general-purpose in-memory data compression algorithm based on
now-classic [LZ77](https://ru.wikipedia.org/wiki/LZ77) data compression
approach. LZAV holds a good position on the Pareto landscape of factors among
a wide selection of similar in-memory compression algorithms.

LZAV algorithm's code is portable, scalar, header-only, inlineable C
(C++ compatible). It supports little- and big-endian platforms, and any memory
alignment models. The algorithm is efficient on both 32- and 64-bit platforms.
Incompressible data expands by no more than 0.58%.

LZAV does not sacrifice internal OOB checks for decompression speed. This
means that LZAV can be used in strict conditions where OOB memory accesses can
lead to an application's crash. LZAV can be safely used to decompress
malformed or damaged compressed data. This means that LZAV does not require
calculation of checksum (or hash) of the compressed data. Only a checksum of
uncompressed data may be required, depending on application's guarantees.

## Usage ##

To compress data:

    #include "lzav.h"

    int max_len = lzav_compress_bound( src_len );
    void* comp_buf = malloc( max_len ); // Or similar.
    int comp_len = lzav_compress_default( src_buf, comp_buf, src_len, max_len );

    if( comp_len == 0 )
    {
        // Error handling.
    }

To decompress data:

    #include "lzav.h"

    int l = lzav_decompress( comp_buf, decomp_buf, comp_len, src_len );

    if( l < 0 )
    {
        // Error handling.
    }

## Comparisons ##

The tables below present performance ballpark numbers of LZAV algorithm.

While there LZ4 seems to be compressing faster, LZAV comparably provides 12%
memory storage cost savings. This is a significant benefit in database and
file system use cases since CPUs rarely run at their maximum capacity anyway.
In general, LZAV holds a very strong position in this class of data
compression algorithms, if one considers all factors: compression and
decompression speeds, compression ratio, and not less important, code
maintainability: LZAV is maximally portable and has a rather small independent
codebase.

Performance of LZAV is not limited to the presented ballpark numbers.
Depending on the data being compressed, LZAV can achieve 750 MB/s compression
and 4100 MB/s decompression speeds. Incompressible data decompresses at 9000
MB/s rate, not that far from the "memcpy". There are cases like the
[enwik9 benchmark](https://mattmahoney.net/dc/textdata.html) where LZAV
provides 22% higher memory storage savings compared to LZ4 while running at a
close decompression speed.

For more comprehensive in-memory compression algorithms benchmarks you may
visit [lzbench](https://github.com/inikep/lzbench).

### Apple clang 12.0.0 64-bit, macOS 12.0.1, Apple M1, 3.5 GHz ###

|Compressor      |Compression    |Decompression  |Ratio          |
|----            |----           |----           |----           |
|LZAV 1.0        |490 MB/s       |2760 MB/s      |41.84          |
|LZ4 1.9.2       |670 MB/s       |3950 MB/s      |47.60          |
|LZF 3.6         |390 MB/s       |810 MB/s       |48.15          |

### LLVM clang-cl 8.0.1 64-bit, Windows 10, Ryzen 3700X (Zen2), 4.2 GHz ###

|Compressor      |Compression    |Decompression  |Ratio          |
|----            |----           |----           |----           |
|LZAV 1.0        |410 MB/s       |2460 MB/s      |41.84          |
|LZ4 1.9.2       |660 MB/s       |4200 MB/s      |47.60          |
|LZF 3.6         |350 MB/s       |700 MB/s       |48.15          |

### LLVM clang 12.0.1 64-bit, CentOS 8, Xeon E-2176G (CoffeeLake), 4.5 GHz ###

|Compressor      |Compression    |Decompression  |Ratio          |
|----            |----           |----           |----           |
|LZAV 1.0        |370 MB/s       |2100 MB/s      |41.84          |
|LZ4 1.9.2       |620 MB/s       |4300 MB/s      |47.60          |
|LZF 3.6         |370 MB/s       |880 MB/s       |48.15          |
