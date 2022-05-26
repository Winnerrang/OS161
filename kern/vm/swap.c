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
#include <uio.h>
#include <tlb_map.h>
#include <vfs.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <vnode.h>
#include <swap.h>
#include <kern/stat.h>
#include <bitmap.h>
#include <pt.h>




#define SWAP_INIT 0

struct Swap* swap;
struct semaphore* destroy_lock;
struct semaphore* disk_lock;
struct semaphore* file_lock;
int swaptest = 1;


void swap_bootstrap(){
    
    swap = kmalloc(sizeof(struct Swap));
    if(swap == NULL){
        panic("can not initialized swap structure\n");
    }
    
    // extract vnode for write and read
    struct vnode* v;
    char* name = kstrdup("lhd0raw:");
    int result = vfs_open(name, O_RDWR, &v);
    if (result){
        panic("open swap disk fails");
    }
    swap -> v = v;

    // determine the size of bitmap
    struct stat status;
    result = VOP_STAT(v, &status);
    if (result){
        panic("stat fails\n");
    }
    u_int32_t bitmap_size = status.st_size / PAGE_SIZE;
    swap -> bitmap_size = bitmap_size;
    swap -> bit_map = bitmap_create(bitmap_size);

    swap -> LRU_arm = 0;
    swap -> swap_disk_index = 0;
    swap -> bitmap_lock = sem_create("bitmap_lock", 1);
    assert(swap -> bitmap_lock != NULL);
    destroy_lock = sem_create("destroy_lock", 1);
    disk_lock = sem_create("disk_lock", 1);
    file_lock = sem_create("file_lock", 1);
    assert(disk_lock != NULL);

    //struct PTE* test1_pte = kmalloc(sizeof(struct PTE));
    //paddr_t test1 = get_ppage

    // char* test = (char*)get_ppage(NULL) + 0x80000000;
    // struct PTE* pte = kmalloc(sizeof(struct PTE));
    // pte -> lock = sem_create("pte_lock", 1);
    // pte -> pte = 0;
    // pte -> pte += (paddr_t)test - 0x80000000;
    // pte -> pte |= PT_VALID_MASK;
    // pte -> pte = SET_REGION(pte -> pte, STACK_REGION);
    // test[0] = 'a';
    // test[1] = 'b';
    // test[2] = 'c';
    // int coremap_index = ((int)(test - 0x80000000) >> 12) - offset;
    // coremap.coremap_array[6].pte = pte;
    // kprintf("the coremap index is: %d\n", coremap_index);
    // kprintf("the swap out pte is %x\n", pte -> pte);
    // swap_out(6);
    // kprintf("the swap out pte change to %x\n", pte -> pte);
    // kprintf("the swap out is stored on index %d\n", pte -> bitmap_index);

    // kprintf("test for swap in\n");
    // swap_in(pte, 15);
    // char* swap_in_test = (char*)((15 + offset) << 12) + 0x80000000;
    // kprintf("the mem result of swap in is: %c\n", swap_in_test[0]);

}

/*
// LUR return a coremap index that is free to use after swap out, and mark it as reserved for get_page_specific and swap in
int LRU_Clock(){
    // based on the current arm, increment it until it finds an unreferenced, non-kernel page
    P(coremap.coremap_lock);
    while (1){
        if (swap -> LRU_arm == coremap_size) swap -> LRU_arm = 0;
        u_int32_t axu_bits = coremap.coremap_array[swap -> LRU_arm].auxilary_bits;
        // if its kernel page, then skip
        if ((axu_bits & KERNEL_BIT_MASK) != 0){
            swap -> LRU_arm ++;
            continue;
        }

        // if its already reserved, then skip
        if ((axu_bits & RESERVATION_BIT_MASK) != 0){
            swap -> LRU_arm ++;
            continue;
        }

        // if its already referenced, then skip
        if ((coremap.coremap_array[swap -> LRU_arm].pte -> pte & PT_REFERENCE_MASK) != 0){
            coremap.coremap_array[swap -> LRU_arm].pte -> pte &= ~(PT_REFERENCE_MASK);
            swap -> LRU_arm ++;
            continue;
        }

        // if we reach here, we must have an unreferenced, non-kernel, non-researved page
        // it is possible that LRU clock has circulated one round
        coremap.coremap_array[swap -> LRU_arm].auxilary_bits |= RESERVATION_BIT_MASK;
        V(coremap.coremap_lock);
        return swap -> LRU_arm;
    }
    
    panic("The LUR clock should not gets here");
    return 0;
}


// swap out takes an index that LRU selects to be free. At this step, we evict it out to make it free
// swap out returns error checking
// swap out only manipulates on the evicting page, has nothing to do with the newly come page
int swap_out(u_int32_t index){
    int error;

    // avoid cs before calling lock of evict_pte
    P(destroy_lock);
    struct PTE* evict_pte = coremap.coremap_array[index].pte;
    if (evict_pte == NULL){
        // a seldom happened corner case
        kprintf("Before I come into the swap out, the page has been freed\n");
        return 0;
    }
    kprintf("swap out check point 0, lock count is: %d\n", evict_pte -> lock -> count);
    P(evict_pte -> lock);
    kprintf("swap out check point 0.5\n");
    V(destroy_lock);

    // swap out the content of this page into the swap disk
    vaddr_t k_vaddr = (evict_pte -> pte & PAGE_FRAME) + MIPS_KSEG0;
    struct uio ku;
    if ((evict_pte -> pte & PT_SWAP_MASK) != 0){
        // if I already have a copy in disk, then replace the copy to save space
        // log-structured style cost too much persisten storage
        mk_kuio(&ku, (void *)(k_vaddr), PAGE_SIZE, (evict_pte -> bitmap_index) * PAGE_SIZE, UIO_WRITE);
    }else{
        // new page that does not have copy in disk
        mk_kuio(&ku, (void *)(k_vaddr), PAGE_SIZE, (swap -> swap_disk_index) * PAGE_SIZE, UIO_WRITE);
        evict_pte -> bitmap_index = swap -> swap_disk_index;
        swap -> swap_disk_index ++;
        if (swap -> swap_disk_index == swap -> bitmap_size) swap -> swap_disk_index = 0;
    }

    P(disk_lock);
    kprintf("swap out check point 1\n");
    error = VOP_WRITE(swap -> v, &ku);
    kprintf("swap out check point 1.5\n");
    if (error) return error;
    V(disk_lock);


    // before reset the pfn, we need to clear the TLB entry of this page
    TLB_remove_paddr(evict_pte -> pte & PAGE_FRAME);

    // set up the evicted page entry bit
    // ref bit is 0 since that's why we select it, modify bit is useless now
    // region bit remains intact
    evict_pte -> pte |= PAGE_FRAME;
    evict_pte -> pte &= ~(PT_VALID_MASK);
    evict_pte -> pte |= PT_SWAP_MASK;

    // if we used swap out, we must append the new page to coremap by get_ppage_specific or swap in
    // normal get_pappage will not work. Therefore, we don't need to empty out the coremap as
    // we always pass in index in get_ppage_specific or swap in to append the new page

    
    V(evict_pte -> lock);
    return 0;
}

// given the new pte that is retrieved by access table, and index which is reserved for me
// swap in the page in the swap disk
// return back error code
int swap_in(struct PTE* new_pte, u_int32_t index){
    // no need to lock as we have already locked in vm fault
    vaddr_t k_vaddr = ((index + offset) << 12) + MIPS_KSEG0;

    struct uio ku;
    mk_kuio(&ku, (void *)(k_vaddr), PAGE_SIZE, (new_pte -> bitmap_index) * PAGE_SIZE, UIO_READ);
    
    P(disk_lock);
    kprintf("swap in check point 1\n");
    int error = VOP_READ(swap -> v, &ku);
    kprintf("swap in check point 1.5\n");
    if (error) return error;
    V(disk_lock);

    // link the coremap to the swaped in place
    kprintf("swap in check point 2\n");
    P(coremap.coremap_lock);
    coremap.coremap_array[index].pte = new_pte;
    coremap.coremap_array[index].auxilary_bits = 0;
    coremap.coremap_array[index].auxilary_bits |= VALID_BIT_MASK;
    // kernel bit and researvation bit is 0
    V(coremap.coremap_lock);

    // in swap out, I have invalidate the pfn, make the valid = 0, swap = 1, reverse such change
    new_pte -> pte &= ~(PAGE_FRAME);
    new_pte -> pte |= (index + offset) << 12;
    new_pte -> pte |= PT_VALID_MASK;
    new_pte -> pte &= ~(PT_SWAP_MASK);

    return 0;
}
*/

int LRU_Clock(){
    
    P(coremap.coremap_lock);
    int kernel = 0;
    if (used_page < coremap_size){
        kprintf("Warning: Calling LRU lock when there are free page\n");
        
        //find the avaliable page
        int i;
        for (i = 0; i < (int)coremap_size; i++){
            if ((coremap.coremap_array[i].auxilary_bits & VALID_BIT_MASK) == 0){
                coremap.coremap_array[i].auxilary_bits |= RESERVATION_BIT_MASK;
                V(coremap.coremap_lock);
                return i;
            }
        }
        panic("LRU_Clock: that does not make sense\n");
    }   
    
    
    while (1){
        
        struct PTE* pte = coremap.coremap_array[swap -> LRU_arm].pte;
        if (pte == NULL){
            // will not kick out kernel page
            swap -> LRU_arm ++;
            if (swap->LRU_arm >= coremap_size){
                    swap->LRU_arm = 0;
            }
            kernel++;
            continue;
        }
        
        if((coremap.coremap_array[swap -> LRU_arm].pte -> pte & PT_REFERENCE_MASK) == 0){
            
            if ((coremap.coremap_array[swap -> LRU_arm].auxilary_bits & RESERVATION_BIT_MASK) == 0){
                // has not recently accessed and the slot is not reserved
                reserve(swap -> LRU_arm, 1);
                int return_value = swap -> LRU_arm;
                
                swap -> LRU_arm++;
                if (swap->LRU_arm >= coremap_size){
                    swap->LRU_arm = 0;
                }

                V(coremap.coremap_lock);
                //kprintf("LRU clock select region with coremap index %d\n", return_value);
                return return_value;
            }

        }
        
        pte -> pte &= (~PT_REFERENCE_MASK);
        
        swap -> LRU_arm ++;
        if (swap->LRU_arm >= coremap_size){
            swap->LRU_arm = 0;
        }

        
        if (kernel >= (int)coremap_size){
            kprintf("warning: everything is kernel pages\n");
            V(coremap.coremap_lock);
            return coremap_size + 1;
        }
    
    }

}

// such implementation is for efficiency

u_int32_t
bitmap_write(){
    P(swap -> bitmap_lock);
    u_int32_t start = swap -> swap_disk_index;
    while(bitmap_isset(swap -> bit_map, swap -> swap_disk_index)){
        swap -> swap_disk_index++;
        if (swap -> swap_disk_index == swap -> bitmap_size){
            swap -> swap_disk_index = 0;
        }
        if (swap -> swap_disk_index == start){
            return -1;
        }
    }
    bitmap_mark(swap -> bit_map, swap -> swap_disk_index);
    V(swap -> bitmap_lock);
    return swap -> swap_disk_index;
}


void
bitmap_remove(u_int32_t index){
    P(swap -> bitmap_lock);
    bitmap_unmark(swap -> bit_map, index);
    V(swap -> bitmap_lock);
}

int swap_out(u_int32_t index){
    paddr_t paddr;
    // check the swap page is in swap disk
    int error;


    P(destroy_lock);
    //kprintf("swap out starts\n");
    struct PTE* old = coremap.coremap_array[index].pte;
    if (old == NULL){
        kprintf("Warning: page already got removed just return\n");
        V(destroy_lock);
        return 0;
    }
    
    P(old -> lock);
    //kprintf("check point 2\n");
    V(destroy_lock);

    
    if ((old -> pte & PT_SWAP_MASK) != 0){
        // if the swap out page is in swap disk
        if ((old -> pte & PT_MODIFY_MASK) != 0){
            // if the swap out page has been modified: swapout the page into its allocated slot
            if (old -> bitmap_index == -1) panic("Error: Swap on, but no bitmap index\n");
            
            paddr = (offset + index) << 12;
            struct uio ku;
            mk_kuio(&ku, (void *)(paddr + MIPS_KSEG0), PAGE_SIZE, (old -> bitmap_index) * PAGE_SIZE, UIO_WRITE);
            
            if (paddr < (paddr_t)(offset << 12) || paddr > (paddr_t)((offset + coremap_size) << 12)){
                panic("what are you doing you are swap out a illegal page\n");
            }
            if (coremap.coremap_array[index].auxilary_bits & KERNEL_BIT_MASK) panic("??? swap out kernel page");
            
            P(disk_lock); 
            error = VOP_WRITE(swap -> v, &ku);
            V(disk_lock);
            if (error){
                V(old->lock);
                panic("VOP write at swap out failed\n");
            }

        }else{
            kprintf("Swap disk has a copy and no modify, so no need to store it on the swap disk\n");
        }
    }else{
        // its in file disk or no mapping to disk
        if ((old -> pte & PT_MODIFY_MASK) != 0 ||
            GET_REGION(old -> pte) == STACK_REGION ||
            GET_REGION(old -> pte) == HEAP_REGION){
            // we need to store it in swap disk
            old->bitmap_index = bitmap_write();
            //old -> bitmap_index = swap -> swap_disk_index ++;
            kprintf("the swap disk index is up to: %d\n", old -> bitmap_index);
            if (old->bitmap_index == -1){
                kprintf("warning: disk has no space\n");
                V(old -> lock);
                return -1;
            }

            paddr = (offset + index) << 12;
            struct uio ku;
            
            if (paddr < (paddr_t)(offset << 12) || paddr > (paddr_t)((offset + coremap_size) << 12)){
                panic("what are you doing you are swap out a illegal page\n");
            }
            if (coremap.coremap_array[index].auxilary_bits & KERNEL_BIT_MASK) panic("??? swap out kernel page");
           
            mk_kuio(&ku, (void *)(paddr + MIPS_KSEG0), PAGE_SIZE, (old -> bitmap_index) * PAGE_SIZE, UIO_WRITE);
            
            if (paddr < (paddr_t)(offset << 12) || paddr > (paddr_t)((offset + coremap_size) << 12)){
                panic("what are you doing you are swap out a illegal page\n");
            }
            if (coremap.coremap_array[index].auxilary_bits & KERNEL_BIT_MASK) panic("??? swap out kernel page");
            
            P(disk_lock); 
            error = VOP_WRITE(swap -> v, &ku);
            V(disk_lock);
            if (error){
                V(old->lock);
                panic("VOP write at swap out failed\n");
            }
            old -> pte |= PT_SWAP_MASK;

        }else{
            kprintf("This page is a code or data region that has not been modified, do nothing\n");
        }
        
    }
    TLB_remove_paddr(old -> pte & PAGE_FRAME);

    old -> pte &= (~PT_REFERENCE_MASK);
    old -> pte &= (~PT_MODIFY_MASK);
    // set valid to 0
    old -> pte &= ~(PT_VALID_MASK);
    
    //set pfn to invalid meaning it is not in the memory
    old-> pte |= PAGE_FRAME;


    P(coremap.coremap_lock);
    coremap.coremap_array[index].auxilary_bits &= ~(VALID_BIT_MASK);
    coremap.coremap_array[index].pte = NULL;
    V(coremap.coremap_lock);

    V(old -> lock);

    // the upper layer knows which index is empty, no need to explicitly allocate coremap
    
    return 0;
    
}

int swap_in(struct PTE* pte, u_int32_t index){
    //(void) pte;
    //(void) index;
    // write the page from swap disk to mem
    
    u_int32_t bitmap_index = pte -> bitmap_index;
    paddr_t pfn = (offset + index) << 12;
    struct uio ku;
    mk_kuio(&ku, (void *)(pfn + MIPS_KSEG0), PAGE_SIZE, bitmap_index * PAGE_SIZE, UIO_READ);
    if (pfn < (paddr_t)(offset << 12) || pfn > (paddr_t)((offset + coremap_size) << 12)){
        panic("what are you doing you are reaplacing a illegal page, pfn: 0x%x, index: %d\n", pfn, index);
    }
    if (coremap.coremap_array[index].auxilary_bits & KERNEL_BIT_MASK) panic("??? the region was a kernel page\n");
    P(disk_lock);
    int error = VOP_READ(swap -> v, &ku);
    V(disk_lock);
    //if (error) return error;
    if (error) panic("Swap in: VOP_READ failed\n");
    // update 
    // we set swap mask to indicate we have a copy in disk
    // this facilitates swap out. since we have set valid to 1, access it is simple
    pte -> pte |= PT_SWAP_MASK;
    // zero the pfn
    pte -> pte &= (~PT_PFN_MASK);
    pte -> pte |= pfn;
    pte -> pte &= (~PT_MODIFY_MASK); // clear the modify bit as this is the new page
    // if its code region or COW, we should stick to it. 
    if (GET_PROTECTION(pte -> pte) != READ_ONLY){
        pte -> pte = SET_PROTECTION(pte -> pte, READ_TEMP);
    }
    
    P(coremap.coremap_lock);
    coremap.coremap_array[index].pte = pte;
    coremap.coremap_array[index].auxilary_bits |= VALID_BIT_MASK;
    //coremap.coremap_array[index].auxilary_bits &= (~RESERVATION_BIT_MASK);
    V(coremap.coremap_lock);

    unreservated(index);
    
    return 0;
}

