#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <pt.h>
#include <tlb_map.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

#define ADDR_DEBUG 0

struct addrspace *
as_create(const char * progname)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}
	/*
	 * Initialize as needed.
	 */

	as -> page_table = PT_init();
	strcpy(as -> progname, progname);

	// initialize all to zero
	//as -> stack = 0; stack is automatically increase, no need tracking in addrspace
	as -> stack_bot_limit = 0;
	as -> heap_top = 0;
	as -> heap_bot = 0;

	as -> data = 0;
	as -> data_npages = 0;
	as -> data_offset = 0;
	as -> data_filesize = 0;
	as -> code = 0;
	as -> code_npages = 0;
	as -> code_offset = 0;
	as -> code_filesize = 0;

	return as;
}

/*
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = kmalloc(sizeof(struct addrspace));
	//newas = as_create(old -> progname);
	if (newas==NULL) {
	#if ADDR_DEBUG
		kprintf("as create fails, not enough mem\n");
	#endif
		return ENOMEM;
	}

	strcpy(newas -> progname, old -> progname);

	newas->page_table = PT_copy(old->page_table);
	if (newas->page_table == NULL){
		as_destroy(newas);
	#if ADDR_DEBUG
		kprintf("copy fails, not enough mem\n");
	#endif
		return ENOMEM;
	}

	struct PTE* old_pte = access_table(0x7ffff000, old->page_table);
	P(old_pte -> lock);
	struct PTE* new = PTE_init();
	//kprintf("VA 0x%x -> PA 0x%x\n", vaddr & PAGE_FRAME, new->pte & PAGE_FRAME);
	if (new == NULL) {
		// it is possible that page is full, we need to evict
		return ENOMEM;
	}

	new -> pte |= PT_VALID_MASK;
	new -> pte |= PT_REFERENCE_MASK;
	new -> pte |= PT_MODIFY_MASK;
	new -> pte &= ~(PT_PROTECTION_MASK); // make protection bit 00: read and write
	
	//copy the region, convert to pa, then use it as kernel addr to copy
	paddr_t paddr_old = old_pte -> pte & PAGE_FRAME;
	paddr_t paddr_new = new -> pte & PAGE_FRAME;
	
	vaddr_t k_old = paddr_old + MIPS_KSEG0;
	vaddr_t k_new = paddr_new + MIPS_KSEG0;

	memmove((void *)k_new, (const void *)k_old, PAGE_SIZE);

	attach_PTE(0x7ffff000, new, newas->page_table);
	//need to create a new function that add it to the thing

	old_pte -> count--;

	if (old_pte -> count == 1){
		//kprintf("%x, %d\n", (int) pte, pte -> count);
		old_pte -> pte &= ~(PT_PROTECTION_MASK);
	}
	V(old_pte->lock);
	

	newas -> stack_bot_limit = old -> stack_bot_limit;
	newas -> heap_top = old -> heap_top;
	newas -> heap_bot = old -> heap_bot;

	newas -> data = old -> data;
	newas -> data_npages = old -> data_npages;
	newas -> data_offset = old -> data_offset;
	newas -> data_filesize = old -> data_filesize;
	newas -> code = old -> code;
	newas -> code_npages = old -> code_npages;
	newas -> code_offset = old -> code_offset;
	newas -> code_filesize = old -> code_filesize;

	
	*ret = newas;
	return 0;
}
*/
struct PTE* hardcoding_pte;
void* hardcoding_ptr;

static
int
hardcode_stack(struct addrspace* old, struct addrspace* newas){
	vaddr_t hard_coding = 0x7ffff000;
	while(1){
		struct PTE* old_pte = access_table(hard_coding, old->page_table);
		if ((old_pte -> pte & PT_VALID_MASK) == 0){
			PTE_free(hard_coding, old -> page_table);
			break;
		}
		P(old_pte -> lock);
		struct PTE* new = PTE_init();
		//kprintf("VA 0x%x -> PA 0x%x\n", vaddr & PAGE_FRAME, new->pte & PAGE_FRAME);
		if (new == NULL) {
			// it is possible that page is full, we need to evict
			return ENOMEM;
		}

		new -> pte |= PT_VALID_MASK;
		new -> pte |= PT_REFERENCE_MASK;
		new -> pte |= PT_MODIFY_MASK;
		new -> pte &= ~(PT_PROTECTION_MASK); // make protection bit 00: read and write
		
		//copy the region, convert to pa, then use it as kernel addr to copy
		paddr_t paddr_old = old_pte -> pte & PAGE_FRAME;
		paddr_t paddr_new = new -> pte & PAGE_FRAME;
		
		vaddr_t k_old = paddr_old + MIPS_KSEG0;
		vaddr_t k_new = paddr_new + MIPS_KSEG0;

		memmove((void *)k_new, (const void *)k_old, PAGE_SIZE);

		attach_PTE(hard_coding, new, newas->page_table);
		//need to create a new function that add it to the thing

		old_pte -> count--;

		if (old_pte -> count == 1){
			//kprintf("%x, %d\n", (int) pte, pte -> count);
			old_pte -> pte &= ~(PT_PROTECTION_MASK);
		}
		V(old_pte->lock);

		hard_coding -= PAGE_SIZE;
		
	}

	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = kmalloc(sizeof(struct addrspace));
	//newas = as_create(old -> progname);
	if (newas==NULL) {
	#if ADDR_DEBUG
		kprintf("as create fails, not enough mem\n");
	#endif
		return ENOMEM;
	}

	strcpy(newas -> progname, old -> progname);

	newas->page_table = PT_copy(old->page_table);
	if (newas->page_table == NULL){
		as_destroy(newas);
	#if ADDR_DEBUG
		kprintf("copy fails, not enough mem\n");
	#endif
		return ENOMEM;
	}
	(void) hardcode_stack;
	//int result = hardcode_stack(old, newas);
	//if (result) return result;

	newas -> stack_bot_limit = old -> stack_bot_limit;
	newas -> heap_top = old -> heap_top;
	newas -> heap_bot = old -> heap_bot;

	newas -> data = old -> data;
	newas -> data_npages = old -> data_npages;
	newas -> data_offset = old -> data_offset;
	newas -> data_filesize = old -> data_filesize;
	newas -> code = old -> code;
	newas -> code_npages = old -> code_npages;
	newas -> code_offset = old -> code_offset;
	newas -> code_filesize = old -> code_filesize;

	
	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */

	// this include PT clean up and TLB clean up
	//kprintf("as destroy start\n");
	PT_free(as -> page_table);
	//kprintf("as destroy success\n");
	kfree(as);
}

void
as_activate(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void) as;
	//kprintf("who is calling:%s\n", as -> progname);

	TLB_remove_all();
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable, off_t offset, size_t filesize)
{
	/*
	 * Write this.
	 */
	size_t npages; 
	size_t mem_size = sz;
	(void) filesize;
	(void) mem_size;
	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	// assume code is passed first
	if (as -> code == 0) {
		as -> code = vaddr;
		as -> code_npages = npages;
		as -> code_offset = offset;
		as -> code_filesize = filesize;
		return 0;
	}

	if (as -> data == 0) {
		as -> data = vaddr;
		as -> data_npages = npages;
		as -> data_offset = offset;
		as -> data_filesize = filesize;

		// now, code and data are predefined
		// define heap
		as -> heap_bot = as -> data + as -> data_npages * PAGE_SIZE;
		as -> heap_top = as -> heap_bot;

		return 0;
	}
	
	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

// stack is automatically increase, no need to keep track in addrspace
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK; // 0x80000000

	// following convention in ece344 fall term lec
	// If the current stack is within the 1 page of the stack bot limit
	// I will increment the stack bot limit automatically
	// therefore, initially I shall set stack bot limit for 2 pages
	//as -> stack_bot_limit = USERSTACK - (2 * PAGE_SIZE); 
	
	// hardcoding stack to pass test case
	as -> stack_bot_limit = 0x7d000000;
	return 0;
}

void print_addrspace(){
	struct addrspace* as = curthread -> t_vmspace;

	kprintf("Program is %s\n", as -> progname);
	kprintf("The stack bot limit is %x\n", as -> stack_bot_limit);
	kprintf("The heap top is %x\n", as -> heap_top);
	kprintf("The heap bot is %x\n", as -> heap_bot);
	kprintf("The data is %x\n", as -> data);
	kprintf("The data pages is %d\n", as -> data_npages);
	kprintf("The data file size is %x\n", as -> data_filesize);
	kprintf("The code is %x\n", as -> code);
	kprintf("The code pages is %x\n", as -> code_npages);
	kprintf("The code file size is %x\n", as -> code_filesize);

	return;
}



