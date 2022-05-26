/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <thread.h>
#include <curthread.h>
#include <vm.h>
#include <vfs.h>
#include <test.h>

#define RUNPROG_DEBUG 0

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */

int runprogram(char *progname, char **args, unsigned long nargs)
{	
	//(void) args;
	//(void) nargs;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, &v);
	if (result) {
		return result;
	}

	/* We should be a new thread. */
	assert(curthread->t_vmspace == NULL);

	/* Create a new address space. */
	curthread->t_vmspace = as_create(progname);
	if (curthread->t_vmspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_vmspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		return result;
	}

	print_addrspace();
	//************************************************************************************************************
	//************************************************************************************************************
	//************************************************************************************************************
	//************************************************************************************************************

	// directly copy paste from exec syscall, please see comment and explanation in there

#if RUNPROG_DEBUG
	kprintf("Cmd prog\n");
#endif
	
	// copy out since arg and nargs are already in the kernel
	// an array to record where argument has stored in the stack
	vaddr_t* user_address = kmalloc((nargs + 1) * sizeof(char *));
	if (user_address == NULL){
		return ENOMEM;
	}
	user_address[nargs] = (unsigned int) NULL;
#if RUNPROG_DEBUG
	kprintf("start to copy out\n");
#endif
	int i, err;
	
	// copy out our arguments
	//int err = copyout(&read_char, (userptr_t)buf, nbytes);
#if RUNPROG_DEBUG
	kprintf("The initial stack ptr is %x\n", stackptr);
#endif
	for (i = nargs - 1; i >= 0; i--){
		int temp = (4 - (strlen(args[i]) + 1) * sizeof(char) % 4) +  (strlen(args[i]) + 1) * sizeof(char);
		stackptr -= temp;
		user_address[i] = stackptr;
	#if RUNPROG_DEBUG
		kprintf("start to copy out\n");
		kprintf("The argument is %s, size is %d, dest: %x\n", args[i], (strlen(args[i]) + 1) * sizeof(char), stackptr);
	#endif
		err = copyout(args[i], (userptr_t) (stackptr), (strlen(args[i]) + 1) * sizeof(char));
		
		if (err == EFAULT) {
		#if RUNPROG_DEBUG
			kprintf("copyout fails\n");
		#endif
			kfree(user_address);
			return EFAULT;
		}
	}
#if RUNPROG_DEBUG
	kprintf("finish copy out data\n");
#endif

	//copyout our ptr
	for (i = nargs; i >= 0; i--){
		stackptr -= sizeof(void *);
		err = copyout(&user_address[i], (userptr_t)(stackptr), sizeof(void *));

		if (err == EFAULT){
		#if RUNPROG_DEBUG
			kprintf("copyout fails\n");
		#endif
			kfree(user_address);
			return EFAULT;

		}
	}

#if RUNPROG_DEBUG
	kprintf("finish copy out data ptr\n");
#endif

	//free the data allocated on the kernal heap
	kfree(user_address);
	
#if RUNPROG_DEBUG
	kprintf("finish free heap\n");
	kprintf("final stack ptr is: %x\n", stackptr);
#endif

	if (stackptr % 4 != 0){
		panic("NOT aligned with word\n");
	}
	
	
	md_usermode(nargs /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
		    stackptr, entrypoint);
	
	/* md_usermode does not return */
	panic("md_usermode returned\n");
	return EINVAL;
}

