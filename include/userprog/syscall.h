#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
int check_address(uintptr_t);

void sys_halt (uint64_t *);
void sys_exit (uint64_t *); 
int64_t sys_fork (uint64_t *);
int64_t sys_exec (uint64_t *); 
int64_t sys_wait (uint64_t *); 
int64_t sys_create (uint64_t *);
int64_t sys_remove (uint64_t *);
int64_t sys_open (uint64_t*);
int64_t sys_filesize (uint64_t* );
int64_t sys_read (uint64_t*);
int64_t sys_write (uint64_t* );
void sys_seek (uint64_t*);
int64_t sys_tell (uint64_t*);
void sys_close (uint64_t*);

#endif /* userprog/syscall.h */