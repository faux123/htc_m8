/*
 * Implement primitive realloc(3) functionality.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2006 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <stddef.h>
#include "types.h"
#include "page.h"
#include "string.h"
#include "ops.h"

#define	ENTRY_BEEN_USED	0x01
#define	ENTRY_IN_USE	0x02

static struct alloc_info {
	unsigned long	flags;
	unsigned long	base;
	unsigned long	size;
} *alloc_tbl;

static unsigned long tbl_entries;
static unsigned long alloc_min;
static unsigned long next_base;
static unsigned long space_left;

static void *simple_malloc(unsigned long size)
{
	unsigned long i;
	struct alloc_info *p = alloc_tbl;

	if (size == 0)
		goto err_out;

	size = _ALIGN_UP(size, alloc_min);

	for (i=0; i<tbl_entries; i++, p++)
		if (!(p->flags & ENTRY_BEEN_USED)) { 
			if (size <= space_left) {
				p->base = next_base;
				p->size = size;
				p->flags = ENTRY_BEEN_USED | ENTRY_IN_USE;
				next_base += size;
				space_left -= size;
				return (void *)p->base;
			}
			goto err_out; 
		}
		
		else if (!(p->flags & ENTRY_IN_USE) && (size <= p->size)) {
			p->flags |= ENTRY_IN_USE;
			return (void *)p->base;
		}
err_out:
	return NULL;
}

static struct alloc_info *simple_find_entry(void *ptr)
{
	unsigned long i;
	struct alloc_info *p = alloc_tbl;

	for (i=0; i<tbl_entries; i++,p++) {
		if (!(p->flags & ENTRY_BEEN_USED))
			break;
		if ((p->flags & ENTRY_IN_USE) &&
		    (p->base == (unsigned long)ptr))
			return p;
	}
	return NULL;
}

static void simple_free(void *ptr)
{
	struct alloc_info *p = simple_find_entry(ptr);

	if (p != NULL)
		p->flags &= ~ENTRY_IN_USE;
}

static void *simple_realloc(void *ptr, unsigned long size)
{
	struct alloc_info *p;
	void *new;

	if (size == 0) {
		simple_free(ptr);
		return NULL;
	}

	if (ptr == NULL)
		return simple_malloc(size);

	p = simple_find_entry(ptr);
	if (p == NULL) 
		return NULL;
	if (size <= p->size) 
		return ptr;

	new = simple_malloc(size);
	memcpy(new, ptr, p->size);
	simple_free(ptr);
	return new;
}

void *simple_alloc_init(char *base, unsigned long heap_size,
			unsigned long granularity, unsigned long max_allocs)
{
	unsigned long heap_base, tbl_size;

	heap_size = _ALIGN_UP(heap_size, granularity);
	alloc_min = granularity;
	tbl_entries = max_allocs;

	tbl_size = tbl_entries * sizeof(struct alloc_info);

	alloc_tbl = (struct alloc_info *)_ALIGN_UP((unsigned long)base, 8);
	memset(alloc_tbl, 0, tbl_size);

	heap_base = _ALIGN_UP((unsigned long)alloc_tbl + tbl_size, alloc_min);

	next_base = heap_base;
	space_left = heap_size;

	platform_ops.malloc = simple_malloc;
	platform_ops.free = simple_free;
	platform_ops.realloc = simple_realloc;

	return (void *)(heap_base + heap_size);
}
