#include <pt.h>
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
#include <tlb_map.h>
#include <swap.h>
#include <bitmap.h>

#define PT_DEBUG 0
#define PT_DEBUG2 0

struct PT* 
PT_init(){
    struct PT* pt = kmalloc(sizeof(struct PT));
    pt -> first_level = generate_table();
    return pt;
}

struct PT*
PT_copy(struct PT* old){
    //int spl = splhigh();
    struct PT* pt = kmalloc(sizeof(struct PT));
    if (pt == NULL) return NULL;
    pt -> first_level = copy_table(old->first_level, 1, 0);
    if (pt -> first_level == NULL){
        kfree(pt);
        return NULL;
    }
    //splx(spl);
    //TLB_remove_all();
    return pt;
}

// only used in COW in fork
void** 
copy_table(void** old, int level, vaddr_t vaddr){
    
    void** new = kmalloc(sizeof(void *) * PT_SIZE);
    if (new == NULL) return NULL;

    int i;

    for (i = 0; i < PT_SIZE; i++){
        if (old[i] != NULL){
            if (level < 4){
                new[i] = copy_table(old[i], level + 1, vaddr + (i << (32 - level * 5)));
                if (new[i] == NULL){

                    //can be optimize, for loop backward and free_table(new[i], level++)
                    free_table(new, level);
                    return NULL;
                }
            }else if (level == 4){
                vaddr = vaddr + (i << 12);
                //page table entry level

                //might have synchronization heres
                struct PTE* pte = (struct PTE*) old[i];
                
                // read only
                P(pte -> lock);
                pte -> pte |= PT_PROTECTION_MASK;
                //pte -> pte |= PT_VALID_MASK;
                pte -> count ++;
                //kprintf("passed vaddr is %x\n", vaddr);
                
                
                #if PT_DEBUG2
                    //kprintf("pfn shared are: %x\n", pte -> pte & PAGE_FRAME);
                #endif
                V(pte -> lock);
                TLB_write(vaddr, pte);

                new[i] = pte;
                
            }else{
                panic("copy table\n");
            }
        }else{
            new[i] = NULL;
        }
    }

    return new;
}

void** 
generate_table(){
    
    void** ptr = kmalloc(sizeof(void *) * PT_SIZE);
    if (ptr == NULL) {
        kprintf("Need swapping algo\n");
        return NULL;
    }

    int i;
    for (i = 0; i < PT_SIZE; i ++){
        ptr[i] = NULL;
    }
    
    return ptr;
}

// passin vaddr
// return the exising pte assigned to the corresponding vaddr
// if does not exist, create a new one and return
// To distinguish two events, the valid bit = 0 means the newly created entry
struct PTE* 
access_table(vaddr_t vaddr, struct PT* page_table){
    int first_level_index = (vaddr & PT_FIRST_MASK) >> 27;
    int second_level_index = (vaddr & PT_SEONCD_MASK) >> 22;
    int third_level_index = (vaddr & PT_THIRD_MASK) >> 17;
    int forth_level_index = (vaddr & PT_FOURTH_MASK) >> 12;

    assert(page_table != NULL);
    void** first_level = page_table->first_level;
    assert(first_level != NULL);
    
    void** second_level = first_level[first_level_index];
    if (second_level == NULL){
        first_level[first_level_index] = generate_table();
        second_level = first_level[first_level_index];
        if (second_level == NULL){
            panic("???, access table\n");
        }
    }

    void** third_level = second_level[second_level_index];
    if (third_level == NULL){
        second_level[second_level_index] = generate_table();
        third_level = second_level[second_level_index];
        if (third_level == NULL){
            panic("???, access table\n");
        }
    }

    void** fourth_level = third_level[third_level_index];
    if (fourth_level == NULL){
        third_level[third_level_index] = generate_table();
        fourth_level = third_level[third_level_index];
        if (fourth_level == NULL){
            panic("???, access table\n");
        }
    }

    struct PTE* result = fourth_level[forth_level_index];
    if (result == NULL){
        result = PTE_init();
        
        // set code to read only
        struct addrspace* as = curthread -> t_vmspace;
        if (vaddr >= as -> code && vaddr < as -> data){
            result -> pte |= PT_PROTECTION_MASK;  //read only
        }

        fourth_level[forth_level_index] = result;
    }
    //kprintf("the pfn before finish access_table is 0x%x\n", result->pte & PT_PFN_MASK);
    return result;
}
  
struct PTE* 
PTE_init(){
    struct PTE* result = kmalloc(sizeof(struct PTE));
    int error;
    if (result == NULL){
        panic("we need to decide to do swapping algorithm here, or else where");
    }
    result -> pte = SET_PROTECTION(result -> pte, READ_TEMP);
    result -> count = 1;
    result -> bitmap_index = -1;
    
    result -> lock = sem_create("pte_lock", 1);
    // find an avaliable RAM in the coremap
    paddr_t paddr = get_ppage(result);
    if (paddr == coremap_size + 1){
        if (!swaptest){
            kprintf("coremap is full\n");
            return NULL;
        }else{
            //kprintf("PTE init need swap out\n");
            int index = LRU_Clock();
            //kprintf("LUR success with index: %d\n", index);
            if (index == -1){
                //no space to swap out
                return NULL;
            }
            error = swap_out(index);
            //kprintf("swap out success\n");
            if (error == -1){
                //no space in disk
                return NULL;
            }
            paddr = get_ppage_specific(result, index);
            //kprintf("get_page success with paddr is: %x\n", paddr);
        }
    }

    result -> pte = 0;
    result -> pte |= paddr;
    
    
    return result;
}

// do we really need this function?
struct PTE* PTE_free(vaddr_t vaddr, struct PT* page_table){
    
    assert(page_table != NULL);
    int first_level_index = (vaddr & PT_FIRST_MASK) >> 27;
    int second_level_index = (vaddr & PT_SEONCD_MASK) >> 22;
    int third_level_index = (vaddr & PT_THIRD_MASK) >> 17;
    int forth_level_index = (vaddr & PT_FOURTH_MASK) >> 12;

    void** first_level = page_table->first_level;
    if (first_level == NULL){
        panic("should not be unallocated1");
    }

    void** second_level = first_level[first_level_index];
    if (second_level == NULL){
        panic("should not be unallocated2");
    }

    void** third_level = second_level[second_level_index];
    if (third_level == NULL){
        panic("should not be unallocated3");
    }

    void** fourth_level = third_level[third_level_index];
    if (fourth_level == NULL){
        panic("should not be unallocated4");
    }

    struct PTE* result = fourth_level[forth_level_index];
    if (result == NULL){
        panic("should not be unallocated5");
    }

    //remove the corresponding entries in TLB
    // if ((result->pte & PT_VALID_MASK) != 0){
    //     int tlb_index = result->pte & PT_TLB_INDEX_MASK;
    //     TLB_remove_specific(tlb_index);
    // }

    // remove the corresponding entries in TLB if exist, if not, the func will do nothing and ret
    TLB_remove_entry(vaddr, result);
    
    P(destroy_lock);
    P(result -> lock);
    //synchronization problem???
    if (result -> count == 1){
        u_int32_t bitmap_index = result -> bitmap_index;
        if ((result -> pte & PT_SWAP_MASK) != 0) {
            bitmap_unmark(swap -> bit_map, bitmap_index);
        }else{
            paddr_t paddr = result -> pte & PT_PFN_MASK;
            //inside the memory
            if (paddr != PAGE_FRAME){
                free_ppage(paddr);
            }
        }
        
        V(result -> lock);
        sem_destroy(result -> lock);  
        kfree(result);
        fourth_level[forth_level_index] = NULL;
        V(destroy_lock);
        
    }else{
        result->count--;
        V(result -> lock);
        V(destroy_lock);
    }
    
    

    fourth_level[forth_level_index] = NULL;

    return result;
}


// recursively delete the entire 4-level page table
void free_table(void** pt, int level){
    if (pt == NULL) return;
    int index;
    for (index = 0; index < PT_SIZE; index++){
        
        if (pt[index] != NULL){
            if (level != 4) free_table(pt[index], level + 1);
            else {
                struct PTE* pte_entry = pt[index];

                // I will only call free_table in as_destroy, I will later remove all entry in TLB
                // No need to clear out the TLB now

                // if ((pte_entry->pte & PT_VALID_MASK) != 0){
                //     int tlb_index = pte_entry->pte & PT_TLB_INDEX_MASK;
                //     TLB_remove_specific(tlb_index);
                // }
                //TLB_remove_entry(vaddr, pte_entry);


                P(destroy_lock);
                P(pte_entry -> lock);
                //synchronization problem???
                if (pte_entry -> count == 1){
                    u_int32_t bitmap_index = pte_entry -> bitmap_index;
                    if ((pte_entry -> pte & PT_SWAP_MASK) != 0) {
                        bitmap_remove(bitmap_index);
                    }
                    
                    if ((pte_entry -> pte & PT_VALID_MASK) != 0){
                        paddr_t paddr = pte_entry -> pte & PT_PFN_MASK;
                        // inside the memory
                        if (paddr != PAGE_FRAME){
                            free_ppage(paddr);
                        }
                    }
                    
                    V(pte_entry -> lock);
                    sem_destroy(pte_entry -> lock);  
                    kfree(pte_entry);
                    pt[index] = NULL;
                    V(destroy_lock);
                    
                }else{
                    pte_entry->count--;
                    V(pte_entry -> lock);
                    V(destroy_lock);
                }

            }
        }

    }
    kfree(pt);
    pt = NULL;
    return;
}


/*
// recursively delete the entire 4-level page table
void free_table(void** pt, int level){
    if (pt == NULL) return;
    int index;
    for (index = 0; index < PT_SIZE; index++){
        
        if (pt[index] != NULL){
            if (level != 4) free_table(pt[index], level + 1);
            else {
                struct PTE* pte_entry = pt[index];

                // I will only call free_table in as_destroy, I will later remove all entry in TLB
                // No need to clear out the TLB now

                // if ((pte_entry->pte & PT_VALID_MASK) != 0){
                //     int tlb_index = pte_entry->pte & PT_TLB_INDEX_MASK;
                //     TLB_remove_specific(tlb_index);
                // }
                //TLB_remove_entry(vaddr, pte_entry);

                P(pte_entry -> lock);
                if (pte_entry -> count == 1){
                    paddr_t paddr = pte_entry -> pte & PT_PFN_MASK;
                    free_ppage(paddr);
                    V(pte_entry -> lock);
                    sem_destroy(pte_entry -> lock);
                    kfree(pte_entry);
                    pt[index] = NULL;
                }else{
                    pte_entry -> count--;
                    V(pte_entry -> lock);
                }
            }
        }

    }
    kfree(pt);
    pt = NULL;
    return;
}
*/

void PT_free(struct PT* page_table){
    if (page_table == NULL) return;
    
    free_table(page_table -> first_level, 1);
    kfree(page_table);
    
}

void attach_PTE(vaddr_t vaddr, struct PTE* pte, struct PT* page_table){
    //find the vpn
    //find the corresponding entry
    //attach pte to the correspoding entry

    // this is used in fork - COW
    // the slot in the page_table that vaddr resides in must exist

    assert(page_table != NULL);
    int first_level_index = (vaddr & PT_FIRST_MASK) >> 27;
    int second_level_index = (vaddr & PT_SEONCD_MASK) >> 22;
    int third_level_index = (vaddr & PT_THIRD_MASK) >> 17;
    int forth_level_index = (vaddr & PT_FOURTH_MASK) >> 12;

    void** first_level = page_table->first_level;
    if (first_level == NULL){
        panic("should not be unallocated6");
    }

    void** second_level = first_level[first_level_index];
    if (second_level == NULL){
        panic("should not be unallocated7");
    }

    void** third_level = second_level[second_level_index];
    if (third_level == NULL){
        panic("should not be unallocated8");
    }

    void** fourth_level = third_level[third_level_index];
    if (fourth_level == NULL){
        panic("should not be unallocated9");
    }

    struct PTE* result = fourth_level[forth_level_index];
    //if (result == NULL){
    //    panic("should not be unallocated");
    //}
    result= NULL;

    // original count as been decremented in the TLB miss handler
    fourth_level[forth_level_index] = pte;

    return;
}

