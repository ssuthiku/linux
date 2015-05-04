/*
 * ACPI GSI IRQ layer
 *
 * Copyright (C) 2015 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 *         Hanjun Guo <hanjun.guo@linaro.org> for stacked irqdomains support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/acpi.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>

enum acpi_irq_model_id acpi_irq_model;

static struct irq_domain *acpi_irq_domain __read_mostly;

void set_acpi_irq_domain(struct irq_domain *domain)
{
	acpi_irq_domain = domain;
}

static unsigned int acpi_gsi_get_irq_type(int trigger, int polarity)
{
	switch (polarity) {
	case ACPI_ACTIVE_LOW:
		return trigger == ACPI_EDGE_SENSITIVE ?
		       IRQ_TYPE_EDGE_FALLING :
		       IRQ_TYPE_LEVEL_LOW;
	case ACPI_ACTIVE_HIGH:
		return trigger == ACPI_EDGE_SENSITIVE ?
		       IRQ_TYPE_EDGE_RISING :
		       IRQ_TYPE_LEVEL_HIGH;
	case ACPI_ACTIVE_BOTH:
		if (trigger == ACPI_EDGE_SENSITIVE)
			return IRQ_TYPE_EDGE_BOTH;
	default:
		return IRQ_TYPE_NONE;
	}
}

/**
 * acpi_gsi_to_irq() - Retrieve the linux irq number for a given GSI
 * @gsi: GSI IRQ number to map
 * @irq: pointer where linux IRQ number is stored
 *
 * irq location updated with irq value [>0 on success, 0 on failure]
 *
 * Returns: linux IRQ number on success (>0)
 *          -EINVAL on failure
 */
int acpi_gsi_to_irq(u32 gsi, unsigned int *irq)
{
	*irq = irq_find_mapping(acpi_irq_domain, gsi);
	/*
	 * *irq == 0 means no mapping, that should
	 * be reported as a failure
	 */
	return (*irq > 0) ? *irq : -EINVAL;
}
EXPORT_SYMBOL_GPL(acpi_gsi_to_irq);

/**
 * acpi_register_gsi() - Map a GSI to a linux IRQ number
 * @dev: device for which IRQ has to be mapped
 * @gsi: GSI IRQ number
 * @trigger: trigger type of the GSI number to be mapped
 * @polarity: polarity of the GSI to be mapped
 *
 * Returns: a valid linux IRQ number on success
 *          -EINVAL on failure
 */
int acpi_register_gsi(struct device *dev, u32 gsi, int trigger,
		      int polarity)
{
	int irq;
	unsigned int irq_type = acpi_gsi_get_irq_type(trigger, polarity);

	irq = irq_find_mapping(acpi_irq_domain, gsi);
	if (irq > 0)
		return irq;

	irq = irq_domain_alloc_irqs(acpi_irq_domain, 1, dev_to_node(dev),
				    &gsi);
	if (irq <= 0)
		return -EINVAL;

	/* Set irq type if specified and different than the current one */
	if (irq_type != IRQ_TYPE_NONE &&
		irq_type != irq_get_trigger_type(irq))
		irq_set_irq_type(irq, irq_type);
	return irq;
}
EXPORT_SYMBOL_GPL(acpi_register_gsi);

/**
 * acpi_unregister_gsi() - Free a GSI<->linux IRQ number mapping
 * @gsi: GSI IRQ number
 */
void acpi_unregister_gsi(u32 gsi)
{
	int irq = irq_find_mapping(acpi_irq_domain, gsi);

	irq_dispose_mapping(irq);
}
EXPORT_SYMBOL_GPL(acpi_unregister_gsi);
