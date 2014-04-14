#ifndef _ASM_X86_MMX_H
#define _ASM_X86_MMX_H


#include <linux/types.h>

extern void *_mmx_memcpy(void *to, const void *from, size_t size);
extern void mmx_clear_page(void *page);
extern void mmx_copy_page(void *to, void *from);

#endif 
