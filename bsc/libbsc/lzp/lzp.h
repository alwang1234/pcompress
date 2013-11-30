/*-----------------------------------------------------------*/
/* Block Sorting, Lossless Data Compression Library.         */
/* Interface to Lempel Ziv Prediction functions              */
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

#ifndef _LIBBSC_LZP_H
#define _LIBBSC_LZP_H

#ifdef __cplusplus
extern "C" {
#endif

    /**
    * Preprocess a memory block by LZP algorithm.
    * @param input      - the input memory block of n bytes.
    * @param output     - the output memory block of n bytes.
    * @param n          - the length of the input/output memory blocks.
    * @param hashSize   - the hash table size.
    * @param minLen     - the minimum match length.
    * @param features   - the set of additional features.
    * @return The length of preprocessed memory block if no error occurred, error code otherwise.
    */
    int bsc_lzp_compress(const unsigned char * input, unsigned char * output, int n, int hashSize, int minLen, int features);

    /**
    * Reconstructs the original memory block after LZP algorithm.
    * @param input      - the input memory block of n bytes.
    * @param output     - the output memory block.
    * @param n          - the length of the input memory block.
    * @param hashSize   - the hash table size.
    * @param minLen     - the minimum match length.
    * @param features   - the set of additional features.
    * @return The length of original memory block if no error occurred, error code otherwise.
    */
    int bsc_lzp_decompress(const unsigned char * input, unsigned char * output, int n, int hashSize, int minLen, int features);

#ifdef __cplusplus
}
#endif

#endif

/*-----------------------------------------------------------*/
/* End                                                 lzp.h */
/*-----------------------------------------------------------*/
