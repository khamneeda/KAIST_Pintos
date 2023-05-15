/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "vm/uninit.h"


struct list frame_table;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
	list_init(&frame_table);

#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);
static void vm_stack_growth (void * addr);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
//vm_alloc_page_with_initializer (VM_ANON, upage, writable, lazy_load_segment, aux)
	ASSERT (VM_TYPE(type) != VM_UNINIT)
	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		/* TODO: Insert the page into the spt. */

		struct page *page = malloc(sizeof(struct page));
		if (page == NULL) goto err;

		bool (*initializer)(struct page *, enum vm_type, void *);

		if (VM_TYPE(type) == VM_ANON) initializer = anon_initializer; //type check 이렇게 하는게 맞나
		else if (VM_TYPE(type) == VM_FILE) initializer = file_backed_initializer;
		else {
			free(page);
			return false;
		}

		uninit_new (page, upage, init, type, aux, initializer);
		page->writable = writable;
		
		bool success = spt_insert_page(spt, page);
		if (success) return true;
		free(page);
	}
err:
	return false;
}

/* 
Find VA from spt and return page. On error, return NULL. 
*/
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	struct page temp_page;
	temp_page.va = va;
	struct hash_elem* h_elem = hash_find(&spt->hash, &temp_page.elem);
	if (h_elem == NULL) return NULL;
	page = hash_entry(h_elem, struct page, elem);
	return page;
}

/* 
Insert PAGE into spt with validation. 
가상 주소가 spt에 존재 안함 체크
*/
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	/* TODO: Fill this function. */

	//이미 spt에 있는지 확인해야함
	struct page* tmp_page = spt_find_page(spt, page->va);
	if (tmp_page != NULL) return false;

	void* success = hash_insert(&spt->hash,&page->elem);
	if(success==NULL) return true;
	return false;
}

/*
delete 성공여부 추가
*/
void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	struct hash_elem* e = hash_delete(&spt->hash, &page->elem);
	ASSERT(e != NULL); //임시로 바꿔둠 error띄우던가 해야할거같은데

	vm_dealloc_page (page);
}


// just get info of evicted frame (not modifying other info)
/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	struct list_elem* c;
	for (c = list_front(&frame_table); c != list_end(&frame_table); c = c->next){
		victim = list_entry(c ,struct frame, elem);
		if (!pml4_is_accessed (thread_current()->pml4, victim->kva) && 
			!pml4_is_dirty(thread_current()->pml4, victim->kva)) 
				return victim;
	}
	for (c = list_front(&frame_table); c != list_end(&frame_table); c = c->next){
		victim = list_entry(c ,struct frame, elem);
		if (!pml4_is_dirty(thread_current()->pml4, victim->kva)) 
			return victim;
	}
	if (c==list_end(&frame_table)){ victim = list_entry(list_front(&frame_table) ,struct frame, elem); }
		return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	/*아래 코드 swap-out에서 할 수 있는지 고려*/
	bool succ = swap_out(victim->page); //dirty bit 등 고려 안 함. 
	// frame 내 정보 바꿈 생각 안 함. 
	victim->page = NULL; //swap out 안에서 바꿔주자 
	list_remove(&victim->elem);

	if (!succ) return NULL;
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.
 * palloc_get_page로 프레임 생성, 리턴
 * 불가시 evict(swap)
 * 
 * */
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	void* kva = palloc_get_page(PAL_USER);

	if(kva == NULL) { 
		frame = vm_evict_frame(); 
	}
	else{
		frame= malloc(sizeof(struct frame));
		frame-> kva = kva;
		frame-> page =NULL;
	}
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	list_push_back(&frame_table, &frame->elem);
	return frame;
}

/* Growing the stack. */
/* This function checks whether the addr is valid.
 * Lower stack floor n times.
*/
static void
vm_stack_growth (void * addr){
	struct thread* curr = thread_current();
	void * margin = USER_STACK - addr;
	if ( addr < curr->stack_floor && margin < 1 <<20){
		int times = (curr->stack_floor - addr) / PGSIZE +1;
		curr->stack_floor = curr->stack_floor - PGSIZE * times;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
/*
first checks if it is a valid page fault. 
By valid, we mean the fault that accesses invalid. 
If it is a bogus fault, you load some contents into the page 
and return control to the user program.

There are three cases of bogus page fault: lazy-loaded, 
swaped-out page, and write-protected page (See Copy-on-Write (Extra)). 
For now, just consider the first case, lazy-loaded page.
*/

/*
exception.c의 page_fault가 호출
true => valid fault, frame할당해서 핸들링 잘 해줌
false => 잘못된 참조, page_fault로 돌아가 kill해줌
잘못된 참조 : 유저일때 커널 주소에 접근?
writable하지 않은데 write?
not_present page? 이건뭐야 True: not-present page, false: writing r/o page
*/
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt= &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if ( is_kernel_vaddr(addr)) return false;
	if (write == true && page->writable == false) return false;
 
 	addr = pg_round_down(addr);
	page = spt_find_page(spt, addr);
	if (page == NULL) return false;
	//?? not_present는 왜 주어진거임?
	bool succ = false;
	if (not_present) {
		vm_stack_growth(addr);
		succ = vm_do_claim_page (page);
	}
	return succ;
	
	//if (succ) succ = uninit_initialize (page, page->frame->kva);
	// 이거 수정해야할듯? 이미 page_claim했으니 처리
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, va);
	if(page == NULL) return false;
	bool success = vm_do_claim_page (page);
	return success;
}

static bool
vm_install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}


/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	bool success= vm_install_page(page->va, frame->kva, page->writable);
	if(!success) return false;

	return swap_in (page, frame->kva);
}

uint64_t hash_hash (const struct hash_elem *e, void *aux){
	struct page* page= hash_entry(e, struct page, elem);
	return hash_bytes(&page->va,sizeof(page->va));
	// ???? size check
}

bool hash_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) { 
	struct page* a_page= hash_entry(a, struct page, elem);
	struct page* b_page= hash_entry(b, struct page, elem);
	return (a_page->va<b_page->va);
}

/* 
Initialize new supplemental page table 
새로운 프로세스 시작시, fork시 호출
*/
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	bool success = hash_init (&spt->hash, hash_hash, hash_less, NULL); // aux ==NULL로 세팅
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
	struct supplemental_page_table *src) {
	struct hash_iterator i;
	bool success = true;

	hash_first (&i, &src->hash);
	while (hash_next (&i))
	{
		struct page *page = hash_entry (hash_cur (&i), struct page, elem);
		struct page *new_page = malloc(sizeof(struct page));
		memcpy(new_page, page, sizeof(struct page));
		success = hash_insert(&dst->hash,&new_page->elem);
		if (!success) return success;
	}
	return success;
}

void hash_free (struct hash_elem *e, void *aux){
	struct page* page= hash_entry(e, struct page, elem);
	if(page!=NULL)	vm_dealloc_page(page);
	//free(page->frame);
	//struct frame은 free해줘도되나
	//page안에 저장된 정보 *frame free또는 뭔가 업데이트
}

/* Free the resource hold by the supplemental page table */
/* This function also free frames linked to pages*/
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_destroy (&spt->hash, hash_free); 
}
