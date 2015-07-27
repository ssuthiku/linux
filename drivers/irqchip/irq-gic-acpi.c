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

struct gic_msi_frame_handle {
	struct list_head list;
	struct acpi_madt_generic_msi_frame frame;
};

static LIST_HEAD(msi_frame_list);

static int acpi_num_msi_frame;

/* GIC version presented in MADT GIC distributor structure */
static u8 gic_version __initdata = ACPI_MADT_GIC_VERSION_NONE;

static phys_addr_t dist_phy_base __initdata;

u8 __init acpi_gic_version(void)
{
	return gic_version;
}

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
gic_acpi_parse_madt_gicc(struct acpi_subtable_header *header,
			 const unsigned long end)
{
	struct acpi_madt_generic_interrupt *gicc;

	gicc = (struct acpi_madt_generic_interrupt *)header;

	if (BAD_MADT_ENTRY(gicc, end))
		return -EINVAL;

	/*
	 * If GICC is enabled but has no valid gicr base address, then it
	 * means GICR is not presented via GICC
	 */
	if ((gicc->flags & ACPI_MADT_ENABLED) && !gicc->gicr_base_address)
		return -ENODEV;

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

	/* else try to find GICR base in GICC entries */
	count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT,
				      gic_acpi_parse_madt_gicc, 0);

	return count > 0;
}

static int __init acpi_gic_version_init(void)
{
	int count;
	u32 reg;
	void __iomem *dist_base;

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

int gic_acpi_gsi_desc_populate(struct acpi_gsi_descriptor *data,
			       u32 gsi, unsigned int irq_type)
{
	/*
	 * Encode GSI and triggering information the way the GIC likes
	 * them.
	 */
	if (WARN_ON(gsi < 16))
		return -EINVAL;

	if (gsi >= 32) {
		data->param[0] = 0;		/* SPI */
		data->param[1] = gsi - 32;
		data->param[2] = irq_type;
	} else {
		data->param[0] = 1; 		/* PPI */
		data->param[1] = gsi - 16;
		data->param[2] = 0xff << 4 | irq_type;
	}

	data->param_count = 3;

	return 0;
}

static int __init
acpi_parse_madt_msi(struct acpi_subtable_header *header,
			const unsigned long end)
{
	struct gic_msi_frame_handle *ms;
	struct acpi_madt_generic_msi_frame *frame;

	frame = (struct acpi_madt_generic_msi_frame *)header;
	if (BAD_MADT_ENTRY(frame, end))
		return -EINVAL;

	ms = kzalloc(sizeof(struct gic_msi_frame_handle *), GFP_KERNEL);
	if (!ms)
		return -ENOMEM;

	memcpy(&ms->frame, frame, sizeof(struct acpi_madt_generic_msi_frame));

	list_add(&ms->list, &msi_frame_list);

	return 0;
}

inline int acpi_get_num_msi_frames(void)
{
	return acpi_num_msi_frame;
}

int __init acpi_madt_msi_frame_init(struct acpi_table_header *table)
{
	int ret = 0;

	if (acpi_num_msi_frame > 0)
		return ret;

	ret = acpi_parse_entries(ACPI_SIG_MADT,
				 sizeof(struct acpi_table_madt),
				 acpi_parse_madt_msi, table,
				 ACPI_MADT_TYPE_GENERIC_MSI_FRAME, 0);
	if (ret == 0) {
		pr_debug("No valid ACPI GIC MSI FRAME exist\n");
		return ret;
	}

	acpi_num_msi_frame = ret;
	return 0;
}

int acpi_get_msi_frame(int index, struct acpi_madt_generic_msi_frame **p)
{
	int i = 0;
	struct gic_msi_frame_handle *m;

	if (index >= acpi_num_msi_frame)
		return -EINVAL;

	list_for_each_entry(m, &msi_frame_list, list) {
		if (i == index)
			break;
		i++;
	}

	if (i == acpi_num_msi_frame)
		return -EINVAL;

	*p = &(m->frame);
	return  0;
}

/*
 * This special acpi_table_id is the sentinel at the end of the
 * acpi_table_id[] array of all irqchips. It is automatically placed at
 * the end of the array by the linker, thanks to being part of a
 * special section.
 */
static const struct acpi_table_id
irqchip_acpi_match_end __used __section(__irqchip_acpi_table_end);

extern struct acpi_table_id __irqchip_acpi_table[];

void __init acpi_irq_init(void)
{
	struct acpi_table_id *id;

	if (acpi_disabled)
		return;

	if (acpi_gic_version_init())
		return;

	/* scan the irqchip table to match the GIC version and its driver */
	for (id = __irqchip_acpi_table; id->id[0]; id++) {
		if (gic_version == (u8)id->driver_data)
			acpi_table_parse(id->id,
					 (acpi_tbl_table_handler)id->handler);
	}
}
