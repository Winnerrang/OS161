#ifndef _SWAP_H_
#define _SWAP_H_

#include <vnode.h>
#include <synch.h>

struct Swap{
    struct vnode* v;
    struct bitmap* bit_map;
    u_int32_t bitmap_size;
    struct semaphore* bitmap_lock;
    u_int32_t LRU_arm;
    u_int32_t swap_disk_index;  //point to avaiable disk in bitmap
};

extern struct semaphore* destroy_lock;
extern struct Swap* swap;
extern int swaptest;
extern struct semaphore* disk_lock;
extern struct semaphore* file_lock;

void swap_bootstrap();
int LRU_Clock();
int swap_out(u_int32_t index);
int swap_in(struct PTE* pte, u_int32_t index);

u_int32_t
bitmap_write();

void
bitmap_remove(u_int32_t index);

#endif

