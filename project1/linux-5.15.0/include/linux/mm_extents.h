#ifndef __LINUX_MM_EXTENTS_H
#define __LINUX_MM_EXTENTS_H

#include <linux/rbtree.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/types.h>

struct extent_table {
    struct rb_root tree;
    int size;
    rwlock_t tree_lock;
    atomic64_t next_extent_id;
};


struct extent_page {
    struct list_head list;
    phys_addr_t phys_addr;
    unsigned long vaddr;
};

struct extent {
    struct rb_node node;
    phys_addr_t start_phys;
    phys_addr_t end_phys;
    unsigned long start_vaddr;
    unsigned long end_vaddr;
    unsigned long num_pages;
    unsigned long extent_id;

    struct list_head page_list;
    spinlock_t page_list_lock;
};

void rb_insert_extent(struct extent_table *table, struct extent *ext);
void rb_delete_extent(struct extent_table *table, struct extent *ext);
struct extent *rb_search_extent_eq_or_less(struct extent_table *table, phys_addr_t addr);
struct extent *rb_search_extent_eq_or_greater(struct extent_table *table, phys_addr_t addr);

void add_to_extents(phys_addr_t paddr, unsigned long vaddr);
void print_ext_tbl(struct extent_table* ex_tlb);

#endif // __LINUX_MM_EXTENTS_H
