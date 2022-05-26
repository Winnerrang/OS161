#ifndef _TLB_UTILITY_
#define _TLB_UTILITY_

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/spl.h>
#include <machine/tlb.h>
#include <pt.h>

/*
extern u_int32_t TLB_ptr;

extern struct PTE* tlb_map[NUM_TLB];

void tlb_map_bootstrap();

// equal to tlb write random
// hi VPN, lo PFN

void TLB_write_replace(u_int32_t entryhi, u_int32_t entrylo, struct PTE* pte);

// equal to tlb write specific slot
void TLB_update_specifc(u_int32_t entryhi, u_int32_t entrylo, int index, struct PTE* pte);


void TLB_remove_specific(int index);

void TLB_remove_all();
*/


// TLB hi: VPN, TLB lo: PFN + aux bits


extern u_int32_t TLB_ptr;

void TLB_bootstrap();

void TLB_write(vaddr_t vaddr, struct PTE* pte);
void TLB_remove_entry(vaddr_t vpn, struct PTE* pte);
void TLB_remove_all();

void TLB_remove_paddr(paddr_t pfn);

#endif
