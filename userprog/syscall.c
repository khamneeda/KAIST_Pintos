#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
///////////
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/file.h"

#include "include/lib/string.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

int check_address(uintptr_t);
void sys_halt (uint64_t *);
void sys_exit (uint64_t *); 
void sys_exit_num (int); 
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
struct file* get_file(int);
int64_t sys_dup2(uint64_t*);

int make_dup2_matching_file(int, struct file*);
struct file* get_dup2_matching_file(int);
int get_dup2_matching_entry(int);

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
		sys_exit_num(-1);
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
			args[2]=(uint64_t) f;
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
		case SYS_DUP2:
			update = sys_dup2(args);
			f->R.rax = update; 
			break;



		/* For project 3, 4 each
		SYS_MMAP
		SYS_MUNMAP

		SYS_CHDIR
		SYS_MKDIR
		SYS_READDIR
		SYS_ISDIR
		SYS_INUMBER
		SYS_SYMLINK		
		SYS_MOUNT
		SYS_UMOUNT
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
sys_exit_num (int status) {
	struct thread* t = thread_current();
	t->exit_status=status;
	thread_exit();
}

void
sys_exit (uint64_t* args) {
	//wait 구현 후 status kernel에 넘겨주는 코드 추가. parent에 접근해서 child의 status를 저장?
	sys_exit_num ((int) args[1]);
}

int64_t
sys_fork (uint64_t* args){
    const char *thread_name = (const char *) args[1];
	struct intr_frame *f = (struct intr_frame *) args[2];
	int tid = process_fork (thread_name, f);
	return tid;
}

int64_t
sys_exec (uint64_t* args) {
	const char *file= (const char *) args[1];
	if(!check_address(file)){ sys_exit_num(-1); }
	int ret = process_exec((void *)file);
	if (ret == -1) sys_exit_num(-1);
	return -1;
}

int64_t
sys_wait (uint64_t* args) {
	int pid = (int) args[1];

	int child_exit_status = process_wait (pid);
	return child_exit_status;
}

int64_t
sys_create (uint64_t* args) {
	const char* name = (const char*) args[1];
	int32_t initial_size= (int32_t) args[2]; //typedef int32_t off_t
	if(name==NULL){ sys_exit_num(-1); return (int64_t) false;}
	if(!check_address(name)){sys_exit_num(-1); return (int64_t) false;}
	bool success = filesys_create (name, initial_size);
	return (int64_t) success;
}



int64_t
sys_remove (uint64_t* args) {
	const char* name = (const char*) args[1];
	bool success = filesys_remove (name); 
	return (int64_t) success;
}


int64_t
sys_open (uint64_t* args) {
    const char* name = (const char *) args[1];
    struct thread* curr = thread_current();
    if(!check_address(name)) sys_exit_num(-1);
	if (curr->num_of_fd == FD_TABLE_SIZE) return -1;
	struct file* open_file = filesys_open(name);
    if (open_file == NULL) return -1;

	if(!strcmp(name,curr->name)){file_deny_write(open_file);}
    curr->fd_table[curr->num_of_fd] = open_file;
    curr->num_of_fd++;
    return curr->num_of_fd-1;
}

int64_t
sys_filesize (uint64_t* args) {
	int fd = (int) args[1];
	struct file* file = get_file(fd);
	ASSERT(file != 0);
	return (int64_t) file_length(file);
}

/*
0: stdin / 1: stout
*/
int64_t
sys_read (uint64_t* args) {

	int fd = (int) args[1];
	uint8_t* buffer = (uint8_t*) args[2]; // 얘 init 안해줘도 되나
	unsigned size = (unsigned) args[3];
	int read_byte = 0;
	uint8_t key;

	if (!check_address(buffer)) sys_exit_num(-1); // Or return -1? Or put it in default

	struct file* file;
	if(fd<0){return -1;}

 	file = get_file(fd);
	if (file == NULL) return -1; //is it right????????

	if( file->inode == NULL){ //stdin or stdout
		if( file->pos == 0 ){ //stdin
			key = input_getc();
			buffer[read_byte] = key;
			read_byte++;
			while ((read_byte < size) && (!buffer_empty())){ 
				key = input_getc();
				buffer[read_byte] = key;
				read_byte++;
			}
			return (int64_t) read_byte;

		}
		else if (file->pos == 1 ){//stdout 
			return (int64_t) -1; //right??????
		}
		else{
			printf("something wrong!");
			return -1; 
		}
	}
	lock_acquire(file_rw_lock(file));
	read_byte = file_read(file, buffer, size);
	lock_release(file_rw_lock(file));
	return (int64_t) read_byte;
}

int64_t
sys_write (uint64_t* args) {
	int fd = (int) args[1];
	const void * buffer = (const void *) args[2];
	unsigned size = (unsigned) args[3];

	int write_byte=0;
	if (!check_address(buffer)) sys_exit_num(-1);
	if(fd<0){return -1;}

	struct file* file;
	long rest;

    file = get_file(fd);
	if (file == NULL)return -1;

	if( file->inode == NULL){ //stdin or stdout
		if( file->pos == 0 ){ //stdin
			return (int64_t) 0;
		}
		else if (file->pos == 1 ){//stdout 
			rest = (long) size; 
			unsigned long temp_size;
			temp_size = 100;
			while(rest>0){
			temp_size= rest<temp_size? rest :temp_size;
			putbuf (buffer, temp_size);
			rest=rest-100;
			}
			return (int64_t) size;
		}
		else{
			printf("something wrong!");
			return -1; 
		}
	}
	lock_acquire(file_rw_lock(file));
	write_byte = file_write(file, buffer, size);
	lock_release(file_rw_lock(file));
	ASSERT(write_byte >= 0);
	return (int64_t) write_byte;		
	
}

void
sys_seek (uint64_t* args) {
	int fd = (int) args[1];
	unsigned position = (unsigned) args[2];
	struct file* file = get_file(fd);
	if(file==NULL){return;}
	if(file->inode!=NULL)
		file_seek(file, position);
}

int64_t
sys_tell (uint64_t* args) {
	int fd = (int) args[1];
	struct file* file = get_file(fd);
	if(file==NULL){return;}
	if(file->inode!=NULL)
		return file_tell(file);
}

void
sys_close (uint64_t* args) {
	int fd = (int) args[1];
	if(fd<0){sys_exit_num(-1);}
	struct file* file = get_file(fd);
	if(file==NULL){sys_exit_num(-1);}
	if(fd>=FD_TABLE_SIZE){ 
		struct thread * curr = thread_current();
		struct dup2_matching* dup2_array=&curr->fd_table[FD_TABLE_SIZE];
		dup2_array[get_dup2_matching_entry(fd)].fd=NULL;
		dup2_array[get_dup2_matching_entry(fd)].file=NULL;
		file_close_after_filecnt_check(file);
		return;
	}
	thread_current()->fd_table[fd]=NULL;
	file_close_after_filecnt_check(file);
}

/* Get file pointer searching in the current threads fd_table */
struct file*
get_file(int fd){
	//ASSERT (thread_current()->num_of_fd != FD_TABLE_SIZE);
	if(fd<FD_TABLE_SIZE){
	struct file* file = thread_current()->fd_table[fd];
	return file;
	}
	else{ return get_dup2_matching_file(fd); }
}

/*	Copy file descriptor from oldfd to newfd
If one of them closed, the other still survives.
Track them by counting file_open_cnt.
Must revise funcitons in file.c, filesys.c, and sys_close.
파일 내 open_cnt 추가. 파일 주소 복사 + file_open_cnt 증가. open_cnt == 1일 때만 파일 닫아줌. 파일 open, close시 open_cnt 변경하도록 file.c, filesys.c 수정
oldfd 나 newfd가 0이나 1이면 어쩜? --> 추후 추가해주기
*/
int64_t
sys_dup2(uint64_t* args){
	int oldfd = (int) args[1];
	int newfd = (int) args[2];
	struct thread* curr = thread_current();

	if ((oldfd < 0) || (newfd<0 ) )// || (!check_address(curr->fd_table[oldfd]))) page kernel단에 있어서 빼주는게 맞을듯?
		return (int64_t) -1;
    struct file* oldfd_file= get_file(oldfd);
	struct file* newfd_file= get_file(newfd);
	if ((oldfd_file== NULL) ){
		return (int64_t) -1;
	}
	if ( oldfd == newfd ){
		return newfd;
	}
	if (oldfd_file == get_file(newfd))
		return newfd;
	if (newfd_file != NULL){
		file_close_after_filecnt_check(newfd_file);
	}
	else{
		if(newfd>=FD_TABLE_SIZE){
			newfd = make_dup2_matching_file(newfd,oldfd_file);
			oldfd_file->file_open_cnt++;
			return newfd;
		}
		else if(curr->num_of_fd<=newfd ){
			curr->num_of_fd=newfd+1;
		}
	}
	curr->fd_table[newfd] = oldfd_file;
	oldfd_file->file_open_cnt++;
	return newfd;
}

int
get_dup2_matching_entry(int fd){
	ASSERT(fd>=FD_TABLE_SIZE);
	struct thread * curr = thread_current();
	struct dup2_matching* dup2_array=&curr->fd_table[FD_TABLE_SIZE];
	for(int i = 0 ; i < curr->num_of_matching_dup2; i++){
		if(dup2_array[i].fd==fd){
			return i;
		}
	}
	return -1;
}

struct file* 
get_dup2_matching_file(int fd){
	ASSERT(fd>=FD_TABLE_SIZE);
	int entry= get_dup2_matching_entry(fd);
	if(entry==-1) return NULL;
	struct thread * curr = thread_current();
	struct dup2_matching* dup2_array=&curr->fd_table[FD_TABLE_SIZE];
	return dup2_array[entry].file;
}

int 
make_dup2_matching_file(int fd, struct file* file){
	ASSERT(fd>=FD_TABLE_SIZE);
	int entry= get_dup2_matching_entry(fd);
	struct thread * curr = thread_current();
	struct dup2_matching* dup2_array=&curr->fd_table[FD_TABLE_SIZE];
	if(entry==-1) {
		//struct dup2_matching dup2_element= dup2_array[curr->num_of_matching_dup2];
		dup2_array[curr->num_of_matching_dup2].fd=fd;
		dup2_array[curr->num_of_matching_dup2].file=file;
		curr->num_of_matching_dup2++;
		return fd;
	}
	else{
		dup2_array[entry].fd=fd;
		dup2_array[entry].file=file;
		return fd;
	}
}