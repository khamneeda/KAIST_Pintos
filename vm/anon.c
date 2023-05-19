/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
/* 얘는 전체 시스템 초기화인듯? */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = NULL;



}

/* Initialize the file mapping */
/* Unitit => anon이 되게 세팅해주는 함수
 * uninit_ops.swap_in => uninit_initialize가 얘를 호출
 * 얘를 swap_in이라고 생각 */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	struct uninit_page *uninit = &page->uninit;
	void* aux = uninit->aux; 
	memset(uninit, 0, sizeof(struct uninit_page));

	struct anon_page *anon_page = &page->anon;
	anon_page->type=type;
    anon_page->kva=kva;
	anon_page->aux=aux;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
//	palloc_free_page(page->frame->kva);
	list_remove(&page->frame->elem);
	free(page->frame);
	
	struct anon_page *anon_page = &page->anon;
	free(anon_page->aux);
	memset(anon_page, 0, sizeof(struct anon_page));
	struct hash_elem* e = hash_delete(&thread_current()->spt.hash, &page->elem);
}
