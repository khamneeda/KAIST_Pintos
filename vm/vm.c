/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

struct list frame_table;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
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

		bool (*initializer)(struct page *, enum vm_type, void *);

		if(VM_TYPE(type)==VM_ANON) initializer = anon_initializer;
		else if (VM_TYPE(type) ==VM_FILE ) initializer = file_backed_initializer;

		uninit_new (page, upage, init, VM_UNINIT, aux,initializer);
		page->writable=writable;
		
		spt_insert_page(spt,page);

	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	struct page temp_page;
	temp_page.va=va;
	page= hash_entry(hash_find(&spt->hash,&temp_page.elem), struct page, elem);
	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	/* TODO: Fill this function. */

	//이미 spt에 있는지 확인해야함

	void* success = hash_insert(&spt->hash,&page->elem);
	if(success==NULL) return true;
	return false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete(&spt->hash,&page->elem);
	vm_dealloc_page (page);
	return true;
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
		if ( !pml4_is_accessed (thread_current()->pml4, victim->kva) && !pml4_is_dirty(thread_current()->pml4, victim->kva)) 
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
	bool succ= swap_out(victim->page); //dirty bit 등 고려 안 함. 
	// frame 내 정보 바꿈 생각 안 함. 
	victim->page=NULL; //swap out 안에서 바꿔주자 
	list_remove(&victim->elem);
	if (!succ) return NULL;
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	void* va = palloc_get_page(PAL_USER); 

	if(va==NULL){ frame = vm_evict_frame(); }
	else{
		frame= malloc(sizeof(struct frame));
		frame-> kva = va;
	}
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	list_push_back(&frame_table,&frame->elem);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
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

bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt= &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	page = spt_find_page(spt,addr);
	if(page==NULL){ 
		return false;
	}
	return vm_do_claim_page (page);
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
	
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	plm4_set_page(thread_current()->pml4,page->va,frame->kva, page->writable);

	return swap_in (page, frame->kva);
}

uint64_t hash_hash (const struct hash_elem *e, void *aux){
	struct page* page= hash_entry(e, struct page, elem);
	return hash_bytes(&page->va,sizeof(uint64_t));
	// ???? size check
}

bool hash_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) { 
	struct page* a_page= hash_entry(a, struct page, elem);
	struct page* b_page= hash_entry(a, struct page, elem);
	return (a_page->va<b_page->va);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	bool success = hash_init (&spt->hash,hash_hash, hash_less, NULL); // aux ==NULL로 세팅
	ASSERT(success==true);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
	struct supplemental_page_table *src) {
	struct hash_iterator i;
	hash_first (&i, &src->hash);
	while (hash_next (&i))
	{
	struct page *page = hash_entry (hash_cur (&i), struct page, elem);
	struct page *new_page = malloc(sizeof(struct page));
	memcpy(new_page, page, sizeof(struct page));
	hash_insert(&dst->hash,&new_page->elem);
	}
}

void hash_free (struct hash_elem *e, void *aux){
	struct page* page= hash_entry(e, struct page, elem);
	free(page);
	//page안에 저장된 정보 *frame free또는 뭔가 업데이트
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_destroy (&spt->hash, hash_free); 
}
