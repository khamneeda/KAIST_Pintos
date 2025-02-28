#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
//#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of processes in THREAD_BLOCKED state, that is, processes
   that are not actually running during the "duration". */
static struct list sleep_list;

/* List of processes in THREAD_BLOCKED state, that is, processes
   that are not actually running during the "duration". */
static struct list total_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
int load_avg;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff }; //??

bool less_ticks(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) { 
	struct thread* a_thread= list_entry(a, struct thread, elem);
	struct thread* b_thread= list_entry(b, struct thread, elem);
	return (a_thread->local_ticks<b_thread->local_ticks);
}
bool less_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) { 
	struct thread* a_thread= list_entry(a, struct thread, elem);
	struct thread* b_thread= list_entry(b, struct thread, elem);
	return (a_thread->priority>b_thread->priority);
}
bool less_donated_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) { 
	struct thread* a_thread= list_entry(a, struct thread, donated_elem);
	struct thread* b_thread= list_entry(b, struct thread, donated_elem);
	return (a_thread->priority>b_thread->priority);
}

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create(). //##

   It is not safe to call thread_current() until this function
   finishes. */ //##
 
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_list);
	list_init (&total_list);
	list_init (&destruction_req);


	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
	list_push_back(&total_list,&initial_thread->total_list_elem);//main에도 이게 필요함?
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);
	load_avg=0;
	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	struct file ** fd_table_page;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	//t->priority_origin=priority; init_thread에 넣음
	tid = t->tid = allocate_tid ();

	if (name != "idle")	list_push_back(&total_list,&t->total_list_elem);

	#ifdef USERPROG
	sema_init(&t->exit_sema, 0);
	sema_init(&t->wait_sema, 0);
	sema_init(&t->load_sema, 0);
	sema_init(&t->fork_sema, 0);
 	fd_table_page = palloc_get_page (PAL_ZERO);
	if (fd_table_page == NULL)
		return TID_ERROR;
	t->fd_table= fd_table_page;

	#endif


	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	//thread_unblock (t);
	ASSERT (t->status == THREAD_BLOCKED);
	dis_intr_treason (t);
	return t->tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;
	ASSERT (is_thread (t));
	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);

	//list_push_back (&ready_list, &t->elem);
	list_insert_ordered(&ready_list,&t->elem,less_priority,0);
	t->status = THREAD_READY;
	
	
	intr_set_level (old_level);
}


/*
Diable interrupt before calling treason.
*/

void
dis_intr_treason (struct thread *t){
	enum intr_level old_level;
	ASSERT (is_thread (t));
	old_level = intr_disable ();
	
	thread_treason(t);
	
	intr_set_level (old_level);
}


/*
Used outside of timer_interrupt.
Interrupt should be disabled outside of treason.
THREAD_BLOCKED should be ASSERTED outside of treason.
*/

void
thread_treason (struct thread *t) {
	//If current thread's priority is bigger than t. Treason failed.  
    if(thread_get_priority()>=t->priority){
		list_insert_ordered(&ready_list,&t->elem,less_priority,0);
		t->status = THREAD_READY;
	}
	else{ //Treason succeeded.
		if(!intr_context()){
       	struct thread *curr = thread_current ();
		if (curr != idle_thread){
			list_insert_ordered(&ready_list,&curr->elem,less_priority,0);
		}
		list_push_front(&ready_list,&t->elem);
		do_schedule (THREAD_READY);
		}
		else{
			struct thread *curr = thread_current ();
			list_push_front(&ready_list,&t->elem);
			intr_yield_on_return();
		}
	}
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}
/* Get thread corresponding to tid */
struct thread*
get_thread(tid_t tid){
	for (struct list_elem* c = list_front(&total_list); c != list_end(&total_list); c = c->next){
		struct thread* t = list_entry(c,struct thread, total_list_elem);
		if (t->tid == tid) return t;
	}
	return NULL;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}  
 
/* Deschedules the current thread and destroys it.  Never 
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit (); 
#endif 

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		//list_push_back (&ready_list, &curr->elem);
		list_insert_ordered(&ready_list,&curr->elem,less_priority,0);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/*   */
void
thread_wakeup (int64_t global_ticks) {
	struct thread *curr = thread_current ();
	//list_sort(&sleep_list,less_ticks,0);
	if (!list_empty (&sleep_list))
	{
		struct thread* t= list_entry(list_front (&sleep_list), struct thread, elem);
	    while (t->local_ticks <= global_ticks )
        {
          t->status=THREAD_READY;
		  list_pop_front(&sleep_list);
		  //thread_treason (t);
			list_insert_ordered(&ready_list,&t->elem,less_priority,0);
		   
			if(curr->priority>=t->priority){
				t->status = THREAD_READY;
			}
			else{
				intr_yield_on_return();
			}
		  if (!list_empty (&sleep_list)){
            t= list_entry(list_front (&sleep_list), struct thread, elem);
		  }
		  else
		  	break;
        }
	}
}


void
thread_sleep (int64_t local_ticks) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;
    
	ASSERT (!intr_context ());
	old_level = intr_disable ();
	if (curr != idle_thread) {
		curr->local_ticks = local_ticks;
		//list_push_back (&sleep_list, &curr->elem); //must_insert
		list_insert_ordered(&sleep_list,&curr->elem,less_ticks,0);
	}
	do_schedule (THREAD_BLOCKED);

	intr_set_level (old_level);
}


/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	if(!thread_mlfqs){
		if(thread_current ()->priority == thread_current ()->priority_origin ){
		thread_current ()->priority = new_priority;
		}
		thread_current ()->priority_origin = new_priority;
		if(!list_empty(&ready_list)){
			struct thread* t = list_entry(list_pop_front(&ready_list), struct thread, elem);
			dis_intr_treason(t);
		}
	}
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
	
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice){ 
	/* TODO: Your implementation goes here */
	enum intr_level old_level;
	old_level = intr_disable ();

	thread_current()->nice=nice;
	mlfqs_update_priority(thread_current()); //재계산 필요

	list_sort(&ready_list,less_priority,0);
	if(!list_empty(&ready_list)){
		struct thread* t = list_entry(list_pop_front(&ready_list), struct thread, elem);
		dis_intr_treason(t);
	}

	intr_set_level (old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level;
	old_level = intr_disable ();
	int nice;
	nice= thread_current()->nice;
	intr_set_level (old_level);
	return nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level;
	old_level = intr_disable ();
	int local_load_avg;
	local_load_avg= conv_to_int_round_near(load_avg*100); ///!!!!multiple 100!!!
	intr_set_level (old_level);
	return local_load_avg;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	enum intr_level old_level;
	old_level = intr_disable ();
	int recent_cpu;
	recent_cpu= conv_to_int_round_near(thread_current()->recent_cpu*100); ///!!!!multiple 100!!!
	intr_set_level (old_level);
	return recent_cpu;
	/* TODO: Your implementation goes here */
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	//idle_thread->priority=0; // ??
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	if (!thread_mlfqs){
	t->priority = priority;
	t->priority_origin = priority;
	}
	t->local_ticks = 0;
	list_init(&t->donated_thread_list);
	t->nice = 0;
	t->recent_cpu = 0;
    t->pressing_lock = NULL;
	t->magic = THREAD_MAGIC;

#ifdef USERPROG
	t->is_exit=0;
	t->exit_status=-1;
	t->load_status=-1;
	list_init(&t->child_list);
	t->is_process_msg = false;


	//사람있어요!!!
	//memset(t->fd_table, 0, PGSIZE);
	// t->fd_table = {0, }; // Memory allocation?
	t->num_of_fd = 2;

#endif

#ifdef VM
	list_init (&t->mmap_info_list);

#endif

}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used bye the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
			list_remove(&curr->total_list_elem);

		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}





/*
We don't need to consider about overflow because we are using 64bit.
However, if error occurs, assertion about overflow detection using
min, max needed in arithmetic operatons.

Following functions are used in calculating load_avg, recent_cpu, priority.
Convert integers used in calculating rational number into fixed point first.
*/

// int64_t add_num (int64_t x, int64_t y){
// 	return x + y;
// }
// int64_t sub_num (int64_t x, int64_t y){
// 	return x - y;
// }
// int64_t mul_mixed (int64_t x, int n){
// 	return x * n
// }
int c2f (int n){
	return n * f_constant;
}
int conv_to_int_round_near (int x){
	if (x >= 0)
		return (x + (f_constant/2)) / f_constant;
	else
		return (x - (f_constant/2)) / f_constant;
}
int conv_to_int_round_zero (int x){
	return x / f_constant;
}

int mul_num (int x, int y){
	return (int)( ( (int64_t) x )* y / f_constant);
}
int div_num (int x, int y){
	return (int)( ( (int64_t) x ) * f_constant / y);
}

/*
Following functions are used in timer_interrupt

Beware overflow! 
Get coefficient of recent_cpu first using load_avg when updating recent_cpu
*/
void mlfqs_update_load_avg (void){
	int num_ready = 0;
	if (thread_current() != idle_thread) num_ready++;
	num_ready += (int)list_size(&ready_list);
	num_ready = div_num(c2f(1),  c2f(60)) * num_ready;
	load_avg = mul_num(div_num(c2f(59), c2f(60)), load_avg) + num_ready;
	if (load_avg < 0) load_avg = 0;
}
void mlfqs_update_recent_cpu (struct thread *t){
	if (t != idle_thread){
		int coeffi = div_num((2*load_avg), (2*load_avg + c2f(1)));
		t->recent_cpu = mul_num(coeffi, t->recent_cpu) + c2f(t->nice);
	}
}
void mlfqs_update_priority (struct thread *t){
	if (t != idle_thread){
		t->priority = conv_to_int_round_zero(c2f(PRI_MAX) -t->recent_cpu/4- 2*c2f(t->nice)) ; 
		if(t->priority>PRI_MAX){
			t->priority=PRI_MAX;
		}
		if(t->priority<PRI_MIN){
			t->priority=PRI_MIN;
		}
	}
}
/*해당 함수는 그냥 timer_interrupt에서 만들어도 될듯*/
// void mlfqs_increse_recent_cpu_running (void aux UNUSED){

void mlfqs_update_all_thread (void){
	mlfqs_update_all_threads_on_list(&total_list);
	//mlfqs_update_all_threads_on_list(&ready_list);
	//mlfqs_update_all_threads_on_list(&sleep_list);
}

void mlfqs_update_all_threads_on_list (struct list* list_addr){
	if(!list_empty(list_addr)){
		struct thread* a_thread= list_entry(list_front(list_addr), struct thread, total_list_elem);
		while(&a_thread->total_list_elem!=list_end(list_addr)){
			mlfqs_update_recent_cpu(a_thread);//update recent_cpu_running
			mlfqs_update_priority(a_thread); //update priority 
			a_thread=list_entry(list_next(&a_thread->total_list_elem), struct thread, total_list_elem);
		}
	}
}
bool is_idle(void){
	return thread_current() == idle_thread;
}

/* Defined to use things in process.c */

void thread_sema_down (struct semaphore * sem){
	sema_down(sem);
}

void thread_push_ready_list(struct thread* t){
	list_insert_ordered(&ready_list, &t->elem, less_priority, 0);
}
