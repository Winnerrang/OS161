#ifndef _COREMAP_
#define _COREMAP_


#include <machine/types.h>
#include <machine/ktypes.h>
#include <pt.h>

//define mask for auxilary bits
#define REFERENCE_BIT_MASK  0x80000000 // useless
#define VALID_BIT_MASK 0x40000000
#define MODIFIED_BIT_MASK 0x20000000 // useless
#define KERNEL_BIT_MASK 0x10000000
#define RESERVATION_BIT_MASK 0x08000000

extern int offset;
extern u_int32_t coremap_size;
extern u_int32_t used_page;
extern struct Coremap coremap;

struct Coremap_Entry{
    struct PTE* pte;
    u_int32_t auxilary_bits;
};

struct Coremap{
    struct Coremap_Entry* coremap_array;
    struct semaphore* coremap_lock;
};

// initialize the coremap in vm initialization
// allocate enough entries for avaliable memory pages
void coremap_initialization();

// coremap utility function
void coremap_lock_bootstrap();
int coremap_avaliable_space();


// assume always allocate or free 1 page

// passin the pte for coremap to backtrace, pass NULL for kernel
// return the PFN 
paddr_t get_ppage(struct PTE* pte);

paddr_t get_ppage_specific(struct PTE* pte, u_int32_t index);
// passin paddr to let the coremap find the corresponding location - retrieve vpn
// return the pte if we will perform swapping, need this pte to replace the new page info
// if we are simply free the page long ptr or as_destroy, nothing needs to be done for the return value

struct PTE* free_ppage(paddr_t paddr);

//lower the reservation bit in certain coremap entry
void unreservated(u_int32_t index);
void reserve(u_int32_t index, int);


#endif

