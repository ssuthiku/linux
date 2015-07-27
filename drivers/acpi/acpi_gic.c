/*
 * File: acpi_gic.c
 *
 * ACPI helper functions for ARM GIC
 *
 * Copyright (C) 2015 Advanced Micro Devices, Inc.
 * Authors: Suravee Suthikulpanit <suravee.suthikulpanit@amd.com>
 */

#include <linux/acpi.h>
#include <linux/init.h>

/*
 * GIC MSI Frame data structures
 */
struct gic_msi_frame_handle {
	struct list_head list;
	struct acpi_madt_generic_msi_frame frame;
};

static LIST_HEAD(msi_frame_list);

static int acpi_num_msi;

/*
 * GIC ITS data structures
 */
struct gic_its_handle {
	struct list_head list;
	struct acpi_madt_generic_translator trans;
};

static LIST_HEAD(its_list);

static int acpi_num_its;

/*
 * GIC MSI Frame parsing stuff
 */
inline int acpi_gic_get_num_msi_frame(void)
{
	return acpi_num_msi;
}

static int __init
acpi_parse_madt_msi(struct acpi_subtable_header *header,
		    const unsigned long end)
{
	struct gic_msi_frame_handle *h;
	struct acpi_madt_generic_msi_frame *frame;

	frame = (struct acpi_madt_generic_msi_frame *)header;
	if (BAD_MADT_ENTRY(frame, end))
		return -EINVAL;

	h = kzalloc(sizeof(struct gic_msi_frame_handle *), GFP_KERNEL);
	if (!h)
		return -ENOMEM;

	/** Note:
	 * We make a copy of this structure since this code is called
	 * prior to acpi_early_init(), which sets the acpi_gbl_permanent_mmap.
	 * Therefore, we could not keep just the pointer sincce the memory
	 * could be unmapped.
	 */
	memcpy(&h->frame, frame, sizeof(struct acpi_madt_generic_msi_frame));

	list_add(&h->list, &msi_frame_list);

	return 0;
}

int __init acpi_gic_msi_init(struct acpi_table_header *table)
{
	int ret = 0;

	if (acpi_num_msi > 0)
		return ret;

	ret = acpi_parse_entries(ACPI_SIG_MADT,
				 sizeof(struct acpi_table_madt),
				 acpi_parse_madt_msi, table,
				 ACPI_MADT_TYPE_GENERIC_MSI_FRAME, 0);
	if (ret == 0) {
		pr_debug("No valid ACPI GIC MSI FRAME exist\n");
		return ret;
	}

	acpi_num_msi = ret;
	return 0;
}

int acpi_gic_get_msi_frame(int index, struct acpi_madt_generic_msi_frame **p)
{
	int i = 0;
	struct gic_msi_frame_handle *m;

	if (index >= acpi_num_msi)
		return -EINVAL;

	list_for_each_entry(m, &msi_frame_list, list) {
		if (i == index)
			break;
		i++;
	}

	if (i == acpi_num_msi)
		return -EINVAL;

	*p = &(m->frame);
	return  0;
}

/*
 * GIC ITS parsing stuff
 */
inline int acpi_gic_get_num_its(void)
{
	return acpi_num_its;
}

static int __init
acpi_parse_madt_its(struct acpi_subtable_header *header,
		    const unsigned long end)
{
	struct gic_its_handle *h;
	struct acpi_madt_generic_translator *trans;

	trans = (struct acpi_madt_generic_translator *)header;
	if (BAD_MADT_ENTRY(trans, end))
		return -EINVAL;

	h = kzalloc(sizeof(struct gic_its_handle *), GFP_KERNEL);
	if (!h)
		return -ENOMEM;

	memcpy(&h->trans, trans, sizeof(struct acpi_madt_generic_translator));

	list_add(&h->list, &its_list);

	return 0;
}

int __init acpi_gic_madt_gic_its_init(struct acpi_table_header *table)
{
	int ret = 0;

	if (acpi_num_its > 0)
		return ret;

	ret = acpi_parse_entries(ACPI_SIG_MADT,
				 sizeof(struct acpi_table_madt),
				 acpi_parse_madt_its, table,
				 ACPI_MADT_TYPE_GENERIC_TRANSLATOR, 0);
	if (ret == 0) {
		pr_debug("No valid ACPI GIC ITS exist\n");
		return ret;
	}

	acpi_num_its = ret;
	return 0;
}

int acpi_gic_get_its(int index, struct acpi_madt_generic_translator **p)
{
	int i = 0;
	struct gic_its_handle *m;

	if (index >= acpi_num_its)
		return -EINVAL;

	list_for_each_entry(m, &its_list, list) {
		if (i == index)
			break;
		i++;
	}

	if (i == acpi_num_its)
		return -EINVAL;

	*p = &(m->trans);
	return  0;
}

static void *acpi_gic_msi_token(struct device *dev)
{
	int err;
	struct acpi_madt_generic_msi_frame *msi;

	/**
	* Since ACPI 5.1 currently does not define
	* a way to associate MSI frame ID to a device,
	* we can only support single MSI frame (index 0)
	* at the moment.
	*/
	err = acpi_gic_get_msi_frame(0, &msi);
	if (err)
		return NULL;

	return (void *) msi->base_address;
}

static void *acpi_gic_its_token(struct device *dev)
{
	int err;
	struct acpi_madt_generic_translator *trans;
	int its_id = 0;

	/**
	 * TODO: We need a way to retrieve GIC ITS ID from
	 * struct device pointer (in this case, the device
	 * would be the PCI host controller.
	 *
	 * This would be done by the IORT-related code.
	 *
	 * its_id = get_its_id(dev);
	 */

	err = acpi_gic_get_its(its_id, &trans);
	if (err)
		return NULL;

	return (void *) trans->base_address;
}

void *acpi_gic_get_msi_token(struct device *dev)
{
	void *token = acpi_gic_msi_token(dev);

	if (!token)
		token = acpi_gic_its_token(dev);

	return token;
}
