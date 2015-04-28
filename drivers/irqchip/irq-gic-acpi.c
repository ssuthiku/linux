/*
 * ACPI based support for ARM GIC init
 *
 * Copyright (C) 2015, Linaro Ltd.
 * 	Author: Hanjun Guo <hanjun.guo@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "ACPI: GIC: " fmt

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/irqchip/arm-gic-acpi.h>
#include <linux/irqchip/arm-gic-v3.h>

/* GIC version presented in MADT GIC distributor structure */
static u8 gic_version __initdata = ACPI_MADT_GIC_VERSION_NONE;

static phys_addr_t dist_phy_base __initdata;

static int __init
acpi_gic_parse_distributor(struct acpi_subtable_header *header,
				const unsigned long end)
{
	struct acpi_madt_generic_distributor *dist;

	dist = (struct acpi_madt_generic_distributor *)header;

	if (BAD_MADT_ENTRY(dist, end))
		return -EINVAL;

	gic_version = dist->version;
	dist_phy_base = dist->base_address;
	return 0;
}

static int __init
match_gic_redist(struct acpi_subtable_header *header, const unsigned long end)
{
	return 0;
}

static bool __init acpi_gic_redist_is_present(void)
{
	int count;

	/* scan MADT table to find if we have redistributor entries */
	count  =  acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR,
					match_gic_redist, 0);

	/* has at least one GIC redistributor entry */
	if (count > 0)
		return true;
	else
		return false;
}

static int __init acpi_gic_version_init(void)
{
	int count;
	void __iomem *dist_base;
	u32 reg;

	count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR,
					acpi_gic_parse_distributor, 0);

	if (count <= 0) {
		pr_err("No valid GIC distributor entry exists\n");
		return -ENODEV;
	}

	if (gic_version >= ACPI_MADT_GIC_VERSION_RESERVED) {
		pr_err("Invalid GIC version %d in MADT\n", gic_version);
		return -EINVAL;
	}

	/*
	 * when the GIC version is 0, we fallback to hardware discovery.
	 * this is also needed to keep compatiable with ACPI 5.1,
	 * which has no gic_version field in distributor structure and
	 * reserved as 0.
	 *
	 * For hardware discovery, the offset for GICv1/2 and GICv3/4 to
	 * get the GIC version is different (0xFE8 for GICv1/2 and 0xFFE8
	 * for GICv3/4), so we need to handle it separately.
	 */
	if (gic_version	== ACPI_MADT_GIC_VERSION_NONE) {
		/* it's GICv3/v4 if redistributor is present */
		if (acpi_gic_redist_is_present()) {
			dist_base = ioremap(dist_phy_base,
					    ACPI_GICV3_DIST_MEM_SIZE);
			if (!dist_base)
				return -ENOMEM;

			reg = readl_relaxed(dist_base + GICD_PIDR2) &
					    GIC_PIDR2_ARCH_MASK;
			if (reg == GIC_PIDR2_ARCH_GICv3)
				gic_version = ACPI_MADT_GIC_VERSION_V3;
			else
				gic_version = ACPI_MADT_GIC_VERSION_V4;

			iounmap(dist_base);
		} else {
			gic_version = ACPI_MADT_GIC_VERSION_V2;
		}
	}

	return 0;
}
