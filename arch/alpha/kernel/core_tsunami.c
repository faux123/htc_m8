/*
 *	linux/arch/alpha/kernel/core_tsunami.c
 *
 * Based on code written by David A. Rusling (david.rusling@reo.mts.dec.com).
 *
 * Code common to all TSUNAMI core logic chips.
 */

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_tsunami.h>
#undef __EXTERN_INLINE

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/ptrace.h>
#include <asm/smp.h>
#include <asm/vga.h>

#include "proto.h"
#include "pci_impl.h"


struct 
{
	unsigned long wsba[4];
	unsigned long wsm[4];
	unsigned long tba[4];
} saved_config[2] __attribute__((common));



#define DEBUG_CONFIG 0

#if DEBUG_CONFIG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif



static int
mk_conf_addr(struct pci_bus *pbus, unsigned int device_fn, int where,
	     unsigned long *pci_addr, unsigned char *type1)
{
	struct pci_controller *hose = pbus->sysdata;
	unsigned long addr;
	u8 bus = pbus->number;

	DBG_CFG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x, "
		 "pci_addr=0x%p, type1=0x%p)\n",
		 bus, device_fn, where, pci_addr, type1));
	
	if (!pbus->parent) 
		bus = 0;
	*type1 = (bus != 0);

	addr = (bus << 16) | (device_fn << 8) | where;
	addr |= hose->config_space_base;
		
	*pci_addr = addr;
	DBG_CFG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

static int 
tsunami_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		    int size, u32 *value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, devfn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		*value = __kernel_ldbu(*(vucp)addr);
		break;
	case 2:
		*value = __kernel_ldwu(*(vusp)addr);
		break;
	case 4:
		*value = *(vuip)addr;
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int 
tsunami_write_config(struct pci_bus *bus, unsigned int devfn, int where,
		     int size, u32 value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, devfn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		__kernel_stb(value, *(vucp)addr);
		mb();
		__kernel_ldbu(*(vucp)addr);
		break;
	case 2:
		__kernel_stw(value, *(vusp)addr);
		mb();
		__kernel_ldwu(*(vusp)addr);
		break;
	case 4:
		*(vuip)addr = value;
		mb();
		*(vuip)addr;
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops tsunami_pci_ops = 
{
	.read =		tsunami_read_config,
	.write = 	tsunami_write_config,
};

void
tsunami_pci_tbi(struct pci_controller *hose, dma_addr_t start, dma_addr_t end)
{
	tsunami_pchip *pchip = hose->index ? TSUNAMI_pchip1 : TSUNAMI_pchip0;
	volatile unsigned long *csr;
	unsigned long value;

	csr = &pchip->tlbia.csr;
	if (((start ^ end) & 0xffff0000) == 0)
		csr = &pchip->tlbiv.csr;

	value = (start & 0xffff0000) >> 12;

	*csr = value;
	mb();
	*csr;
}

#ifdef NXM_MACHINE_CHECKS_ON_TSUNAMI
static long __init
tsunami_probe_read(volatile unsigned long *vaddr)
{
	long dont_care, probe_result;
	int cpu = smp_processor_id();
	int s = swpipl(IPL_MCHECK - 1);

	mcheck_taken(cpu) = 0;
	mcheck_expected(cpu) = 1;
	mb();
	dont_care = *vaddr;
	draina();
	mcheck_expected(cpu) = 0;
	probe_result = !mcheck_taken(cpu);
	mcheck_taken(cpu) = 0;
	setipl(s);

	printk("dont_care == 0x%lx\n", dont_care);

	return probe_result;
}

static long __init
tsunami_probe_write(volatile unsigned long *vaddr)
{
	long true_contents, probe_result = 1;

	TSUNAMI_cchip->misc.csr |= (1L << 28); 
	true_contents = *vaddr;
	*vaddr = 0;
	draina();
	if (TSUNAMI_cchip->misc.csr & (1L << 28)) {
		int source = (TSUNAMI_cchip->misc.csr >> 29) & 7;
		TSUNAMI_cchip->misc.csr |= (1L << 28); 
		probe_result = 0;
		printk("tsunami_probe_write: unit %d at 0x%016lx\n", source,
		       (unsigned long)vaddr);
	}
	if (probe_result)
		*vaddr = true_contents;
	return probe_result;
}
#else
#define tsunami_probe_read(ADDR) 1
#endif 

static void __init
tsunami_init_one_pchip(tsunami_pchip *pchip, int index)
{
	struct pci_controller *hose;

	if (tsunami_probe_read(&pchip->pctl.csr) == 0)
		return;

	hose = alloc_pci_controller();
	if (index == 0)
		pci_isa_hose = hose;
	hose->io_space = alloc_resource();
	hose->mem_space = alloc_resource();

	hose->sparse_mem_base = 0;
	hose->sparse_io_base = 0;
	hose->dense_mem_base
	  = (TSUNAMI_MEM(index) & 0xffffffffffL) | 0x80000000000L;
	hose->dense_io_base
	  = (TSUNAMI_IO(index) & 0xffffffffffL) | 0x80000000000L;

	hose->config_space_base = TSUNAMI_CONF(index);
	hose->index = index;

	hose->io_space->start = TSUNAMI_IO(index) - TSUNAMI_IO_BIAS;
	hose->io_space->end = hose->io_space->start + TSUNAMI_IO_SPACE - 1;
	hose->io_space->name = pci_io_names[index];
	hose->io_space->flags = IORESOURCE_IO;

	hose->mem_space->start = TSUNAMI_MEM(index) - TSUNAMI_MEM_BIAS;
	hose->mem_space->end = hose->mem_space->start + 0xffffffff;
	hose->mem_space->name = pci_mem_names[index];
	hose->mem_space->flags = IORESOURCE_MEM;

	if (request_resource(&ioport_resource, hose->io_space) < 0)
		printk(KERN_ERR "Failed to request IO on hose %d\n", index);
	if (request_resource(&iomem_resource, hose->mem_space) < 0)
		printk(KERN_ERR "Failed to request MEM on hose %d\n", index);


	saved_config[index].wsba[0] = pchip->wsba[0].csr;
	saved_config[index].wsm[0] = pchip->wsm[0].csr;
	saved_config[index].tba[0] = pchip->tba[0].csr;

	saved_config[index].wsba[1] = pchip->wsba[1].csr;
	saved_config[index].wsm[1] = pchip->wsm[1].csr;
	saved_config[index].tba[1] = pchip->tba[1].csr;

	saved_config[index].wsba[2] = pchip->wsba[2].csr;
	saved_config[index].wsm[2] = pchip->wsm[2].csr;
	saved_config[index].tba[2] = pchip->tba[2].csr;

	saved_config[index].wsba[3] = pchip->wsba[3].csr;
	saved_config[index].wsm[3] = pchip->wsm[3].csr;
	saved_config[index].tba[3] = pchip->tba[3].csr;

	hose->sg_isa = iommu_arena_new(hose, 0x00800000, 0x00800000, 0);
	
        hose->sg_isa->align_entry = 4;

	hose->sg_pci = iommu_arena_new(hose, 0x40000000,
				       size_for_memory(0x40000000), 0);
        hose->sg_pci->align_entry = 4; 

	__direct_map_base = 0x80000000;
	__direct_map_size = 0x80000000;

	pchip->wsba[0].csr = hose->sg_isa->dma_base | 3;
	pchip->wsm[0].csr  = (hose->sg_isa->size - 1) & 0xfff00000;
	pchip->tba[0].csr  = virt_to_phys(hose->sg_isa->ptes);

	pchip->wsba[1].csr = hose->sg_pci->dma_base | 3;
	pchip->wsm[1].csr  = (hose->sg_pci->size - 1) & 0xfff00000;
	pchip->tba[1].csr  = virt_to_phys(hose->sg_pci->ptes);

	pchip->wsba[2].csr = 0x80000000 | 1;
	pchip->wsm[2].csr  = (0x80000000 - 1) & 0xfff00000;
	pchip->tba[2].csr  = 0;

	pchip->wsba[3].csr = 0;

	
	pchip->pctl.csr |= pctl_m_mwin;

	tsunami_pci_tbi(hose, 0, -1);
}


void __iomem *
tsunami_ioportmap(unsigned long addr)
{
	FIXUP_IOADDR_VGA(addr);
	return (void __iomem *)(addr + TSUNAMI_IO_BIAS);
}

void __iomem *
tsunami_ioremap(unsigned long addr, unsigned long size)
{
	FIXUP_MEMADDR_VGA(addr);
	return (void __iomem *)(addr + TSUNAMI_MEM_BIAS);
}

#ifndef CONFIG_ALPHA_GENERIC
EXPORT_SYMBOL(tsunami_ioportmap);
EXPORT_SYMBOL(tsunami_ioremap);
#endif

void __init
tsunami_init_arch(void)
{
#ifdef NXM_MACHINE_CHECKS_ON_TSUNAMI
	unsigned long tmp;
	
	wrent(entInt, 0);

	tmp = (unsigned long)(TSUNAMI_cchip - 1);
	printk("%s: probing bogus address:  0x%016lx\n", __func__, bogus_addr);
	printk("\tprobe %s\n",
	       tsunami_probe_write((unsigned long *)bogus_addr)
	       ? "succeeded" : "failed");
#endif 

#if 0
	printk("%s: CChip registers:\n", __func__);
	printk("%s: CSR_CSC 0x%lx\n", __func__, TSUNAMI_cchip->csc.csr);
	printk("%s: CSR_MTR 0x%lx\n", __func__, TSUNAMI_cchip.mtr.csr);
	printk("%s: CSR_MISC 0x%lx\n", __func__, TSUNAMI_cchip->misc.csr);
	printk("%s: CSR_DIM0 0x%lx\n", __func__, TSUNAMI_cchip->dim0.csr);
	printk("%s: CSR_DIM1 0x%lx\n", __func__, TSUNAMI_cchip->dim1.csr);
	printk("%s: CSR_DIR0 0x%lx\n", __func__, TSUNAMI_cchip->dir0.csr);
	printk("%s: CSR_DIR1 0x%lx\n", __func__, TSUNAMI_cchip->dir1.csr);
	printk("%s: CSR_DRIR 0x%lx\n", __func__, TSUNAMI_cchip->drir.csr);

	printk("%s: DChip registers:\n");
	printk("%s: CSR_DSC 0x%lx\n", __func__, TSUNAMI_dchip->dsc.csr);
	printk("%s: CSR_STR 0x%lx\n", __func__, TSUNAMI_dchip->str.csr);
	printk("%s: CSR_DREV 0x%lx\n", __func__, TSUNAMI_dchip->drev.csr);
#endif
	
	ioport_resource.end = ~0UL;


	tsunami_init_one_pchip(TSUNAMI_pchip0, 0);
	if (TSUNAMI_cchip->csc.csr & 1L<<14)
		tsunami_init_one_pchip(TSUNAMI_pchip1, 1);

	
	find_console_vga_hose();
}

static void
tsunami_kill_one_pchip(tsunami_pchip *pchip, int index)
{
	pchip->wsba[0].csr = saved_config[index].wsba[0];
	pchip->wsm[0].csr = saved_config[index].wsm[0];
	pchip->tba[0].csr = saved_config[index].tba[0];

	pchip->wsba[1].csr = saved_config[index].wsba[1];
	pchip->wsm[1].csr = saved_config[index].wsm[1];
	pchip->tba[1].csr = saved_config[index].tba[1];

	pchip->wsba[2].csr = saved_config[index].wsba[2];
	pchip->wsm[2].csr = saved_config[index].wsm[2];
	pchip->tba[2].csr = saved_config[index].tba[2];

	pchip->wsba[3].csr = saved_config[index].wsba[3];
	pchip->wsm[3].csr = saved_config[index].wsm[3];
	pchip->tba[3].csr = saved_config[index].tba[3];
}

void
tsunami_kill_arch(int mode)
{
	tsunami_kill_one_pchip(TSUNAMI_pchip0, 0);
	if (TSUNAMI_cchip->csc.csr & 1L<<14)
		tsunami_kill_one_pchip(TSUNAMI_pchip1, 1);
}

static inline void
tsunami_pci_clr_err_1(tsunami_pchip *pchip)
{
	pchip->perror.csr;
	pchip->perror.csr = 0x040;
	mb();
	pchip->perror.csr;
}

static inline void
tsunami_pci_clr_err(void)
{
	tsunami_pci_clr_err_1(TSUNAMI_pchip0);

	
	if (TSUNAMI_cchip->csc.csr & 1L<<14)
		tsunami_pci_clr_err_1(TSUNAMI_pchip1);
}

void
tsunami_machine_check(unsigned long vector, unsigned long la_ptr)
{
	
	mb();
	mb();  
	draina();
	tsunami_pci_clr_err();
	wrmces(0x7);
	mb();

	process_mcheck_info(vector, la_ptr, "TSUNAMI",
			    mcheck_expected(smp_processor_id()));
}
