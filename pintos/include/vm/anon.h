#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"

struct page;
enum vm_type;

struct anon_page {
    enum vm_type type;
    void *kva;
    void *aux;
    size_t idx;
    uint64_t* pml4;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
