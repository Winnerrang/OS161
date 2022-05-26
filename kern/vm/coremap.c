#include <coremap.h>
#include <types.h>
#include <kern/errno.h>
#include <thread.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/spl.h>
#include <machine/tlb.h>
#include <machine/vm.h>
#include <synch.h>
#include <lib.h>

int offset;
u_int32_t coremap_size;
u_int32_t used_page;
struct Coremap coremap;


#define COREMAP_DEBUG 1

// this function is called in vm_virtualization
void 
coremap_initialization(){
    u_int32_t firstpaddr,lastpaddr;
    ram_getsize(&firstpaddr, &lastpaddr);

    // calculate the avaliable page number
    // -1 is to store the coremap
    coremap_size = (lastpaddr - firstpaddr) / PAGE_SIZE - 1;
    offset = (firstpaddr + PAGE_SIZE) / PAGE_SIZE; // offset is used to access the coremap

#if COREMAP_DEBUG
    kprintf("coremap size is %d\n", coremap_size);
    kprintf("offset of the coremap is %d\n", offset);
#endif
    coremap.coremap_array = (struct Coremap_Entry*) PADDR_TO_KVADDR(firstpaddr);
    
    // now we have 1 page memory avaliable for coremap
    // we can directly access it by index
    u_int32_t i;
    for (i = 0; i < coremap_size; i ++){
        coremap.coremap_array[i].pte = NULL;
        coremap.coremap_array[i].auxilary_bits = 0;
    }

    used_page = 0;
    coremap.coremap_lock = NULL;

}

void coremap_lock_bootstrap(){
    coremap.coremap_lock = sem_create("coremap_lock", 1);
    assert(coremap.coremap_lock != NULL);
}

paddr_t 
get_ppage_specific(struct PTE* pte, u_int32_t index){
    
    P(coremap.coremap_lock);

    coremap.coremap_array[index].auxilary_bits |= VALID_BIT_MASK;

    // this is the researved page, now, it has dine in
    //coremap.coremap_array[index].auxilary_bits &= (~RESERVATION_BIT_MASK);

    // map the coremap entry to pte
    if (pte == NULL){
        coremap.coremap_array[index].auxilary_bits |= KERNEL_BIT_MASK;
        coremap.coremap_array[index].pte = NULL;
    }else{
        coremap.coremap_array[index].pte = pte;
    }
    
    V(coremap.coremap_lock);
    
    return (index + offset) << 12;
}

paddr_t 
get_ppage(struct PTE* pte){
    
    // find an empty page at coremap
    if (coremap.coremap_lock != NULL){
        P(coremap.coremap_lock);
    }
    //int spl = splhigh();
    //kprintf("coremap reamain: %d, 0x%x\n",coremap_avaliable_space(), (int)pte);
    //splx(spl);
    
    u_int32_t i;
    for (i = 0; i < coremap_size; i ++){
        if ((coremap.coremap_array[i].auxilary_bits & VALID_BIT_MASK) == 0 &&
            (coremap.coremap_array[i].auxilary_bits & RESERVATION_BIT_MASK) == 0){
            
            
            coremap.coremap_array[i].auxilary_bits |= VALID_BIT_MASK;

            //kernal address does not need pte
            if (pte == NULL){
                coremap.coremap_array[i].auxilary_bits |= KERNEL_BIT_MASK;
                coremap.coremap_array[i].pte = NULL;
            }else{
                coremap.coremap_array[i].pte = pte;
            }
            
            if (coremap.coremap_lock != NULL){
                V(coremap.coremap_lock);
            }

            // reserve this index so that other will not occupy it before we fill it in
            // we fill it in when demand paging or swap in or copy on write 
            reserve(i, 1);

            vaddr_t vaddr = (((int) i + offset) << 12) + MIPS_KSEG0;
            bzero((void *) vaddr, PAGE_SIZE);
            used_page++;
            return (i + offset) << 12;
        } 
    }

    //coremap is full, handled by upper level functions
   
    if (coremap.coremap_lock != NULL){
        V(coremap.coremap_lock);
    }
    
    return coremap_size + 1;
    
}

// I only return the pte associated with the pfn
// let upper function determine what to do
struct PTE*
free_ppage(paddr_t paddr){
    
    if (coremap.coremap_lock != NULL){
        P(coremap.coremap_lock);
    }
    
    u_int32_t index = (paddr >> 12) - offset;

    //assert(index >= 0 && index < coremap_size);
    coremap.coremap_array[index].auxilary_bits = 0;
    struct PTE* temp = coremap.coremap_array[index].pte;
    coremap.coremap_array[index].pte = NULL;
    
    used_page--;
    if (coremap.coremap_lock != NULL){
        V(coremap.coremap_lock);
    }
    return temp;

}

int coremap_avaliable_space(){
    int i, count = 0;
    for (i = 0; i < (int)coremap_size; i ++){
        if ((coremap.coremap_array[i].auxilary_bits & VALID_BIT_MASK) == 0) {
            count++;
        }
    }
    return count;
}

void unreservated(u_int32_t index){
    // we only lower the reservation bit if we completely load in our page
    // namely after load from disk or swap in
    
    P(coremap.coremap_lock);
    if ((coremap.coremap_array[index].auxilary_bits & RESERVATION_BIT_MASK) != 0){
        coremap.coremap_array[index].auxilary_bits &= ~RESERVATION_BIT_MASK;
        //kprintf("SUCCESS: finished lower reservation bit at index %d\n",index);
    }else{
        kprintf("WARNING: the reservation bit at index %d is already 0\n", index);
    }
    V(coremap.coremap_lock);
    
}

void reserve(u_int32_t index, int from_get_ppage){
    // we only lower the reservation bit if we completely load in our page
    // namely after load from disk or swap in
    
    if (!from_get_ppage){
        P(coremap.coremap_lock);
    }

    if ((coremap.coremap_array[index].auxilary_bits & RESERVATION_BIT_MASK) != 0){
        kprintf("WARNING: you are researving a researved index at index %d, FAIL !!!\n",index);
    }else{
        //kprintf("WARNING: the reservation bit at index %d is already 0\n", index);
        coremap.coremap_array[index].auxilary_bits |= RESERVATION_BIT_MASK;
    }

    if (!from_get_ppage){
        V(coremap.coremap_lock);
    }
    

    
}

