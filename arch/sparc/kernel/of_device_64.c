#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <asm/spitfire.h>

#include "of_device_common.h"

void __iomem *of_ioremap(struct resource *res, unsigned long offset, unsigned long size, char *name)
{
	unsigned long ret = res->start + offset;
	struct resource *r;

	if (res->flags & IORESOURCE_MEM)
		r = request_mem_region(ret, size, name);
	else
		r = request_region(ret, size, name);
	if (!r)
		ret = 0;

	return (void __iomem *) ret;
}
EXPORT_SYMBOL(of_ioremap);

void of_iounmap(struct resource *res, void __iomem *base, unsigned long size)
{
	if (res->flags & IORESOURCE_MEM)
		release_mem_region((unsigned long) base, size);
	else
		release_region((unsigned long) base, size);
}
EXPORT_SYMBOL(of_iounmap);


static int of_bus_pci_match(struct device_node *np)
{
	if (!strcmp(np->name, "pci")) {
		const char *model = of_get_property(np, "model", NULL);

		if (model && !strcmp(model, "SUNW,simba"))
			return 0;

		if (!of_find_property(np, "ranges", NULL))
			return 0;

		return 1;
	}

	return 0;
}

static int of_bus_simba_match(struct device_node *np)
{
	const char *model = of_get_property(np, "model", NULL);

	if (model && !strcmp(model, "SUNW,simba"))
		return 1;

	if (!strcmp(np->name, "pci")) {
		if (!of_find_property(np, "ranges", NULL))
			return 1;
	}

	return 0;
}

static int of_bus_simba_map(u32 *addr, const u32 *range,
			    int na, int ns, int pna)
{
	return 0;
}

static void of_bus_pci_count_cells(struct device_node *np,
				   int *addrc, int *sizec)
{
	if (addrc)
		*addrc = 3;
	if (sizec)
		*sizec = 2;
}

static int of_bus_pci_map(u32 *addr, const u32 *range,
			  int na, int ns, int pna)
{
	u32 result[OF_MAX_ADDR_CELLS];
	int i;

	
	if (!((addr[0] ^ range[0]) & 0x03000000))
		goto type_match;

	if ((addr[0] & 0x03000000) == 0x03000000 &&
	    (range[0] & 0x03000000) == 0x02000000)
		goto type_match;

	return -EINVAL;

type_match:
	if (of_out_of_range(addr + 1, range + 1, range + na + pna,
			    na - 1, ns))
		return -EINVAL;

	
	memcpy(result, range + na, pna * 4);

	
	for (i = 0; i < na - 1; i++)
		result[pna - 1 - i] +=
			(addr[na - 1 - i] -
			 range[na - 1 - i]);

	memcpy(addr, result, pna * 4);

	return 0;
}

static unsigned long of_bus_pci_get_flags(const u32 *addr, unsigned long flags)
{
	u32 w = addr[0];

	
	flags = 0;
	switch((w >> 24) & 0x03) {
	case 0x01:
		flags |= IORESOURCE_IO;
		break;

	case 0x02: 
	case 0x03: 
		flags |= IORESOURCE_MEM;
		break;
	}
	if (w & 0x40000000)
		flags |= IORESOURCE_PREFETCH;
	return flags;
}

static int of_bus_fhc_match(struct device_node *np)
{
	return !strcmp(np->name, "fhc") ||
		!strcmp(np->name, "central");
}

#define of_bus_fhc_count_cells of_bus_sbus_count_cells


static struct of_bus of_busses[] = {
	
	{
		.name = "pci",
		.addr_prop_name = "assigned-addresses",
		.match = of_bus_pci_match,
		.count_cells = of_bus_pci_count_cells,
		.map = of_bus_pci_map,
		.get_flags = of_bus_pci_get_flags,
	},
	
	{
		.name = "simba",
		.addr_prop_name = "assigned-addresses",
		.match = of_bus_simba_match,
		.count_cells = of_bus_pci_count_cells,
		.map = of_bus_simba_map,
		.get_flags = of_bus_pci_get_flags,
	},
	
	{
		.name = "sbus",
		.addr_prop_name = "reg",
		.match = of_bus_sbus_match,
		.count_cells = of_bus_sbus_count_cells,
		.map = of_bus_default_map,
		.get_flags = of_bus_default_get_flags,
	},
	
	{
		.name = "fhc",
		.addr_prop_name = "reg",
		.match = of_bus_fhc_match,
		.count_cells = of_bus_fhc_count_cells,
		.map = of_bus_default_map,
		.get_flags = of_bus_default_get_flags,
	},
	
	{
		.name = "default",
		.addr_prop_name = "reg",
		.match = NULL,
		.count_cells = of_bus_default_count_cells,
		.map = of_bus_default_map,
		.get_flags = of_bus_default_get_flags,
	},
};

static struct of_bus *of_match_bus(struct device_node *np)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(of_busses); i ++)
		if (!of_busses[i].match || of_busses[i].match(np))
			return &of_busses[i];
	BUG();
	return NULL;
}

static int __init build_one_resource(struct device_node *parent,
				     struct of_bus *bus,
				     struct of_bus *pbus,
				     u32 *addr,
				     int na, int ns, int pna)
{
	const u32 *ranges;
	int rone, rlen;

	ranges = of_get_property(parent, "ranges", &rlen);
	if (ranges == NULL || rlen == 0) {
		u32 result[OF_MAX_ADDR_CELLS];
		int i;

		memset(result, 0, pna * 4);
		for (i = 0; i < na; i++)
			result[pna - 1 - i] =
				addr[na - 1 - i];

		memcpy(addr, result, pna * 4);
		return 0;
	}

	
	rlen /= 4;
	rone = na + pna + ns;
	for (; rlen >= rone; rlen -= rone, ranges += rone) {
		if (!bus->map(addr, ranges, na, ns, pna))
			return 0;
	}

	if (!strcmp(bus->name, "pci") &&
	    (addr[0] & 0x03000000) == 0x01000000)
		return 0;

	return 1;
}

static int __init use_1to1_mapping(struct device_node *pp)
{
	
	if (of_find_property(pp, "ranges", NULL) != NULL)
		return 0;

	if (!strcmp(pp->name, "dma") ||
	    !strcmp(pp->name, "espdma") ||
	    !strcmp(pp->name, "ledma") ||
	    !strcmp(pp->name, "lebuffer"))
		return 0;

	if (!strcmp(pp->name, "pci"))
		return 0;

	return 1;
}

static int of_resource_verbose;

static void __init build_device_resources(struct platform_device *op,
					  struct device *parent)
{
	struct platform_device *p_op;
	struct of_bus *bus;
	int na, ns;
	int index, num_reg;
	const void *preg;

	if (!parent)
		return;

	p_op = to_platform_device(parent);
	bus = of_match_bus(p_op->dev.of_node);
	bus->count_cells(op->dev.of_node, &na, &ns);

	preg = of_get_property(op->dev.of_node, bus->addr_prop_name, &num_reg);
	if (!preg || num_reg == 0)
		return;

	
	num_reg /= 4;

	
	num_reg /= na + ns;

	
	if (num_reg > PROMREG_MAX) {
		printk(KERN_WARNING "%s: Too many regs (%d), "
		       "limiting to %d.\n",
		       op->dev.of_node->full_name, num_reg, PROMREG_MAX);
		num_reg = PROMREG_MAX;
	}

	op->resource = op->archdata.resource;
	op->num_resources = num_reg;
	for (index = 0; index < num_reg; index++) {
		struct resource *r = &op->resource[index];
		u32 addr[OF_MAX_ADDR_CELLS];
		const u32 *reg = (preg + (index * ((na + ns) * 4)));
		struct device_node *dp = op->dev.of_node;
		struct device_node *pp = p_op->dev.of_node;
		struct of_bus *pbus, *dbus;
		u64 size, result = OF_BAD_ADDR;
		unsigned long flags;
		int dna, dns;
		int pna, pns;

		size = of_read_addr(reg + na, ns);
		memcpy(addr, reg, na * 4);

		flags = bus->get_flags(addr, 0);

		if (use_1to1_mapping(pp)) {
			result = of_read_addr(addr, na);
			goto build_res;
		}

		dna = na;
		dns = ns;
		dbus = bus;

		while (1) {
			dp = pp;
			pp = dp->parent;
			if (!pp) {
				result = of_read_addr(addr, dna);
				break;
			}

			pbus = of_match_bus(pp);
			pbus->count_cells(dp, &pna, &pns);

			if (build_one_resource(dp, dbus, pbus, addr,
					       dna, dns, pna))
				break;

			flags = pbus->get_flags(addr, flags);

			dna = pna;
			dns = pns;
			dbus = pbus;
		}

	build_res:
		memset(r, 0, sizeof(*r));

		if (of_resource_verbose)
			printk("%s reg[%d] -> %llx\n",
			       op->dev.of_node->full_name, index,
			       result);

		if (result != OF_BAD_ADDR) {
			if (tlb_type == hypervisor)
				result &= 0x0fffffffffffffffUL;

			r->start = result;
			r->end = result + size - 1;
			r->flags = flags;
		}
		r->name = op->dev.of_node->name;
	}
}

static struct device_node * __init
apply_interrupt_map(struct device_node *dp, struct device_node *pp,
		    const u32 *imap, int imlen, const u32 *imask,
		    unsigned int *irq_p)
{
	struct device_node *cp;
	unsigned int irq = *irq_p;
	struct of_bus *bus;
	phandle handle;
	const u32 *reg;
	int na, num_reg, i;

	bus = of_match_bus(pp);
	bus->count_cells(dp, &na, NULL);

	reg = of_get_property(dp, "reg", &num_reg);
	if (!reg || !num_reg)
		return NULL;

	imlen /= ((na + 3) * 4);
	handle = 0;
	for (i = 0; i < imlen; i++) {
		int j;

		for (j = 0; j < na; j++) {
			if ((reg[j] & imask[j]) != imap[j])
				goto next;
		}
		if (imap[na] == irq) {
			handle = imap[na + 1];
			irq = imap[na + 2];
			break;
		}

	next:
		imap += (na + 3);
	}
	if (i == imlen) {
		if (pp->irq_trans)
			return pp;

		return NULL;
	}

	*irq_p = irq;
	cp = of_find_node_by_phandle(handle);

	return cp;
}

static unsigned int __init pci_irq_swizzle(struct device_node *dp,
					   struct device_node *pp,
					   unsigned int irq)
{
	const struct linux_prom_pci_registers *regs;
	unsigned int bus, devfn, slot, ret;

	if (irq < 1 || irq > 4)
		return irq;

	regs = of_get_property(dp, "reg", NULL);
	if (!regs)
		return irq;

	bus = (regs->phys_hi >> 16) & 0xff;
	devfn = (regs->phys_hi >> 8) & 0xff;
	slot = (devfn >> 3) & 0x1f;

	if (pp->irq_trans) {
		if (bus & 0x80) {
			
			bus  = 0x00;
			slot = (slot - 1) << 2;
		} else {
			
			bus  = 0x10;
			slot = (slot - 2) << 2;
		}
		irq -= 1;

		ret = (bus | slot | irq);
	} else {
		ret = ((irq - 1 + (slot & 3)) & 3) + 1;
	}

	return ret;
}

static int of_irq_verbose;

static unsigned int __init build_one_device_irq(struct platform_device *op,
						struct device *parent,
						unsigned int irq)
{
	struct device_node *dp = op->dev.of_node;
	struct device_node *pp, *ip;
	unsigned int orig_irq = irq;
	int nid;

	if (irq == 0xffffffff)
		return irq;

	if (dp->irq_trans) {
		irq = dp->irq_trans->irq_build(dp, irq,
					       dp->irq_trans->data);

		if (of_irq_verbose)
			printk("%s: direct translate %x --> %x\n",
			       dp->full_name, orig_irq, irq);

		goto out;
	}

	pp = dp->parent;
	ip = NULL;
	while (pp) {
		const void *imap, *imsk;
		int imlen;

		imap = of_get_property(pp, "interrupt-map", &imlen);
		imsk = of_get_property(pp, "interrupt-map-mask", NULL);
		if (imap && imsk) {
			struct device_node *iret;
			int this_orig_irq = irq;

			iret = apply_interrupt_map(dp, pp,
						   imap, imlen, imsk,
						   &irq);

			if (of_irq_verbose)
				printk("%s: Apply [%s:%x] imap --> [%s:%x]\n",
				       op->dev.of_node->full_name,
				       pp->full_name, this_orig_irq,
				       (iret ? iret->full_name : "NULL"), irq);

			if (!iret)
				break;

			if (iret->irq_trans) {
				ip = iret;
				break;
			}
		} else {
			if (!strcmp(pp->name, "pci")) {
				unsigned int this_orig_irq = irq;

				irq = pci_irq_swizzle(dp, pp, irq);
				if (of_irq_verbose)
					printk("%s: PCI swizzle [%s] "
					       "%x --> %x\n",
					       op->dev.of_node->full_name,
					       pp->full_name, this_orig_irq,
					       irq);

			}

			if (pp->irq_trans) {
				ip = pp;
				break;
			}
		}
		dp = pp;
		pp = pp->parent;
	}
	if (!ip)
		return orig_irq;

	irq = ip->irq_trans->irq_build(op->dev.of_node, irq,
				       ip->irq_trans->data);
	if (of_irq_verbose)
		printk("%s: Apply IRQ trans [%s] %x --> %x\n",
		      op->dev.of_node->full_name, ip->full_name, orig_irq, irq);

out:
	nid = of_node_to_nid(dp);
	if (nid != -1) {
		cpumask_t numa_mask;

		cpumask_copy(&numa_mask, cpumask_of_node(nid));
		irq_set_affinity(irq, &numa_mask);
	}

	return irq;
}

static struct platform_device * __init scan_one_device(struct device_node *dp,
						 struct device *parent)
{
	struct platform_device *op = kzalloc(sizeof(*op), GFP_KERNEL);
	const unsigned int *irq;
	struct dev_archdata *sd;
	int len, i;

	if (!op)
		return NULL;

	sd = &op->dev.archdata;
	sd->op = op;

	op->dev.of_node = dp;

	irq = of_get_property(dp, "interrupts", &len);
	if (irq) {
		op->archdata.num_irqs = len / 4;

		
		if (op->archdata.num_irqs > PROMINTR_MAX) {
			printk(KERN_WARNING "%s: Too many irqs (%d), "
			       "limiting to %d.\n",
			       dp->full_name, op->archdata.num_irqs, PROMINTR_MAX);
			op->archdata.num_irqs = PROMINTR_MAX;
		}
		memcpy(op->archdata.irqs, irq, op->archdata.num_irqs * 4);
	} else {
		op->archdata.num_irqs = 0;
	}

	build_device_resources(op, parent);
	for (i = 0; i < op->archdata.num_irqs; i++)
		op->archdata.irqs[i] = build_one_device_irq(op, parent, op->archdata.irqs[i]);

	op->dev.parent = parent;
	op->dev.bus = &platform_bus_type;
	if (!parent)
		dev_set_name(&op->dev, "root");
	else
		dev_set_name(&op->dev, "%08x", dp->phandle);

	if (of_device_register(op)) {
		printk("%s: Could not register of device.\n",
		       dp->full_name);
		kfree(op);
		op = NULL;
	}

	return op;
}

static void __init scan_tree(struct device_node *dp, struct device *parent)
{
	while (dp) {
		struct platform_device *op = scan_one_device(dp, parent);

		if (op)
			scan_tree(dp->child, &op->dev);

		dp = dp->sibling;
	}
}

static int __init scan_of_devices(void)
{
	struct device_node *root = of_find_node_by_path("/");
	struct platform_device *parent;

	parent = scan_one_device(root, NULL);
	if (!parent)
		return 0;

	scan_tree(root->child, &parent->dev);
	return 0;
}
postcore_initcall(scan_of_devices);

static int __init of_debug(char *str)
{
	int val = 0;

	get_option(&str, &val);
	if (val & 1)
		of_resource_verbose = 1;
	if (val & 2)
		of_irq_verbose = 1;
	return 1;
}

__setup("of_debug=", of_debug);
