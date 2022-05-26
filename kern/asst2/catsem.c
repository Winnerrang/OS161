/*
 * catsem.c
 *
 * Please use SEMAPHORES to solve the cat syncronization problem in 
 * this file.
 */


/*
 * 
 * Includes
 *
 */

#include <types.h>
#include <lib.h>
#include <test.h>
#include <thread.h>
#include <synch.h>
#include "catmouse.h"

struct semaphore* mutex; // just a lock
struct semaphore* mutex2;
struct semaphore* mice_eat; 
struct semaphore* bowl;

int cat_count = 0;
int bowl_count = 0;

//record bowl status
int cat_bowl[NCATS];
int mouse_bowl[NMICE];
// 0 is free, 1 is being used
int bowl1 = 0;
int bowl2 = 0;
/*
 * 
 * Function Definitions
 * 
 */

/*
 * catsem()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long catnumber: holds the cat identifier from 0 to NCATS - 1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using semaphores.
 *
 */

static
void
catsem(void * unusedpointer, 
       unsigned long catnumber)
{       
    (void) unusedpointer;

	int i;
	for (i = 0; i < NMEALS; i ++){
		
		P(bowl); // if there's bowl empty, then go eating
		P(mutex);
		//start to eat
		cat_count++;

		if (!bowl1){
			bowl1 = 1;
			cat_bowl[catnumber] = 1;
		}else{
			bowl2 = 1;
			cat_bowl[catnumber] = 2;
		}
		
		if (cat_count == 1) {
			// if cat start to eat, then no mouse can eat
			P(mice_eat);
		}
		V(mutex); 

		//P(mutex);
		catmouse_eat("cat", catnumber, cat_bowl[catnumber], i);
		//V(mutex);
		
		P(mutex);
		// finish eating once
		cat_count --;
		
		if (cat_bowl[catnumber] == 1) bowl1 = 0;
		else bowl2 = 0;

		if (cat_count == 0){
			// now no cat is eating, its the mouse's chance to eat
			V(mice_eat);
		}
		V(mutex);
		V(bowl);
	}
}
        

/*
 * mousesem()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long mousenumber: holds the mouse identifier from 0 to 
 *              NMICE - 1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using semaphores.
 *
 */

static
void
mousesem(void * unusedpointer, 
         unsigned long mousenumber)
{      
    (void) unusedpointer;

	int i;
    for (i = 0; i < NMEALS; i ++){
		// mice start eating
		P(mice_eat);
		P(bowl); // if there's bowl empty, then go eating
		P(mutex);
		if (!bowl1){
			bowl1 = 1;
			mouse_bowl[mousenumber] = 1;
		}else{
			bowl2 = 1;
			mouse_bowl[mousenumber] = 2;
		}
		V(mutex);

		
				
		// when a cat is eating, no one can disrupt them
		catmouse_eat("mouse", mousenumber, mouse_bowl[mousenumber], i);
		
		

		// mice finish eating
		P(mutex);
		if (mouse_bowl[mousenumber] == 1) bowl1 = 0;
		else bowl2 = 0;
		V(mutex);
		V(bowl);
		V(mice_eat);
	}
}


/*
 * catmousesem()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up catsem() and mousesem() threads.  Change this 
 *      code as necessary for your solution.
 */

int
catmousesem(int nargs,
            char ** args)
{
        int index, error;
		mutex = sem_create("mutex", 1);
		mutex2 = sem_create("mutex2", 1);
		mice_eat = sem_create("mice eat", 1);
        bowl = sem_create("bowl", NFOODBOWLS);
        /*
         * Start NCATS catsem() threads.
         */

        for (index = 0; index < NCATS; index++) {
           
                error = thread_fork("catsem Thread", 
                                    NULL, 
                                    index, 
                                    catsem, 
                                    NULL
                                    );
                
                /*
                 * panic() on error.
                 */

                if (error) {
                 
                        panic("catsem: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }
        
        /*
         * Start NMICE mousesem() threads.
         */

        for (index = 0; index < NMICE; index++) {
   
                error = thread_fork("mousesem Thread", 
                                    NULL, 
                                    index, 
                                    mousesem, 
                                    NULL
                                    );
                
                /*
                 * panic() on error.
                 */

                if (error) {
         
                        panic("mousesem: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }

        /*
         * wait until all other threads finish
         */

        while (thread_count() > 1)
                thread_yield();

        (void)nargs;
        (void)args;
        kprintf("catsem test done\n");

        return 0;
}

