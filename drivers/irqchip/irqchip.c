/*
 * Copyright (C) 2012 Thomas Petazzoni
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/of_irq.h>
#include <linux/irqchip.h>

/*
 * This special of_device_id is the sentinel at the end of the
 * of_device_id[] array of all irqchips. It is automatically placed at
 * the end of the array by the linker, thanks to being part of a
 * special section.
 */
static const struct of_device_id
irqchip_of_match_end __used __section(__irqchip_of_table_end);

extern struct of_device_id __irqchip_of_table[];

#ifdef CONFIG_ACPI
/* Same dance for ACPI */
static const struct acpi_table_id
irqchip_acpi_match_end __used __section(__irqchip_acpi_table_end);
extern struct acpi_table_id __irqchip_acpi_table[];
static struct acpi_table_id *iterator;

static int __init acpi_match_irqchip(struct acpi_subtable_header *header,
				     const unsigned long end)
{
	acpi_table_id_validate validate = iterator->validate;
	acpi_tbl_entry_handler handler = iterator->handler;

	if (validate && !validate(header, iterator))
		return 0;

	handler(header, end);
	return 0;
}

static void __init acpi_irq_init(void)
{
	if (!acpi_disabled)
		for (iterator = __irqchip_acpi_table;
		     iterator->id[0]; iterator++)
			acpi_table_parse_madt(iterator->type,
					      acpi_match_irqchip, 0);
}
#else
#define acpi_irq_init()	do {} while(0)
#endif

void __init irqchip_init(void)
{
	of_irq_init(__irqchip_of_table);
	acpi_irq_init();
}
