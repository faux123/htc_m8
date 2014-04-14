/*
 * Copyright (C) 2001  Dave Engebretsen & Todd Inglett IBM Corporation.
 * Copyright 2001-2012 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _POWERPC_EEH_H
#define _POWERPC_EEH_H
#ifdef __KERNEL__

#include <linux/init.h>
#include <linux/list.h>
#include <linux/string.h>

struct pci_dev;
struct pci_bus;
struct device_node;

#ifdef CONFIG_EEH

#define EEH_MODE_SUPPORTED	(1<<0)	
#define EEH_MODE_NOCHECK	(1<<1)	
#define EEH_MODE_ISOLATED	(1<<2)	
#define EEH_MODE_RECOVERING	(1<<3)	
#define EEH_MODE_IRQ_DISABLED	(1<<4)	

struct eeh_dev {
	int mode;			
	int class_code;			
	int config_addr;		
	int pe_config_addr;		
	int check_count;		
	int freeze_count;		
	int false_positives;		
	u32 config_space[16];		
	struct pci_controller *phb;	
	struct device_node *dn;		
	struct pci_dev *pdev;		
};

static inline struct device_node *eeh_dev_to_of_node(struct eeh_dev *edev)
{
	return edev->dn;
}

static inline struct pci_dev *eeh_dev_to_pci_dev(struct eeh_dev *edev)
{
	return edev->pdev;
}

#define EEH_OPT_DISABLE		0	
#define EEH_OPT_ENABLE		1	
#define EEH_OPT_THAW_MMIO	2	
#define EEH_OPT_THAW_DMA	3	
#define EEH_STATE_UNAVAILABLE	(1 << 0)	
#define EEH_STATE_NOT_SUPPORT	(1 << 1)	
#define EEH_STATE_RESET_ACTIVE	(1 << 2)	
#define EEH_STATE_MMIO_ACTIVE	(1 << 3)	
#define EEH_STATE_DMA_ACTIVE	(1 << 4)	
#define EEH_STATE_MMIO_ENABLED	(1 << 5)	
#define EEH_STATE_DMA_ENABLED	(1 << 6)	
#define EEH_RESET_DEACTIVATE	0	
#define EEH_RESET_HOT		1	
#define EEH_RESET_FUNDAMENTAL	3	
#define EEH_LOG_TEMP		1	
#define EEH_LOG_PERM		2	

struct eeh_ops {
	char *name;
	int (*init)(void);
	int (*set_option)(struct device_node *dn, int option);
	int (*get_pe_addr)(struct device_node *dn);
	int (*get_state)(struct device_node *dn, int *state);
	int (*reset)(struct device_node *dn, int option);
	int (*wait_state)(struct device_node *dn, int max_wait);
	int (*get_log)(struct device_node *dn, int severity, char *drv_log, unsigned long len);
	int (*configure_bridge)(struct device_node *dn);
	int (*read_config)(struct device_node *dn, int where, int size, u32 *val);
	int (*write_config)(struct device_node *dn, int where, int size, u32 val);
};

extern struct eeh_ops *eeh_ops;
extern int eeh_subsystem_enabled;

#define EEH_MAX_ALLOWED_FREEZES 5

void * __devinit eeh_dev_init(struct device_node *dn, void *data);
void __devinit eeh_dev_phb_init_dynamic(struct pci_controller *phb);
void __init eeh_dev_phb_init(void);
void __init eeh_init(void);
#ifdef CONFIG_PPC_PSERIES
int __init eeh_pseries_init(void);
#endif
int __init eeh_ops_register(struct eeh_ops *ops);
int __exit eeh_ops_unregister(const char *name);
unsigned long eeh_check_failure(const volatile void __iomem *token,
				unsigned long val);
int eeh_dn_check_failure(struct device_node *dn, struct pci_dev *dev);
void __init pci_addr_cache_build(void);
void eeh_add_device_tree_early(struct device_node *);
void eeh_add_device_tree_late(struct pci_bus *);
void eeh_remove_bus_device(struct pci_dev *);

#define EEH_POSSIBLE_ERROR(val, type)	((val) == (type)~0 && eeh_subsystem_enabled)

#define EEH_IO_ERROR_VALUE(size)	(~0U >> ((4 - (size)) * 8))

#else 

static inline void *eeh_dev_init(struct device_node *dn, void *data)
{
	return NULL;
}

static inline void eeh_dev_phb_init_dynamic(struct pci_controller *phb) { }

static inline void eeh_dev_phb_init(void) { }

static inline void eeh_init(void) { }

#ifdef CONFIG_PPC_PSERIES
static inline int eeh_pseries_init(void)
{
	return 0;
}
#endif 

static inline unsigned long eeh_check_failure(const volatile void __iomem *token, unsigned long val)
{
	return val;
}

static inline int eeh_dn_check_failure(struct device_node *dn, struct pci_dev *dev)
{
	return 0;
}

static inline void pci_addr_cache_build(void) { }

static inline void eeh_add_device_tree_early(struct device_node *dn) { }

static inline void eeh_add_device_tree_late(struct pci_bus *bus) { }

static inline void eeh_remove_bus_device(struct pci_dev *dev) { }
#define EEH_POSSIBLE_ERROR(val, type) (0)
#define EEH_IO_ERROR_VALUE(size) (-1UL)
#endif 

#ifdef CONFIG_PPC64
static inline u8 eeh_readb(const volatile void __iomem *addr)
{
	u8 val = in_8(addr);
	if (EEH_POSSIBLE_ERROR(val, u8))
		return eeh_check_failure(addr, val);
	return val;
}

static inline u16 eeh_readw(const volatile void __iomem *addr)
{
	u16 val = in_le16(addr);
	if (EEH_POSSIBLE_ERROR(val, u16))
		return eeh_check_failure(addr, val);
	return val;
}

static inline u32 eeh_readl(const volatile void __iomem *addr)
{
	u32 val = in_le32(addr);
	if (EEH_POSSIBLE_ERROR(val, u32))
		return eeh_check_failure(addr, val);
	return val;
}

static inline u64 eeh_readq(const volatile void __iomem *addr)
{
	u64 val = in_le64(addr);
	if (EEH_POSSIBLE_ERROR(val, u64))
		return eeh_check_failure(addr, val);
	return val;
}

static inline u16 eeh_readw_be(const volatile void __iomem *addr)
{
	u16 val = in_be16(addr);
	if (EEH_POSSIBLE_ERROR(val, u16))
		return eeh_check_failure(addr, val);
	return val;
}

static inline u32 eeh_readl_be(const volatile void __iomem *addr)
{
	u32 val = in_be32(addr);
	if (EEH_POSSIBLE_ERROR(val, u32))
		return eeh_check_failure(addr, val);
	return val;
}

static inline u64 eeh_readq_be(const volatile void __iomem *addr)
{
	u64 val = in_be64(addr);
	if (EEH_POSSIBLE_ERROR(val, u64))
		return eeh_check_failure(addr, val);
	return val;
}

static inline void eeh_memcpy_fromio(void *dest, const
				     volatile void __iomem *src,
				     unsigned long n)
{
	_memcpy_fromio(dest, src, n);

	if (n >= 4 && EEH_POSSIBLE_ERROR(*((u32 *)(dest + n - 4)), u32))
		eeh_check_failure(src, *((u32 *)(dest + n - 4)));
}

static inline void eeh_readsb(const volatile void __iomem *addr, void * buf,
			      int ns)
{
	_insb(addr, buf, ns);
	if (EEH_POSSIBLE_ERROR((*(((u8*)buf)+ns-1)), u8))
		eeh_check_failure(addr, *(u8*)buf);
}

static inline void eeh_readsw(const volatile void __iomem *addr, void * buf,
			      int ns)
{
	_insw(addr, buf, ns);
	if (EEH_POSSIBLE_ERROR((*(((u16*)buf)+ns-1)), u16))
		eeh_check_failure(addr, *(u16*)buf);
}

static inline void eeh_readsl(const volatile void __iomem *addr, void * buf,
			      int nl)
{
	_insl(addr, buf, nl);
	if (EEH_POSSIBLE_ERROR((*(((u32*)buf)+nl-1)), u32))
		eeh_check_failure(addr, *(u32*)buf);
}

#endif 
#endif 
#endif 
