#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include "threads/synch.h"
#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif



/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */
#define FD_TABLE_SIZE 130				/* The number of fd possible in fd_table */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */
	int64_t local_ticks; 					/* local_ticks */
	int priority_origin;				/* Given priority. Not donated */
	struct lock* pressing_lock;			/* Lock on thread */

	struct list donated_thread_list;	/* List of donated priority */
	struct list_elem donated_elem;      /* List element for donated_thread_list*/

	int nice;                           /* Nice */
	int recent_cpu;                     /* Recent_cpu */
	struct list_elem total_list_elem;   /* to manage total list_elem*/

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
	int is_exit;       					//1  : 종료  0: 실행
	int exit_status;   					//0  : 정상 -1: error
	int load_status;   					//0  : 정상 -1: error
	struct list child_list;				/* Forked child list */
	struct list_elem child_elem;		/* List element of child_list */
	struct thread * parent;				/* Parent thread of this thread */
    
	struct semaphore exit_sema;			/* Whether this thread is exited */
	struct semaphore wait_sema;			/* Whether this thread is exited */	
	struct semaphore load_sema;			/* Whether this thread is loaded */
	struct semaphore fork_sema;			/* Whether its child's cloning is finished */

//struct file* fd_table [FD_TABLE_SIZE];			/* File descriptor table of this process */
	struct file** fd_table; 
	int num_of_fd;						/* The number of file descriptors in fd_table */

	bool is_process_msg;				/* true: msg, false: no msg */

#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
	uint64_t stack_floor;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

bool less_ticks(const struct list_elem *,const struct list_elem *,void * );
bool less_priority(const struct list_elem *,const struct list_elem *,void * );
bool less_donated_priority(const struct list_elem *,const struct list_elem *,void * );

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

void dis_intr_treason (struct thread *);
void thread_treason (struct thread *);

struct thread *thread_current (void);

struct thread* get_thread(tid_t);

tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);
void thread_wakeup(int64_t);
void thread_sleep(int64_t);

int thread_get_priority (void);
void thread_set_priority (int);


int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */


#define f_constant (1<<14)
// #define int_max ((1<<31) - 1)
// #define int_min (-(1<<31))
// #define int64_max (((int64_t 1)<<63) - 1)
// #define int64_min (-((int64_t 1)<<63))

int c2f (int);			/* Convert to fixed point number*/
int conv_to_int_round_near (int);		/* Convert to int from f.p.n */
int conv_to_int_round_zero (int);
// int64_t add_num (int64_t, int64_t);
// int64_t sub_num (int64_t, int64_t);
/* Arithmetic operations used in floating point number calculation */
int mul_num (int, int);
int div_num (int, int);
// int64_t mul_mixed (int64_t, int)




void mlfqs_update_load_avg (void);
void mlfqs_update_recent_cpu (struct thread *);
void mlfqs_update_priority (struct thread *);
// void mlfqs_increse_recent_cpu_running (void);
void mlfqs_update_all_thread (void);
void mlfqs_update_all_threads_on_list (struct list*);
bool is_idle(void);


void thread_sema_down (struct semaphore *);
void thread_push_ready_list(struct thread* );
