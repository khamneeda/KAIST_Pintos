/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

static bool lazy_load_segment_file (struct page *page, void *aux);

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	struct uninit_page *uninit = &page->uninit;
	void* aux = uninit->aux; 
	memset(uninit, 0, sizeof(struct uninit_page));

	struct file_page *file_page = &page->file;
	file_page->type=type;
    file_page->kva=kva;
	file_page->aux=aux;
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;

	//palloc_free_page(page->frame->kva);
	list_remove(&page->frame->elem);
	free(page->frame);
	
	free(file_page->aux);
	memset(file_page, 0, sizeof(struct file_page));
	struct hash_elem* e = hash_delete(&thread_current()->spt.hash, &page->elem);
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		int fd, off_t ofs) {
	uint8_t *upage = addr;
	uint32_t read_bytes = length;
	uint32_t zero_bytes;


	if (read_bytes % PGSIZE) zero_bytes = PGSIZE - (read_bytes % PGSIZE);

	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		struct lazy_args_set *aux_set = malloc(sizeof(struct lazy_args_set)); //??
		aux_set->file=thread_current()->fd_table[fd];
		aux_set->ofs=ofs;
		aux_set->read_bytes=read_bytes;
		aux_set->zero_bytes=zero_bytes;
		aux_set->page_read_bytes=page_read_bytes;
		aux_set->page_zero_bytes=page_zero_bytes;

		aux= (void *) aux_set;

		if (!vm_alloc_page_with_initializer (VM_FILE, upage,
					writable, lazy_load_segment_file, aux))
			return NULL;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	
		ofs += page_read_bytes;
	}
	return addr;

}

static bool
lazy_load_segment_file (struct page *page, void *aux) {
	struct lazy_args_set* aux_set = (struct lazy_args_set*) aux;
	struct file* file = aux_set->file;
	size_t page_read_bytes= aux_set->page_read_bytes;
	size_t page_zero_bytes= aux_set->page_zero_bytes;
	void* kpage=page->frame->kva;

	file_seek(file, aux_set->ofs);
/*
	if (file_read(file, kpage, page_read_bytes) != (int) page_read_bytes){
		return false;
	}
	memset(kpage + page_read_bytes, 0, page_zero_bytes);
*/
    size_t temp_read_bytes = file_read(file, kpage, page_read_bytes) ; //이게 0나옴; ??
	memset(kpage + temp_read_bytes, 0, page_read_bytes-temp_read_bytes);
	memset(kpage + page_read_bytes, 0, page_zero_bytes);
	//free(aux_set); //destory시 free하기 --> copy시 사용해야함
	return true;
}


/* Do the munmap */
void
do_munmap (void *addr) {
	//find information about the file
	struct thread* curr = thread_current();
	size_t length;
	int fd;
	if(!list_empty(&curr->mmap_info_list)){
		for (struct list_elem* c = list_front(&curr->mmap_info_list); c != list_end(&curr->mmap_info_list); ){
			struct mmap_info* mmap_info = list_entry(c, struct mmap_info, elem);
			c = c->next;
			if (mmap_info->addr == addr) {
				length = mmap_info->length;
				fd= mmap_info->length;
				break;
			}
		}
	}

	size_t write_bytes = PGSIZE;
	// Dirty check
	for (int i = 0; i <(length / PGSIZE) +1; i++){
		void* pgaddr = addr + i * PGSIZE;
		struct page* page = spt_find_page(&curr->spt, pgaddr);
		struct file* file= curr->fd_table[fd];

		if (i==(length / PGSIZE)) write_bytes = length % PGSIZE; 
		if (pml4_is_dirty(curr->pml4, pgaddr)){
			if((file_write (file, pgaddr,write_bytes)!= write_bytes)){
				// some error...
			}
		
	// Decoupling addr with frame
		pml4_clear_page(curr->pml4, pgaddr);
		palloc_free_page(page->frame->kva);
		vm_dealloc_page (page);
		}
	}
}