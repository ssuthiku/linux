#ifndef __ECAM_H
#define __ECAM_H
#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/acpi.h>

/* "PCI ECAM %04x [bus %02x-%02x]" */
#define PCI_ECAM_RESOURCE_NAME_LEN (22 + 4 + 2 + 2)

struct pci_ecam_region {
	struct list_head list;
	struct resource res;
	u64 address;
	char __iomem *virt;
	u16 segment;
	u8 start_bus;
	u8 end_bus;
	char name[PCI_ECAM_RESOURCE_NAME_LEN];
};

struct pci_ecam_mmio_ops {
	u32 (*read)(int len, void __iomem *addr);
	void (*write)(int len, void __iomem *addr, u32 value);
};

struct pci_ecam_region *pci_ecam_lookup(int segment, int bus);
struct pci_ecam_region *pci_ecam_alloc(int segment, int start,
						   int end, u64 addr);
int pci_ecam_inject(struct pci_ecam_region *cfg);
struct pci_ecam_region *pci_ecam_add(int segment, int start,
						 int end, u64 addr);
void pci_ecam_list_add_sorted(struct pci_ecam_region *new);
void pci_ecam_free_all(void);
int pci_ecam_delete(u16 seg, u8 start, u8 end);

/* Arch specific calls */
int pci_ecam_arch_init(void);
void pci_ecam_arch_free(void);
int pci_ecam_arch_map(struct pci_ecam_region *cfg);
void pci_ecam_arch_unmap(struct pci_ecam_region *cfg);
extern u32 pci_mmio_read(int len, void __iomem *addr);
extern void pci_mmio_write(int len, void __iomem *addr, u32 value);
extern void pci_ecam_register_mmio(struct pci_ecam_mmio_ops *ops);

extern struct list_head pci_ecam_list;

#define PCI_ECAM_BUS_OFFSET(bus)      ((bus) << 20)

int pci_ecam_read(unsigned int seg, unsigned int bus,
		   unsigned int devfn, int reg, int len, u32 *value);
int pci_ecam_write(unsigned int seg, unsigned int bus,
		    unsigned int devfn, int reg, int len, u32 value);

#endif  /* __KERNEL__ */
#endif  /* __ECAM_H */
