#ifndef _PT_
#define _PT_

#include <machine/types.h>
#include <machine/ktypes.h>

#define PT_SIZE 32
#define PT_PFN_MASK 0xfffff000
#define PT_VALID_MASK   0x00000800  //indicate whether tlb index bits are valid
#define PT_REFERENCE_MASK   0x00000400
#define PT_MODIFY_MASK 0x00000200
// 11 is read only, 00 is read and write, 01 is executable
// protection bit will be set to read temp (which will set TLB read only)
// by doing so, the modify bit can be actually implemented; otherwise TLB hit on write
// will not change the modify bit to 1.
#define PT_PROTECTION_MASK  0x00000180

//#define PT_RESIDENT_MASK    0x00000060
#define PT_SWAP_MASK 0x00000040

// stack is 3
// heap is 2
// data is 1
// code is 0
#define PT_REGION_MASK  0x00000018

#define PT_FIRST_MASK   0xf8000000
#define PT_SEONCD_MASK  0x07c00000
#define PT_THIRD_MASK   0x003e0000
#define PT_FOURTH_MASK  0x0001f000

#define GET_PROTECTION(pte) (((pte) & PT_PROTECTION_MASK) >> 7)
#define SET_PROTECTION(pte, protection) (((protection) << 7) | ((pte) & (~PT_PROTECTION_MASK)))
#define READ_ONLY 3
#define READ_WRITE 0
#define EXECUTABLE 1
#define READ_TEMP 2

#define STACK_REGION 3
#define HEAP_REGION 2
#define DATA_REGION 1
#define CODE_REGION 0
#define GET_REGION(pte) (((pte) & PT_REGION_MASK) >> 3)
#define SET_REGION(pte, region) (((region) << 3) | ((pte) & (~PT_REGION_MASK)))

struct PTE{
    struct semaphore* lock;
    u_int32_t pte;
    int count; //a variable that indicate how many table points to this entry
    int bitmap_index;
};

struct PT{
    void ** first_level;
};


// initialize the pte
struct PTE* PTE_init();
struct PTE* PTE_free(vaddr_t vaddr, struct PT* page_table);
void attach_PTE(vaddr_t vaddr, struct PTE* pte, struct PT* page_table);

// allocate the first level of PT
// return the allocated ptr of an array of ptr
struct PT* PT_init();

//this function will copy everything from the page table, and set all
//the page table entry inside to read only
struct PT* PT_copy(struct PT* old);

//helper function for copy table
void** copy_table(void** old, int level, vaddr_t);

void PT_free(struct PT*);
void free_table(void** pt, int level);

// PT utility
void** generate_table();
struct PTE* access_table(vaddr_t vaddr, struct PT* page_table);

#endif
