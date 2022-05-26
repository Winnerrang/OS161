/*
 * Synchronization primitives.
 * See synch.h for specifications of the functions.
 */

#include <types.h>
#include <lib.h>
#include <synch.h>
#include <thread.h>
#include <curthread.h>
#include <machine/spl.h>
//#include <coremap.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *namearg, int initial_count)
{
	struct semaphore *sem;

	assert(initial_count >= 0);

	sem = kmalloc(sizeof(struct semaphore));
	if (sem == NULL) {
		return NULL;
	}
	
	//The strdup() and strndup() functions are used to duplicate a string. 
	// returns a pointer to a null-terminated string
	sem->name = kstrdup(namearg);
	if (sem->name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->count = initial_count;
	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	spl = splhigh();
	assert(thread_hassleepers(sem)==0);
	splx(spl);

	/*
	 * Note: while someone could theoretically start sleeping on
	 * the semaphore after the above test but before we free it,
	 * if they're going to do that, they can just as easily wait
	 * a bit and start sleeping on the semaphore after it's been
	 * freed. Consequently, there's not a whole lot of point in 
	 * including the kfrees in the splhigh block, so we don't.
	 */

	kfree(sem->name);
	kfree(sem);
}

void 
P(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	//assert(in_interrupt==0);

	spl = splhigh();
	while (sem->count==0) {
		thread_sleep(sem);
	}
	assert(sem->count>0);
	sem->count--;
	splx(spl);
}

void
V(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	if (sem->count == 1) {
		//panic(">?>\n");
		kprintf("Increament lock to 2\n");
		kprintf("lock name: %s\n", sem->name);
		//int i;
		//for(i = 0; i < (int)coremap_size; i++){
		//	if (coremap.coremap_array[i].pte != NULL && coremap.coremap_array[i].pte->lock == sem){
		//		kprintf("Trying to V the lock that is in coremap index: %d\n", i);
		//	}
		//}
		kprintf("the lock is not in the coremap\n");
	}
	spl = splhigh();
	sem->count++;
	assert(sem->count>0);
	thread_wakeup(sem);
	splx(spl);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(struct lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->name = kstrdup(name);
	if (lock->name == NULL) {
		kfree(lock);
		return NULL;
	}
	
	// add stuff here as needed
	lock->holder = NULL;
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);
	assert(lock->holder == NULL);
	// add stuff here as needed
	int spl;

	spl = splhigh();
	assert(thread_hassleepers(lock)==0);
	splx(spl);

	kfree(lock->name);
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
	int spl;
	// Write this
	spl = splhigh();
	while(lock->holder != NULL){
		thread_sleep(lock);
	}
	lock->holder = curthread;
	splx(spl);
}

void
lock_release(struct lock *lock)
{
	// Write this
	assert(lock_do_i_hold(lock));
	int spl;

	spl = splhigh();
	lock->holder = NULL;
	thread_wakeup(lock);
	splx(spl);
}

int
lock_do_i_hold(struct lock *lock)
{
	// Write this

	int iAmHolder, spl;

	spl = splhigh();
	iAmHolder = lock->holder == curthread;
	splx(spl);

	return iAmHolder;    
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(struct cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->name = kstrdup(name);
	if (cv->name==NULL) {
		kfree(cv);
		return NULL;
	}
	
	// add stuff here as needed
	
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	// add stuff here as needed
	
	kfree(cv->name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);
	
	// Write this
	int spl;

	spl = splhigh();
	lock_release(lock);
	thread_sleep(cv);
	lock_acquire(lock);
	splx(spl);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	int spl;
	
	spl = splhigh();
	thread_single_wakeup(cv);
	splx(spl);

	(void)lock;  // suppress warning until code gets written
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	int spl;
	
	spl = splhigh();
	thread_wakeup(cv);
	splx(spl);

	(void)lock;  // suppress warning until code gets written
}





