#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <machine/pcb.h>
#include <machine/spl.h>
#include <machine/trapframe.h>
#include <kern/callno.h>
#include <syscall.h>
#include <thread.h>
#include <clock.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <kern/unistd.h>
#include <coremap.h>

#define SYS_CALL_DEBUG 0
#define SYS_CALL_DEBUG2 0

/*
 * System call handler.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. In addition, the system call number is
 * passed in the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, like an ordinary function call, and the a3 register is
 * also set to 0 to indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/lib/libc/syscalls.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * Since none of the OS/161 system calls have more than 4 arguments,
 * there should be no need to fetch additional arguments from the
 * user-level stack.
 *
 * Watch out: if you make system calls that have 64-bit quantities as
 * arguments, they will get passed in pairs of registers, and not
 * necessarily in the way you expect. We recommend you don't do it.
 * (In fact, we recommend you don't use 64-bit quantities at all. See
 * arch/mips/include/types.h.)
 */

void
mips_syscall(struct trapframe *tf)
{
	int callno;
	int32_t retval;
	int err;

	assert(curspl==0);

	callno = tf->tf_v0;

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values, 
	 * like write.
	 */

	retval = 0;
	

	switch (callno) {
		
	    case SYS_reboot:
		err = sys_reboot(tf->tf_a0);
		break;

		case SYS__exit:
		_exit(tf->tf_a0); // only one arg
		err = 0;
		break;

		case SYS_write:
		retval = write(tf->tf_a0, (void *)tf->tf_a1, tf->tf_a2);
		if (retval < 0) err = -1 * retval;
		else err = 0;
		break;

		case SYS_read:
		retval = read(tf->tf_a0, (void *)tf->tf_a1, tf->tf_a2);
		if (retval < 0) err = -1 * retval;
		else err = 0;
		break;

		case SYS_sleep:
		retval = sleep(tf->tf_a0);
		err = 0;
		break;

		case SYS___time:
		retval = __time((time_t*)tf->tf_a0, (unsigned long *)tf->tf_a1);
		err = retval != -1 ? 0 : EFAULT;
		break;

		case SYS_fork:
		retval = sys_fork(tf, curthread->t_vmspace);
		if (retval == -1) err = EAGAIN;
		else if (retval == -2) err = ENOMEM;
		else err = 0;
		break;

		case SYS_getpid:
		retval = sys_getpid();
		err = 0;
		break;

		case SYS_waitpid:
		retval = sys_waitpid(tf->tf_a0, (int *)tf->tf_a1, tf->tf_a2);
		if (retval == -1) err = EFAULT;
		else if (retval == -2) err = EINVAL;
		else err = 0;
		break;

		case SYS_execv:
		err = sys_execv((const char *)tf->tf_a0, (char **)tf->tf_a1);
		break;
		
		case SYS_sbrk:
		retval = sys_sbrk(tf->tf_a0);
		if (retval == ENOMEM) err = ENOMEM;
		else if (retval == EINVAL) err = EINVAL;
		else err = 0;
		break;

	    /* Add stuff here */
  
	    default:
		kprintf("Unknown syscall %d\n", callno);
		err = ENOSYS;
		break;
	}


	if (err) {
		/*
		 * Return the error code. This gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      /* signal an error */
	}
	else {
		/* Success. */
		tf->tf_v0 = retval;
		tf->tf_a3 = 0;      /* signal no error */
	}
	
	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */
	// now, user pc points to int 80h, now increment 4 to execute next step
	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	assert(curspl==0);
}

int
sys_sbrk(int amount){
	struct addrspace* as = curthread -> t_vmspace;

	vaddr_t old_heap = as -> heap_top;
	kprintf("the amount is: %d, heap: %x\n",amount, old_heap);
	// if (amount % PAGE_SIZE != 0){
	// 	kprintf("amount is not page aligned\n");
	// 	return ENOMEM;
	// }

	// badcall handler
	if (amount >= (int)0x40000000){
		return ENOMEM;
	}
	
	
	if (amount <= -(int)0x40000000){
		return EINVAL;
	}
	
	if (as -> heap_top + amount < as -> heap_bot){
		return EINVAL;
	}

	as -> heap_top += amount;

	//kprintf("the heap: %x\n", as -> heap_top);
	return old_heap;
}

void
md_forkentry(void* child_setup_info, unsigned long unusednumber)
{
	// now, I am in the child thread, tf and address space are in parent heap
	(void) unusednumber;

	// convert the void ptr to the corresponding type and assign trapframe to the stack of the child
	struct child_setup* self_child_setup_info = (struct child_setup*)child_setup_info;
	struct trapframe self_tf = *(self_child_setup_info -> tf);

	// curthread points to an address in the kernel heap; therefore should not be freed at now
	// It will be eventaully freed during thread exit
	// using a separate malloc require to judge the success of malloc which we cant do in here
	curthread -> t_vmspace = self_child_setup_info -> child_vmspace;
	as_activate(curthread -> t_vmspace); // activate the mem space that is currently seen by the processor
  
	// set up the return value and success flag for the return value of fork
	// epc += 4 is to prevent infinite loop of calling fork
	self_tf.tf_v0 = 0;
	self_tf.tf_a3 = 0;
	self_tf.tf_epc += 4;

	// free up the used space in the kernel heap, except child_vmspace which will be freed in the thread exit
	kfree(self_child_setup_info -> tf);
	//kfree(self_child_setup_info -> child_vmspace);
	kfree(self_child_setup_info);

#if SYS_CALL_DEBUG2 
	kprintf("child %d is created, parent is %d\n", curthread->self_pid, process_table[curthread->self_pid]->parent_pid);
#endif
	mips_usermode(&self_tf);
}

int sys_fork(struct trapframe* tf, struct addrspace *vm_space){

	// thread fork the child will go to forkentry
	struct thread *newguy;
#if SYS_CALL_DEBUG2
	kprintf("Process %d start fork\n", curthread->self_pid);
#endif	
	// create datas structure that contains all info we need for forkentry since it only has one ptr entry
	struct child_setup* child_setup_info = kmalloc(sizeof(struct child_setup));
	if (child_setup_info == NULL) return -2; //out of memory handler (fork bomb)

	// since the trapframe is reside in stack, I need to allocate it in the parent heap
	child_setup_info -> tf = kmalloc(sizeof(struct trapframe));
	if (child_setup_info -> tf == NULL){
		// out of memory handler (fork bomb)
		kfree(child_setup_info);
		return -2;
	} 
	*(child_setup_info -> tf) = *tf;

	// copy the address now in case of insufficent memory, save it in the kernel heap for latter assignment
	// as instructed in addrspace.h, create an empty address space
	// child_setup_info -> child_vmspace = as_create(vm_space -> progname);
	// if (child_setup_info -> child_vmspace == NULL){
	// 	// out of memory handler (fork bomb)
	// 	kfree(child_setup_info -> tf);
	// 	kfree(child_setup_info);
	// 	return -2;
	// }

	// fill it in
	// we have to copy the user address here, CANT DO IT in md_forkentry 
	// e.g our parent process exit from sys_fork, continues with user space code
	// stack and heap continue increase. Also the return value of sys_fork has been put on user stack
	// Therefore, if we copy the user addresss space in child thread, it will copy the growed stack
	// and heap from the parent since address space is just a ptr of code and data segment, not the real PA
	int result = as_copy(vm_space, &(child_setup_info -> child_vmspace)); 
	if (result == ENOMEM){
		// out of memory handler (fork bomb)
		kfree(child_setup_info -> tf);
		kfree(child_setup_info);
		return -2;
	}

	// check the avaliability of process table
	// such check should close to process create so that it avoids 
	// out of process during context switch at the max level
	if (process_table_full() == -1) {
		kprintf("process table is full\n");
		kfree(child_setup_info -> tf);
		//maybe call as_destroy
		kfree(child_setup_info -> child_vmspace);
		kfree(child_setup_info);
		return -1;
	}

	// set up the new thread, process create is triggered inside the thread fork
	// new thread should go to md_fork_entry with required data that is encapusalted in child_setup_info
	// pass 0 as an unused unsign
	result = thread_fork("child process", child_setup_info, 0, md_forkentry, &newguy);
	if (result == ENOMEM) {
		kprintf("Out of memory\n");
		kfree(child_setup_info -> tf);
		kfree(child_setup_info -> child_vmspace);
		kfree(child_setup_info);
		return -2;
	}

#if SYS_CALL_DEBUG2
	kprintf("Process %d finish fork, child is %d\n", curthread->self_pid, newguy->self_pid);
#endif	
	//process_table_print(15);
	
	return newguy -> self_pid;
}



int sys_getpid(){
	return curthread -> self_pid;
}

int sys_waitpid(int pid, int *status, int options){
	if (options) return -2;
	int exitcode;

#if SYS_CALL_DEBUG2 
	//kprintf("avaliable mem space: %d\n", coremap_avaliable_space());
	kprintf("I %d is waiting %d whose parent is %d\n", curthread ->self_pid, pid, process_table[pid]->parent_pid);
#endif
	// check for invalid pid

	if (pid <= 0 || pid > NUM_PROCESS){
		return -2;
	} 
	
	// check if status ptr is null, invalid or from kernel.
	if (status == NULL) return -1;
	int *k_dest = kmalloc(sizeof(int));
	if (k_dest == NULL) return -1;
	int err = copyin((const_userptr_t)status, k_dest, sizeof(int));
	if (err == EFAULT) return -1;

	//lock required since we are manipulating the process_table
	P(mutex);
	
	if (process_table[pid]->self_pid == -1){
		// this is waitpid on a process that does not exist
	#if SYS_CALL_DEBUG2 
		kprintf("self pid does not exist\n");
	#endif
		V(mutex);
		return -2;
	}
	
	if (process_table[pid]->parent_pid != curthread ->self_pid){
		// only parent can wait for his child.
	#if SYS_CALL_DEBUG2 
		kprintf("FAIL: I %d is waiting %d and his parent is %d\n", curthread ->self_pid, pid, process_table[pid]->parent_pid);
	#endif
		V(mutex);
		return -2;
	}
	V(mutex);

	P(process_table[pid]->lock);
	// P(mutex) must be inside. If it is outside, then the thread will never able to call
	// exit(), which will never release process_table[pid]->lock, which cause the deadlock
	// On the other hand, if I put it inside, I now allow the exit to execute and protects the table
	P(mutex);
	exitcode = process_table[pid]->exitcode;
	
	err = copyout(&exitcode, (userptr_t) status, sizeof(int));
	if (err == EFAULT) {
		// rarely happen, just in case
		V(mutex);
		V(process_table[pid]->lock);
		return -1;
	}

	process_table[pid]->self_pid = -1; // reap
	V(mutex);
	V(process_table[pid]->lock);

	return pid;
}

void _exit(int exitcode){
	//kprintf("Process start exited\n");
	P(mutex);
	assert(curthread->self_pid != -1);
	process_table[curthread->self_pid]->exitcode = exitcode;
	process_table[curthread->self_pid]->valid = 0; // may be useful in the future, some design overthink
#if SYS_CALL_DEBUG2 
	kprintf("proces %d exit\n", curthread->self_pid);
#endif
	V(process_table[curthread->self_pid]->lock);
	V(mutex);
	
	thread_exit();
    return;
}

int
sys_execv(const char *program, char **args){
	// both program and args are from parent user space
	// our goal is to copy them into kernel
	// then push them into the user stack

	// check if program and args are null, and program is invalid or from kernel or empty
	if (args == NULL || program == NULL) return EFAULT;
	
	char* test_program = kmalloc(sizeof(char));
	if (test_program == NULL) return ENOMEM;
	
	int err = copyin((const_userptr_t)program, test_program, sizeof(char));
	if (err == EFAULT) return EFAULT;
	if (program[0] == '\0') return EINVAL;
	kfree(test_program);

	// check if arglist is invalid or from kernel
	char **test_args = kmalloc(sizeof(char *));
	if (test_args == NULL) return ENOMEM;

	err = copyin((const_userptr_t)args, test_args, sizeof(char*));
	if (err == EFAULT) {
	#if SYS_CALL_DEBUG
		kprintf("copy in fails\n");
	#endif			
		return EFAULT;
	}
	kfree(test_args);

	// kmalloc to the kernel space

	// copy program string to kernel space
	char* k_program = kmalloc((strlen(program) + 1) * sizeof(char));
	if (k_program == NULL){
	#if SYS_CALL_DEBUG
		kprintf("Out of memory");
	#endif	
		
		return ENOMEM;
	}
	
	err = copyin((const_userptr_t)program, k_program, (strlen(program) + 1) * sizeof(char));
	if (err == EFAULT) {
	#if SYS_CALL_DEBUG
		kprintf("copyin fails\n");
	#endif	
		return EFAULT;
	}

#if SYS_CALL_DEBUG
	kprintf("Program path: %s\n", k_program);
#endif
	
	// copy argv to kernel space
	int args_size = 1;
	while(args[args_size - 1] != NULL){
		args_size++;	
	}
	
#if SYS_CALL_DEBUG
	kprintf("size of argument %d\n", args_size);
#endif	
	
	char** k_args = kmalloc(args_size * sizeof(char *));
	if (k_args == NULL){
		kfree(k_program);
		return ENOMEM;
	}
	
	int i;
	for (i = 0; i < args_size - 1; i++){
		// check if arg is invalid or from kernel
		char* test_arg = kmalloc(sizeof(char));
		if (test_arg == NULL) return ENOMEM;
		err = copyin((const_userptr_t)args[i], test_arg, sizeof(char));
		if (err == EFAULT) return EFAULT;
		kfree(test_arg);

		k_args[i] = kmalloc((strlen(args[i]) + 1) * sizeof(char));
		if (k_args[i] == NULL){
			//free every entry that just allocated
			int j;
			for (j = i - 1; j >= 0; j--){
				kfree(k_args[j]);
			}
			kfree(k_args);
			kfree(k_program);
			return ENOMEM;
		}

		int err = copyin((const_userptr_t)args[i], k_args[i], (strlen(args[i]) + 1) * sizeof(char));
		if (err == EFAULT) {
		#if SYS_CALL_DEBUG
			kprintf("size of argument %d\n", args_size);
			kprintf("copyin fails\n");
		#endif
			return EFAULT;
		}
	#if SYS_CALL_DEBUG
		kprintf("Argument %d: %s\n", i, k_args[i]);
	#endif
	}
	
	k_args[args_size - 1] = NULL;
#if SYS_CALL_DEBUG
	kprintf("Success load argument in\n");
#endif

	// copy from runprogram
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(k_program, O_RDONLY, &v);
	if (result) {
		int i;
		
		//free every argument entry except the last entry which
		//is a null pointer
		for (i = 0; i < args_size - 1; i++){
			kfree(k_args[i]);
		}
		kfree(k_args);
		kfree(k_program);

		return result;
	}

	/* We should NOT be a new thread. */
	// we must already have an address space
	as_destroy(curthread -> t_vmspace);

	/* Create a new address space. */
	curthread->t_vmspace = as_create(k_program);
	if (curthread->t_vmspace==NULL) {
		vfs_close(v);

		//free every argument entry except the last entry which
		//is a null pointer
		for (i = 0; i < args_size - 1; i++){
			kfree(k_args[i]);
		}
		kfree(k_args);
		kfree(k_program);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_vmspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		vfs_close(v);

		//free every argument entry except the last entry which
		//is a null pointer
		for (i = 0; i < args_size - 1; i++){
			kfree(k_args[i]);
		}
		kfree(k_args);
		kfree(k_program);

		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		
		//free every argument entry except the last entry which
		//is a null pointer
		for (i = 0; i < args_size - 1; i++){
			kfree(k_args[i]);
		}
		kfree(k_args);
		kfree(k_program);

		return result;
	}
	
	// an array to record where argument has stored in the stack
	vaddr_t* user_address = kmalloc(args_size * sizeof(char *));
	if (user_address == NULL){
		for (i = 0; i < args_size - 1; i++){
			kfree(k_args[i]);
		}
		kfree(k_args);
		kfree(k_program);

		return ENOMEM;
	}
	user_address[args_size - 1] = (unsigned int) NULL;
#if SYS_CALL_DEBUG
	kprintf("start to copy out\n");
#endif
	
	
	// copy out our arguments
#if SYS_CALL_DEBUG
	kprintf("The initial stack ptr is %x\n", stackptr);
#endif

	for (i = args_size - 2; i >= 0; i--){
		// word alignment, must be divisible by 4
		int temp = (4 - (strlen(k_args[i]) + 1) * sizeof(char) % 4) +  (strlen(k_args[i]) + 1) * sizeof(char);
		stackptr -= temp; // push the stack
		user_address[i] = stackptr; // store our stack ptr for double ptr where its 1D is an array of ptr
	#if SYS_CALL_DEBUG
		kprintf("The argument is %s, size is %d, dest: %x\n", k_args[i], (strlen(k_args[i]) + 1) * sizeof(char), stackptr);
	#endif

		// copy out to user stack
		err = copyout(k_args[i], (userptr_t) (stackptr), (strlen(k_args[i]) + 1) * sizeof(char));
		if (err == EFAULT) {
		#if SYS_CALL_DEBUG
			kprintf("copyout fails\n");
		#endif
			
			for (i = 0; i < args_size - 1; i++){
				kfree(k_args[i]);
			}
			kfree(k_args);
			kfree(k_program);
			kfree(user_address);
			return EFAULT;
		}
	}
#if SYS_CALL_DEBUG
	kprintf("finish copy out data\n");
#endif

	//copyout our ptr
	for (i = args_size - 1; i >= 0; i--){
		// an array of ptr, just push the sizeof a ptr 4 bytes
		stackptr -= sizeof(void *);
		err = copyout(&user_address[i], (userptr_t)(stackptr), sizeof(void *));

		if (err == EFAULT){
				#if SYS_CALL_DEBUG
					kprintf("copyout fails\n");
				#endif
			for (i = 0; i < args_size - 1; i++){
				kfree(k_args[i]);
			}
			kfree(k_args);
			kfree(k_program);
			kfree(user_address);
			return EFAULT;

		}
	}
#if SYS_CALL_DEBUG
	kprintf("finish copy out data ptr\n");
#endif

	//free the data allocated on the kernal heap
	for (i = 0; i < args_size - 1; i++){
		kfree(k_args[i]);
	}
	kfree(k_args);
	kfree(k_program);
	kfree(user_address);

#if SYS_CALL_DEBUG
	kprintf("finish free heap\n");
	kprintf("final stack ptr is: %x\n", stackptr);
#endif

	if (stackptr % 4 != 0){
		panic("NOT aligned with word\n");
	}

	/* Warp to user mode. */
	md_usermode(args_size - 1 /*argc*/, (userptr_t) (stackptr)/*userspace addr of argv*/,
		    stackptr, entrypoint);
	
	/* md_usermode does not return */
	panic("md_usermode returned\n");
	return EINVAL;
}

int write(int fd, const void *buf, size_t nbytes){
	
	// check valid file descriptor
	if (fd != 1 && fd != 2) {
		return -1 * EBADF;
	}
	// check if EFAULT will occur
	char* k_dest = kmalloc(nbytes + 1);
	int err = copyin((const_userptr_t)buf, k_dest, nbytes);
	if (err == EFAULT){
		//kprintf("test\n");
		return -1 * EFAULT;
	} 

	k_dest[nbytes] = '\0';
	int print_byte = kprintf(k_dest);
	kfree(k_dest);
	//if (err == EFAULT) return EFAULT;

	//int kp = kprintf(buf);
	//kprintf("byte print:%d", kp);
	//nbytes = 1;
	return print_byte;
}

int read(int fd, void *buf, size_t nbytes){
	//kprintf("check point1\n");
	//if (buf == NULL) return -1 * EFAULT; 
	// check valid file descriptors
	if (fd != 0) {
		return -1 * EBADF;
	}

	if (nbytes != 1) return -1 * EUNIMP;
	
	//int* k_dest = kmalloc(sizeof(int));
	//kgets(k_dest,1);
	char read_char = (char)getch();
	//kprintf("check point2\n");
	//kprintf("qwer:%c, number of bytes:%d\n",(int)read_char, nbytes);
	//k_dest[1] = '\0';
	//kprintf("qwer,%c", k_dest[0]);
	//kprintf("qwer:%c\n", k_dest[0]);	
	int err = copyout(&read_char, (userptr_t)buf, nbytes);
	if (err == EFAULT){
		//kprintf("test\n");
		return -1 * EFAULT;
	} 
	//kprintf("check point3\n");
	return nbytes;
}

u_int32_t sleep(u_int32_t seconds){
	//void sleep_address;
	//thread_sleep(&sleep_address);

	//hardclock() is called from the timer interrupt HZ times a second.
	//which will wakeup lbolt that is slept address by clocksleep
	//clocksleep(seconds - 0.5);
	
	time_t before_secs, after_secs, secs;
	u_int32_t before_nsecs, after_nsecs, nsecs;
	gettime(&before_secs, &before_nsecs);
	clocksleep(seconds);
	//int spin = 0;
	//while(spin < 10000000) spin ++;
	gettime(&after_secs, &after_nsecs);

	getinterval(before_secs, before_nsecs,
				    after_secs, after_nsecs,
				    &secs, &nsecs);
	/*
	kprintf("sleep took %lu.%09lu seconds\n",
				(unsigned long) secs,
				(unsigned long) nsecs);
	*/
	//thread_wakeup(&sleep_address)
	
	return 0;
}

time_t
__time(time_t *seconds, unsigned long *nanoseconds){
	time_t k_seconds;
	u_int32_t k_nanoseconds;
	int err1, err2;

	// check the validity of user address
	if (seconds != NULL){
		err1 = copyin((const_userptr_t)seconds, &k_seconds, sizeof(time_t));
		if (err1 == EFAULT) return -1;
	}

	if (nanoseconds != NULL){
		err2 = copyin((const_userptr_t)nanoseconds, &k_nanoseconds, sizeof(unsigned long));
		if (err2 == EFAULT) return -1;
	}
	
	gettime(&k_seconds, &k_nanoseconds);

	if (seconds != NULL){
		err1 = copyout(&k_seconds, (userptr_t)seconds, sizeof(time_t));
		if (err1 == EFAULT) return -1;
	}

	if (nanoseconds != NULL){
		err2 = copyout(&k_nanoseconds, (userptr_t)nanoseconds, sizeof(unsigned long));
		if (err2 == EFAULT) return -1;
	}

	kprintf("current time %lu.%09lu seconds\n",
				(unsigned long) k_seconds,
				(unsigned long) k_nanoseconds);

	return k_seconds;

}
