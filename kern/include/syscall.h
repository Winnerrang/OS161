#ifndef _SYSCALL_H_
#define _SYSCALL_H_



/*
 * Prototypes for IN-KERNEL entry points for system call implementations.
 */

struct child_setup{
    struct trapframe* tf;
    struct addrspace* child_vmspace;
};



int sys_reboot(int code);

void _exit(int exitcode);

int write(int fd, const void *buf, size_t nbytes);

int read(int fd, void *buf, size_t nbytes);

u_int32_t sleep(u_int32_t seconds);

time_t
__time(time_t *seconds, unsigned long *nanoseconds);

int sys_fork(struct trapframe* tf, struct addrspace *vm_space);

int sys_getpid();

int sys_waitpid(int pid, int *status, int options);

int
sys_execv(const char *program, char **args);

int
sys_sbrk(int amount);

#endif /* _SYSCALL_H_ */
