/*
 * MCFG ACPI table parser.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/acpi.h>
#include <linux/ecam.h>

#define	PREFIX	"MCFG: "

/*
 * raw_pci_read/write - ACPI PCI config space accessors.
 *
 * ACPI spec defines MCFG table as the way we can describe access to PCI config
 * space, so let MCFG be default (__weak).
 *
 * If platform needs more fancy stuff, should provides its own implementation.
 */
int __weak raw_pci_read(unsigned int domain, unsigned int bus,
			unsigned int devfn, int reg, int len, u32 *val)
{
	return pci_ecam_read(domain, bus, devfn, reg, len, val);
}

int __weak raw_pci_write(unsigned int domain, unsigned int bus,
			 unsigned int devfn, int reg, int len, u32 val)
{
	return pci_ecam_write(domain, bus, devfn, reg, len, val);
}

int __init acpi_parse_mcfg(struct acpi_table_header *header)
{
	struct acpi_table_mcfg *mcfg;
	struct acpi_mcfg_allocation *cfg_table, *cfg;
	unsigned long i;
	int entries;

	if (!header)
		return -EINVAL;

	mcfg = (struct acpi_table_mcfg *)header;

	/* how many config structures do we have */
	pci_ecam_free_all();
	entries = 0;
	i = header->length - sizeof(struct acpi_table_mcfg);
	while (i >= sizeof(struct acpi_mcfg_allocation)) {
		entries++;
		i -= sizeof(struct acpi_mcfg_allocation);
	}
	if (entries == 0) {
		pr_err(PREFIX "MCFG table has no entries\n");
		return -ENODEV;
	}

	cfg_table = (struct acpi_mcfg_allocation *) &mcfg[1];
	for (i = 0; i < entries; i++) {
		cfg = &cfg_table[i];
		if (acpi_mcfg_check_entry(mcfg, cfg)) {
			pci_ecam_free_all();
			return -ENODEV;
		}

		if (pci_ecam_add(cfg->pci_segment, cfg->start_bus_number,
				 cfg->end_bus_number, cfg->address) == NULL) {
			pr_warn(PREFIX "no memory for MCFG entries\n");
			pci_ecam_free_all();
			return -ENOMEM;
		}
	}

	return 0;
}

int __init __weak acpi_mcfg_check_entry(struct acpi_table_mcfg *mcfg,
					struct acpi_mcfg_allocation *cfg)
{
	return 0;
}

void __init __weak pci_mmcfg_early_init(void)
{

}

void __init __weak pci_mmcfg_late_init(void)
{
	struct pci_ecam_region *cfg;

	acpi_table_parse(ACPI_SIG_MCFG, acpi_parse_mcfg);

	if (list_empty(&pci_ecam_list))
		return;
	if (!pci_ecam_arch_init())
		pci_ecam_free_all();

	list_for_each_entry(cfg, &pci_ecam_list, list)
		insert_resource(&iomem_resource, &cfg->res);
}
