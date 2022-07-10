#include <linux/fs.h>
#include <linux/module.h>
#include <linux/backing-dev.h>
#include <linux/init.h>
#include <linux/f2fs_fs.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/sched/signal.h>
// #include <include/linux/xarray.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "hc.h"
// #include <trace/events/f2fs.h>

static struct kmem_cache *hotness_entry_slab;
struct hc_list *hc_list_ptr;

/* 热度元数据操作 */
/* 1、添加 */
// create
void insert_hotness_entry(
    struct f2fs_sb_info *sbi, 
    block_t blkaddr, 
    unsigned int *IRR, 
    unsigned int *LWS){
	/* 
	1、基数树中添加以blkaddr为index的节点
	2、创建blkaddr热度元数据内存对象 */
	printk("In insert_hotness_entry\n");
	printk("*IRR = %u, *LWS = %u", *IRR, *LWS);
	struct hotness_entry *new_he;
	new_he = f2fs_kmem_cache_alloc(hotness_entry_slab, GFP_NOFS);
	new_he->blk_addr = blkaddr;
	new_he->IRR = *IRR;
	new_he->LWS = *LWS;
	hc_list_ptr->count++;
	f2fs_radix_tree_insert(&hc_list_ptr->iroot, blkaddr, new_he);
	printk("Before list_add_tail\n");
	printk("new_he = 0x%p, new_he->list in 0x%p", new_he, &new_he->list);
	printk("ilist in 0x%p, next = 0x%p, prev = 0x%p, prev->next = 0x%p", &hc_list_ptr->ilist, hc_list_ptr->ilist.next, hc_list_ptr->ilist.prev, hc_list_ptr->ilist.prev->next);
	list_add_tail(&new_he->list, &hc_list_ptr->ilist);
	printk("After list_add_tail\n");
	printk("ilist in 0x%p, next = 0x%p, prev = 0x%p, prev->next = 0x%p", &hc_list_ptr->ilist, hc_list_ptr->ilist.next, hc_list_ptr->ilist.prev, hc_list_ptr->ilist.prev->next);
}

/* 2、查询 */
int lookup_hotness_entry(
    struct f2fs_sb_info *sbi, 
    block_t blkaddr, 
    unsigned int *IRR, 
    unsigned int *LWS){
	printk("In lookup_hotness_entry, xa_head = 0x%p\n", hc_list_ptr->iroot.xa_head);
	/* 
	1、基数树中查询以blkaddr为index的节点
	2、释放blkaddr热度元数据内存对象 */
	struct hotness_entry *he;
	if (hc_list_ptr->iroot.xa_head == NULL) goto no_xa_head;
	printk("iroot in 0x%p, blkaddr = %u\n", &hc_list_ptr->iroot, blkaddr);
	he = radix_tree_lookup(&hc_list_ptr->iroot, blkaddr);
	printk("radix_tree_lookup returns 0x%p\n", he);
	if (he) {
        *IRR = he->IRR;
        *LWS = he->LWS;
        return 0;
    } else {
no_xa_head:
		printk("In lookup_hotness_entry, no_xa_head\n");
        *IRR = __UINT32_MAX__;
        *LWS = sbi->total_writed_block_count;
		printk("*IRR = %u, *LWS = %u\n", *IRR, *LWS);
        return -1;
    }
}

/* 3、移除 */
void delete_hotness_entry(
    struct f2fs_sb_info *sbi, 
    block_t blkaddr){
	/* 
	1、基数树中移除以blkaddr为index的节点
	2、释放blkaddr热度元数据内存对象 */
	printk("In delete_hotness_entry\n");
	struct hotness_entry *he;
	he = radix_tree_lookup(&hc_list_ptr->iroot, blkaddr);
	printk("delete_hotness_entry find 0x%p\n", he);
	if (he) {
		hc_list_ptr->count--;
		radix_tree_delete(&hc_list_ptr->iroot, he->blk_addr);
		list_del(&he->list);
		kmem_cache_free(hotness_entry_slab, he);
	}
}

int f2fs_create_hotness_clustering_cache(void)
{
	printk("In f2fs_create_hotness_clustering_cache\n");
	hotness_entry_slab = f2fs_kmem_cache_create("f2fs_hotness_entry",
					sizeof(struct hotness_entry));
	printk("Finish f2fs_kmem_cache_create\n");
	if (!hotness_entry_slab)
		return -ENOMEM;
	return 0;
}

static void init_hc_management(struct f2fs_sb_info *sbi)
{
	int err;
	printk("In init_hc_management\n");
	err = f2fs_create_hotness_clustering_cache();
	if (err)
		printk("f2fs_create_hotness_clustering_cache error.\n");
	sbi->total_writed_block_count = 0;
	static struct hc_list hc_list_var = {
		.ilist = LIST_HEAD_INIT(hc_list_var.ilist),
		.iroot = RADIX_TREE_INIT(hc_list_var.iroot, GFP_NOFS),
	};
	hc_list_ptr = &hc_list_var;
	printk(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	printk("ilist size: %lu\n", sizeof(hc_list_ptr->ilist));
	printk("iroot size: %lu\n", sizeof(hc_list_ptr->iroot));
	printk("hc_list_ptr = 0x%p\n", hc_list_ptr);
	printk("0x%p, 0x%p", hc_list_var.ilist.next, hc_list_var.ilist.prev);
	printk("%u, %u, 0x%p\n", hc_list_var.iroot.xa_lock, hc_list_var.iroot.xa_flags, hc_list_var.iroot.xa_head);
}

void f2fs_build_hc_manager(struct f2fs_sb_info *sbi)
{
	printk("****************************\n");
	printk("In f2fs_build_hc_manager\n");
	init_hc_management(sbi);
	printk("Finish f2fs_build_hc_manager\n");
}