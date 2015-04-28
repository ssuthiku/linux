/*
 * Code borrowed from powerpc/kernel/pci-common.c
 *
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2014 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 */

#include <linux/acpi.h>
#include <linux/ecam.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci-acpi.h>
#include <linux/slab.h>

#include <asm/pci-bridge.h>

/*
 * Called after each bus is probed, but before its children are examined
 */
void pcibios_fixup_bus(struct pci_bus *bus)
{
	/* nothing to do, expected to be removed in the future */
}

/*
 * We don't have to worry about legacy ISA devices, so nothing to do here
 */
resource_size_t pcibios_align_resource(void *data, const struct resource *res,
				resource_size_t size, resource_size_t align)
{
	return res->start;
}

/*
 * Try to assign the IRQ number from DT when adding a new device
 */
int pcibios_add_device(struct pci_dev *dev)
{
	if (acpi_disabled)
		dev->irq = of_irq_parse_and_map_pci(dev, 0, 0);

	return 0;
}

#ifdef CONFIG_ACPI
int pcibios_root_bridge_prepare(struct pci_host_bridge *bridge)
{
	struct pci_controller *sd = bridge->bus->sysdata;

	ACPI_COMPANION_SET(&bridge->dev, sd->companion);
	return 0;
}

void pcibios_add_bus(struct pci_bus *bus)
{
	acpi_pci_add_bus(bus);
}

void pcibios_remove_bus(struct pci_bus *bus)
{
	acpi_pci_remove_bus(bus);
}

int pcibios_enable_irq(struct pci_dev *dev)
{
	if (!pci_dev_msi_enabled(dev))
		acpi_pci_irq_enable(dev);
	return 0;
}

int pcibios_disable_irq(struct pci_dev *dev)
{
	if (!pci_dev_msi_enabled(dev))
		acpi_pci_irq_disable(dev);
	return 0;
}

int pcibios_enable_device(struct pci_dev *dev, int bars)
{
	int err;

	err = pci_enable_resources(dev, bars);
	if (err < 0)
		return err;

	return pcibios_enable_irq(dev);
}

static int __init pcibios_assign_resources(void)
{
	struct pci_bus *root_bus;

	if (acpi_disabled)
		return 0;

	list_for_each_entry(root_bus, &pci_root_buses, node) {
		pcibios_resource_survey_bus(root_bus);
		pci_assign_unassigned_root_bus_resources(root_bus);
	}
	return 0;
}
/*
 * fs_initcall comes after subsys_initcall, so we know acpi scan
 * has run.
 */
fs_initcall(pcibios_assign_resources);

static int pci_read(struct pci_bus *bus, unsigned int devfn, int where,
		    int size, u32 *value)
{
	return raw_pci_read(pci_domain_nr(bus), bus->number,
			    devfn, where, size, value);
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where,
		     int size, u32 value)
{
	return raw_pci_write(pci_domain_nr(bus), bus->number,
			     devfn, where, size, value);
}

struct pci_ops pci_root_ops = {
	.read = pci_read,
	.write = pci_write,
};

struct pci_root_info {
	struct acpi_pci_root_info common;
#ifdef  CONFIG_PCI_MMCONFIG
	bool mcfg_added;
	u8 start_bus;
	u8 end_bus;
#endif
};

#ifdef CONFIG_PCI_MMCONFIG
static int pci_add_mmconfig_region(struct acpi_pci_root_info *ci)
{
	struct pci_mmcfg_region *cfg;
	struct pci_root_info *info;
	struct acpi_pci_root *root = ci->root;
	int err, seg = ci->controller.segment;

	info = container_of(ci, struct pci_root_info, common);
	info->start_bus = (u8)root->secondary.start;
	info->end_bus = (u8)root->secondary.end;
	info->mcfg_added = false;

	rcu_read_lock();
	cfg = pci_mmconfig_lookup(seg, info->start_bus);
	rcu_read_unlock();
	if (cfg)
		return 0;

	cfg = pci_mmconfig_alloc(seg, info->start_bus, info->end_bus,
				 root->mcfg_addr);
	if (!cfg)
		return -ENOMEM;

	err = pci_mmconfig_inject(cfg);
	if (!err)
		info->mcfg_added = true;

	return err;
}

static void pci_remove_mmconfig_region(struct acpi_pci_root_info *ci)
{
	struct pci_root_info *info;

	info = container_of(ci, struct pci_root_info, common);
	if (info->mcfg_added) {
		pci_mmconfig_delete(ci->controller.segment, info->start_bus,
				    info->end_bus);
		info->mcfg_added = false;
	}
}
#else
static int pci_add_mmconfig_region(struct acpi_pci_root_info *ci)
{
	return 0;
}

static void pci_remove_mmconfig_region(struct acpi_pci_root_info *ci) { }
#endif

static int pci_acpi_root_init_info(struct acpi_pci_root_info *ci)
{
	return pci_add_mmconfig_region(ci);
}

static void pci_acpi_root_release_info(struct acpi_pci_root_info *ci)
{
	struct pci_root_info *info;

	info = container_of(ci, struct pci_root_info, common);
	pci_remove_mmconfig_region(ci);
	kfree(info);
}

static int pci_acpi_root_prepare_resources(struct acpi_pci_root_info *ci,
					   int status)
{
	struct resource_entry *entry, *tmp;

	resource_list_for_each_entry_safe(entry, tmp, &ci->resources) {
		struct resource *res = entry->res;

		/*
		 * Special handling for ARM IO range
		 * TODO: need to move pci_register_io_range() function out
		 * of drivers/of/address.c for both used by DT and ACPI
		 */
		if (res->flags & IORESOURCE_IO) {
			unsigned long port;
			int err;
			resource_size_t length = res->end - res->start;

			err = pci_register_io_range(res->start, length);
			if (err) {
				resource_list_destroy_entry(entry);
				continue;
			}

			port = pci_address_to_pio(res->start);
			if (port == (unsigned long)-1) {
				resource_list_destroy_entry(entry);
				continue;
			}

			res->start = port;
			res->end = res->start + length - 1;

			if (pci_remap_iospace(res, res->start) < 0)
				resource_list_destroy_entry(entry);
		}
	}

	return status;
}

static struct acpi_pci_root_ops acpi_pci_root_ops = {
	.pci_ops = &pci_root_ops,
	.init_info = pci_acpi_root_init_info,
	.release_info = pci_acpi_root_release_info,
	.prepare_resources = pci_acpi_root_prepare_resources,
};

/* Root bridge scanning */
struct pci_bus *pci_acpi_scan_root(struct acpi_pci_root *root)
{
	struct pci_root_info *info;
	int node;
	int domain = root->segment;
	int busnum = root->secondary.start;
	struct pci_bus *bus;

	if (domain && !pci_domains_supported) {
		pr_warn("PCI %04x:%02x: multiple domains not supported.\n",
			domain, busnum);
		return NULL;
	}

	node = acpi_get_node(root->device->handle);
	info = kzalloc_node(sizeof(*info), GFP_KERNEL, node);
	if (!info) {
		dev_err(&root->device->dev, "pci_bus %04x:%02x: ignored (out of memory)\n",
			domain, busnum);
		return NULL;
	}

	bus = acpi_pci_root_create(root, &acpi_pci_root_ops, &info->common);

	/* After the PCI-E bus has been walked and all devices discovered,
	 * configure any settings of the fabric that might be necessary.
	 */
	if (bus) {
		struct pci_bus *child;

		list_for_each_entry(child, &bus->children, node)
			pcie_bus_configure_settings(child);
	}

	return bus;
}
#endif
