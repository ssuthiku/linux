/*
 * Copyright (C) 2013-2015 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/iort.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_irq.h>

static struct irq_chip its_pmsi_irq_chip = {
	.name			= "ITS-pMSI",
};

static int its_pmsi_prepare(struct irq_domain *domain, struct device *dev,
			    int nvec, msi_alloc_info_t *info)
{
	struct msi_domain_info *msi_info;
	u32 dev_id;
	int ret;

	msi_info = msi_get_domain_info(domain->parent);

	/* Suck the DeviceID out of the msi-parent property */
	ret = of_property_read_u32_index(dev->of_node, "msi-parent",
					 1, &dev_id);
	if (ret)
		ret = iort_find_dev_id(dev, &dev_id);

	if (ret)
		return ret;

	/* ITS specific DeviceID, as the core ITS ignores dev. */
	info->scratchpad[0].ul = dev_id;

	return msi_info->ops->msi_prepare(domain->parent,
					  dev, nvec, info);
}

static struct msi_domain_ops its_pmsi_ops = {
	.msi_prepare	= its_pmsi_prepare,
};

static struct msi_domain_info its_pmsi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS),
	.ops	= &its_pmsi_ops,
	.chip	= &its_pmsi_irq_chip,
};

static struct of_device_id its_device_id[] = {
	{	.compatible	= "arm,gic-v3-its",	},
	{},
};

static int __init its_pmsi_init_one(void *token)
{
	struct irq_domain *parent;

	parent = irq_find_matching_host(token, DOMAIN_BUS_NEXUS);
	if (!parent || !msi_get_domain_info(parent)) {
		pr_err("Unable to locate ITS domain\n");
		return -ENXIO;
	}

	if (!platform_msi_create_irq_domain(token, &its_pmsi_domain_info,
					    parent)) {
		pr_err("Unable to create platform domain\n");
		return -ENOMEM;
	}

	return 0;
}

static int __init its_pmsi_of_init(void)
{
	struct device_node *np;

	for (np = of_find_matching_node(NULL, its_device_id); np;
	     np = of_find_matching_node(np, its_device_id)) {
		if (!of_property_read_bool(np, "msi-controller"))
			continue;

		if (its_pmsi_init_one(np))
			continue;

		pr_info("Platform MSI: %s domain created\n", np->full_name);
	}

	return 0;
}

#ifdef CONFIG_ACPI

static int __init
its_pmsi_parse_madt(struct acpi_subtable_header *header,
		    const unsigned long end)
{
	struct acpi_madt_generic_translator *its_entry =
				(struct acpi_madt_generic_translator *)header;

	if (its_pmsi_init_one((void *)its_entry->base_address))
		return 0;

	pr_info("Platform MSI: ITS@ID[%d] domain created\n",
		its_entry->translation_id);
	return 0;
}

static int __init its_pmsi_acpi_init(void)
{

	if (acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_TRANSLATOR,
				  its_pmsi_parse_madt, 0) < 0)
		pr_err("Platform MSI: error while parsing GIC ITS entries\n");

	return 0;
}
#else
inline static int __init its_pmsi_acpi_init(void)
{
	return 0;
}
#endif

static int __init its_pmsi_init(void)
{
	its_pmsi_of_init();
	its_pmsi_acpi_init();

	return 0;
}
early_initcall(its_pmsi_init);
