#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
//#include "threads/synch.h" //Added for semaphore approach
//#include "threads/thread.c" //Added to use ready_list
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void **);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	size_t command_length = strlen(file_name);
	char command[80];
	strlcpy(command, file_name, command_length+1);
	char* saving_str;
    char* real_name = strtok_r (command, " ", &saving_str);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (real_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	
	enum intr_level old_level;
	old_level = intr_disable ();
	
	struct thread* child = get_thread(tid);
	child->parent = thread_current();
	list_push_back(&thread_current()->child_list, &child->child_elem);

	intr_set_level(old_level);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
	lock_init(&open_lock);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ ) {
	/* Clone current thread to new thread.*/
	/*
	우리 regs도 복사해줘야함 ->aux
	*/
	void * temp_array[2];
	temp_array[0] = thread_current();
	temp_array[1] = if_; 
	tid_t child_tid = thread_create (name, PRI_DEFAULT, __do_fork, temp_array);
	if(child_tid==TID_ERROR){ return TID_ERROR;}
	struct thread* child = get_thread(child_tid);
	list_push_back(&thread_current()->child_list, &child->child_elem);
	sema_down(&child->fork_sema);
	return child->tid;

}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */

	if (is_kernel_vaddr(va)) return true;

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */

	newpage = palloc_get_page(PAL_USER);
	if (!newpage) return false;
    
	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */

	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pml4e_walk (parent->pml4, va, 0));


	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */

	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		palloc_free_page(newpage);
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void ** aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux[0];
	struct thread *curr = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if  = (struct intr_frame *) aux[1]; 
	bool succ = true;
	curr->is_process_msg = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax=0;

	/* 2. Duplicate PT */
	curr->pml4 = pml4_create();
	if (curr->pml4 == NULL)
		goto error;

	process_activate (curr);
#ifdef VM
	supplemental_page_table_init (&curr->spt);
	if (!supplemental_page_table_copy (&curr->spt, &parent->spt))
		goto error;
	curr->stack_floor=USER_STACK;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	curr->parent = parent;
	//curr->tf=if_;

	for (int i = 0; i < parent->num_of_fd; i++){
		if (parent->fd_table[i])
			curr->fd_table[i] = file_duplicate(parent->fd_table[i]);
	}
	curr->num_of_fd = parent->num_of_fd;

	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		sema_up (&curr->fork_sema);
		do_iret (&if_);
error:
	curr->tid = TID_ERROR;
	sema_up (&curr->fork_sema);
	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
/* 여기에서 fork와 wait을 모두 해 줌*/
int
process_exec (void *f_name) {
	char file_name[100];
	strlcpy(file_name, f_name, 100);
	bool success;
	thread_current()->is_process_msg = true;


	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	void* temp_page = malloc(100);
	memcpy(temp_page, file_name, 100);

	/* We first kill the current context */
	process_cleanup ();
	#ifdef VM
    supplemental_page_table_init(&thread_current()->spt);
    #endif

	/* And then load the binary */
	memcpy(file_name, temp_page, 100);
	success = load (file_name, &_if);

	/* If load failed, quit. */
	free(temp_page);
	//palloc_free_page (file_name);
	if (!success)
		return -1;

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */

/*
	wait전에 kill될 경우 자식 알 수 없기 때문에 fork에서 다음을 해줘야 함
	1. child->parent 지정 // 이건 fork에서 해줘야할듯 wait전에 kill되면 
	2. parent->child_list에 추가
	대신 main의 경우 process_initd에서 따로 해줘야

	여기서 해줄 것
	3. child status = READY
	그외 sema_down이 해주는 것: parent block, waiters 추가, pop, parent ready

	이외 할 것
	if child exist -> status 반환
	else -> -1

	kill과 exit에서 반드시 status를 지정한 후 종료해야 함

	main_thread 실행시 sema_down전에 intr가 걸리면 조기종료됨 ->disable
*/
int
process_wait (tid_t child_tid) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	enum intr_level old_level;
	old_level = intr_disable ();

	//child의 status를 ready로 바꿔주고 ready_list에 넣어줌 이건 해줘야함
	struct thread* child = get_thread(child_tid);
	//child->status = THREAD_READY;
	//thread_push_ready_list(child);
	if (child -> parent != thread_current()){ return -1;}
	if (child -> is_exit ) goto done;

	intr_set_level(old_level);


	//sema_down이 알아서 curr BLOCK으로 바꿔서 waiters에 넣어줌
	thread_sema_down(&child->wait_sema);//부모가 아니라 자식의 sema여야함 -> 자식세마 웨이터에 부모넣기


	//pop child in child_list in parent

	/*
	자원해제
	exit, kill 후 schedule이 두번 돌면 exit_status를 알 수 없기 때문에
	1. exit, kill 후 바로 sema_up 해주기
	2. 이 함수에서 sema_down이후로 intr_disable()
	*/

done:

	old_level = intr_disable ();

	struct thread* curr = thread_current();
	if(!list_empty(&curr->child_list)){
		struct thread* target_child= list_entry(list_front(&curr->child_list), struct thread, child_elem);
		while (target_child->tid != child_tid){
			if (&target_child->child_elem == list_end(&curr->child_list) ){
				intr_set_level (old_level);
				return -1;		
			}
			target_child=list_entry(list_next(&target_child->child_elem), struct thread, child_elem);
		}
		int exit_status=target_child->exit_status;
		list_remove(&target_child->child_elem);
		sema_up(&target_child->exit_sema);
		intr_set_level (old_level);
		
		return exit_status;
	}
	intr_set_level (old_level);
	return -1;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	//print exit
	curr->is_exit=1;
	process_cleanup ();
	if(!list_empty(&curr->child_list)){
	for (struct list_elem* c = list_front(&curr->child_list); c != list_end(&curr->child_list); ){
		struct thread* t = list_entry(c,struct thread, child_elem);
		c = c->next;
		sema_up(&t->exit_sema);
	}}
	
	for (int i = 2; i < curr->num_of_fd; i++){
		if (curr->fd_table[i])
			file_close(curr->fd_table[i]);
	}
	palloc_free_page((void *)curr->fd_table);
	
	if (curr->is_process_msg)
		printf("%s: exit(%d)\n", curr->name, curr->exit_status);
	sema_up(&curr->wait_sema);
	if(curr->parent) sema_down(&curr->exit_sema);
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);



/*
Get the number of splited words in command line.
*/
int get_rank(const char *file_name) {
   size_t s_length = strlen(file_name);
   size_t temp_size=100;
   char s[temp_size];

   //if(s_length < temp_size) strlcpy(s, file_name, s_length+1);
   ASSERT(s_length < temp_size);
   strlcpy(s, file_name, s_length+1);


  int number=0;
  int status=0;
    for (int length = 0; s[length] != '\0' && length < s_length; length++){
    if( s[length] == ' '){
      status=0;
    }
    else if (s[length]!=' '&&status==0){
      status=1;
      number++;
    }
  }
  return number;
}


/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Split command line */
	int arg_len = get_rank(file_name);
	size_t command_length = strlen(file_name);
	char command[100];
	strlcpy(command, file_name, command_length+1);

	const char* arg[50] = {NULL, };
	char* saving_str;
	char* tocken;
	int j = 0;
    for (tocken = strtok_r (command, " ", &saving_str); tocken != NULL; tocken = strtok_r (NULL, " ", &saving_str)){
        arg[j] = tocken;
        j++;
    }
	const char* name_of_file = arg[0];




	/* Open executable file. */
	lock_acquire(&open_lock);
	file = filesys_open (name_of_file);

	if (file == NULL) {
		printf ("load: %s: open failed\n", name_of_file);
		goto done;
	}
	//file_deny_write(file);
	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", name_of_file);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);


		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr){
			goto done;
		}


		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	//file_deny_write(file);


	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */


	char * arg_addr[50] = {0, };


	/* Argument data */
	for (int i = arg_len - 1; i >= 0; i--) {
		if_->rsp = if_->rsp- strlen(arg[i])-1 ;
		arg_addr[arg_len -1 -i] = (char *) if_->rsp;
		memcpy(if_->rsp, arg[i], strlen(arg[i])+1 );
	}

	/* Padding */
	int rest=if_->rsp%8;
    if(rest!=0){ if_->rsp-=rest;}
    memset(  if_->rsp,0,rest);
    if_->rsp-=8;
    memset(if_->rsp,0,8);

	/* Data address */
    for(int i=0; i< arg_len; i++){
        if_->rsp-=8;
        memcpy(if_->rsp, &arg_addr[i], 8);
    }

	/* Fake Address */
	if_->rsp-=8;
    memset(if_->rsp,0,8);

	if_->R.rdi= arg_len;
	if_->R.rsi= if_->rsp+8;

//strlen(file_name);
	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	//file_close (file);
	lock_release(&open_lock);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		//lock_acquire(&open_lock);
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			//lock_release(&open_lock);
			return false;
		}
		//lock_release(&open_lock);
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct lazy_args_set* aux_set = (struct lazy_args_set*) aux;
	struct file* file = aux_set->file;
	size_t page_read_bytes= aux_set->page_read_bytes;
	size_t page_zero_bytes= aux_set->page_zero_bytes;
	void* kpage=page->frame->kva;

	file_seek(file, aux_set->ofs);

	if (file_read(file, kpage, page_read_bytes) != (int) page_read_bytes){
		//palloc_free_page(kpage);
		return false;
	}
	memset(kpage + page_read_bytes, 0, page_zero_bytes);

	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */


static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		struct lazy_args_set *aux_set = malloc(sizeof(struct lazy_args_set)); //??
		aux_set->file=file;
		aux_set->ofs=ofs;
		aux_set->page_read_bytes=page_read_bytes;
		aux_set->page_zero_bytes=page_zero_bytes;

		aux= (void *) aux_set;

		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	success = vm_alloc_page_with_initializer(VM_ANON, stack_bottom, true, NULL, NULL);
	if(!success) return false;
	success = vm_claim_page(stack_bottom);
	if (success) {
		if_->rsp = USER_STACK;
		thread_current()->stack_floor=stack_bottom;
	}
	return success;
}
#endif /* VM */