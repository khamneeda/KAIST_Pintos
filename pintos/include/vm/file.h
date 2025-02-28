#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"


struct page;
enum vm_type;

struct file_page {
	enum vm_type type;
    void *kva;
    void *aux;
    uint64_t* pml4;
};

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
		int fd, off_t offset);
void do_munmap (void *va);
#endif
