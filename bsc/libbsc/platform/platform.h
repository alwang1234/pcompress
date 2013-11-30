/*-----------------------------------------------------------*/
/* Block Sorting, Lossless Data Compression Library.         */
/* Interface to platform specific functions and constants    */
/*-----------------------------------------------------------*/

/*--

This file is a part of bsc and/or libbsc, a program and a library for
lossless, block-sorting data compression.

Copyright (c) 2009-2012 Ilya Grebnov <ilya.grebnov@gmail.com>

See file AUTHORS for a full list of contributors.

The bsc and libbsc is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

The bsc and libbsc is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
License for more details.

You should have received a copy of the GNU Lesser General Public License
along with the bsc and libbsc. If not, see http://www.gnu.org/licenses/.

Please see the files COPYING and COPYING.LIB for full copyright information.

See also the bsc and libbsc web site:
  http://libbsc.com/ for more information.

--*/

#ifndef _LIBBSC_PLATFORM_H
#define _LIBBSC_PLATFORM_H

#if defined(_OPENMP) && defined(LIBBSC_OPENMP_SUPPORT)
    #include <omp.h>
    #define LIBBSC_OPENMP
#endif

#if defined(__GNUC__)
    #define INLINE __inline__
#elif defined(_MSC_VER)
    #define INLINE __forceinline
#elif defined(__IBMC__)
    #define INLINE _Inline
#elif defined(__cplusplus)
    #define INLINE inline
#else
    #define INLINE /* */
#endif

#if defined(__GNUC__) && (__GNUC__ >= 4 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4))
    #define RESTRICT __restrict__
#elif defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 600)
    #define RESTRICT __restrict
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
    #define RESTRICT __restrict
#elif defined(__CUDACC__) && (CUDA_VERSION >= 3000)
    #define RESTRICT __restrict__
#else
    #define RESTRICT /* */
#endif

#define ALPHABET_SIZE (256)

#ifdef __cplusplus
extern "C" {
#endif

    /**
    * You should call this function before you call any of the other platform specific functions.
    * @param features   - the set of additional features.
    * @return LIBBSC_NO_ERROR if no error occurred, error code otherwise.
    */
    int bsc_platform_init(int features);

    /**
    * Allocates memory blocks.
    * @param size       - bytes to allocate.
    * @return a pointer to allocated space or NULL if there is insufficient memory available.
    */
    void * bsc_malloc(size_t size);

    /**
    * Allocates memory blocks and initializes all its bits to zero.
    * @param size       - bytes to allocate.
    * @return a pointer to allocated space or NULL if there is insufficient memory available.
    */
    void * bsc_zero_malloc(size_t size);

    /**
    * Deallocates or frees a memory block.
    * @param address    - previously allocated memory block to be freed.
    */
    void bsc_free(void * address);

#ifdef __cplusplus
}
#endif

#endif

/*-----------------------------------------------------------*/
/* End                                            platform.h */
/*-----------------------------------------------------------*/
