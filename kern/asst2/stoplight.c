/* 
 * stoplight.c
 *
 * You can use any synchronization primitives available to solve
 * the stoplight problem in this file.
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


/*
 * Number of cars created.
 */

#define NCARS 20


/*
 *
 * Function Definitions
 *
 */


struct lock* straight, *straight1, *straight2, *straight3, *left, *left1, *left2, *left3, *left4, *right, *right1, *right2;
struct cv* NW, *NE, *SW, *SE, *N_buffer, *W_buffer, *S_buffer, *E_buffer;

int NW_count = 0, NE_count = 0, SW_count = 0, SE_count = 0,
	N_buffer_count = 0, W_buffer_count = 0, S_buffer_count = 0, E_buffer_count = 0;
int deadlock_count = 3;

int test_leave = 0;

static const char *directions[] = { "N", "E", "S", "W" };

static const char *msgs[] = {
        "approaching:",
        "region1:    ",
        "region2:    ",
        "region3:    ",
        "leaving:    "
};

/* use these constants for the first parameter of message */
enum { APPROACHING, REGION1, REGION2, REGION3, LEAVING };

static void
message1(int msg_nr, int carnumber, int cardirection, int destdirection)
{
        kprintf("%s car = %2d, direction = %s, destination = %s 	\n",
                msgs[msg_nr], carnumber,
                directions[cardirection], directions[destdirection]);
}

static void
message(int msg_nr, int carnumber, int cardirection, int destdirection)
{
        kprintf("%s car = %2d, direction = %s, destination = %s, deadlock = %d\nNW_count = %d, NE_count = %d, SW_count = %d, SE_count = %d\nN_buffer_count = %d, W_buffer_count = %d, S_buffer_count = %d, E_buffer_count = %d\nS_lock = %d, R_lock = %d, L_lock = %d\n\n",
                msgs[msg_nr], carnumber,
                directions[cardirection], directions[destdirection], deadlock_count, NW_count,NE_count,SW_count,SE_count,N_buffer_count, W_buffer_count, S_buffer_count, E_buffer_count, lock_do_i_hold(straight), lock_do_i_hold(right),lock_do_i_hold(left));
}
 
/*
 * gostraight()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement passing straight through the
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
gostraight(unsigned long cardirection,
           unsigned long carnumber)
{	
	
	lock_acquire(straight);
	
	if (cardirection == 0){
		// from north to south, going through region NW, SW
		lock_acquire(straight1);
		while (N_buffer_count != 0) cv_wait(N_buffer, straight1);
		N_buffer_count++;
		message(0, carnumber, cardirection, cardirection + 2);
		lock_release(straight1);

		// passing through NW region
		lock_acquire(straight2);
		while (NW_count != 0 || deadlock_count == 0) cv_wait(NW,straight2);
		NW_count++, deadlock_count--, N_buffer_count--;
		message(1, carnumber, cardirection, cardirection + 2);
		cv_signal(N_buffer,straight1);
		lock_release(straight2);

		// passing through SW region
		lock_acquire(straight3);
		while (SW_count != 0) cv_wait(SW,straight3);
		SW_count ++, NW_count--;
		message(2, carnumber, cardirection, cardirection + 2);
		cv_signal(NW,straight2);
		lock_release(straight3);
		
		deadlock_count++, SW_count--;
		message(4, carnumber, cardirection, cardirection + 2);
		cv_signal(SW,straight3);
		
	}
	else if (cardirection == 1){
		// from east to west, going through region NE, NW
		lock_acquire(straight1);
		while (E_buffer_count != 0) cv_wait(E_buffer, straight1);
		E_buffer_count++;
		message(0, carnumber, cardirection, cardirection + 2);
		lock_release(straight1);

		// passing through NE region
		lock_acquire(straight2);
		while (NE_count != 0 || deadlock_count == 0) cv_wait(NE,straight2);
		NE_count++, deadlock_count--, E_buffer_count--;
		message(1, carnumber, cardirection, cardirection + 2);
		cv_signal(E_buffer,straight1);
		lock_release(straight2);

		// passing through NW region
		lock_acquire(straight3);
		while (NW_count != 0) cv_wait(NW,straight3);
		NW_count ++, NE_count--;
		message(2, carnumber, cardirection, cardirection + 2);
		cv_signal(NE,straight2);
		lock_release(straight3);

		deadlock_count++, NW_count--;
		message(4, carnumber, cardirection, cardirection + 2);
		cv_signal(NW,straight3);

	}else if (cardirection == 2){
		// from south to north, going through region SE, NE
		lock_acquire(straight1);
		while (S_buffer_count != 0) cv_wait(S_buffer, straight1);
		S_buffer_count++;
		message(0, carnumber, cardirection, cardirection - 2);
		lock_release(straight1);

		// passing through SE region
		lock_acquire(straight2);
		while (SE_count != 0 || deadlock_count == 0) cv_wait(SE,straight2);
		SE_count++, deadlock_count--, S_buffer_count--;
		message(1, carnumber, cardirection, cardirection - 2);
		cv_signal(S_buffer,straight1);
		lock_release(straight2);

		// passing through NE region
		lock_acquire(straight3);
		while (NE_count != 0) cv_wait(NE,straight3);
		NE_count ++, SE_count--;
		message(2, carnumber, cardirection, cardirection - 2);
		cv_signal(SE,straight2);
		lock_release(straight3);
		
		deadlock_count++, NE_count--;
		message(4, carnumber, cardirection, cardirection - 2);
		cv_signal(NE,straight3);

	}else{
		// from west to east, going through region SW, SE
		lock_acquire(straight1);
		while (W_buffer_count != 0) cv_wait(W_buffer, straight1);
		W_buffer_count++;
		message(0, carnumber, cardirection, cardirection - 2);
		lock_release(straight1);

		// passing through SW region
		lock_acquire(straight2);
		while (SW_count != 0 || deadlock_count == 0) cv_wait(SW,straight2);
		SW_count++, deadlock_count--, W_buffer_count--;
		message(1, carnumber, cardirection, cardirection - 2);
		cv_signal(W_buffer,straight1);
		lock_release(straight2);

		// passing through SE region
		lock_acquire(straight3);
		while (SE_count != 0) cv_wait(SE,straight3);
		SE_count ++, SW_count--;
		message(2, carnumber, cardirection, cardirection - 2);
		cv_signal(SW,straight2);
		lock_release(straight3);

		deadlock_count++, SE_count--;
		message(4, carnumber, cardirection, cardirection - 2);
		cv_signal(SE,straight3);
	}
	
	lock_release(straight);
}



/*
 * turnleft()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement making a left turn through the 
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
turnleft(unsigned long cardirection,
         unsigned long carnumber)
{
    lock_acquire(left);
	
	if (cardirection == 0){
		// from north to east, going through region NW, SW, SE
		lock_acquire(left1);
		while (N_buffer_count != 0) cv_wait(N_buffer, left1);
		N_buffer_count++;
		message(0, carnumber, cardirection, cardirection + 1);
		lock_release(left1);

		// passing through NW region
		lock_acquire(left2);
		while (NW_count != 0 || deadlock_count == 0) cv_wait(NW,left2);
		NW_count++, deadlock_count--, N_buffer_count--;
		message(1, carnumber, cardirection, cardirection + 1);
		cv_signal(N_buffer, left1);
		lock_release(left2);

		// passing through SW region
		lock_acquire(left3);
		while (SW_count != 0) cv_wait(SW,left3);
		SW_count ++, NW_count--;
		message(2, carnumber, cardirection, cardirection + 1);
		cv_signal(NW,left2);
		lock_release(left3);

		// passing through SE region
		lock_acquire(left4);
		while (SE_count != 0) cv_wait(SE,left4);
		SE_count ++, SW_count--;
		message(3, carnumber, cardirection, cardirection + 1);
		cv_signal(SW,left3);
		lock_release(left4);
		
		deadlock_count++, SE_count--;
		message(4, carnumber, cardirection, cardirection + 1);
		cv_signal(SE,left4);
		
	}
	else if (cardirection == 1){
		// from east to south, going through region NE, NW, SW
		lock_acquire(left1);
		while (E_buffer_count != 0) cv_wait(E_buffer, left1);
		E_buffer_count++;
		message(0, carnumber, cardirection, cardirection + 1);
		lock_release(left1);

		// passing through NE region
		lock_acquire(left2);
		while (NE_count != 0 || deadlock_count == 0) cv_wait(NE,left2);
		NE_count++, deadlock_count--, E_buffer_count--;
		message(1, carnumber, cardirection, cardirection + 1);
		cv_signal(E_buffer, left1);
		lock_release(left2);

		// passing through NW region
		lock_acquire(left3);
		while (NW_count != 0) cv_wait(NW,left3);
		NW_count ++, NE_count--;
		message(2, carnumber, cardirection, cardirection + 1);
		cv_signal(NE,left2);
		lock_release(left3);

		// passing through SW region
		lock_acquire(left4);
		while (SW_count != 0) cv_wait(SW,left4);
		SW_count ++, NW_count--;
		message(3, carnumber, cardirection, cardirection + 1);
		cv_signal(NW,left3);
		lock_release(left4);

		deadlock_count++, SW_count--;
		message(4, carnumber, cardirection, cardirection + 1);
		cv_signal(SW,left4);

	}else if (cardirection == 2){
		// from south to west, going through region SE, NE, NW
		lock_acquire(left1);
		while (S_buffer_count != 0) cv_wait(S_buffer, left1);
		S_buffer_count++;
		message(0, carnumber, cardirection, cardirection + 1);
		lock_release(left1);

		// passing through SE region
		lock_acquire(left2);
		while (SE_count != 0 || deadlock_count == 0) cv_wait(SE,left2);
		SE_count++, deadlock_count--,S_buffer_count--;
		message(1, carnumber, cardirection, cardirection + 1);
		cv_signal(S_buffer, left1);
		lock_release(left2);

		// passing through NE region
		lock_acquire(left3);
		while (NE_count != 0) cv_wait(NE,left3);
		NE_count ++, SE_count--;
		message(2, carnumber, cardirection, cardirection + 1);
		cv_signal(SE,left2);
		lock_release(left3);

		// passing through NW region
		lock_acquire(left4);
		while (NW_count != 0) cv_wait(NW,left4);
		NW_count ++, NE_count--;
		message(3, carnumber, cardirection, cardirection + 1);
		cv_signal(NE,left3);
		lock_release(left4);
		
		deadlock_count++, NW_count--;
		message(4, carnumber, cardirection, cardirection + 1);
		cv_signal(NW,left4);

	}else{
		// from west to north, going through region SW, SE, NE
		lock_acquire(left1);
		while (W_buffer_count != 0) cv_wait(W_buffer, left1);
		W_buffer_count++;
		message(0, carnumber, cardirection, cardirection - 3);
		lock_release(left1);

		// passing through SW region
		lock_acquire(left2);
		while (SW_count != 0 || deadlock_count == 0) cv_wait(SW,left2);
		SW_count++, deadlock_count--, W_buffer_count--;
		message(1, carnumber, cardirection, cardirection - 3);
		cv_signal(W_buffer, left1);
		lock_release(left2);


		// passing through SE region
		lock_acquire(left3);
		while (SE_count != 0) cv_wait(SE,left3);
		SE_count ++, SW_count--;
		message(2, carnumber, cardirection, cardirection - 3);
		cv_signal(SW,left2);
		lock_release(left3);

		// passing through NE region
		lock_acquire(left4);
		while (NE_count != 0) cv_wait(NE,left4);
		NE_count ++, SE_count--;
		message(3, carnumber, cardirection, cardirection - 3);
		cv_signal(SE,left3);
		lock_release(left4);

		deadlock_count++, NE_count--;
		message(4, carnumber, cardirection, cardirection - 3);
		cv_signal(NE,left4);
	}
	
	lock_release(left);
}


/*
 * turnright()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement making a right turn through the 
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
turnright(unsigned long cardirection,
          unsigned long carnumber)
{
    lock_acquire(right);
	
	if (cardirection == 0){
		// from north to west, going through region NW
		lock_acquire(right1);
		while (N_buffer_count != 0) cv_wait(N_buffer, right1);
		N_buffer_count++;
		message(0, carnumber, cardirection, cardirection + 3);
		lock_release(right1);

		// passing through NW region
		lock_acquire(right2);
		while (NW_count != 0 || deadlock_count == 0) cv_wait(NW,right2);
		NW_count++, deadlock_count--, N_buffer_count--;
		message(1, carnumber, cardirection, cardirection + 3);
		cv_signal(N_buffer, right1);
		lock_release(right2);
		
		deadlock_count++, NW_count--;
		message(4, carnumber, cardirection, cardirection + 3);
		cv_signal(NW,right2);
		
	}
	else if (cardirection == 1){
		// from east to north, going through region NE
		lock_acquire(right1);
		while (E_buffer_count != 0) cv_wait(E_buffer, right1);
		E_buffer_count++;
		message(0, carnumber, cardirection, cardirection - 1);
		lock_release(right1);

		// passing through NE region
		lock_acquire(right2);
		while (NE_count != 0 || deadlock_count == 0) cv_wait(NE,right2);
		NE_count++, deadlock_count--, E_buffer_count--;
		message(1, carnumber, cardirection, cardirection - 1);
		cv_signal(E_buffer, right1);
		lock_release(right2);

		deadlock_count++, NE_count--;
		message(4, carnumber, cardirection, cardirection - 1);
		cv_signal(NE,right2);

	}else if (cardirection == 2){
		// from south to east, going through region SE
		lock_acquire(right1);
		while (S_buffer_count != 0) cv_wait(S_buffer, right1);
		S_buffer_count++;
		message(0, carnumber, cardirection, cardirection - 1);
		lock_release(right1);

		// passing through SE region
		lock_acquire(right2);
		while (SE_count != 0 || deadlock_count == 0) cv_wait(SE,right2);
		SE_count++, deadlock_count--, S_buffer_count--;
		message(1, carnumber, cardirection, cardirection - 1);
		cv_signal(S_buffer, right1);
		lock_release(right2);

		deadlock_count++, SE_count--;
		message(4, carnumber, cardirection, cardirection - 1);
		cv_signal(SE,right2);

	}else{
		// from west to east, going through region SW
		lock_acquire(right1);
		while (W_buffer_count != 0) cv_wait(W_buffer, right1);
		W_buffer_count++;
		message(0, carnumber, cardirection, cardirection - 1);
		lock_release(right1);

		// passing through SW region
		lock_acquire(right2);
		while (SW_count != 0 || deadlock_count == 0) cv_wait(SW,right2);
		SW_count++, deadlock_count--, W_buffer_count--;
		message(1, carnumber, cardirection, cardirection - 1);
		cv_signal(W_buffer, right1);
		lock_release(right2);

		deadlock_count++, SW_count--;
		message(4, carnumber, cardirection, cardirection - 1);
		cv_signal(SW,right2);
	}
	
	lock_release(right);
}


/*
 * approachintersection()
 *
 * Arguments: 
 *      void * unusedpointer: currently unused.
 *      unsigned long carnumber: holds car id number.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Change this function as necessary to implement your solution. These
 *      threads are created by createcars().  Each one must choose a direction
 *      randomly, approach the intersection, choose a turn randomly, and then
 *      complete that turn.  The code to choose a direction randomly is
 *      provided, the rest is left to you to implement.  Making a turn
 *      or going straight should be done by calling one of the functions
 *      above.
 */

/*
static
void
gostraight(unsigned long cardirection,
           unsigned long carnumber)
{
	lock_acquire(straight);




	lock_release(straight);
}

static
void
turnleft(unsigned long cardirection,
         unsigned long carnumber)
{

}

static
void
turnright(unsigned long cardirection,
          unsigned long carnumber)
{

}
*/
static
void
approachintersection(void * unusedpointer,
                     unsigned long carnumber)
{
        int cardirection, destdirection;

        /*
         * Avoid unused variable and function warnings.
         */

        (void) unusedpointer;
		(void) message1;
		
        //(void) carnumber;
        //(void) gostraight;
        //(void) turnleft;
        //(void) turnright;
		

        /*
         * cardirection is set randomly.
         */
		
		//"N" 0, "E" 1, "S" 2, "W" 3
        cardirection = random() % 4;
        // straight 0, left 1, right 2
		destdirection = random() % 3;

		// sync primitive initialization
		//straight = lock_create("straight");
		

		//kprintf("enter %d\n", destdirection);
        if (destdirection == 0){
        	// go straight
			gostraight(cardirection, carnumber);
        }else if (destdirection == 1){
			// turn left
			turnleft(cardirection, carnumber);
		}else {
			// turn right
			turnright(cardirection, carnumber);
		}

		return;
}


/*
 * createcars()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up the approachintersection() threads.  You are
 *      free to modiy this code as necessary for your solution.
 */

int
createcars(int nargs,
           char ** args)
{
        int index, error;
		straight = lock_create("straight");
		straight1 = lock_create("straight1");
		straight2 = lock_create("straight2");
		straight3 = lock_create("straight3");
		left = lock_create("left");
		left1 = lock_create("left1");
		left2 = lock_create("left2");
		left3 = lock_create("left3");
		left4 = lock_create("left4");
		right = lock_create("right");
		right1 = lock_create("right1");
		right2 = lock_create("right2");

		NW = cv_create("NW");
		NE = cv_create("NE");
		SW = cv_create("SW");
		SE = cv_create("SE");

		N_buffer = cv_create("N_buffer");
		W_buffer = cv_create("W_buffer");
		S_buffer = cv_create("S_buffer");
		E_buffer = cv_create("E_buffer");

	
        /*
         * Start NCARS approachintersection() threads.
         */

        for (index = 0; index < NCARS; index++) {
                error = thread_fork("approachintersection thread",
                                    NULL, index, approachintersection, NULL);

                /*
                * panic() on error.
                */

                if (error) {         
                        panic("approachintersection: thread_fork failed: %s\n",
                              strerror(error));
                }
        }
        
		
        /*
         * wait until all other threads finish
         */

        while (thread_count() > 1)
                thread_yield();

	(void)message;
        (void)nargs;
        (void)args;
        kprintf("stoplight test done\n");
        return 0;
}

