#ifndef _VM_H_
#define _VM_H_

#include <machine/vm.h>
#include "opt-dumbvm.h"
#include <pt.h>
/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */


/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/



/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

// pte is use to prevent repeated lock and double calling of access table
int
TLB_Protection_Handler(vaddr_t faultaddress, struct PTE** return_pte, struct PTE* from_write_pte);

int
TLB_Read_Handler(vaddr_t faultaddress, struct PTE** return_pte);

int
TLB_Write_Handler(vaddr_t faultaddress, struct PTE** return_pte);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(int npages);
void free_kpages(vaddr_t addr);

#endif /* _VM_H_ */
