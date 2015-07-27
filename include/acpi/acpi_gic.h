/*
 *  include/acpi/acpi_gic.h
 *
 * Copyright (C) 2015 Advanced Micro Devices, Inc.
 * Authors: Suravee Suthikulpanit <suravee.suthikulpanit@amd.com>
 */

#ifndef __ACPI_GIC_H__
#define __ACPI_GIC_H__

#ifdef CONFIG_ACPI
int acpi_gic_get_num_msi_frame(void);
int acpi_gic_msi_init(struct acpi_table_header *table);
int acpi_gic_get_msi_frame(int index, struct acpi_madt_generic_msi_frame **p);

int acpi_gic_get_num_its(void);
int acpi_gic_its_init(struct acpi_table_header *table);
int acpi_gic_get_its(int index, struct acpi_madt_generic_translator **p);

void *acpi_gic_get_token(struct device *dev);
#endif

#endif /*__ACPI_GIC_H__*/
