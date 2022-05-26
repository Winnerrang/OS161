/*
 * Core thread system.
 */
#include <types.h>
#include <lib.h>
#include <kern/errno.h>
#include <array.h>
#include <machine/spl.h>
#include <machine/pcb.h>
#include <thread.h>
#include <curthread.h>
#include <scheduler.h>
#include <addrspace.h>
#include <vnode.h>
#include "opt-synchprobs.h"

/* States a thread can be in. */
typedef enum {
	S_RUN,
	S_READY,
	S_SLEEP,
	S_ZOMB,
} threadstate_t;

/* Global variable for the thread currently executing at any given time. */
struct thread *curthread;

/* Table of sleeping threads. */
static struct array *sleepers;

/* List of dead threads to be disposed of. */
static struct array *zombies;

/* Total number of outstanding threads. Does not count zombies[]. */
static int numthreads;

// process table
struct process* process_table[NUM_PROCESS];
/*
 * Returns number of active threads
 */

int
thread_count(void)
{
	return numthreads;
}

/*
 * Create a thread. This is used both to create the first thread's 
 * thread structure and to create subsequent threads.
 */

static
struct thread *
thread_create(const char *name)
{
	struct thread *thread = kmalloc(sizeof(struct thread));
	if (thread==NULL) {
		return NULL;
	}
	thread->t_name = kstrdup(name);
	if (thread->t_name==NULL) {
		kfree(thread);
		return NULL;
	}
	thread->t_sleepaddr = NULL;
	thread->t_stack = NULL;
	
	thread->t_vmspace = NULL;

	thread->t_cwd = NULL;
	
	// If you add things to the thread structure, be sure to initialize
	// them here.
	thread->self_pid = -1;
	
	return thread;
}

/*
 * Destroy a thread.
 *
 * This function cannot be called in the victim thread's own context.
 * Freeing the stack you're actually using to run would be... inadvisable.
 */
static
void
thread_destroy(struct thread *thread)
{
	assert(thread != curthread);

	// If you add things to the thread structure, be sure to dispose of
	// them here or in thread_exit.

	// These things are cleaned up in thread_exit.
	assert(thread->t_vmspace==NULL);
	assert(thread->t_cwd==NULL);
	
	if (thread->t_stack) {
		kfree(thread->t_stack);
	}

	kfree(thread->t_name);
	kfree(thread);
}


/*
 * Remove zombies. (Zombies are threads/processes that have exited but not
 * been fully deleted yet.)
 */
static
void
exorcise(void)
{
	int i, result;

	assert(curspl>0);
	
	for (i=0; i<array_getnum(zombies); i++) {
		struct thread *z = array_getguy(zombies, i);
		assert(z!=curthread);
		thread_destroy(z);
	}
	result = array_setsize(zombies, 0);
	/* Shrinking the array; not supposed to be able to fail. */
	assert(result==0);
}

/*
 * Kill all sleeping threads. This is used during panic shutdown to make 
 * sure they don't wake up again and interfere with the panic.
 */
static
void
thread_killall(void)
{
	int i, result;

	assert(curspl>0);

	/*
	 * Move all sleepers to the zombie list, to be sure they don't
	 * wake up while we're shutting down.
	 */

	for (i=0; i<array_getnum(sleepers); i++) {
		struct thread *t = array_getguy(sleepers, i);
		kprintf("sleep: Dropping thread %s\n", t->t_name);

		/*
		 * Don't do this: because these threads haven't
		 * been through thread_exit, thread_destroy will
		 * get upset. Just drop the threads on the floor,
		 * which is safer anyway during panic.
		 *
		 * array_add(zombies, t);
		 */
	}

	result = array_setsize(sleepers, 0);
	/* shrinking array: not supposed to fail */
	assert(result==0);
}

/*
 * Shut down the other threads in the thread system when a panic occurs.
 */
void
thread_panic(void)
{
	assert(curspl > 0);

	thread_killall();
	scheduler_killall();
}

/*
 * Thread initialization.
 */
struct thread *
thread_bootstrap(void)
{	
	process_bootstrap();

	struct thread *me;

	/* Create the data structures we need. */
	sleepers = array_create();
	if (sleepers==NULL) {
		panic("Cannot create sleepers array\n");
	}

	zombies = array_create();
	if (zombies==NULL) {
		panic("Cannot create zombies array\n");
	}
	
	/*
	 * Create the thread structure for the first thread
	 * (the one that's already running)
	 */
	me = thread_create("<boot/menu>");
	
	if (me==NULL) {
		panic("thread_bootstrap: Out of memory\n");
	}
	
	process_table[0] -> main_thread = me;

	me -> self_pid = 0;
	
	/*
	 * Leave me->t_stack NULL. This means we're using the boot stack,
	 * which can't be freed.
	 */

	/* Initialize the first thread's pcb */
	md_initpcb0(&me->t_pcb);

	/* Set curthread */
	curthread = me;

	/* Number of threads starts at 1 */
	numthreads = 1;

	/* Done */
	return me;
}

/*
 * Thread final cleanup.
 */
void
thread_shutdown(void)
{
	array_destroy(sleepers);
	sleepers = NULL;
	array_destroy(zombies);
	zombies = NULL;
	// Don't do this - it frees our stack and we blow up
	//thread_destroy(curthread);
}

/*
 * Create a new thread based on an existing one.
 * The new thread has name NAME, and starts executing in function FUNC.
 * DATA1 and DATA2 are passed to FUNC.
 */
 
int
thread_fork(const char *name, 
	    void *data1, unsigned long data2,
	    void (*func)(void *, unsigned long),
	    struct thread **ret)
{
	struct thread *newguy;
	int s, result;

	/* Allocate a thread */
	newguy = thread_create(name);
	if (newguy==NULL) {
		return ENOMEM;
	}

	int pid = process_create(newguy);
	if (pid == -1) return ENOMEM;
	
	/* Allocate a stack */
	newguy->t_stack = kmalloc(STACK_SIZE);
	if (newguy->t_stack==NULL) {
		kfree(newguy->t_name);
		kfree(newguy);
		process_destroy(pid);

		//this is only situation that this entry had a process before
		//but it will consider as not exist. We want to do that
		//becuase sending the crash code is better than sending a wrong
		//code
		process_table[pid]->self_pid = -1;

		return ENOMEM;
	}

	/* stick a magic number on the bottom end of the stack */
	newguy->t_stack[0] = 0xae;
	newguy->t_stack[1] = 0x11;
	newguy->t_stack[2] = 0xda;
	newguy->t_stack[3] = 0x33;

	/* Inherit the current directory */
	if (curthread->t_cwd != NULL) {
		VOP_INCREF(curthread->t_cwd);
		newguy->t_cwd = curthread->t_cwd;
	}

	/* Set up the pcb (this arranges for func to be called) */
	md_initpcb(&newguy->t_pcb, newguy->t_stack, data1, data2, func);

	/* Interrupts off for atomicity */
	s = splhigh();

	/*
	 * Make sure our data structures have enough space, so we won't
	 * run out later at an inconvenient time.
	 */
	result = array_preallocate(sleepers, numthreads+1);
	if (result) {
		goto fail;
	}
	result = array_preallocate(zombies, numthreads+1);
	if (result) {
		goto fail;
	}

	/* Do the same for the scheduler. */
	result = scheduler_preallocate(numthreads+1);
	if (result) {
		goto fail;
	}

	/* Make the new thread runnable */
	result = make_runnable(newguy);
	if (result != 0) {
		goto fail;
	}

	/*
	 * Increment the thread counter. This must be done atomically
	 * with the preallocate calls; otherwise the count can be
	 * temporarily too low, which would obviate its reason for
	 * existence.
	 */
	numthreads++;

	/* Done with stuff that needs to be atomic */
	splx(s);

	/*
	 * Return new thread structure if it's wanted.  Note that
	 * using the thread structure from the parent thread should be
	 * done only with caution, because in general the child thread
	 * might exit at any time.
	 */
	if (ret != NULL) {
		*ret = newguy;
	}

	return 0;

 fail:
	splx(s);
	if (newguy->t_cwd != NULL) {
		VOP_DECREF(newguy->t_cwd);
	}
	kfree(newguy->t_stack);
	kfree(newguy->t_name);
	kfree(newguy);

	return result;
}

/*
 * Suspend execution of curthread until thread terminates. 
 * Return zero on success, EDEADLK if deadlock would occur.
 */
int thread_join(struct thread * thread)
{
        // if I don't update this thread_join
		// the boot/menu thread may print new prompt line before the 
		// user program has exited. Need same mechanism for waitpid
		int pid = thread -> self_pid;

		// same reasoning and coding in syscall.c waitpid
		P(process_table[pid]->lock);
		P(mutex);
		
		process_table[pid]->self_pid = -1; // reap
		
        V(mutex);
		V(process_table[pid]->lock);
		
        return 0;
}

/*
 * High level, machine-independent context switch code.
 */
static
void
mi_switch(threadstate_t nextstate)
{
	struct thread *cur, *next;
	int result;
	
	/* Interrupts should already be off. */
	assert(curspl>0);

	if (curthread != NULL && curthread->t_stack != NULL) {
		/*
		 * Check the magic number we put on the bottom end of
		 * the stack in thread_fork. If these assertions go
		 * off, it most likely means you overflowed your stack
		 * at some point, which can cause all kinds of
		 * mysterious other things to happen.
		 */
		assert(curthread->t_stack[0] == (char)0xae);
		assert(curthread->t_stack[1] == (char)0x11);
		assert(curthread->t_stack[2] == (char)0xda);
		assert(curthread->t_stack[3] == (char)0x33);
	}
	
	/* 
	 * We set curthread to NULL while the scheduler is running, to
	 * make sure we don't call it recursively (this could happen
	 * otherwise, if we get a timer interrupt in the idle loop.)
	 */
	if (curthread == NULL) {
		return;
	}
	cur = curthread;
	curthread = NULL;

	/*
	 * Stash the current thread on whatever list it's supposed to go on.
	 * Because we preallocate during thread_fork, this should not fail.
	 */

	if (nextstate==S_READY) {
		result = make_runnable(cur);
	}
	else if (nextstate==S_SLEEP) {
		/*
		 * Because we preallocate sleepers[] during thread_fork,
		 * this should never fail.
		 */
		result = array_add(sleepers, cur);
	}
	else {
		assert(nextstate==S_ZOMB);
		result = array_add(zombies, cur);
	}
	assert(result==0);

	/*
	 * Call the scheduler (must come *after* the array_adds)
	 */

	next = scheduler();

	/* update curthread */
	curthread = next;
	
	/* 
	 * Call the machine-dependent code that actually does the
	 * context switch.
	 */
	md_switch(&cur->t_pcb, &next->t_pcb);
	
	/*
	 * If we switch to a new thread, we don't come here, so anything
	 * done here must be in mi_threadstart() as well, or be skippable,
	 * or not apply to new threads.
	 *
	 * exorcise is skippable; as_activate is done in mi_threadstart.
	 */

	// the new thread will come to here and reap the zombie old thread
	exorcise();

	// activate mem space as a new process has been switched in
	if (curthread->t_vmspace) {

		as_activate(curthread->t_vmspace);
	}
}

/*
 * Cause the current thread to exit.
 *
 * We clean up the parts of the thread structure we don't actually
 * need to run right away. The rest has to wait until thread_destroy
 * gets called from exorcise().
 */
void
thread_exit(void)
{
	if (curthread->t_stack != NULL) {
		/*
		 * Check the magic number we put on the bottom end of
		 * the stack in thread_fork. If these assertions go
		 * off, it most likely means you overflowed your stack
		 * at some point, which can cause all kinds of
		 * mysterious other things to happen.
		 */
		assert(curthread->t_stack[0] == (char)0xae);
		assert(curthread->t_stack[1] == (char)0x11);
		assert(curthread->t_stack[2] == (char)0xda);
		assert(curthread->t_stack[3] == (char)0x33);
	}

	splhigh();

	if (curthread->t_vmspace) {
		/*
		 * Do this carefully to avoid race condition with
		 * context switch code.
		 */
		struct addrspace *as = curthread->t_vmspace;
		curthread->t_vmspace = NULL;
		as_destroy(as);
	}

	if (curthread->t_cwd) {
		VOP_DECREF(curthread->t_cwd);
		curthread->t_cwd = NULL;
	}

	assert(numthreads>0);
	numthreads--;
	mi_switch(S_ZOMB);

	panic("Thread came back from the dead!\n");
}

/*
 * Yield the cpu to another process, but stay runnable.
 */
void
thread_yield(void)
{
	int spl = splhigh();

	/* Check sleepers just in case we get here after shutdown */
	assert(sleepers != NULL);

	mi_switch(S_READY);
	splx(spl);
}

/*
 * Yield the cpu to another process, and go to sleep, on "sleep
 * address" ADDR. Subsequent calls to thread_wakeup with the same
 * value of ADDR will make the thread runnable again. The address is
 * not interpreted. Typically it's the address of a synchronization
 * primitive or data structure.
 *
 * Note that (1) interrupts must be off (if they aren't, you can
 * end up sleeping forever), and (2) you cannot sleep in an 
 * interrupt handler.
 */
void
thread_sleep(const void *addr)
{
	// may not sleep in an interrupt handler
	assert(in_interrupt==0);
	
	curthread->t_sleepaddr = addr;
	mi_switch(S_SLEEP);
	curthread->t_sleepaddr = NULL;
}

/*
 * Wake up one or more threads who are sleeping on "sleep address"
 * ADDR.
 */
void
thread_wakeup(const void *addr)
{
	int i, result;
	
	// meant to be called with interrupts off
	assert(curspl>0);
	
	// This is inefficient. Feel free to improve it.
	
	for (i=0; i<array_getnum(sleepers); i++) {
		struct thread *t = array_getguy(sleepers, i);
		if (t->t_sleepaddr == addr) {
			
			// Remove from list
			array_remove(sleepers, i);
			
			// must look at the same sleepers[i] again
			i--;

			/*
			 * Because we preallocate during thread_fork,
			 * this should never fail.
			 */
			result = make_runnable(t);
			assert(result==0);
		}
	}
}

void
thread_single_wakeup(const void *addr)
{
	int i, result;
	
	// meant to be called with interrupts off
	assert(curspl>0);
	
	// This is inefficient. Feel free to improve it.
	
	for (i=0; i<array_getnum(sleepers); i++) {
		struct thread *t = array_getguy(sleepers, i);
		if (t->t_sleepaddr == addr) {
			
			// Remove from list
			array_remove(sleepers, i);
			
			// must look at the same sleepers[i] again
			i--;

			/*
			 * Because we preallocate during thread_fork,
			 * this should never fail.
			 */
			result = make_runnable(t);
			assert(result==0);
			return;
		}
	}
}

/*
 * Return nonzero if there are any threads who are sleeping on "sleep address"
 * ADDR. This is meant to be used only for diagnostic purposes.
 */
int
thread_hassleepers(const void *addr)
{
	int i;
	
	// meant to be called with interrupts off
	assert(curspl>0);
	
	for (i=0; i<array_getnum(sleepers); i++) {
		struct thread *t = array_getguy(sleepers, i);
		if (t->t_sleepaddr == addr) {
			return 1;
		}
	}
	return 0;
}

/*
 * New threads actually come through here on the way to the function
 * they're supposed to start in. This is so when that function exits,
 * thread_exit() can be called automatically.
 */
void
mi_threadstart(void *data1, unsigned long data2, 
	       void (*func)(void *, unsigned long))
{
	/* If we have an address space, activate it */
	if (curthread->t_vmspace) {
		as_activate(curthread->t_vmspace);
	}

	/* Enable interrupts */
	spl0();

#if OPT_SYNCHPROBS
	/* Yield a random number of times to get a good mix of threads */
	{
		int i, n;
		n = random()%161 + random()%161;
		for (i=0; i<n; i++) {
			thread_yield();
		}
	}
#endif
	
	/* Call the function */
	func(data1, data2);

	/* Done. */
	thread_exit();
}


struct semaphore* mutex;
int start_pindex;
/*
struct semaphore* lock_inquire_table;

void process_bootstrap(){
	// mutex for accessing process table
	mutex = sem_create("mutex", 1);
	lock_inquire_table = sem_create("lock_inquire_table", 1);
	
	// only one thread, no need for synchronization
	
	// for the first process, set both pid and parent pid to 0
	process_table[0] = kmalloc(sizeof(struct process));
	process_table[0] -> self_pid = 0;
	process_table[0] -> parent_pid = 0;
	process_table[0] -> lock = sem_create("sem", 1);


	// rest of the process are set to null
	int i;
	for (i = 1; i < NUM_PROCESS; i ++){
		process_table[i] = NULL;
	}


	
}*/

void process_bootstrap(){
	// mutex for accessing process table
	mutex = sem_create("mutex", 1);
	start_pindex = 1;
	
	// only one thread, no need for synchronization
	
	// for the first process, set both pid and parent pid to 0
	process_table[0] = kmalloc(sizeof(struct process));
	process_table[0] -> self_pid = 0;
	process_table[0] -> parent_pid = 0;
	process_table[0] -> valid = 1;
	process_table[0] -> lock = sem_create("sem", 1);
	assert(process_table[0] -> lock);

	// rest of the process are set to null
	int i;
	for (i = 1; i < NUM_PROCESS; i ++){
		process_table[i] = kmalloc(sizeof(struct process));
		assert(process_table[i]);
		process_table[i] -> self_pid = -1;
		process_table[i] -> parent_pid = -1;
		process_table[i] -> lock = sem_create("sem", 1);
		assert(process_table[i] -> lock);
		process_table[i] -> valid = 0;
		process_table[i] -> exitcode = -1;
	}


	
}

int process_table_full(){
	P(mutex);
	int i;
	for (i = 0; i < NUM_PROCESS; i ++){
		//change this back to NULL if we do revision
		if (process_table[i] -> valid  == 0) {
			V(mutex);
			return 0;
		}
	} 
	V(mutex);
	return -1;

}


void process_table_print(int num_entry){
	int i;
	for (i = 0; i < num_entry; i ++){
		kprintf("self pid is %d, parent pid is %d\n", process_table[i]->self_pid, process_table[i]->parent_pid);
	}
}

/*
int process_create(struct thread* new_thread){
	P(mutex);
	int i;
	for (i = 1; i < NUM_PROCESS; i ++){

		if (process_table[i] == NULL){
			// find the empty process slot, initialize it
			process_table[i] = kmalloc(sizeof(struct process));
			if (process_table[i] == NULL) return -2;

			process_table[i] -> self_pid = i;
			process_table[i] -> parent_pid = curthread -> self_pid;
			process_table[i] -> lock = sem_create("sem", 1);
			if (process_table[i] -> lock == NULL){
				kfree(process_table[i]);
				process_table[i] = NULL;
				return -2;
			}
			//P(process_table[i] -> lock);
			//process_table[i] -> run = 1;
			process_table[i] -> main_thread = new_thread;
			process_table[i] -> valid = 1;
			process_table[i] -> exitcode = 0; 
			
			//(PROBLEM???)there might be a synchronization problem here
			//the new_thread exit so fast and it becomes zombie and new_thread
			// might already got free
			new_thread -> self_pid = i;
			P(process_table[i] -> lock);

			V(mutex);
			return i; 
				
		}
		
	}
	
	// no empty process slot is found, return -1
	V(mutex);
	kprintf("Should not be here\n");
	//panic("WRONG WRONG WRONG");
	return -1;
}
*/

int process_create(struct thread* new_thread){
	P(mutex);
	//starting point =1
	int i = start_pindex;
	int end;
	if (i == 1){
		end = NUM_PROCESS - 1;
	}else{
		end = i - 1;
	}
	while (i != end){
		if (process_table[i] -> self_pid  == -1){
			P(process_table[i] -> lock);
			process_table[i] -> self_pid = i;
			process_table[i] -> parent_pid = curthread -> self_pid;
			process_table[i] -> main_thread = new_thread;
			process_table[i] -> valid = 1;
			
			new_thread -> self_pid = i;
			
			start_pindex = i + 1;
			if (start_pindex == NUM_PROCESS){
				start_pindex = 1;
			}
			//startingpoing += 1;
			//if (<)
			//	startingpoint
			V(mutex);
			return i; 
				
		}

		i++;
		if (i == NUM_PROCESS) i = 1;
	}
	
	
	// no empty process slot is found, return -1
	V(mutex);
	kprintf("Should not be here\n");
	//panic("WRONG WRONG WRONG");
	return -1;
}

/*
void process_destroy(int pid){
	P(mutex);
	
	//unlock first if someone is waiting
	V(process_table[pid]->lock);
	sem_destroy(process_table[pid] -> lock);
	kfree(process_table[pid]);
	process_table[pid] = NULL;

	V(mutex);
	
	return;
	
}*/

void process_destroy(int pid){
	P(mutex);
	
	process_table[pid] -> valid = 0;

	V(mutex);
	
	return;
	
}
