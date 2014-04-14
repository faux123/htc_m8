/*
 *  include/asm-s390/page.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 */

#ifndef _S390_PAGE_H
#define _S390_PAGE_H

#include <linux/const.h>
#include <asm/types.h>

#define PAGE_SHIFT      12
#define PAGE_SIZE	(_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK       (~(PAGE_SIZE-1))
#define PAGE_DEFAULT_ACC	0
#define PAGE_DEFAULT_KEY	(PAGE_DEFAULT_ACC << 4)

#define HPAGE_SHIFT	20
#define HPAGE_SIZE	(1UL << HPAGE_SHIFT)
#define HPAGE_MASK	(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

#define ARCH_HAS_SETCLEAR_HUGE_PTE
#define ARCH_HAS_HUGE_PTE_TYPE
#define ARCH_HAS_PREPARE_HUGEPAGE
#define ARCH_HAS_HUGEPAGE_CLEAR_FLUSH

#include <asm/setup.h>
#ifndef __ASSEMBLY__

static inline void clear_page(void *page)
{
	if (MACHINE_HAS_PFMF) {
		asm volatile(
			"	.insn	rre,0xb9af0000,%0,%1"
			: : "d" (0x10000), "a" (page) : "memory", "cc");
	} else {
		register unsigned long reg1 asm ("1") = 0;
		register void *reg2 asm ("2") = page;
		register unsigned long reg3 asm ("3") = 4096;
		asm volatile(
			"	mvcl	2,0"
			: "+d" (reg2), "+d" (reg3) : "d" (reg1)
			: "memory", "cc");
	}
}

static inline void copy_page(void *to, void *from)
{
	if (MACHINE_HAS_MVPG) {
		register unsigned long reg0 asm ("0") = 0;
		asm volatile(
			"	mvpg	%0,%1"
			: : "a" (to), "a" (from), "d" (reg0)
			: "memory", "cc");
	} else
		asm volatile(
			"	mvc	0(256,%0),0(%1)\n"
			"	mvc	256(256,%0),256(%1)\n"
			"	mvc	512(256,%0),512(%1)\n"
			"	mvc	768(256,%0),768(%1)\n"
			"	mvc	1024(256,%0),1024(%1)\n"
			"	mvc	1280(256,%0),1280(%1)\n"
			"	mvc	1536(256,%0),1536(%1)\n"
			"	mvc	1792(256,%0),1792(%1)\n"
			"	mvc	2048(256,%0),2048(%1)\n"
			"	mvc	2304(256,%0),2304(%1)\n"
			"	mvc	2560(256,%0),2560(%1)\n"
			"	mvc	2816(256,%0),2816(%1)\n"
			"	mvc	3072(256,%0),3072(%1)\n"
			"	mvc	3328(256,%0),3328(%1)\n"
			"	mvc	3584(256,%0),3584(%1)\n"
			"	mvc	3840(256,%0),3840(%1)\n"
			: : "a" (to), "a" (from) : "memory");
}

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

#define __alloc_zeroed_user_highpage(movableflags, vma, vaddr) \
	alloc_page_vma(GFP_HIGHUSER | __GFP_ZERO | movableflags, vma, vaddr)
#define __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE


typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct { unsigned long pgste; } pgste_t;
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pud; } pud_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef pte_t *pgtable_t;

#define pgprot_val(x)	((x).pgprot)
#define pgste_val(x)	((x).pgste)
#define pte_val(x)	((x).pte)
#define pmd_val(x)	((x).pmd)
#define pud_val(x)	((x).pud)
#define pgd_val(x)      ((x).pgd)

#define __pgste(x)	((pgste_t) { (x) } )
#define __pte(x)        ((pte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pud(x)	((pud_t) { (x) } )
#define __pgd(x)        ((pgd_t) { (x) } )
#define __pgprot(x)     ((pgprot_t) { (x) } )

static inline void page_set_storage_key(unsigned long addr,
					unsigned char skey, int mapped)
{
	if (!mapped)
		asm volatile(".insn rrf,0xb22b0000,%0,%1,8,0"
			     : : "d" (skey), "a" (addr));
	else
		asm volatile("sske %0,%1" : : "d" (skey), "a" (addr));
}

static inline unsigned char page_get_storage_key(unsigned long addr)
{
	unsigned char skey;

	asm volatile("iske %0,%1" : "=d" (skey) : "a" (addr));
	return skey;
}

static inline int page_reset_referenced(unsigned long addr)
{
	unsigned int ipm;

	asm volatile(
		"	rrbe	0,%1\n"
		"	ipm	%0\n"
		: "=d" (ipm) : "a" (addr) : "cc");
	return !!(ipm & 0x20000000);
}

#define _PAGE_CHANGED		0x02	
#define _PAGE_REFERENCED	0x04	
#define _PAGE_FP_BIT		0x08	
#define _PAGE_ACC_BITS		0xf0	

#define __HAVE_ARCH_PAGE_TEST_AND_CLEAR_DIRTY
static inline int page_test_and_clear_dirty(unsigned long pfn, int mapped)
{
	unsigned char skey;

	skey = page_get_storage_key(pfn << PAGE_SHIFT);
	if (!(skey & _PAGE_CHANGED))
		return 0;
	page_set_storage_key(pfn << PAGE_SHIFT, skey & ~_PAGE_CHANGED, mapped);
	return 1;
}

#define __HAVE_ARCH_PAGE_TEST_AND_CLEAR_YOUNG
static inline int page_test_and_clear_young(unsigned long pfn)
{
	return page_reset_referenced(pfn << PAGE_SHIFT);
}

struct page;
void arch_free_page(struct page *page, int order);
void arch_alloc_page(struct page *page, int order);
void arch_set_page_states(int make_stable);

static inline int devmem_is_allowed(unsigned long pfn)
{
	return 0;
}

#define HAVE_ARCH_FREE_PAGE
#define HAVE_ARCH_ALLOC_PAGE

#endif 

#define __PAGE_OFFSET           0x0UL
#define PAGE_OFFSET             0x0UL
#define __pa(x)                 (unsigned long)(x)
#define __va(x)                 (void *)(unsigned long)(x)
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#define __HAVE_ARCH_GATE_AREA 1

#endif 
