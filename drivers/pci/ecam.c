/*
 * Arch agnostic direct PCI config space access via
 * ECAM (Enhanced Configuration Access Mechanism)
 *
 * Per-architecture code takes care of the mappings, region validation and
 * accesses themselves.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/ecam.h>

#include <asm/io.h>

#define PREFIX "PCI: "

static DEFINE_MUTEX(pci_mmcfg_lock);

LIST_HEAD(pci_mmcfg_list);

#ifdef CONFIG_GENERIC_PCI_ECAM
static char __iomem *pci_dev_base(unsigned int seg, unsigned int bus,
				  unsigned int devfn)
{
	struct pci_mmcfg_region *cfg = pci_mmconfig_lookup(seg, bus);

	if (cfg && cfg->virt)
		return cfg->virt + (PCI_MMCFG_BUS_OFFSET(bus) | (devfn << 12));
	return NULL;
}

int pci_mmcfg_read(unsigned int seg, unsigned int bus,
			  unsigned int devfn, int reg, int len, u32 *value)
{
	char __iomem *addr;

	/* Why do we have this when nobody checks it. How about a BUG()!? -AK */
	if (unlikely((bus > 255) || (devfn > 255) || (reg > 4095))) {
err:		*value = -1;
		return -EINVAL;
	}

	rcu_read_lock();
	addr = pci_dev_base(seg, bus, devfn);
	if (!addr) {
		rcu_read_unlock();
		goto err;
	}

	*value = pci_mmio_read(len, addr + reg);
	rcu_read_unlock();

	return 0;
}

int pci_mmcfg_write(unsigned int seg, unsigned int bus,
			   unsigned int devfn, int reg, int len, u32 value)
{
	char __iomem *addr;

	/* Why do we have this when nobody checks it. How about a BUG()!? -AK */
	if (unlikely((bus > 255) || (devfn > 255) || (reg > 4095)))
		return -EINVAL;

	rcu_read_lock();
	addr = pci_dev_base(seg, bus, devfn);
	if (!addr) {
		rcu_read_unlock();
		return -EINVAL;
	}

	pci_mmio_write(len, addr + reg, value);
	rcu_read_unlock();

	return 0;
}

static void __iomem *mcfg_ioremap(struct pci_mmcfg_region *cfg)
{
	void __iomem *addr;
	u64 start, size;
	int num_buses;

	start = cfg->address + PCI_MMCFG_BUS_OFFSET(cfg->start_bus);
	num_buses = cfg->end_bus - cfg->start_bus + 1;
	size = PCI_MMCFG_BUS_OFFSET(num_buses);
	addr = ioremap_nocache(start, size);
	if (addr)
		addr -= PCI_MMCFG_BUS_OFFSET(cfg->start_bus);
	return addr;
}

int __init pci_mmcfg_arch_init(void)
{
	struct pci_mmcfg_region *cfg;

	list_for_each_entry(cfg, &pci_mmcfg_list, list)
		if (pci_mmcfg_arch_map(cfg)) {
			pci_mmcfg_arch_free();
			return 0;
		}

	return 1;
}

void __init pci_mmcfg_arch_free(void)
{
	struct pci_mmcfg_region *cfg;

	list_for_each_entry(cfg, &pci_mmcfg_list, list)
		pci_mmcfg_arch_unmap(cfg);
}

int pci_mmcfg_arch_map(struct pci_mmcfg_region *cfg)
{
	cfg->virt = mcfg_ioremap(cfg);
	if (!cfg->virt) {
		pr_err(PREFIX "can't map MMCONFIG at %pR\n", &cfg->res);
		return -ENOMEM;
	}

	return 0;
}

void pci_mmcfg_arch_unmap(struct pci_mmcfg_region *cfg)
{
	if (cfg && cfg->virt) {
		iounmap(cfg->virt + PCI_MMCFG_BUS_OFFSET(cfg->start_bus));
		cfg->virt = NULL;
	}
}
#endif

static u32
pci_mmconfig_generic_read(int len, void __iomem *addr)
{
	u32 data = 0;

	switch (len) {
	case 1:
		data = readb(addr);
		break;
	case 2:
		data = readw(addr);
		break;
	case 4:
		data = readl(addr);
		break;
	}

	return data;
}

static void
pci_mmconfig_generic_write(int len, void __iomem *addr, u32 value)
{
	switch (len) {
	case 1:
		writeb(value, addr);
		break;
	case 2:
		writew(value, addr);
		break;
	case 4:
		writel(value, addr);
		break;
	}
}

static struct pci_mmcfg_mmio_ops pci_mmcfg_mmio_default = {
	.read = pci_mmconfig_generic_read,
	.write = pci_mmconfig_generic_write,
};

static struct pci_mmcfg_mmio_ops *pci_mmcfg_mmio = &pci_mmcfg_mmio_default;

void
pci_mmconfig_register_mmio(struct pci_mmcfg_mmio_ops *ops)
{
	pci_mmcfg_mmio = ops;
}

u32
pci_mmio_read(int len, void __iomem *addr)
{
	if (!pci_mmcfg_mmio) {
		pr_err("PCI config space has no accessors !");
		return 0;
	}

	return pci_mmcfg_mmio->read(len, addr);
}

void
pci_mmio_write(int len, void __iomem *addr, u32 value)
{
	if (!pci_mmcfg_mmio) {
		pr_err("PCI config space has no accessors !");
		return;
	}

	pci_mmcfg_mmio->write(len, addr, value);
}

static void __init pci_mmconfig_remove(struct pci_mmcfg_region *cfg)
{
	if (cfg->res.parent)
		release_resource(&cfg->res);
	list_del(&cfg->list);
	kfree(cfg);
}

void __init free_all_mmcfg(void)
{
	struct pci_mmcfg_region *cfg, *tmp;

	pci_mmcfg_arch_free();
	list_for_each_entry_safe(cfg, tmp, &pci_mmcfg_list, list)
		pci_mmconfig_remove(cfg);
}

void list_add_sorted(struct pci_mmcfg_region *new)
{
	struct pci_mmcfg_region *cfg;

	/* keep list sorted by segment and starting bus number */
	list_for_each_entry_rcu(cfg, &pci_mmcfg_list, list) {
		if (cfg->segment > new->segment ||
		    (cfg->segment == new->segment &&
		     cfg->start_bus >= new->start_bus)) {
			list_add_tail_rcu(&new->list, &cfg->list);
			return;
		}
	}
	list_add_tail_rcu(&new->list, &pci_mmcfg_list);
}

struct pci_mmcfg_region *pci_mmconfig_alloc(int segment, int start,
					    int end, u64 addr)
{
	struct pci_mmcfg_region *new;
	struct resource *res;

	if (addr == 0)
		return NULL;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	new->address = addr;
	new->segment = segment;
	new->start_bus = start;
	new->end_bus = end;

	res = &new->res;
	res->start = addr + PCI_MMCFG_BUS_OFFSET(start);
	res->end = addr + PCI_MMCFG_BUS_OFFSET(end + 1) - 1;
	res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	snprintf(new->name, PCI_MMCFG_RESOURCE_NAME_LEN,
		 "PCI MMCONFIG %04x [bus %02x-%02x]", segment, start, end);
	res->name = new->name;

	return new;
}

struct pci_mmcfg_region *pci_mmconfig_add(int segment, int start,
					  int end, u64 addr)
{
	struct pci_mmcfg_region *new;

	new = pci_mmconfig_alloc(segment, start, end, addr);
	if (new) {
		mutex_lock(&pci_mmcfg_lock);
		list_add_sorted(new);
		mutex_unlock(&pci_mmcfg_lock);

		pr_info(PREFIX
		       "MMCONFIG for domain %04x [bus %02x-%02x] at %pR "
		       "(base %#lx)\n",
		       segment, start, end, &new->res, (unsigned long)addr);
	}

	return new;
}

struct pci_mmcfg_region *pci_mmconfig_lookup(int segment, int bus)
{
	struct pci_mmcfg_region *cfg;

	list_for_each_entry_rcu(cfg, &pci_mmcfg_list, list)
		if (cfg->segment == segment &&
		    cfg->start_bus <= bus && bus <= cfg->end_bus)
			return cfg;

	return NULL;
}

/* Delete MMCFG information for host bridges */
int pci_mmconfig_delete(u16 seg, u8 start, u8 end)
{
	struct pci_mmcfg_region *cfg;

	mutex_lock(&pci_mmcfg_lock);
	list_for_each_entry_rcu(cfg, &pci_mmcfg_list, list)
		if (cfg->segment == seg && cfg->start_bus == start &&
		    cfg->end_bus == end) {
			list_del_rcu(&cfg->list);
			synchronize_rcu();
			pci_mmcfg_arch_unmap(cfg);
			if (cfg->res.parent)
				release_resource(&cfg->res);
			mutex_unlock(&pci_mmcfg_lock);
			kfree(cfg);
			return 0;
		}
	mutex_unlock(&pci_mmcfg_lock);

	return -ENOENT;
}

int pci_mmconfig_inject(struct pci_mmcfg_region *cfg)
{
	struct pci_mmcfg_region *cfg_conflict;
	int err = 0;

	mutex_lock(&pci_mmcfg_lock);
	cfg_conflict = pci_mmconfig_lookup(cfg->segment, cfg->start_bus);
	if (cfg_conflict) {
		if (cfg_conflict->end_bus < cfg->end_bus)
			pr_info(FW_INFO "MMCONFIG for "
				"domain %04x [bus %02x-%02x] "
				"only partially covers this bridge\n",
				cfg_conflict->segment, cfg_conflict->start_bus,
				cfg_conflict->end_bus);
		err = -EEXIST;
		goto out;
	}

	if (pci_mmcfg_arch_map(cfg)) {
		pr_warn("fail to map MMCONFIG %pR.\n", &cfg->res);
		err = -ENOMEM;
		goto out;
	} else {
		list_add_sorted(cfg);
		pr_info("MMCONFIG at %pR (base %#lx)\n",
			&cfg->res, (unsigned long)cfg->address);

	}
out:
	mutex_unlock(&pci_mmcfg_lock);
	return err;
}
