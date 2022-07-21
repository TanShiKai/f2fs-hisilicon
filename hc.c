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
#include "kmeans.h"
// #include <trace/events/f2fs.h>

static struct kmem_cache *hotness_entry_slab;
struct kmem_cache *hotness_entry_info_slab;
struct hc_list *hc_list_ptr;
// nid_t last_ino;

/* 热度元数据操作 */
/* 1、添加 */
// create
void insert_hotness_entry(struct f2fs_sb_info *sbi, block_t blkaddr, unsigned int *IRR, unsigned int *LWS, struct hotness_entry_info *hei)
{
	/* 
	1、基数树中添加以blkaddr为index的节点
	2、创建blkaddr热度元数据内存对象 */
	// printk("In insert_hotness_entry\n");
	// printk("*IRR = %u, *LWS = %u", *IRR, *LWS);
	struct hotness_entry *new_he;
	new_he = f2fs_kmem_cache_alloc(hotness_entry_slab, GFP_NOFS);
	new_he->blk_addr = blkaddr;
	new_he->IRR = *IRR;
	new_he->LWS = *LWS;
	new_he->hei = hei;
	hc_list_ptr->count++;
	f2fs_radix_tree_insert(&hc_list_ptr->iroot, blkaddr, new_he);
	// printk("Before list_add_tail\n");
	// printk("new_he = 0x%p, new_he->list in 0x%p", new_he, &new_he->list);
	// printk("ilist in 0x%p, next = 0x%p, prev = 0x%p, prev->next = 0x%p", &hc_list_ptr->ilist, hc_list_ptr->ilist.next, hc_list_ptr->ilist.prev, hc_list_ptr->ilist.prev->next);
	list_add_tail(&new_he->list, &hc_list_ptr->ilist);
	// printk("After list_add_tail\n");
	// printk("ilist in 0x%p, next = 0x%p, prev = 0x%p, prev->next = 0x%p", &hc_list_ptr->ilist, hc_list_ptr->ilist.next, hc_list_ptr->ilist.prev, hc_list_ptr->ilist.prev->next);
}

/* 2、查询 */
int lookup_hotness_entry(struct f2fs_sb_info *sbi, block_t blkaddr, unsigned int *IRR, unsigned int *LWS)
{
	// printk("In lookup_hotness_entry, xa_head = 0x%p\n", hc_list_ptr->iroot.xa_head);
	/* 
	1、基数树中查询以blkaddr为index的节点
	2、释放blkaddr热度元数据内存对象 */
	struct hotness_entry *he;
	if (hc_list_ptr->iroot.xa_head == NULL) goto no_xa_head;
	// printk("iroot in 0x%p, blkaddr = %u\n", &hc_list_ptr->iroot, blkaddr);
	he = radix_tree_lookup(&hc_list_ptr->iroot, blkaddr);
	// printk("radix_tree_lookup returns 0x%p\n", he);
	if (he) {
        *IRR = he->IRR;
        *LWS = he->LWS;
        return 0;
    } else {
no_xa_head:
		// printk("In lookup_hotness_entry, no_xa_head\n");
        *IRR = __UINT32_MAX__;
        *LWS = sbi->total_writed_block_count;
		// printk("*IRR = %u, *LWS = %u\n", *IRR, *LWS);
        return -1;
    }
}

/* 3、移除 */
void delete_hotness_entry(struct f2fs_sb_info *sbi, block_t blkaddr)
{
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
	// printk("In f2fs_create_hotness_clustering_cache\n");
	hotness_entry_slab = f2fs_kmem_cache_create("f2fs_hotness_entry", sizeof(struct hotness_entry));
	// printk("Finish f2fs_kmem_cache_create\n");
	if (!hotness_entry_slab)
		return -ENOMEM;
	hotness_entry_info_slab = f2fs_kmem_cache_create("f2fs_hotness_entry_info", sizeof(struct hotness_entry_info));
	if (!hotness_entry_info_slab)
		return -ENOMEM;
	return 0;
}

void f2fs_destroy_hotness_clustering_cache(void)
{
	kmem_cache_destroy(hotness_entry_slab);
	kmem_cache_destroy(hotness_entry_info_slab);
}

static void init_hc_management(struct f2fs_sb_info *sbi)
{
	int err;
	// printk("In init_hc_management\n");
	err = f2fs_create_hotness_clustering_cache();
	if (err)
		printk("f2fs_create_hotness_clustering_cache error.\n");
	static struct hc_list hc_list_var = {
		.ilist = LIST_HEAD_INIT(hc_list_var.ilist),
		.iroot = RADIX_TREE_INIT(hc_list_var.iroot, GFP_NOFS),
	};
	hc_list_ptr = &hc_list_var;

	sbi->total_writed_block_count = 0;
	sbi->n_clusters = N_CLUSTERS;
	sbi->centers = kmalloc(sizeof(unsigned int) * sbi->n_clusters, GFP_KERNEL);
	sbi->centers_valid = 0;
	
	// last_ino = kmalloc(sizeof(nid_t), GFP_KERNEL);
	// printk(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	// printk("ilist size: %lu\n", sizeof(hc_list_ptr->ilist));
	// printk("iroot size: %lu\n", sizeof(hc_list_ptr->iroot));
	// printk("hc_list_ptr = 0x%p\n", hc_list_ptr);
	// printk("0x%p, 0x%p", hc_list_var.ilist.next, hc_list_var.ilist.prev);
	// printk("%u, %u, 0x%p\n", hc_list_var.iroot.xa_lock, hc_list_var.iroot.xa_flags, hc_list_var.iroot.xa_head);

	struct file *fp;
	loff_t pos = 0;
	// char buf[100];
	// memset(buf, 0, 100);
	unsigned int capacity = 1000000;
	char *buf = kmalloc(capacity, GFP_KERNEL);
	if (!buf) {
		printk("kmalloc buffer failed!\n");
		goto out;
	}
	memset(buf, 0, capacity);
	fp = filp_open("/tmp/f2fs_hotness", O_RDWR|O_CREAT, 0644);
	if (IS_ERR(fp)) {
		printk("failed to open /tmp/f2fs_hotness.\n");
		goto out;
	}
	// printk(">>>>>>>>>>>\n");
	unsigned int n_clusters;
	kernel_read(fp, &n_clusters, sizeof(unsigned int), &pos);
	printk("n_clusters = %u, pos = %llu\n", n_clusters, pos);


	
	// sscanf(buf, "%u ", &n_clusters);
	// printk("n_clusters = %u\n", n_clusters);
	// if (n_clusters <= 3) {
	// 	int i;
	// 	for (i = 0; i < n_clusters; i++) {
	// 		sscanf(buf, "%u ", &sbi->centers[i]);
	// 		printk("%u", sbi->centers[i]);
	// 	}
	// }

	filp_close(fp, NULL);
out:
	return;
}

void f2fs_build_hc_manager(struct f2fs_sb_info *sbi)
{
	printk("In f2fs_build_hc_manager\n");
	init_hc_management(sbi);
	printk("Finish f2fs_build_hc_manager\n");
}

static int kmeans_thread_func(void *data)
{
	printk("In kmeans_thread_func\n");
	struct f2fs_sb_info *sbi = data;
	struct f2fs_hc_kthread *hc_th = sbi->hc_thread;
	wait_queue_head_t *wq = &sbi->hc_thread->hc_wait_queue_head;
	unsigned int wait_ms;

	wait_ms = hc_th->min_sleep_time;

	set_freezable();
	do {
		wait_event_interruptible_timeout(*wq, kthread_should_stop() || freezing(current), msecs_to_jiffies(wait_ms));
		int err = f2fs_hc(hc_list_ptr, sbi);
		if (!err) sbi->centers_valid = 1;
	} while (!kthread_should_stop());
	return 0;
}

int f2fs_start_hc_thread(struct f2fs_sb_info *sbi)
{
	printk("In f2fs_start_hc_thread\n");
    struct f2fs_hc_kthread *hc_th;
	dev_t dev = sbi->sb->s_bdev->bd_dev;
	int err = 0;

	hc_th = f2fs_kmalloc(sbi, sizeof(struct f2fs_hc_kthread), GFP_KERNEL);
	if (!hc_th) {
		err = -ENOMEM;
		goto out;
	}

	hc_th->min_sleep_time = DEF_HC_THREAD_MIN_SLEEP_TIME;
	hc_th->max_sleep_time = DEF_HC_THREAD_MAX_SLEEP_TIME;
	hc_th->no_hc_sleep_time = DEF_HC_THREAD_NOHC_SLEEP_TIME;

    sbi->hc_thread = hc_th;
	init_waitqueue_head(&sbi->hc_thread->hc_wait_queue_head);
    sbi->hc_thread->f2fs_hc_task = kthread_run(kmeans_thread_func, sbi,
			"f2fs_hc-%u:%u", MAJOR(dev), MINOR(dev));
	if (IS_ERR(hc_th->f2fs_hc_task)) {
		err = PTR_ERR(hc_th->f2fs_hc_task);
		kfree(hc_th);
		sbi->hc_thread = NULL;
	}
out:
	return err;
}

void f2fs_stop_hc_thread(struct f2fs_sb_info *sbi) 
{
	printk("In f2fs_stop_hc_thread");
	struct f2fs_hc_kthread *hc_th = sbi->hc_thread;

	if (!hc_th)
		return;
	kthread_stop(hc_th->f2fs_hc_task);
	kfree(hc_th);
	sbi->hc_thread = NULL;
}

/**
 * @brief 把热度元数据写到flash
 * 
 * @param sbi 
 */
void save_hotness_entry(struct f2fs_sb_info *sbi)
{
	printk("In save_hotness_entry");
	struct hotness_entry *he, *tmp;
	unsigned int capacity = 1000000;
	unsigned int idx = 0;
	char *buf = kmalloc(capacity, GFP_KERNEL);
	if (!buf)  {
		printk("kmalloc buffer failed!\n");
		goto out;
	}
	memset(buf, 0, capacity);
	// char *msg = "tsk is a good boy.\n";
	// memcpy(buf, msg, strlen(msg));
	// strcpy(buf, msg);
	// idx += strlen(msg);
	// sprintf(buf, "%s\n", msg);
	
	// printk("buf = %s\n", buf);
	// struct seq_file *s = kmalloc(sizeof(struct seq_file), GFP_KERNEL);
	// if (!s) {
	// 	printk("kmalloc seq_file failed!\n");
	// 	goto out;
	// }
	// printk("%s, %u, %u, %u", s->buf, s->count, s->from, s->size);
	// seq_printf(s, "tsk is a good boy.\n");
	// printk("%s, %lu\n", s->buf, sizeof(s->buf));
	// kfree(s);	

	struct file *fp;
	loff_t pos = 0;
	fp = filp_open("/tmp/f2fs_hotness", O_RDWR|O_CREAT, 0644);
	if (IS_ERR(fp)) goto out;
	kernel_write(fp, &sbi->n_clusters, sizeof(sbi->n_clusters), &pos);
	printk("pos = %llu\n", pos);
	/* 保存质心、元数据 */
	// sprintf(buf, "%u ", sbi->n_clusters);
	// kernel_write(fp, buf, strlen(buf), &pos);
	// int i;
	// for (i = 0; i < sbi->n_clusters; i++) {
	// 	sprintf(buf, "%u ", sbi->centers[i]);
	// 	kernel_write(fp, buf, strlen(buf), &pos);
	// }
	// sprintf(buf, "%s", "\n");
	// kernel_write(fp, buf, strlen(buf), &pos);
	// sprintf(buf, "%u\n", hc_list_ptr->count);
	// kernel_write(fp, buf, strlen(buf), &pos);
	// list_for_each_entry_safe(he, tmp, &hc_list_ptr->ilist, list) {
	// 	sprintf(buf, "%u, %u\n", he->IRR, he->LWS);
	// 	printk("buf = %s, pos = %llu\n", buf, pos);
	// 	kernel_write(fp, buf, strlen(buf), &pos);
	// }
	filp_close(fp, NULL);
	kfree(buf);
out:
	return;
}

/**
 * @brief 释放内存占用
 */
void release_hotness_entry(struct f2fs_sb_info *sbi)
{
	printk("In release_hotness_entry");
	struct hotness_entry *he, *tmp;
	list_for_each_entry_safe(he, tmp, &hc_list_ptr->ilist, list) {
		list_del(&he->list);
		kmem_cache_free(hotness_entry_slab, he);
		hc_list_ptr->count--;
	}
	INIT_RADIX_TREE(&hc_list_ptr->iroot, GFP_NOFS);
	kfree(sbi->centers);
	// kfree(last_ino);

	// f2fs_bug_on(sbi, hc_list_ptr->count);
	// f2fs_bug_on(sbi, !list_empty(&hc_list_ptr->ilist));
}