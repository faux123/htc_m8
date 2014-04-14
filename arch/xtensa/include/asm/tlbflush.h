/*
 * include/asm-xtensa/tlbflush.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_TLBFLUSH_H
#define _XTENSA_TLBFLUSH_H

#ifdef __KERNEL__

#include <linux/stringify.h>
#include <asm/processor.h>

#define DTLB_WAY_PGD	7

#define ITLB_ARF_WAYS	4
#define DTLB_ARF_WAYS	4

#define ITLB_HIT_BIT	3
#define DTLB_HIT_BIT	4

#ifndef __ASSEMBLY__


extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct*);
extern void flush_tlb_page(struct vm_area_struct*,unsigned long);
extern void flush_tlb_range(struct vm_area_struct*,unsigned long,unsigned long);

#define flush_tlb_kernel_range(start,end) flush_tlb_all()


static inline unsigned long itlb_probe(unsigned long addr)
{
	unsigned long tmp;
	__asm__ __volatile__("pitlb  %0, %1\n\t" : "=a" (tmp) : "a" (addr));
	return tmp;
}

static inline unsigned long dtlb_probe(unsigned long addr)
{
	unsigned long tmp;
	__asm__ __volatile__("pdtlb  %0, %1\n\t" : "=a" (tmp) : "a" (addr));
	return tmp;
}

static inline void invalidate_itlb_entry (unsigned long probe)
{
	__asm__ __volatile__("iitlb  %0; isync\n\t" : : "a" (probe));
}

static inline void invalidate_dtlb_entry (unsigned long probe)
{
	__asm__ __volatile__("idtlb  %0; dsync\n\t" : : "a" (probe));
}

static inline void invalidate_itlb_entry_no_isync (unsigned entry)
{
	
	__asm__ __volatile__ ("iitlb  %0\n" : : "a" (entry) );
}

static inline void invalidate_dtlb_entry_no_isync (unsigned entry)
{
	
	__asm__ __volatile__ ("idtlb  %0\n" : : "a" (entry) );
}

static inline void set_itlbcfg_register (unsigned long val)
{
	__asm__ __volatile__("wsr  %0, "__stringify(ITLBCFG)"\n\t" "isync\n\t"
			     : : "a" (val));
}

static inline void set_dtlbcfg_register (unsigned long val)
{
	__asm__ __volatile__("wsr  %0, "__stringify(DTLBCFG)"; dsync\n\t"
	    		     : : "a" (val));
}

static inline void set_ptevaddr_register (unsigned long val)
{
	__asm__ __volatile__(" wsr  %0, "__stringify(PTEVADDR)"; isync\n"
			     : : "a" (val));
}

static inline unsigned long read_ptevaddr_register (void)
{
	unsigned long tmp;
	__asm__ __volatile__("rsr  %0, "__stringify(PTEVADDR)"\n\t" : "=a" (tmp));
	return tmp;
}

static inline void write_dtlb_entry (pte_t entry, int way)
{
	__asm__ __volatile__("wdtlb  %1, %0; dsync\n\t"
			     : : "r" (way), "r" (entry) );
}

static inline void write_itlb_entry (pte_t entry, int way)
{
	__asm__ __volatile__("witlb  %1, %0; isync\n\t"
	                     : : "r" (way), "r" (entry) );
}

static inline void invalidate_page_directory (void)
{
	invalidate_dtlb_entry (DTLB_WAY_PGD);
	invalidate_dtlb_entry (DTLB_WAY_PGD+1);
	invalidate_dtlb_entry (DTLB_WAY_PGD+2);
}

static inline void invalidate_itlb_mapping (unsigned address)
{
	unsigned long tlb_entry;
	if (((tlb_entry = itlb_probe(address)) & (1 << ITLB_HIT_BIT)) != 0)
		invalidate_itlb_entry(tlb_entry);
}

static inline void invalidate_dtlb_mapping (unsigned address)
{
	unsigned long tlb_entry;
	if (((tlb_entry = dtlb_probe(address)) & (1 << DTLB_HIT_BIT)) != 0)
		invalidate_dtlb_entry(tlb_entry);
}

#define check_pgt_cache()	do { } while (0)



static inline unsigned long read_dtlb_virtual (int way)
{
	unsigned long tmp;
	__asm__ __volatile__("rdtlb0  %0, %1\n\t" : "=a" (tmp), "+a" (way));
	return tmp;
}

static inline unsigned long read_dtlb_translation (int way)
{
	unsigned long tmp;
	__asm__ __volatile__("rdtlb1  %0, %1\n\t" : "=a" (tmp), "+a" (way));
	return tmp;
}

static inline unsigned long read_itlb_virtual (int way)
{
	unsigned long tmp;
	__asm__ __volatile__("ritlb0  %0, %1\n\t" : "=a" (tmp), "+a" (way));
	return tmp;
}

static inline unsigned long read_itlb_translation (int way)
{
	unsigned long tmp;
	__asm__ __volatile__("ritlb1  %0, %1\n\t" : "=a" (tmp), "+a" (way));
	return tmp;
}

#endif	
#endif	
#endif	
