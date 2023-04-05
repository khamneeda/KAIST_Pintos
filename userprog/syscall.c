#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"


#include "include/lib/string.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */	
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	if(!check_address(f->rsp)){
		printf("not\n");
		//exit(-1);
	}

	/* Get arguments */
	uint64_t args[7];
    args[0]= f->R.rax;
    args[1]= f->R.rdi;
    args[2]= f->R.rsi;
    args[3]= f->R.rdx;
    args[4]= f->R.r10;
    args[5]= f->R.r8;
    args[6]= f->R.r9;


	/* 	Switch cases 
	Call functions for each cases due to the system call number.
	Get return value of function if it has, and update rax in IF.
	Use int64_t for updating rax to match the type of ret in syscall.
	*/
	int64_t update;
	//printf("syscall: rax:%d\n",f->R.rax);
	switch (f->R.rax){
		default:
		   	thread_exit ();
		case SYS_HALT:
			sys_halt(args);
			break;		
		case SYS_EXIT:
			sys_exit(args);
			break;		
		case SYS_FORK:
			update = sys_fork(args);
			f->R.rax = update; 
			break;		
		case SYS_EXEC:
			update = sys_exec(args);
			f->R.rax = update; 
			break;		
		case SYS_WAIT:
			update = sys_wait(args);
			f->R.rax = update; 
			break;		
		case SYS_CREATE:
			update = sys_create(args);
			f->R.rax = update; 
			break;		
		case SYS_REMOVE:
			update = sys_remove(args);
			f->R.rax = update; 
			break;		
		case SYS_OPEN:
			update = sys_open(args);
			f->R.rax = update; 
			break;		
		case SYS_FILESIZE:
			update = sys_filesize(args);
			f->R.rax = update; 
			break;		
		case SYS_READ:
			update = sys_read(args);
			f->R.rax = update; 
			break;		
		case SYS_WRITE:
			update = sys_write(args);
			f->R.rax = update; 
			break;		
		case SYS_SEEK:
			sys_seek(args);
			break;		
		case SYS_TELL:
			update = sys_tell(args);
			f->R.rax = update; 
			break;		
		case SYS_CLOSE:
			sys_close(args);
			break;		

		/* For project2 Extra*/		
		// case SYS_MOUNT
		// case SYS_UMOUNT


		/* For project 3, 4 each
		SYS_MMAP
		SYS_MUNMAP

		SYS_CHDIR
		SYS_MKDIR
		SYS_READDIR
		SYS_ISDIR
		SYS_INUMBER
		SYS_SYMLINK
		SYS_DUP2
		*/

	}
	
}

int
check_address(uintptr_t f){
	//printf("%lld",KERN_BASE);
	if(f != NULL && f < KERN_BASE){
		struct thread* curr=thread_current();
		const uint64_t va = f;
		uint64_t *pte= pml4e_walk(curr->pml4,f,0);
		if(pte!=NULL){
			return 1;
		}
	}
	return 0;
	//exit(-1);  
}


void
sys_halt (uint64_t* args) {
	power_off();
}

void
sys_exit (uint64_t* args) {

	//wait 구현 후 status kernel에 넘겨주는 코드 추가. parent에 접근해서 child의 status를 저장?
	struct thread* t = thread_current();
	//printf("%s: exit(%d)\n", t->name, arg[1]);
	t->exit_status=(int) args[1];
	thread_exit();
}

int64_t
sys_fork (uint64_t* args){
	return 0;
}

int64_t
sys_exec (uint64_t* args) {
	return 0;
}

int64_t
sys_wait (uint64_t* args) {
	return 0;
}

int64_t
sys_create (uint64_t* args) {
	const char* name = (const char*) args[1];
	int32_t initial_size= (int32_t) args[2]; //typedef int32_t off_t
	if(name==NULL){ process_exit(-1); return (int64_t) false;}
	if(!check_address(name)){process_exit(-1); return (int64_t) false;}
	bool success = filesys_create (name, initial_size);
	return (int64_t) success;
}

int64_t
sys_remove (uint64_t* args) {
	//remove opening file??????
	const char* name = (const char*) args[1];
	bool success = filesys_remove (name);
	return (int64_t) success;
}

int64_t
sys_open (uint64_t* args) {
	const char* name = (const char*) args[1];
	//struct file * open_file = filesys_open (name);
	//if(open_file==NULL) return -1;
	return 0;
}

int64_t
sys_filesize (uint64_t* args) {
	return 0;
}

int64_t
sys_read (uint64_t* args) {
	return (int64_t) 0;
}

int64_t
sys_write (uint64_t* args) {
	int fd = (int) args[1];
	const void * buffer = (const void *) args[2];
	unsigned size = (unsigned) args[3];
	/*char temp[20];
	strlcpy(temp, buffer,size);
	printf("%s\n",temp);*/
	return 1;
}

void
sys_seek (uint64_t* args) {
}

int64_t
sys_tell (uint64_t* args) {
	return 0;
}

void
sys_close (uint64_t* args) {
}