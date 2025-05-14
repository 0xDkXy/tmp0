#include <linux/mm_extents.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/list.h>
#include <linux/slab.h>


void rb_insert_extent(struct extent_table *table, struct extent *new_extent)
{
    struct rb_node **link = &table->tree.rb_node;
    struct rb_node *parent = NULL;
    struct extent *entry;

    // write_lock(&table->tree_lock);

    while (*link) {
        parent = *link;
        entry = rb_entry(parent, struct extent, node);

        if (new_extent->start_phys < entry->start_phys)
            link = &(*link)->rb_left;
        else if (new_extent->start_phys > entry->start_phys)
            link = &(*link)->rb_right;
        else {
            write_unlock(&table->tree_lock);
            return; // duplicate start_phys not allowed
        }
    }

    rb_link_node(&new_extent->node, parent, link);
    rb_insert_color(&new_extent->node, &table->tree);

    // write_unlock(&table->tree_lock);
}

struct extent *rb_search_extent_eq_or_less(struct extent_table *table, phys_addr_t addr)
{
    struct rb_node *node;
    struct extent *best;
    if (table == NULL) {
        pr_err("%s: table is NULL\n", __func__);
        return NULL;
    }
    node = table->tree.rb_node;
    best = NULL;

    read_lock(&(table->tree_lock));

    while (node) {
        struct extent *entry;
        entry = rb_entry(node, struct extent, node);

        if (entry->start_phys <= addr) {
            best = entry;
            node = node->rb_right;
        } else {
            node = node->rb_left;
        }
    }
    read_unlock(&table->tree_lock);
    return best;
}

struct extent *rb_search_extent_eq_or_greater(struct extent_table *table, phys_addr_t addr)
{
    struct rb_node *node;
    struct extent *best;
    if (table == NULL) {
        pr_err("%s: table is NULL\n", __func__);
        return NULL;
    }
    if (table->size == 0) {
        return NULL;
    }
    node = table->tree.rb_node;
    best = NULL;

    read_lock(&table->tree_lock);

    while (node) {
        struct extent *entry = rb_entry(node, struct extent, node);

        if (entry->start_phys >= addr) {
            best = entry;
            node = node->rb_left;
        } else {
            node = node->rb_right;
        }
    }

    read_unlock(&table->tree_lock);
    return best;
}

void rb_delete_extent(struct extent_table *table, struct extent *ext)
{
    struct extent_page *page, *tmp;
    rb_erase(&ext->node, &table->tree);

    spin_lock(&ext->page_list_lock);
    list_for_each_entry_safe(page, tmp, &ext->page_list, list) {
        list_del(&page->list);
        kfree(page);
    }
    spin_unlock(&ext->page_list_lock);

    kfree(ext);
}

void init_extent(struct extent *ext, phys_addr_t addr, unsigned long vaddr,
		 unsigned long extent_id)
{
    ext->start_phys = addr;
    ext->start_vaddr = vaddr;
    ext->end_phys = addr + PAGE_SIZE - 1;
    ext->end_vaddr = vaddr + PAGE_SIZE - 1;
    ext->num_pages = 1;
    ext->extent_id = extent_id;
    INIT_LIST_HEAD(&ext->page_list);
    spin_lock_init(&ext->page_list_lock);
}

void add_page_to_extent(struct extent *ext, phys_addr_t addr,
			unsigned long vaddr)
{
    struct extent_page *new_page = kzalloc(sizeof(struct extent_page), GFP_KERNEL);
    if (!new_page)
        return;

    new_page->phys_addr = addr;
    new_page->vaddr = vaddr;
    list_add_tail(&new_page->list, &ext->page_list);
    ext->num_pages++;
    ext->end_phys = addr + PAGE_SIZE - 1;
    ext->end_vaddr = vaddr + PAGE_SIZE - 1;
}

struct extent *create_and_insert_extent(struct extent_table *table,
					phys_addr_t phys_addr,
					unsigned long vaddr)
{

    unsigned long extent_id;
    struct extent *prev, *next;
    struct extent *ext = NULL;
    // pr_info("enter %s\n", __func__);

    prev = NULL;
    next = NULL;
    prev = rb_search_extent_eq_or_less(table, phys_addr);
    next = rb_search_extent_eq_or_greater(table, phys_addr);

    // pr_info("%s: got prev and next\n", __func__);
    table->size += 1;

    write_lock(&table->tree_lock);
    if (prev && prev->end_phys + 1 == phys_addr &&
            prev->end_vaddr + 1 == vaddr) {
        // pr_info("%s: prev\n", __func__);

        spin_lock(&prev->page_list_lock);

        add_page_to_extent(prev, phys_addr, vaddr);

        spin_unlock(&prev->page_list_lock);

        if (next && phys_addr + PAGE_SIZE == next->start_phys &&
                vaddr + PAGE_SIZE == next->start_vaddr) {

            // pr_info("%s: prev and next\n", __func__);

            // merge next into prev
            spin_lock(&next->page_list_lock);
            spin_lock(&prev->page_list_lock);

            list_splice_tail_init(&next->page_list, &prev->page_list);

            prev->end_phys = next->end_phys;
            prev->end_vaddr = next->end_vaddr;
            prev->num_pages += next->num_pages;

            spin_unlock(&prev->page_list_lock);
            spin_unlock(&next->page_list_lock);

            rb_delete_extent(table, next);
        }
        write_unlock(&table->tree_lock);
        return prev;
    } else if (next && phys_addr + PAGE_SIZE == next->start_phys &&
            vaddr + PAGE_SIZE == next->start_vaddr) {
        // FIXME: we chagne start physical address here, which is the key
        // of rbtree, so we have to delete this node and insert a new one.
        // pr_info("%s: only next\n", __func__);
        ext = kzalloc(sizeof(struct extent), GFP_KERNEL);
        if (!ext) {
            write_unlock(&table->tree_lock);
            return NULL;
        }

        extent_id = next->extent_id;
        init_extent(ext, phys_addr, vaddr, extent_id);

        spin_lock(&(ext->page_list_lock));
        spin_lock(&(next->page_list_lock));

        add_page_to_extent(ext, phys_addr, vaddr);

        ext->end_phys = next->end_phys;
        ext->end_vaddr = next->end_vaddr;
        ext->num_pages = next->num_pages + 1;

        list_splice_tail_init(&next->page_list, &ext->page_list);

        spin_unlock(&(next->page_list_lock));
        spin_unlock(&(ext->page_list_lock));

        rb_delete_extent(table, next);
        rb_insert_extent(table, ext);

        write_unlock(&table->tree_lock);
        return ext;
    }

    // pr_info("%s: no prev no next\n", __func__);

    // create new extent
    ext = kzalloc(sizeof(struct extent), GFP_ATOMIC);
    if (!ext) {
        write_unlock(&table->tree_lock);
        return NULL;
    }

    extent_id = atomic64_inc_return(&table->next_extent_id);
    init_extent(ext, phys_addr, vaddr, extent_id);

    spin_lock(&ext->page_list_lock);

    add_page_to_extent(ext, phys_addr, vaddr);

    spin_unlock(&ext->page_list_lock);

    rb_insert_extent(table, ext);

    write_unlock(&table->tree_lock);
    return ext;
}

void print_ext(struct extent* _ext)
{

    struct extent_page *pos, *tmp;
    spin_lock(&(_ext->page_list_lock));

    pr_info("\nEXTID: %ld\n", _ext->extent_id);
    list_for_each_entry_safe(pos, tmp, &_ext->page_list, list) {
        pr_info("\tphysical addr: 0x%llx, vaddr: 0x%lx\n", pos->phys_addr, pos->vaddr);

    }
    pr_info("\n");

    spin_unlock(&(_ext->page_list_lock));
}

void add_to_extents(phys_addr_t paddr, unsigned long vaddr)
{
    struct extent* _ext;
    // pr_info("enter %s\n", __func__);
    // pr_info("%s line %d: tree size: %d\n", __func__, __LINE__, current->mm->ex_tlb.size);
    _ext = create_and_insert_extent(&(current->mm->ex_tlb), paddr, vaddr);
    // print_ext(_ext);
}

void print_ext_tbl(struct extent_table* ex_tlb)
{
    struct rb_node* node;
    struct extent* _ext;
    int i;

    read_lock(&ex_tlb->tree_lock);
    
    pr_info("\n PRINT ALL NODES: NODE SIZE: %d\n", ex_tlb->size);

    for(node = rb_first(&ex_tlb->tree), i = 0; node; node = rb_next(node), i++) {

        pr_info("NODE %d\n", i);
        _ext = rb_entry(node, struct extent, node);
        print_ext(_ext);

    }
    pr_info("\n");

    read_unlock(&ex_tlb->tree_lock);
}
