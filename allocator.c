/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012 Moinak Ghosh. All rights reserved.
 * Use is subject to license terms.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 *      
 * This program includes partly-modified public domain source
 * code from the LZMA SDK: http://www.7-zip.org/sdk.html
 */

/*
 * A basic slab allocator that uses power of 2 and fixed interval
 * slab sizes and uses integer hashing to track pointers. It uses
 * per-slab and per-hash-bucket locking for scalability. This
 * allocator is being used in Pcompress as repeated compression of
 * fixed-size chunks causes repeated and predictable memory allocation
 * and freeing patterns. Using pre-allocated buffer pools in this
 * case causes significant speedup.
 *
 * There is no provision yet to reap buffers from high-usage slabs
 * and return them to the heap.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>
#include <math.h>
#include "utils.h"
#include "allocator.h"

/*
 * Number of slabs:
 * 256 bytes to 1M in power of 2 steps: 13
 * 1M to 128M in linear steps of 1M: 128
 * 200 dynamic slab slots: 200
 *
 * By doing this we try to get reasonable memory usage while not
 * sacrificing performance.
 */
#define	NUM_POW2	13
#define NUM_LINEAR	128
#define NUM_SLAB_HASH	200 /* Dynamic slabs hashtable size. */
#define	NUM_SLABS	(NUM_POW2 + NUM_LINEAR + NUM_SLAB_HASH)
#define SLAB_POS_HASH	(NUM_POW2 + NUM_LINEAR)
#define	SLAB_START_SZ	256 /* Starting slab size in Bytes. */
#define	SLAB_START_POW2	8 /* 2 ^ SLAB_START_POW2 = SLAB_START. */

#define	HTABLE_SZ	8192
#define	TWOM		(2UL * 1024UL * 1024UL)
#define	ONEM		(1UL * 1024UL * 1024UL)

static const unsigned int bv[] = {
	0xAAAAAAAA,
	0xCCCCCCCC,
	0xF0F0F0F0, 
	0xFF00FF00,
	0xFFFF0000
};

struct slabentry {
	struct bufentry *avail;
	struct slabentry *next;
	size_t sz;
	uint64_t allocs, hits;
	pthread_mutex_t slab_lock;
};
struct bufentry {
	void *ptr;
	struct slabentry *slab;
	struct bufentry *next;
};

static struct slabentry slabheads[NUM_SLABS];
static struct bufentry **htable;
static pthread_mutex_t *hbucket_locks;
static pthread_mutex_t htable_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t slab_table_lock = PTHREAD_MUTEX_INITIALIZER;
static int inited = 0;

static uint64_t total_allocs, oversize_allocs, hash_collisions, hash_entries;

/*
 * Hash function for 64Bit pointers that generates a 32Bit hash value.
 * Taken from Thomas Wang's Integer hashing paper:
 * http://www.cris.com/~Ttwang/tech/inthash.htm
 */
uint32_t
hash6432shift(uint64_t key)
{
	key = (~key) + (key << 18); // key = (key << 18) - key - 1;
	key = key ^ (key >> 31);
	key = key * 21; // key = (key + (key << 2)) + (key << 4);
	key = key ^ (key >> 11);
	key = key + (key << 6);
	key = key ^ (key >> 22);
	return (uint32_t) key;
}

void
slab_init()
{
	int i;
	size_t slab_sz;
	int nprocs;

	/* Initialize first NUM_POW2 power of 2 slots. */
	slab_sz = SLAB_START_SZ;
	for (i = 0; i < NUM_POW2; i++) {
		slabheads[i].avail = NULL;
		slabheads[i].sz = slab_sz;
		slabheads[i].allocs = 0;
		slabheads[i].hits = 0;
		/* Speed up: Copy from already inited but not yet used lock object. */
		slabheads[i].slab_lock = htable_lock;
		slab_sz *= 2;
	}

	/* At this point slab_sz is 1M. So linear slots start at 1M. */
	for (i = NUM_POW2; i < SLAB_POS_HASH; i++) {
		slabheads[i].avail = NULL;
		slabheads[i].next = NULL;
		slabheads[i].sz = slab_sz;
		slabheads[i].allocs = 0;
		slabheads[i].hits = 0;
		/* Speed up: Copy from already inited but not yet used lock object. */
		slabheads[i].slab_lock = htable_lock;
		slab_sz += ONEM;
	}

	for (i = SLAB_POS_HASH; i < NUM_SLABS; i++) {
		slabheads[i].avail = NULL;
		slabheads[i].next = NULL;
		slabheads[i].sz = 0;
		slabheads[i].allocs = 0;
		slabheads[i].hits = 0;
		/* Do not init locks here. They will be inited on demand. */
	}
	htable = (struct bufentry **)calloc(HTABLE_SZ, sizeof (struct bufentry *));
	hbucket_locks = (pthread_mutex_t *)malloc(HTABLE_SZ * sizeof (pthread_mutex_t));

	for (i=0; i<HTABLE_SZ; i++)
		hbucket_locks[i] = htable_lock;

	total_allocs = 0;
	oversize_allocs = 0;
	hash_collisions = 0;
	hash_entries = 0;
	inited = 1;
}

void
slab_cleanup(int quiet)
{
	int i;
	struct bufentry *buf, *buf1;
	uint64_t nonfreed_oversize;

	if (!inited) return;

	if (!quiet) {
		fprintf(stderr, "Slab Allocation Stats\n");
		fprintf(stderr, "==================================================================\n");
		fprintf(stderr, " Slab Size           | Allocations         | Hits                |\n");
		fprintf(stderr, "==================================================================\n");
	}

	for (i=0; i<NUM_SLABS; i++)
	{
		struct slabentry *slab;

		slab = &slabheads[i];
		while (slab) {
			if (slab->avail) {
				if (!quiet) {
					fprintf(stderr, "%21llu %21llu %21llu\n",slab->sz,
					slab->allocs, slab->hits);
				}
				slab->allocs = 0;
				buf = slab->avail;
				do {
					buf1 = buf->next;
					free(buf->ptr);
					free(buf);
					buf = buf1;
				} while (buf);
			}
			slab = slab->next;
		}
	}

	if (!quiet) {
		fprintf(stderr, "==================================================================\n");
		fprintf(stderr, "Oversize Allocations  : %llu\n", oversize_allocs);
		fprintf(stderr, "Total Requests        : %llu\n", total_allocs);
		fprintf(stderr, "Hash collisions       : %llu\n", hash_collisions);
		fprintf(stderr, "Leaked allocations    : %llu\n", hash_entries);
	}

	if (hash_entries > 0) {
		nonfreed_oversize = 0;
		for (i=0; i<HTABLE_SZ; i++) {
			buf = htable[i];

			while (buf) {
				if (buf->slab == NULL) {
					nonfreed_oversize++;
				} else {
					buf->slab->allocs++;
				}
				buf1 = buf->next;
				free(buf->ptr);
				free(buf);
				buf = buf1;
			}
		}
		free(htable);
		free(hbucket_locks);

		if (!quiet) {
			fprintf(stderr, "==================================================================\n");
			fprintf(stderr, " Slab Size           | Allocations: leaked |\n");
			fprintf(stderr, "==================================================================\n");
			for (i=0; i<NUM_SLABS; i++)
			{
				struct slabentry *slab;

				slab = &slabheads[i];
				do {
					if (slab->allocs > 0)
						fprintf(stderr, "%21llu %21llu\n", \
						    slab->sz, slab->allocs);
					slab = slab->next;
				} while (slab);
			}
		}
	}
	for (i=0; i<NUM_SLABS; i++)
	{
		struct slabentry *slab, *pslab;
		int j;

		slab = &slabheads[i];
		j = 0;
		do {
			pslab = slab;
			slab = slab->next;
			if (j > 0) free(pslab);
			j++;
		} while (slab);
	}
	if (!quiet) fprintf(stderr, "\n\n");
}

void *
slab_calloc(void *p, size_t items, size_t size) {
	void *ptr;

	ptr = slab_alloc(p, items * size);
	memset(ptr, 0, items * size);
	return (ptr);
}

/*
 * Find the power of 2 slab slot which will hold a given size.
 */
static unsigned int
find_slot(unsigned int v)
{
	unsigned int r, i;

	/* Round up to nearest power of 2 */
	v = roundup_pow_two(v);

	/*
	 * Get the log2 of the above. From Bit Twiddling Hacks:
	 * http://graphics.stanford.edu/~seander/bithacks.html
	 *
	 * This essentially tells us which bit is set.
	 */
	r = (v & bv[0]) != 0;
	r |= ((v & bv[4]) != 0) << 4;
	r |= ((v & bv[3]) != 0) << 3;
	r |= ((v & bv[2]) != 0) << 2;
	r |= ((v & bv[1]) != 0) << 1;

	/* Rebase to starting power of 2 slot number. */ 
	if (r > SLAB_START_POW2)
		r -= SLAB_START_POW2;
	else
		r = 0;

	return (r);
}

static void *
try_dynamic_slab(size_t size)
{
	uint32_t sindx;
	struct slabentry *slab;

	/* Locate the hash slot for the size. */
	sindx = hash6432shift((unsigned long)size) & (NUM_SLAB_HASH - 1);
	sindx += SLAB_POS_HASH;
	if (slabheads[sindx].sz == 0) return (NULL);

	/* Linear search in the chained buckets. */
	slab = &slabheads[sindx];
	while (slab && slab->sz != size) {
		slab = slab->next;
	}

	return (slab);
}

int
slab_cache_add(size_t size)
{
	uint32_t sindx;
	struct slabentry *slab;
	if (try_dynamic_slab(size)) return (0); /* Already added. */

	/* Locate the hash slot for the size. */
	sindx = hash6432shift((unsigned long)size) & (NUM_SLAB_HASH - 1);
	sindx += SLAB_POS_HASH;

	if (slabheads[sindx].sz == 0) {
		pthread_mutex_init(&(slabheads[sindx].slab_lock), NULL);
		pthread_mutex_lock(&(slabheads[sindx].slab_lock));
		slabheads[sindx].sz = size;
		pthread_mutex_unlock(&(slabheads[sindx].slab_lock));
	} else {
		slab = (struct slabentry *)malloc(sizeof (struct slabentry));
		if (!slab) return (0);
		slab->avail = NULL;
		slab->sz = size;
		slab->allocs = 0;
		slab->hits = 0;
		pthread_mutex_init(&(slab->slab_lock), NULL);

		pthread_mutex_lock(&(slabheads[sindx].slab_lock));
		slabheads[sindx].next = slab;
		slab->next = slabheads[sindx].next;
		pthread_mutex_unlock(&(slabheads[sindx].slab_lock));
	}
	return (1);
}

void *
slab_alloc(void *p, size_t size)
{
	size_t slab_sz = SLAB_START_SZ;
	int i;
	size_t div;
	void *ptr;
	struct slabentry *slab;

	ATOMIC_ADD(total_allocs, 1);
	slab = NULL;

	/* First check if we can use a dynamic slab of this size. */
	slab = try_dynamic_slab(size);

	if (!slab) {
		if (size <= ONEM) {
			/* First eleven slots are power of 2 sizes upto 1M. */
			slab = &slabheads[find_slot(size)];
		} else {
			/* Next slots are in intervals of 1M. */
			div = size / ONEM;
			if (size % ONEM) div++;
			if (div < NUM_LINEAR) slab = &slabheads[div + NUM_POW2];
		}
	}
	if (!slab) {
		struct bufentry *buf = (struct bufentry *)malloc(sizeof (struct bufentry));
		uint32_t hindx;

		buf->ptr = malloc(size);
		buf->slab = NULL;
		hindx = hash6432shift((unsigned long)(buf->ptr)) & (HTABLE_SZ - 1);

		pthread_mutex_lock(&hbucket_locks[hindx]);
		buf->next = htable[hindx];
		htable[hindx] = buf;
		pthread_mutex_unlock(&hbucket_locks[hindx]);
		ATOMIC_ADD(oversize_allocs, 1);
		ATOMIC_ADD(hash_entries, 1);
		return (buf->ptr);
	} else {
		struct bufentry *buf;
		uint32_t hindx;

		pthread_mutex_lock(&(slab->slab_lock));
		if (slab->avail == NULL) {
			slab->allocs++;
			pthread_mutex_unlock(&(slab->slab_lock));
			buf = (struct bufentry *)malloc(sizeof (struct bufentry));
			buf->ptr = malloc(slab->sz);
			buf->slab = slab;
		} else {
			buf = slab->avail;
			slab->avail = buf->next;
			slab->hits++;
			pthread_mutex_unlock(&(slab->slab_lock));
		}

		hindx = hash6432shift((unsigned long)(buf->ptr)) & (HTABLE_SZ - 1);
		if (htable[hindx]) ATOMIC_ADD(hash_collisions, 1);
		pthread_mutex_lock(&hbucket_locks[hindx]);
		buf->next = htable[hindx];
		htable[hindx] = buf;
		pthread_mutex_unlock(&hbucket_locks[hindx]);
		ATOMIC_ADD(hash_entries, 1);
		return (buf->ptr);
	}
}

void
slab_free(void *p, void *address)
{
	struct bufentry *buf, *pbuf;
	int found = 0;
	uint32_t hindx;

	if (!address) return;
	hindx = hash6432shift((uint64_t)(address)) & (HTABLE_SZ - 1);

	pthread_mutex_lock(&hbucket_locks[hindx]);
	buf = htable[hindx];
	pbuf = NULL;
	while (buf) {
		if (buf->ptr == address) {
			if (hash_entries <=0) {
				fprintf(stderr, "Inconsistent allocation hash\n");
				abort();
			}
			if (pbuf)
				pbuf->next = buf->next;
			else
				htable[hindx] = buf->next;
			pthread_mutex_unlock(&hbucket_locks[hindx]);
			ATOMIC_SUB(hash_entries, 1);

			if (buf->slab == NULL) {
				free(buf->ptr);
				free(buf);
				found = 1;
				break;
			} else {
				pthread_mutex_lock(&(buf->slab->slab_lock));
				buf->next = buf->slab->avail;
				buf->slab->avail = buf;
				pthread_mutex_unlock(&(buf->slab->slab_lock));
				found = 1;
				break;
			}
		}
		pbuf = buf;
		buf = buf->next;
	}
	if (!found) {
		pthread_mutex_unlock(&hbucket_locks[hindx]);
		free(address);
		fprintf(stderr, "Freed buf(%p) not in slab allocations!\n", address);
		fflush(stderr);
	}
}
