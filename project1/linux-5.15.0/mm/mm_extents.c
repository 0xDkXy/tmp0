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

    write_lock(&table->tree_lock);

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

    write_unlock(&table->tree_lock);
}

struct extent *rb_search_extent_eq_or_less(struct extent_table *table, phys_addr_t addr)
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
    pr_info("enter %s\n", __func__);
    node = table->tree.rb_node;
    best = NULL;

    read_lock(&table->tree_lock);

    while (node) {
        struct extent *entry = rb_entry(node, struct extent, node);

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
    pr_info("enter %s\n", __func__);
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
    write_lock(&table->tree_lock);
    rb_erase(&ext->node, &table->tree);
    write_unlock(&table->tree_lock);

    spin_lock(&ext->page_list_lock);
    list_for_each_entry_safe(page, tmp, &ext->page_list, list) {
        list_del(&page->list);
        kfree(page);
    }
    spin_unlock(&ext->page_list_lock);

    kfree(ext);
}

void init_extent(struct extent *ext, phys_addr_t addr, unsigned long extent_id)
{
    ext->start_phys = addr;
    ext->end_phys = addr + PAGE_SIZE - 1;
    ext->num_pages = 1;
    ext->extent_id = extent_id;
    INIT_LIST_HEAD(&ext->page_list);
    spin_lock_init(&ext->page_list_lock);
}

void add_page_to_extent(struct extent *ext, phys_addr_t addr)
{
    struct extent_page *new_page = kzalloc(sizeof(struct extent_page), GFP_KERNEL);
    if (!new_page)
        return;

    new_page->phys_addr = addr;
    spin_lock(&ext->page_list_lock);
    list_add_tail(&new_page->list, &ext->page_list);
    ext->num_pages++;
    ext->end_phys = addr + PAGE_SIZE - 1;
    spin_unlock(&ext->page_list_lock);
}

struct extent *create_and_insert_extent(struct extent_table *table, phys_addr_t phys_addr)
{
    unsigned long extent_id;
    struct extent *prev, *next;
    struct extent *ext = NULL;
    pr_info("enter %s\n", __func__);

    write_lock(&table->tree_lock);

    prev = NULL;
    next = NULL;
    pr_info("%s before prev\n", __func__);
    prev = rb_search_extent_eq_or_less(table, phys_addr);
    pr_info("%s before next\n", __func__);
    next = rb_search_extent_eq_or_greater(table, phys_addr);
    pr_info("%s after next\n", __func__);

    table->size += 1;

    if (prev && prev->end_phys + 1 == phys_addr) {
        pr_info("prev and next\n");
        add_page_to_extent(prev, phys_addr);
        if (next && phys_addr + PAGE_SIZE == next->start_phys) {
            // merge next into prev
            spin_lock(&next->page_list_lock);
            spin_lock(&prev->page_list_lock);
            list_splice_tail_init(&next->page_list, &prev->page_list);
            prev->end_phys = next->end_phys;
            prev->num_pages += next->num_pages;
            spin_unlock(&prev->page_list_lock);
            spin_unlock(&next->page_list_lock);
            rb_delete_extent(table, next);
        }
        write_unlock(&table->tree_lock);
        return prev;
    } else if (next && phys_addr + PAGE_SIZE == next->start_phys) {
        // FIXME: we chagne start physical address here, which is the key
        // of rbtree, so we have to delete this node and insert a new one.
        pr_info("only next\n");
        ext = kzalloc(sizeof(struct extent), GFP_KERNEL);
        if (!ext) {
            write_unlock(&table->tree_lock);
            return NULL;
        }
        extent_id = next->extent_id;
        init_extent(ext, phys_addr, extent_id);

        spin_lock(&(ext->page_list_lock));
        spin_lock(&(next->page_list_lock));

        ext->end_phys = next->end_phys;
        ext->num_pages = next->num_pages + 1;
        list_splice_tail_init(&next->page_list, &ext->page_list);
        spin_unlock(&(next->page_list_lock));
        spin_unlock(&(ext->page_list_lock));

        rb_delete_extent(table, next);
        rb_insert_extent(table, ext);
        write_unlock(&table->tree_lock);
        return ext;
    }

    // create new extent
    pr_info("no prev no next\n");
    ext = kzalloc(sizeof(struct extent), GFP_ATOMIC);
    if (!ext) {
        write_unlock(&table->tree_lock);
        return NULL;
    }
    extent_id = atomic64_inc_return(&table->next_extent_id);
    init_extent(ext, phys_addr, extent_id);
    // add_page_to_extent(ext, phys_addr);
    rb_insert_extent(table, ext);

    write_unlock(&table->tree_lock);
    return ext;
}

void add_to_extents(phys_addr_t paddr, unsigned long vaddr)
{
    pr_info("enter %s\n", __func__);
    create_and_insert_extent(&(current->mm->ex_tlb), paddr);
}
