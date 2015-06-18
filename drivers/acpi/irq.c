/*
 * Copyright (C) 2015, Linaro Ltd.
 * 	Author: Tomasz Nowicki <tomasz.nowicki@linaro.org>
 * 	Author: Hanjun Guo <hanjun.guo@linaro.org>
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
#include <linux/init.h>

/*
 * This special acpi_table_id is automatically placed at the end of the
 * acpi_table_id[] array of all irqchips, used as the proper section
 * termination when scannin the acpi_table_id[] array.
 */
static const struct acpi_table_id
irqchip_acpi_match_end __used __section(__irqchip_acpi_table_end);

extern struct acpi_table_id __irqchip_acpi_table[];
static struct acpi_table_id *iterator;

static int __init
acpi_match_gic_redist(struct acpi_subtable_header *header,
		      const unsigned long end)
{
	return 0;
}

static bool __init
acpi_gic_redist_is_present(void)
{
	int count;

	count  =  acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR,
					acpi_match_gic_redist, 0);
	return count > 0;
}

static int __init
acpi_match_madt_subtable(struct acpi_subtable_header *header,
			 const unsigned long end)
{
	struct acpi_madt_generic_distributor *dist;
	u8 gic_version = ACPI_MADT_GIC_VERSION_NONE;

	/* Found appropriated subtable, now try to do additional matching */
	switch (header->type) {
	case ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR:

		dist = (struct acpi_madt_generic_distributor *)header;
		gic_version = dist->version;

		/*
		 * This is for backward compatibility with ACPI 5.1,
		 * which has no gic_version field.
		 */
		if (gic_version	== ACPI_MADT_GIC_VERSION_NONE) {
			/* It is GICv3/v4 if redistributor is present */
			if (acpi_gic_redist_is_present())
				gic_version = ACPI_MADT_GIC_VERSION_V3;
			else
				gic_version = ACPI_MADT_GIC_VERSION_V2;

			return 0;
		}

		/*
		 * GICv4 has meaning to KVM, for host IRQ controller we can
		 * treat it as GICv3 to avoid another IRQCHIP_ACPI_DECLARE
		 * entry.
		 */
		if (gic_version == ACPI_MADT_GIC_VERSION_V4)
			gic_version = ACPI_MADT_GIC_VERSION_V3;

		if (gic_version == iterator->driver_data)
			return 0;

		return -AE_NOT_FOUND;
	}

	/* No additional matching for the rest of subtable types for now */
	return 0;
}

void __init acpi_irq_init(void)
{

	if (acpi_disabled)
		return;

	for (iterator = __irqchip_acpi_table; iterator->id[0]; iterator++) {
		if (acpi_table_parse_madt(iterator->type,
					  acpi_match_madt_subtable, 0) <= 0)
			continue; /* No match or invalid subtables */

		acpi_table_parse(ACPI_SIG_MADT,
				 (acpi_tbl_table_handler)iterator->handler);
	}
}
