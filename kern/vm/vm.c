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

#define VM_DEBUG 0
#define PROTECTION_DEBUG 0
#define HARDCODING_DEBUG 0

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

/*
 * alloc_kpages() and free_kpages() are called by kmalloc() and thus the whole
 * kernel will not boot if these 2 functions are not completed.
 */
int swaptest;
void
vm_bootstrap(void)
{
	coremap_initialization();
}

vaddr_t 
alloc_kpages(int npages)
{
	/*
	 * Write this.
	 */
	 
	assert(npages == 1);

	paddr_t paddr = get_ppage(NULL);
	if (paddr == coremap_size + 1){
		if (!swaptest){
			return 0;
		}else{
			//kprintf("kernel page allocation need swap out\n");
			int index = LRU_Clock();
			if (index == -1){
				//no space to swap out
				return 0;
			}
			int error = swap_out(index);
			if (error == -1){
				//no space in disk
				return 0;
			}
			paddr = get_ppage_specific(NULL, index);
			unreservated(index);
		}
	}

	// insert vpn and the process id to coremap
	//insert_VPN_node(paddr, paddr + MIPS_KSEG0, 0);
	
	return paddr + MIPS_KSEG0;
}

void 
free_kpages(vaddr_t addr)
{
	/*
	 * Write this.
	 */
	assert(addr >= MIPS_KSEG0);
	free_ppage(addr - MIPS_KSEG0);
	
}

// load one page from the disk, copy it into the ram
static 
int
load_page_from_disk(vaddr_t vaddr){
	P(file_lock);
	
	size_t fillamt = 0;
	struct addrspace* as = curthread -> t_vmspace;

	// convert the vaddr to the k_vaddr using the hint
	// vaddr -> paddr -> k_addr -> direct mapping
	struct PTE* pte = access_table(vaddr, as -> page_table);
	
	if (pte == NULL){
		// this should not happen as we have handled eviction previously
		kprintf("something went wrong 1\n");
		return 1;
	}
	
	// already locked in read
	//P(pte->lock);
	paddr_t paddr = pte -> pte & PT_PFN_MASK;
	vaddr_t k_paddr = paddr + MIPS_KSEG0;
	//V(pte->lock);
	
	// create the vnode of the loading file
	struct vnode* v;
	//int spl = splhigh();
	//kprintf("open vfs\n");
	int result = vfs_open(as -> progname, O_RDONLY, &v);
	if (result){
		kprintf("something went wrong 2\n");
		return result;
	}
	
	//kprintf("finish opening vfs\n");
	//splx(spl);
	struct uio ku;
	if (as -> code <= vaddr && vaddr < as -> code + as -> code_npages * PAGE_SIZE){
		// check if this is the not full page
		// must use filesize, which means the actual allocation in disk. memsize is not
		// important here as we have already allocated the entire data space. 
		// User program are free to use any space in data
		size_t remaining_bytes = as -> code_filesize - ((vaddr & PAGE_FRAME) - as -> code);
		// not full, only load the remaining bytes
		size_t len;
		if (remaining_bytes < PAGE_SIZE) len = remaining_bytes;
		else len = PAGE_SIZE;
		// page alignment, will copy the page that the vaddr belongs to the kernel addr
		mk_kuio(&ku, (void *)k_paddr, len, (vaddr & PAGE_FRAME) - as -> code + as -> code_offset, UIO_READ);
		fillamt = PAGE_SIZE - len;
	}else{
		//kprintf("data file size %d\n", as -> data_filesize);
		size_t remaining_bytes = as -> data_filesize - ((vaddr & PAGE_FRAME) - as -> data);
		size_t len;
		if (remaining_bytes < PAGE_SIZE) len = remaining_bytes;
		else len = PAGE_SIZE;
		mk_kuio(&ku, (void *)k_paddr, len, (vaddr & PAGE_FRAME) - as -> data + as -> data_offset, UIO_READ);
		fillamt = PAGE_SIZE - len;
		
	}
	
	//kprintf("Begin to VOP read\n");
	result = VOP_READ(v, &ku);
	//kprintf("VOP read lock passed\n");
	

	// V(pte -> lock);
	// (pte->lock->count == 0) panic("???\n");
	if (fillamt > 0) {
		bzero((void *) (k_paddr + PAGE_SIZE - fillamt), fillamt);
	}
	
	if (result){
	
		kprintf("who is calling: %d, vaddr cause: %x, its kaddr is %x, something went wrong 3\n", curthread -> self_pid, vaddr, k_paddr);
		kprintf("its pte is %x, pte count is %d\n", pte -> pte, pte -> count);
		vfs_close(v);
		return result;
	}

	vfs_close(v);
	//kprintf("close vfs\n");

	V(file_lock);
	return 0;
}

static
void
stack_auto_increment(vaddr_t faultaddress){
	struct addrspace* as = curthread -> t_vmspace;

	
	if (faultaddress - as -> stack_bot_limit < PAGE_SIZE){
		as -> stack_bot_limit -= PAGE_SIZE;
	}

}

static
int
check_bound(vaddr_t v_addr){
	struct addrspace* as = curthread -> t_vmspace;

	// check heap up and stack down
	if (v_addr > as -> heap_top && v_addr < as -> stack_bot_limit) return 1;

	// check null
	if (v_addr == 0) return 1;

	return 0;
}

static
int
demand_paging(struct addrspace* as, vaddr_t faultaddress, struct PTE* pte){
	int result;
	
	// demand paging, must already have the free page on faultaddress
	// it is either a stack or heap memory allocation or a load from code or data
	if (check_bound(faultaddress)) {
	#if VM_DEBUG
		kprintf("Out of bound\n");
	#endif
		return EFAULT;
	}
	#if PROTECTION_DEBUG
		kprintf("Demand paging happens at vr 0x%x\n", faultaddress);
	#endif

	// stack allocation
	if (faultaddress > as -> stack_bot_limit){
		stack_auto_increment(faultaddress);
	}
	
	if (faultaddress >= as -> code && faultaddress < as -> heap_bot){
		result = load_page_from_disk(faultaddress);
		//int spl = splhigh();
		//kprintf("translation: 0x%x -> 0x%x\n", faultaddress &PAGE_FRAME, pte->pte & PAGE_FRAME);
		//splx(spl);
		if (result){
			kprintf("load_page error\n");
			return EFAULT;
		}
	}

	// now is the time to lower reservation bit since we have successfully
	// loaded the file from disk to the memory
	//kprintf("demand paging\n");
	unreservated(((pte -> pte & PAGE_FRAME) >> 12) - offset);
	//kprintf("demand paging finished\n");
	return 0;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{	
	if (check_bound(faultaddress)) return 1;

	
	int error;
	struct PTE* pte;
	switch (faulttype) {
	    case VM_FAULT_READONLY:
			// pass NULL to indicate we need to access table
			// pass pte in the case of write handler, avoid double locking
			error = TLB_Protection_Handler(faultaddress, &pte, NULL);
			break;
	    case VM_FAULT_READ:
			error = TLB_Read_Handler(faultaddress, &pte);
			break;
	    case VM_FAULT_WRITE:
			error = TLB_Write_Handler(faultaddress, &pte);
			break;
	    default:
			return EINVAL;
	}
	
	//set the reservation bit
	return error;
}

/*
int 
TLB_Protection_Handler(vaddr_t faultaddress, struct PTE** return_pte, struct PTE* from_write_pte){
	
	struct PTE* pte, *final;
	paddr_t paddr;
	int coremap_index, error;
	struct addrspace* as = curthread -> t_vmspace;
	
	
	if (from_write_pte == NULL){
		//come directly from VM fault
		pte = access_table(faultaddress, as->page_table);
		P(pte -> lock);
	}else{
		//come from pte_write
		pte = from_write_pte;
	}

	// page got swap out before I enter lock my pte, need to swap in
	if ((pte -> pte & PT_VALID_MASK) == 0 && (pte -> pte & PT_SWAP_MASK) != 0){
		kprintf("SWAPPING in protection handler\n");
		if (swaptest){
			paddr = get_ppage(pte);
			coremap_index = (paddr >> 12) - offset;
			if (paddr == coremap_size + 1 && swaptest){
				// out of memory
				coremap_index = LRU_Clock();
				if (coremap_index == (int)coremap_size + 1) return ENOMEM;
				error = swap_out(coremap_index);
				if (error) return ENOMEM;
			}
			error = swap_in(pte, coremap_index);
		}
	}
	
	if (GET_PROTECTION(pte -> pte) == READ_TEMP){
		// This is for the purpose of changing modify bit
		// A page is read and writable, but it was set to read only previously in order for us
		// to change the modify bit here

		pte -> pte |= PT_MODIFY_MASK;
		pte -> pte &= ~(PT_PROTECTION_MASK);
		final = pte;
	}else if (GET_PROTECTION(pte -> pte) == READ_ONLY){
		// COW: the only case where you write on a read only page
		if (pte -> count > 1){
			
			// pfn is known
			//the reservation bit will got raised in coremap
			struct PTE* new = PTE_init();
			if (new == NULL) {
				kprintf("everything is kernel page or both mem and disk are full\n");
				return ENOMEM;
			}

			new -> pte |= PT_VALID_MASK;
			new -> pte |= PT_REFERENCE_MASK;
			new -> pte |= PT_MODIFY_MASK;
			new -> pte &= ~(PT_PROTECTION_MASK); // make protection bit 00: read and write
			int region = GET_REGION(pte -> pte);
			new -> pte = SET_REGION(new -> pte, region);
			
			//copy the region, convert to pa, then use it as kernel addr to copy
			paddr_t paddr_old = pte -> pte & PAGE_FRAME;
			paddr_t paddr_new = new -> pte & PAGE_FRAME;
			
			vaddr_t k_old = paddr_old + MIPS_KSEG0;
			vaddr_t k_new = paddr_new + MIPS_KSEG0;
			
			memmove((void *)k_new, (const void *)k_old, PAGE_SIZE);
			
			//attach the new PTE to the page table
			attach_PTE(faultaddress, new, as -> page_table);
			//need to create a new function that add it to the thing

			pte -> count--;
			
			if (pte -> count == 1){
				pte -> pte &= ~(PT_PROTECTION_MASK);
			}
			unreservated((paddr_new >> 12) - offset);
			final = new;
		}else{
			// this should not happen
			kprintf("why here, should not, addr is: %x\n", faultaddress);
			
			pte -> pte &= (~PT_PROTECTION_MASK);
			final = pte;
		}

	}else{
		// one process might already be in the TLB Proctection handler
		// even the preivous process set the protection bit to 
		// read and write, thus we just only change the modified bit
		
		// do nothing since we have set up everything in the previous case
		final = pte;
	}

	*return_pte = final;
	
	
	if (from_write_pte == NULL){
		// if not NULL, it is called from write which will do the following in its func
		TLB_write(faultaddress, final);
		V(pte -> lock);
	}
	return 0;
}*/

/*
int
TLB_Write_Handler(vaddr_t faultaddress, struct PTE** return_pte){
	
	struct addrspace* as = curthread->t_vmspace;
	if (as == NULL) {
		return EFAULT;
	}

	paddr_t paddr;
	int coremap_index, error;

	struct PTE* pte = access_table(faultaddress, as -> page_table);
	struct PTE* final;
	if (pte == NULL){
		// page table is full
		return EFAULT;
	}

	P(pte -> lock);
		
	if (pte -> pte & PT_VALID_MASK){
		// still possible to generate read only fault
		// When you COW a page or read temp page, if the page is not in the TLB (e.g after context switch)
		// and you issue a write, it will trigger TLB miss write fault, need to call 
		// protection handler here if it is a read only page
		
		if (GET_PROTECTION(pte-> pte) != READ_WRITE){
			error = TLB_Protection_Handler(faultaddress, return_pte, pte);
			if (error) return error;
			final = *return_pte;
		}else{
			final = pte;
		}
		
	}else{
		// not valid: then we are either allocating new page or loading from swap disk
		if(pte -> pte & PT_SWAP_MASK){
			// load from swap disk
			// check if we are run out of memory: call swap out
			if (swaptest){
				kprintf("enter write swap in\n");
				paddr = get_ppage(pte);
				if (paddr == coremap_size + 1){
					kprintf("write: swap out requires\n");
					coremap_index = LRU_Clock();
					//kprintf("The selected lock count is: %d\n",coremap.coremap_array[coremap_index].pte -> lock -> count);
					if (coremap_index == (int)coremap_size + 1) return ENOMEM;
					
					error = swap_out(coremap_index);
					if (error) return ENOMEM;
					kprintf("Write: swap out success\n");
				}
				
				error = swap_in(pte, coremap_index);
				kprintf("Write: swap in success\n");
				if (error) return EFAULT;
			
				// swap in a COW page, we need to call protection handler as we are writing it
				// Similarly, if you swap in a read teamp, we need to make it read write as this is write
				if (GET_PROTECTION(pte-> pte) != READ_WRITE){
					kprintf("Write: it is protected forward this to protection handler\n");
					error = TLB_Protection_Handler(faultaddress, return_pte, pte);
					if (error) return error;
					final = *return_pte;
				}else{
					final = pte;
					*return_pte = pte;
				}
			
			}
			
		}else{
			// new page call demand paging
			error = demand_paging(as, faultaddress, pte);
			kprintf("Write: finish demand paging\n");
			if (error) return EFAULT;
			// when you issue a write on a newly allocated page (read write)
			// we don't need read temp, make it read write immediately
			pte-> pte &= (~PT_PROTECTION_MASK);
			final = pte;
		}
	}
	
	final -> pte |= PT_VALID_MASK;
	final -> pte |= PT_REFERENCE_MASK;
	final -> pte |= PT_MODIFY_MASK;

	TLB_write(faultaddress, final);
	*return_pte = final;
	V(pte -> lock);
	return 0;
}

int
TLB_Read_Handler(vaddr_t faultaddress, struct PTE** return_pte){
	
	struct addrspace* as = curthread->t_vmspace;
	if (as == NULL) {
		return EFAULT;
	}

	paddr_t paddr;
	int coremap_index, error;
	
	struct PTE* pte = access_table(faultaddress, as -> page_table);
	if (pte == NULL){
		// page table is full
		return EFAULT;
	}
	
	P(pte -> lock);
	
	if (pte -> pte & PT_VALID_MASK){
		// we have a valid page, just write it in TLB
		*return_pte = pte;
	}else{
		// not valid: then we are either allocating new page or loading from swap disk
		if(pte -> pte & PT_SWAP_MASK){
			// load from swap disk
			// check if we are run out of memory: call swap out
			if (swaptest){
				paddr = get_ppage(pte);
				if (paddr == coremap_size + 1){
					// out of memory: need swap out another page
					kprintf("Read: swap out begin\n");
					coremap_index = LRU_Clock();
					kprintf("LRU clock finishes\n");
					if (coremap_index == (int)coremap_size + 1) return ENOMEM;
					error = swap_out(coremap_index);
					if (error) return ENOMEM;
					kprintf("Read: swap out success\n");
				}
				error = swap_in(pte, coremap_index);
				if (error) return EFAULT;
				kprintf("Read: swap in success\n");
			}
			
		}else{
			// new page call demand: swap bit = 0, valid bit = 0, pfn is valid
			error = demand_paging(as, faultaddress, pte);
			if (error) return EFAULT;
		}
	}
	
	pte -> pte |= PT_VALID_MASK;
	pte -> pte |= PT_REFERENCE_MASK;

	*return_pte = pte;
	
	TLB_write(faultaddress, pte);
	V(pte->lock);
	
	return 0;
}

*/
/*
int
TLB_Write_Handler(vaddr_t faultaddress, struct PTE** return_pte){
	
	struct addrspace* as = curthread->t_vmspace;
	if (as == NULL) {
		return EFAULT;
	}
	paddr_t paddr;
	int coremap_index, error;

	struct PTE* pte = access_table(faultaddress, as -> page_table);
	struct PTE* final;
	if (pte == NULL){
		// page table is full
		return EFAULT;
	}

	paddr_t begin = pte -> pte & PAGE_FRAME;

	P(pte -> lock);
		
	if (pte -> pte & PT_VALID_MASK){
		// still possible to generate read only fault
		// When you COW a page or read temp page, if the page is not in the TLB (e.g after context switch)
		// and you issue a write, it will trigger TLB miss write fault, need to call 
		// protection handler here if it is a read only page

		// also, the read temp implementation will used will also lead to this case
		
		if (GET_PROTECTION(pte-> pte) != READ_WRITE){
			
			error = TLB_Protection_Handler(faultaddress, return_pte, pte);
			if (error) return error;
			final = *return_pte;
		}else{
			final = pte;
		}
		
	}else{
	
		// not valid: then we are either allocating new page or loading from swap disk
		if(pte -> pte & PT_SWAP_MASK){
			// load from swap disk
			// check if we are run out of memory: call swap out
			if (swaptest){
				kprintf("enter write swap\n");
				paddr = get_ppage(pte);
				coremap_index = (paddr >> 12) - offset;
				if (paddr == coremap_size + 1){
					
					coremap_index = LRU_Clock();
					kprintf("The selected lock count is: %d\n",coremap.coremap_array[coremap_index].pte -> lock -> count);
					if (coremap_index == (int)coremap_size + 1) return ENOMEM;
					
					error = swap_out(coremap_index);
					if (error) return ENOMEM;
					kprintf("Write: swap out success\n");
				}
				kprintf("write: swap in begin\n");
				error = swap_in(pte, coremap_index);
				kprintf("Write: swap in success\n");
				if (error) return EFAULT;

				pte -> pte |= PT_VALID_MASK;
				
				// swap in a COW page, we need to call protection handler as we are writing it
				// Similarly, if you swap in a read teamp, we need to make it read write as this is write
				if (GET_PROTECTION(pte-> pte) != READ_WRITE){
					kprintf("Write: it is protected forward this to protection handler\n");
					error = TLB_Protection_Handler(faultaddress, return_pte, pte);
					if (error) return error;
					final = *return_pte;
				}else{
					final = pte;
					*return_pte = pte;
				}
			
			}
			
		}else{
			kprintf("Write: This is a new page or the copy is on the testbin\n");
			if ((pte -> pte & PAGE_FRAME) == PAGE_FRAME){
				paddr = get_ppage(pte);
				coremap_index = (paddr >> 12) - offset;
				if (paddr == coremap_size + 1){
					// out of memory
					kprintf("Memory is full\n");
					coremap_index = LRU_Clock();
					if (coremap_index == (int)coremap_size + 1) return ENOMEM;
					error = swap_out(coremap_index);
					kprintf("swap out success\n");
					if (error) return ENOMEM;
				}
				get_ppage_specific(pte, coremap_index);
				error = demand_paging(as, faultaddress, pte);
				unreservated(coremap_index);
				kprintf("put the new page in the coremap entry %d\n", coremap_index);
			}else{
				// new page call demand paging
				error = demand_paging(as, faultaddress, pte);
				paddr = pte -> pte & PAGE_FRAME;
				unreservated((paddr >> 12) - offset);
			}
			if (error) return EFAULT;
			// when you issue a write on a newly allocated page (read write)
			// we don't need read temp, make it read write immediately
			pte-> pte &= (~PT_PROTECTION_MASK);
			final = pte;
		}
	}
	
	final -> pte |= PT_VALID_MASK;
	final -> pte |= PT_REFERENCE_MASK;
	final -> pte |= PT_MODIFY_MASK;

	if (faultaddress >= as -> stack_bot_limit) final -> pte = SET_REGION(final -> pte, STACK_REGION);
	else if (as -> heap_bot <= faultaddress && faultaddress < as -> heap_bot) final -> pte = SET_REGION(final -> pte, HEAP_REGION);
	else if (as -> data <= faultaddress && faultaddress < as -> heap_bot) final -> pte = SET_REGION(final -> pte, DATA_REGION);
	else final -> pte = SET_REGION(final -> pte, CODE_REGION);

	paddr_t end = pte -> pte & PAGE_FRAME;
	if (begin != end){
		kprintf("Write:The begin is: %x, the end is %x\n",begin, end);
	}
	TLB_write(faultaddress, final);
	*return_pte = final;
	V(pte -> lock);
	return 0;
}
*/
/*
int
TLB_Read_Handler(vaddr_t faultaddress, struct PTE** return_pte){
	
	struct addrspace* as = curthread->t_vmspace;
	if (as == NULL) {
		return EFAULT;
	}

	paddr_t paddr;
	int coremap_index, error;
	

	struct PTE* pte = access_table(faultaddress, as -> page_table);
	if (pte == NULL){
		// page table is full
		return EFAULT;
	}
	
	

	P(pte -> lock);
	
	
	if (pte -> pte & PT_VALID_MASK){
		// we have a valid page, just write it in TLB
		*return_pte = pte;
		
	}else{
		// not valid: then we are either allocating new page or loading from swap disk
		if(pte -> pte & PT_SWAP_MASK){
			// load from swap disk
			// check if we are run out of memory: call swap out

			if (swaptest){
				kprintf("enter read swapping\n");
				paddr = get_ppage(pte);
				coremap_index = (paddr >> 12) - offset;
				if (paddr == coremap_size + 1){
					// out of memory
					
					coremap_index = LRU_Clock();
					
					if (coremap_index == (int)coremap_size + 1) return ENOMEM;
					error = swap_out(coremap_index);
					if (error) return ENOMEM;
					kprintf("Read: swap out success\n");
				}
				kprintf("Read: swap in begin\n");
				error = swap_in(pte, coremap_index);
				if (error) return EFAULT;
				kprintf("Read: swap in success\n");
			}
			
		}else{
			// this happens if swap out a unmodified code and data region
			// it has both 0 on swap and valid bit. However, its forth level PT is not NULL
			// It contains FFFFF000 as the pfn, we need to assign it to a correct pfn
			if ((pte -> pte & PAGE_FRAME) == PAGE_FRAME){
				kprintf("enter this designated case\n");
				paddr = get_ppage(pte);
				coremap_index = (paddr >> 12) - offset;
				if (paddr == coremap_size + 1){
					// out of memory
					kprintf("Need swap out for our designated case as mem is full\n");
					coremap_index = LRU_Clock();
					if (coremap_index == (int)coremap_size + 1) return ENOMEM;
					error = swap_out(coremap_index);
					if (error) return ENOMEM;
					kprintf("Designated: swap out success\n");
				}
				get_ppage_specific(pte, coremap_index);
				error = demand_paging(as, faultaddress, pte);
				unreservated(coremap_index);
			}else{
				// new page call demand: swap bit = 0, valid bit = 0, pfn is valid
				error = demand_paging(as, faultaddress, pte);
				paddr = pte -> pte & PAGE_FRAME;
				unreservated((paddr >> 12) - offset);
			}
			 	
			if (error){
				V(pte->lock);
				return EFAULT;
			} 
			
		}
	}
	

	pte -> pte |= PT_VALID_MASK;
	pte -> pte |= PT_REFERENCE_MASK;

	if (faultaddress >= as -> stack_bot_limit) pte -> pte = SET_REGION(pte -> pte, STACK_REGION);
	else if (as -> heap_bot <= faultaddress && faultaddress < as -> heap_bot) pte -> pte = SET_REGION(pte -> pte, HEAP_REGION);
	else if (as -> data <= faultaddress && faultaddress < as -> heap_bot) pte -> pte = SET_REGION(pte -> pte, DATA_REGION);
	else pte -> pte = SET_REGION(pte -> pte, CODE_REGION);

	*return_pte = pte;
	if ((pte -> pte & PAGE_FRAME) == 0){
		//int spl = splhigh();
		kprintf("is coremap full: %d\n", coremap_avaliable_space());
		kprintf("ERROR READ HANDLER NULL, faultaddress: 0x%x\n",faultaddress);
		//splx(spl);
	}

	
	
	TLB_write(faultaddress, pte);
	V(pte->lock);
	
	return 0;
}
*/
/*
int
vm_fault(int faulttype, vaddr_t faultaddress)
{	

	
	#if VM_DEBUG
		kprintf("TLB miss address is %x\n", faultaddress);
	#endif
	int result;

	switch (faulttype) {
	    case VM_FAULT_READONLY:
			//We always create pages read-write, so we can't get this 
			TLB_Protection_Handler(faultaddress);
			break;
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		
		return EINVAL;
	}

	struct addrspace* as = curthread->t_vmspace;
	if (as == NULL) {
		return EFAULT;
	}
	
	// check if there's a valid match in the page table
	struct PTE* pte = access_table(faultaddress, as -> page_table);
	if (pte == NULL){
		// page table is full
		// need implementing evict
		
		return EFAULT;
	}


	P(pte->lock);
	
	if (pte -> pte & PT_VALID_MASK){
		// still possible to generate read only fault
		//TLB_Protection_Handler(faultaddress);
		pte -> pte &= PT_REFERENCE_MASK;
		V(pte->lock);
		if (GET_PROTECTION(pte-> pte) == READ_ONLY && faulttype == VM_FAULT_WRITE){
			//kprintf("leo is right\n");
			TLB_Protection_Handler(faultaddress);
			pte = access_table(faultaddress, as -> page_table);
		}
		// has the valid page entry
		
		TLB_write(faultaddress, pte);
		
		return 0;
	}

	// page fault
	// P(pte->lock);
	if (!(pte -> pte & PT_SWAP_MASK)){
		V(pte->lock);
		result = demand_paging(as, faultaddress, pte);
		if (result){
			return EFAULT;
		}
	}else{
		// swapp in
		int index;
		// test to see if we need to swap out first
		paddr_t paddr = get_ppage(pte);
		if(paddr == coremap_size + 1){
			index = LRU_Clock();
			if (index == -1){
				//no space to swap out
				return ENOMEM;
			}
			int err = swap_out(index);
			if (err == -1){
				//no space in disk
				return ENOMEM;
			}
		}
		
		swap_in(pte, index);
		
		V(pte->lock);
	}

	// all access table and load_page_from_disk does not write to TLB
	P(pte->lock);
	pte -> pte |= PT_VALID_MASK;
	pte -> pte |= PT_REFERENCE_MASK;
	if (faulttype == VM_FAULT_WRITE) pte -> pte |= PT_MODIFY_MASK;
	
	if (faultaddress >= as -> stack_bot_limit) pte -> pte = SET_REGION(pte -> pte, STACK_REGION);
	else if (as -> heap_bot <= faultaddress && faultaddress < as -> heap_bot) pte -> pte = SET_REGION(pte -> pte, HEAP_REGION);
	else if (as -> data <= faultaddress && faultaddress < as -> heap_bot) pte -> pte = SET_REGION(pte -> pte, DATA_REGION);
	else pte -> pte = SET_REGION(pte -> pte, CODE_REGION);

	V(pte->lock);
	
	TLB_write(faultaddress, pte);
	
	return 0;
}
*/

	

/*
int
TLB_Protection_Handler(vaddr_t vaddr){


	struct addrspace* as = curthread -> t_vmspace;
	// old pte
	struct PTE* pte = access_table(vaddr, as->page_table);
	P(pte->lock);



	int protection_bit = GET_PROTECTION(pte-> pte);

	if (protection_bit == EXECUTABLE || 
		(vaddr >= as->code && vaddr < as->code + as->code_npages * PAGE_SIZE )){
		return EINVAL;
		//kill curthread
	}else if(protection_bit == READ_TEMP){
		//used for modify dirty bit of page table entry
		// we will see if we plan to implement this
		kprintf("read temp not implemented\n");
	}else if(protection_bit == READ_ONLY){
		//copy on write
		if (pte -> count > 1){
			// pfn is known
			struct PTE* new = PTE_init();
			if (new == NULL) {
				// it is possible that page is full, we need to evict
				return ENOMEM;
			}

			new -> pte |= PT_VALID_MASK;
			new -> pte |= PT_REFERENCE_MASK;
			new -> pte |= PT_MODIFY_MASK;
			new -> pte &= ~(PT_PROTECTION_MASK); // make protection bit 00: read and write
			
			//copy the region, convert to pa, then use it as kernel addr to copy
			paddr_t paddr_old = pte -> pte & PAGE_FRAME;
			paddr_t paddr_new = new -> pte & PAGE_FRAME;
			
			vaddr_t k_old = paddr_old + MIPS_KSEG0;
			vaddr_t k_new = paddr_new + MIPS_KSEG0;
			
			memmove((void *)k_new, (const void *)k_old, PAGE_SIZE);
			
			//attach the new PTE to the page table
			attach_PTE(vaddr, new, as->page_table);
			//need to create a new function that add it to the thing

			pte -> count--;
			
			if (pte -> count == 1){
				//kprintf("%x, %d\n", (int) pte, pte -> count);
				pte -> pte &= ~(PT_PROTECTION_MASK);
			}

		}else{
			pte -> pte &= (~PT_PROTECTION_MASK);
		}
		
	}else{
		// one process might already be in the TLB Proc handler
		// even the preivous process set the protection bit to 
		// read and write, thus we just only change the modified
		// bit
		//kprintf("why here\n");
		pte->pte |= PT_MODIFY_MASK;
	}
	V(pte -> lock);

	return 0;
}
*/

/*
int
TLB_Protection_Handler(vaddr_t vaddr){
	// this vaddr causes the read only fault, which could only happen in COW

	// at the moment, we will ignore the dirty bit implementation (setting all pages to read only)

	// check bound, kill thread if needed
	// access pte
	// initialize a new pte
	// set the corresponding bit
	//

}
*/

// evict

static
void set_region(vaddr_t faultaddress, struct addrspace* as, struct PTE* final){
	if (faultaddress >= as -> stack_bot_limit) final -> pte = SET_REGION(final -> pte, STACK_REGION);
	else if (as -> heap_bot <= faultaddress && faultaddress < as -> heap_bot) final -> pte = SET_REGION(final -> pte, HEAP_REGION);
	else if (as -> data <= faultaddress && faultaddress < as -> heap_bot) final -> pte = SET_REGION(final -> pte, DATA_REGION);
	else final -> pte = SET_REGION(final -> pte, CODE_REGION);
}

//version 3.0
int
TLB_Read_Handler(vaddr_t faultaddress, struct PTE** return_pte){
	kprintf("Read: start\n");
	struct addrspace* as = curthread->t_vmspace;
	if (as == NULL) {
		return EFAULT;
	}
	
	paddr_t paddr;
	int coremap_index, error;

	struct PTE* pte = access_table(faultaddress, as -> page_table);
	if (pte == NULL){
		// page table is full
		return EFAULT;
	}

	P(pte->lock);
	if ((pte -> pte & PT_VALID_MASK) != 0){
		// the page is inside the memory
		*return_pte = pte;
		pte -> pte |= PT_REFERENCE_MASK;	// the page will got accessed
	}else{
		// the page is not inside the memory
		kprintf("Read: not in memory\n");
		if ((pte -> pte & PT_SWAP_MASK) != 0){
			// the page is in swap disk
			// allocate a page , note that the reservation bit will be raised. 
			
			paddr = get_ppage(pte);

			if (paddr == coremap_size + 1){
				// the memory is full, need to swap out a page
				coremap_index = LRU_Clock();
				
				if (coremap_index == (int) coremap_size + 1){
					kprintf("LRU_clock can not allocated a page for me\n");
					V(pte->lock);
					return ENOMEM;
				}

				// the reservation bit is already raised
				error = swap_out(coremap_index);
				if (error){
					V(pte->lock);
					return EFAULT;
				}
				
				// swap in will set the required bit in pte for me, reservation bit is unraised
				// note the protection bit will always be protected: either read temp or read only (code)
				error = swap_in(pte, coremap_index);
				if (error){
					V(pte->lock);
					return EFAULT;
				} 
				
			}else{
				//memory is not full, just call swap in
				coremap_index = (paddr >> 12) - offset;
				
				//it will set the required bit in pte for me
				//note the protection bit will always beprotected
				error = swap_in(pte, coremap_index);
				if (error){
					V(pte->lock);
					return EFAULT;
				} 
			}
			*return_pte = pte;
			pte -> pte |= PT_VALID_MASK; 	//the page is now in the memory
			pte -> pte |= PT_REFERENCE_MASK;//the page just got accessed
			
		}else{
			//the page is not in swap disk
			//it must has a copy on the testbin folder

			if ((pte -> pte & PAGE_FRAME) == PAGE_FRAME){
				//this page was allocated before, so its pfn is 0xfffff000
				//there is no memory allocated, thus need to allocated first

				// a complicated deadlock problem: if get_ppage is the last page
				// then its not a kernel page, reference bit is 0, and reservation bit is 0
				// IN demand paging, vfs open will call kmalloc which will swap out page since mem is full
				// therefore, LRU clock will select the page that is allocated by get_ppage
				// causing the deadlock to happen.
				
				paddr = get_ppage(pte);
				if (paddr == coremap_size + 1){
					//the memory is full, need to swap out a page
					coremap_index = LRU_Clock();
					
					if (coremap_index == (int) coremap_size + 1){
						kprintf("LRU_clock can not allocated a page for me\n");
						V(pte->lock);
						return ENOMEM;
					}

					//the reservation bit is raised
					error = swap_out(coremap_index);
					if (error){
						V(pte->lock);
						return EFAULT;
					}

					// the reservation will not got lower here
					get_ppage_specific(pte, coremap_index);

					paddr = (coremap_index + offset) << 12;
					pte -> pte &= ~PAGE_FRAME;
					pte -> pte |= paddr;
					// the reservation will got lower in here
					error = demand_paging(as, faultaddress, pte);


				}else{
					// memory is not full, just call demand paging
					
					//demand paging required the physical page
					pte -> pte &= ~PAGE_FRAME;
					pte -> pte |= paddr;

					// reservation bit will get lower
					error = demand_paging(as, faultaddress, pte);
					if (error){
						V(pte -> lock);
						return EFAULT;
					}

				}

				*return_pte = pte;
				pte -> pte |= PT_VALID_MASK;		//it is now in the memory
				pte -> pte |= PT_REFERENCE_MASK;	//it will accessed
			}else{
				// this is a new page
				// the memory is already allocated by getppage
				// lower the reservation bit that is raised in PTE_init
				error = demand_paging(as, faultaddress, pte);
				kprintf("Process %d, new translation 0x%x -> 0x%x\n", curthread->self_pid, faultaddress & PAGE_FRAME, pte -> pte & PAGE_FRAME);
				if (error){
					V(pte -> lock);
					return EFAULT;
				}

				*return_pte = pte;
				pte -> pte |= PT_VALID_MASK;		//it is now in the memory
				pte -> pte |= PT_REFERENCE_MASK;	//it will accessed
			}
		}
		
	}
	
	set_region(faultaddress, as, pte);

	TLB_write(faultaddress, pte);

	V(pte->lock);
	kprintf("Read: finish\n");
	return 0;
}

int
TLB_Write_Handler(vaddr_t faultaddress, struct PTE** return_pte){
	kprintf("Write: start\n");
	struct addrspace* as = curthread->t_vmspace;
	if (as == NULL) {
		return EFAULT;
	}
	paddr_t paddr;
	int coremap_index, error;

	struct PTE* pte = access_table(faultaddress, as -> page_table);
	if (pte == NULL){
		// page table is full
		return EFAULT;
	}
	
	struct PTE* final = NULL;

	
	P(pte -> lock);
	if ((pte -> pte & PT_VALID_MASK) != 0){
		// the page is inside the memory
		// do nothing, the bit in pte will be set later
	}else{
		
		// the page is not inside the memory
		if ((pte -> pte & PT_SWAP_MASK) != 0){
			
			// the page is in swap disk
			// allocate a page , note that the reservation bit will be raised. 
			paddr = get_ppage(pte);

			if (paddr == coremap_size + 1){
				// the memory is full, need to swap out a page
				coremap_index = LRU_Clock();
				
				if (coremap_index == (int) coremap_size + 1){
					kprintf("LRU_clock can not allocated a page for me\n");
					V(pte->lock);
					return ENOMEM;
				}

				// the reservation bit is already raised
				error = swap_out(coremap_index);
				if (error){
					V(pte->lock);
					return EFAULT;
				}
				
				// swap in will set the required bit in pte for me, reservation bit is unraised
				// note the protection bit will always be protected: either read temp or read only (code)
				error = swap_in(pte, coremap_index);
				if (error){
					V(pte->lock);
					return EFAULT;
				} 

			}else{
				//memory is not full, just call swap in
				coremap_index = (paddr >> 12) - offset;
				
				//it will set the required bit in pte for me
				//note the protection bit will always beprotected
				error = swap_in(pte, coremap_index);
				if (error){
					V(pte->lock);
					return EFAULT;
				} 
			}
			pte -> pte |= PT_VALID_MASK;			// it is now in memory
			
		}else{
			//the page is not in swap disk
			//it must has a copy on the testbin folder

			if ((pte -> pte & PAGE_FRAME) == PAGE_FRAME){
				//this page was allocated before, so its pfn is 0xfffff000
				//there is no memory allocated, thus need to allocated first

				// a complicated deadlock problem: if get_ppage is the last page
				// then its not a kernel page, reference bit is 0, and reservation bit is 0
				// IN demand paging, vfs open will call kmalloc which will swap out page since mem is full
				// therefore, LRU clock will select the page that is allocated by get_ppage
				// causing the deadlock to happen.
				
				paddr = get_ppage(pte);
				if (paddr == coremap_size + 1){
					//the memory is full, need to swap out a page
					coremap_index = LRU_Clock();
					
					if (coremap_index == (int) coremap_size + 1){
						kprintf("LRU_clock can not allocated a page for me\n");
						V(pte->lock);
						return ENOMEM;
					}

					//the reservation bit is raised
					error = swap_out(coremap_index);
					if (error){
						V(pte->lock);
						return EFAULT;
					}

					// the reservation will not got lower here
					get_ppage_specific(pte, coremap_index);
					
					paddr = (coremap_index + offset) << 12;
					pte -> pte &= ~PAGE_FRAME;
					pte -> pte |= paddr;

					// the reservation will got lower in here
					error = demand_paging(as, faultaddress, pte);


				}else{
					// memory is not full, just call demand paging
					
					//demand paging required the physical page
					pte -> pte &= ~PAGE_FRAME;
					pte -> pte |= paddr;

					// reservation bit will get lower
					error = demand_paging(as, faultaddress, pte);
					if (error){
						V(pte -> lock);
						return EFAULT;
					}

				}
				
				pte -> pte |= PT_VALID_MASK;		//it is now in the memory
			}else{
				// this is a new page
				// the memory is already allocated by getppage
				// lower the reservation bit that is raised in PTE_init
				// pfn is already set is pte_init
				error = demand_paging(as, faultaddress, pte);
				//kprintf("Process %d, new translation 0x%x -> 0x%x\n", curthread->self_pid, faultaddress & PAGE_FRAME, pte -> pte & PAGE_FRAME);
				if (error){
					V(pte -> lock);
					return EFAULT;
				}

				pte -> pte |= PT_VALID_MASK;		//it is now in the memory
			}
		}
		
	}

	if (GET_PROTECTION(pte -> pte) != READ_WRITE){
		// swap in always bring a read temp page to memory which we are currently writing
		// also it is possible that a page is read only or read temp whose TLB 
		// does not store such a page after context switch

		//it will set the bits for us inside the protection handler, no need to worry about
		// however, the valid bit must be set to 1 so that our protection handler won't confuse
		// our page is already been swap out by other threads
		error = TLB_Protection_Handler(faultaddress, &final, pte);
		
		if (error){
			V(pte -> lock);
			return error;
		}
	}else{
		pte -> pte |= PT_REFERENCE_MASK;
		pte -> pte |= PT_MODIFY_MASK;
		final = pte;
	}
	
	set_region(faultaddress, as, final);
	TLB_write(faultaddress, final);
	*return_pte = final;

	V(pte -> lock);
	kprintf("Write: finish\n");
	return 0;
}

int 
TLB_Protection_Handler(vaddr_t faultaddress, struct PTE** return_pte, struct PTE* from_write_pte){
	
	struct PTE* pte, *final;
	paddr_t paddr;
	int coremap_index, error;
	struct addrspace* as = curthread -> t_vmspace;
	
	kprintf("start protection handler\n");

	if (from_write_pte == NULL){
		//come directly from VM fault
		pte = access_table(faultaddress, as->page_table);
		P(pte -> lock);
	}else{
		//come from pte_write
		pte = from_write_pte;
	}

	// page got swap out before I enter lock my pte, need to swap in
	if ((pte -> pte & PT_VALID_MASK) == 0 && (pte -> pte & PT_SWAP_MASK) != 0){
		kprintf("SWAPPING in protection handler\n");
		if (swaptest){
			paddr = get_ppage(pte);
			coremap_index = (paddr >> 12) - offset;
			if (paddr == coremap_size + 1 && swaptest){
				// out of memory
				coremap_index = LRU_Clock();
				if (coremap_index == (int)coremap_size + 1) return ENOMEM;
				error = swap_out(coremap_index);
				if (error) return ENOMEM;
			}
			error = swap_in(pte, coremap_index);
		}
	}else if ((pte -> pte & PT_VALID_MASK) == 0 && (pte -> pte & PT_SWAP_MASK) == 0){
		kprintf("why are you here\n");
		//this page is in file system
		//this is not a new page, because otherwise the protection handler will not be called
		paddr = get_ppage(pte);
		if (paddr == coremap_size + 1){
			//the memory is full, need to swap out a page
			coremap_index = LRU_Clock();
			
			if (coremap_index == (int) coremap_size + 1){
				kprintf("LRU_clock can not allocated a page for me\n");
				V(pte->lock);
				return ENOMEM;
			}

			//the reservation bit is raised
			error = swap_out(coremap_index);
			if (error){
				V(pte->lock);
				return EFAULT;
			}

			// the reservation will not got lower here
			get_ppage_specific(pte, coremap_index);

			paddr = (coremap_index + offset) << 12;
			pte -> pte &= ~PAGE_FRAME;
			pte -> pte |= paddr;


			// the reservation will got lower in here
			error = demand_paging(as, faultaddress, pte);


		}else{
			// memory is not full, just call demand paging
			
			pte -> pte &= ~PAGE_FRAME;
			pte -> pte |= paddr;

			// reservation bit will get lower
			error = demand_paging(as, faultaddress, pte);
			if (error){
				V(pte -> lock);
				return EFAULT;
			}

		}

		pte -> pte |= PT_VALID_MASK;
	}
	
	if (GET_PROTECTION(pte -> pte) == READ_TEMP){
		// This is for the purpose of changing modify bit
		// A page is read and writable, but it was set to read only previously in order for us
		// to change the modify bit here

		pte -> pte |= PT_MODIFY_MASK;
		pte -> pte &= ~(PT_PROTECTION_MASK);
		final = pte;
	}else if (GET_PROTECTION(pte -> pte) == READ_ONLY){
		// COW: the only case where you write on a read only page
		if (pte -> count > 1){
			
			// pfn is known
			//the reservation bit will got raised in coremap
			struct PTE* new = PTE_init();
			if (new == NULL) {
				kprintf("everything is kernel page or both mem and disk are full\n");
				return ENOMEM;
			}

			new -> pte |= PT_VALID_MASK;
			new -> pte |= PT_REFERENCE_MASK;
			new -> pte |= PT_MODIFY_MASK;
			new -> pte &= ~(PT_PROTECTION_MASK); // make protection bit 00: read and write
			int region = GET_REGION(pte -> pte);
			new -> pte = SET_REGION(new -> pte, region);
			
			//copy the region, convert to pa, then use it as kernel addr to copy
			paddr_t paddr_old = pte -> pte & PAGE_FRAME;
			paddr_t paddr_new = new -> pte & PAGE_FRAME;
			
			vaddr_t k_old = paddr_old + MIPS_KSEG0;
			vaddr_t k_new = paddr_new + MIPS_KSEG0;
			
			memmove((void *)k_new, (const void *)k_old, PAGE_SIZE);
			
			//attach the new PTE to the page table
			attach_PTE(faultaddress, new, as -> page_table);
			//need to create a new function that add it to the thing

			pte -> count--;
			
			if (pte -> count == 1){
				pte -> pte = SET_PROTECTION(pte->pte, READ_TEMP);
			}
			unreservated((paddr_new >> 12) - offset);
			final = new;
		}else{
			// this should not happen
			kprintf("why here, should not, addr is: %x\n", faultaddress);
			
			pte -> pte &= (~PT_PROTECTION_MASK);
			final = pte;
		}

	}else{
		// one process might already be in the TLB Proctection handler
		// even the preivous process set the protection bit to 
		// read and write, thus we just only change the modified bit
		
		// do nothing since we have set up everything in the previous case
		kprintf("Theoratically, this should not happens\n");
		pte -> pte |= PT_MODIFY_MASK;
		final = pte;
	}

	*return_pte = final;
	

	if (from_write_pte == NULL){
		// if not NULL, it is called from write which will do the following in its func
		TLB_write(faultaddress, final);
		V(pte -> lock);
	}
	kprintf("finish protection handler\n");
	return 0;
}




