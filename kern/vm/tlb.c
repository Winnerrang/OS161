#include <tlb_map.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/spl.h>
#include <machine/tlb.h>
#include <coremap.h>

/*
u_int32_t TLB_ptr;
struct PTE* tlb_map[NUM_TLB];

void 
tlb_map_bootstrap(){
    int i;
    TLB_ptr = 0;
    for (i = 0; i < NUM_TLB; i++){
        tlb_map[i] = NULL;
    }
}

void TLB_write_replace(u_int32_t entryhi, u_int32_t entrylo, struct PTE* pte){
    struct PTE* old = tlb_map[TLB_ptr];
    assert(pte != NULL);
    
    // we don't need to inform the old pte as it will automatically be TLB fault
    // if (old != NULL){
    //     old -> pte &= (~ PT_VALID_MASK);
    // }

    tlb_map[TLB_ptr] = pte;
    //pte -> pte |= PT_VALID_MASK;
    pte -> pte |= TLB_ptr;      // set the corresponding index in the TLB
    
    
    TLB_Write(entryhi, entrylo, TLB_ptr);
    TLB_ptr++;
    if (TLB_ptr == NUM_TLB) TLB_ptr = 0;
    
}

void TLB_update_specifc(u_int32_t entryhi, u_int32_t entrylo, int index, struct PTE* pte){
    int spl = splhigh();
    struct PTE* old = tlb_map[index];
    assert(pte != NULL);

    // invalidate the valid bit: now valid bit refers to its not in the TLB
    // since we use link to the PTE to express the valid context talked in lec
    if (old != NULL){
        old -> pte &= (~ PT_VALID_MASK);
    }

    // set the pte's new valid bit to 1
    tlb_map[index] = pte;
    pte -> pte |= PT_VALID_MASK;
    pte -> pte |= TLB_ptr;      // set the corresponding index in the TLB
    
    
    TLB_Write(entryhi, entrylo, index);
    splx(spl);
}

void TLB_remove_specific(int index){
    int spl = splhigh();
    struct PTE* old = tlb_map[index];
    tlb_map[index] = NULL;
    if (old == NULL) return;

    old -> pte &= (~ PT_VALID_MASK);
    TLB_Write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
    splx(spl);
}

void TLB_remove_all(){
    int spl = splhigh();
    int i;
	for (i=0; i<NUM_TLB; i++) {
        struct PTE* old = tlb_map[i];
        if (old != NULL){
            old -> pte &= (~ PT_VALID_MASK);
            tlb_map[i] = NULL;
        }
        
        
		TLB_Write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
    TLB_ptr = 0;
	splx(spl);
}
*/

#define TLB_DEBUG 0

u_int32_t TLB_ptr;

// a FIFO replacement policy
void 
TLB_bootstrap(){
    TLB_ptr = 0;
}

// according to the TLB_ptr's value, write to the corrsponding entry
void
TLB_write(vaddr_t vaddr, struct PTE* pte){
    
    int spl = splhigh();
    paddr_t paddr;
    
    if (GET_PROTECTION(pte -> pte) == READ_WRITE)
        paddr = (pte -> pte & PAGE_FRAME) | TLBLO_VALID | TLBLO_DIRTY; // read and write
    else 
        paddr = (pte -> pte & PAGE_FRAME) | TLBLO_VALID; // read only
    
    if ((pte -> pte & PT_VALID_MASK) == 0){
        kprintf("WARNING: Process %d writing an invalid pte into tlb\n", curthread->self_pid);
        //return;
    }

    if ((int)(pte -> pte & PAGE_FRAME) < (int)offset * (int)PAGE_SIZE || (int)(pte -> pte & PAGE_FRAME) > ((int)offset + (int)coremap_size) * (int)PAGE_SIZE){
        kprintf("WARNING: Process %d trying to write to TLB but its pfn is invalid: 0x%x\n", curthread->self_pid, pte -> pte & PAGE_FRAME);
    }

    struct addrspace* as = curthread->t_vmspace;
    // if (vaddr >= as -> stack_bot_limit){}
	// else if (as -> heap_bot <= vaddr && (vaddr & PAGE_FRAME) < as -> heap_top) {}
	// else if (as -> data <= vaddr && vaddr < as -> heap_bot) {}
	// else if (as -> code + as -> code_npages * PAGE_SIZE > vaddr && vaddr >= as -> code){}
    // else{

    //     kprintf("WARNING: Process %d trying to write to TLB but its vaddr is invalid, 0x%x\n", curthread->self_pid, vaddr);
    //     return;
    // }
    if (vaddr > as -> heap_top && vaddr < as -> stack_bot_limit){
        kprintf("WARNING: Process %d trying to write to TLB but its vaddr is invalid, 0x%x\n", curthread->self_pid, vaddr);
        //return;
    }
#if TLB_DEBUG
    
    //kprintf("TLB has written vpn: %x\n", vaddr & PAGE_FRAME);
    //kprintf("TLB has written pfn: %x\n", paddr);
    //kprintf("Process %d\n", curthread->self_pid);
    kprintf("TLB has written: 0x%x -> 0x%x\n", vaddr &PAGE_FRAME, pte->pte & PAGE_FRAME);
    //kprintf("TLB has written on index: %d\n", TLB_ptr);
#endif


    // replace TLB entry for COW
    int result = TLB_Probe(vaddr & PAGE_FRAME, paddr);
    if (result >= 0){
        // matching pair for COW
        TLB_Write(vaddr & PAGE_FRAME, paddr, result);
        splx(spl);
        return;
    }
    
    TLB_Write(vaddr & PAGE_FRAME, paddr, TLB_ptr);

    TLB_ptr ++;

    if (TLB_ptr == NUM_PROCESS) TLB_ptr = 0;

    splx(spl);
    
}

void TLB_remove_entry(vaddr_t vpn, struct PTE* pte){
    int spl = splhigh();
    
    paddr_t paddr = (pte -> pte & PAGE_FRAME) | TLBLO_VALID;
    
    int result = TLB_Probe(vpn, paddr);
    if (result < 0){
        // no matching vaddr
        splx(spl);
        return;
    }

    // matching vaddr -> invalidate the entry
    TLB_Write(TLBHI_INVALID(result), TLBLO_INVALID(), result);
    
    splx(spl);
    return;
}

void TLB_remove_all(){
    int spl = splhigh();

    
    int i;
	for (i=0; i<NUM_TLB; i++) {
		TLB_Write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
    TLB_ptr = 0;

	splx(spl);
}

void TLB_remove_paddr(paddr_t pfn){
    int spl = splhigh();
    int i;
    for (i = 0; i < NUM_TLB; i ++){
        u_int32_t entryhi, entrylo; 
        TLB_Read(&entryhi, &entrylo, i);
        if ((entrylo & TLBLO_PPAGE) == pfn){
            TLB_Write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
            splx(spl);
            return;
        }
    }

    splx(spl);
    return;
}


