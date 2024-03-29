#include <linux/fs.h>
#include <linux/module.h>
#include <linux/backing-dev.h>
#include <linux/init.h>
#include <linux/f2fs_fs.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/sched/signal.h>
#include <linux/mutex.h>
// #include <include/linux/xarray.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "hc.h"
#include "kmeans.h"
// #include <trace/events/f2fs.h>

static DEFINE_MUTEX(list_lock);
static struct kmem_cache *hotness_entry_slab;
struct kmem_cache *hotness_entry_info_slab;
struct hc_list *hc_list_ptr;
nid_t last_ino;
char segment_valid[MAX_SEGNO];

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
	mutex_lock(&list_lock);
	list_add_tail_rcu(&new_he->list, &hc_list_ptr->ilist);
	mutex_unlock(&list_lock);
	synchronize_rcu();
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
		mutex_lock(&list_lock);
		list_del_rcu(&he->list);
		mutex_unlock(&list_lock);
		synchronize_rcu();
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

	struct file *fp;
	loff_t pos = 0;
	// fp = filp_open("/tmp/f2fs_hotness", O_RDWR, 0644);
	fp = filp_open("/tmp/f2fs_hotness_no", O_RDWR, 0644);
	if (IS_ERR(fp)) {
		printk("failed to open /tmp/f2fs_hotness.\n");
		sbi->total_writed_block_count = 0;
		sbi->n_clusters = N_CLUSTERS;
		sbi->centers = kmalloc(sizeof(unsigned int) * sbi->n_clusters, GFP_KERNEL);
		sbi->centers_valid = 0;
		goto out;
	}

	printk(">>>>>>>>>>>\n");
	unsigned int n_clusters;
	kernel_read(fp, &n_clusters, sizeof(n_clusters), &pos);
	printk("n_clusters = %u, pos = %llu\n", n_clusters, pos);
	sbi->n_clusters = n_clusters;

	// read centers
	unsigned int i;
	unsigned int *centers = kmalloc(sizeof(unsigned int) * sbi->n_clusters, GFP_KERNEL);
	for(i = 0; i < n_clusters; ++i) {
		kernel_read(fp, &centers[i], sizeof(centers[i]), &pos);
		printk("%u, 0x%x\n", centers[i], centers[i]);
	}
	sbi->centers = centers;
	sbi->centers_valid = 1;

	// read count
	unsigned int count;
	kernel_read(fp, &count, sizeof(count), &pos);
	printk("%u, 0x%x\n", count, count);
	sbi->total_writed_block_count = count;

	// read blk_addr & IRR & LWS for each block to init hc_list
	block_t blk_addr_tmp;
	unsigned int IRR_tmp;
	unsigned int LWS_tmp;
	struct hotness_entry_info *new_hei = NULL;
	for(i = 0; i < count; i++) {
		kernel_read(fp, &blk_addr_tmp, sizeof(blk_addr_tmp), &pos);
		kernel_read(fp, &IRR_tmp, sizeof(IRR_tmp), &pos);
		kernel_read(fp, &LWS_tmp, sizeof(LWS_tmp), &pos);
		insert_hotness_entry(sbi, blk_addr_tmp, &IRR_tmp, &LWS_tmp, new_hei);
		printk("%u, %u, %u\n", blk_addr_tmp, IRR_tmp, LWS_tmp);
	}

	filp_close(fp, NULL);
out:
	return;
}

void f2fs_build_hc_manager(struct f2fs_sb_info *sbi)
{
	printk("In f2fs_build_hc_manager\n");
	init_hc_management(sbi);
	last_ino = __UINT32_MAX__;
	memset(segment_valid, 0, MAX_SEGNO);
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

	struct file *fp;
	loff_t pos = 0;
	fp = filp_open("/tmp/f2fs_hotness", O_RDWR|O_CREAT, 0644);
	if (IS_ERR(fp)) goto out;

	// save n_clusters
	kernel_write(fp, &sbi->n_clusters, sizeof(sbi->n_clusters), &pos);
	printk("pos = 0x%x\n", pos);
	// save centers
	unsigned int i;
	for(i = 0; i < sbi->n_clusters; i++) {
		kernel_write(fp, &sbi->centers[i], sizeof(sbi->centers[i]), &pos);
		printk("%u, 0x%x\n", sbi->centers[i], sbi->centers[i]);
	}
	printk("pos = 0x%x\n", pos);
	// save total_writed_block_count
	kernel_write(fp, &sbi->total_writed_block_count, sizeof(sbi->total_writed_block_count), &pos);
	printk("%u, 0x%x\n", sbi->total_writed_block_count, sbi->total_writed_block_count);
	printk("pos = 0x%x\n", pos);
	// save blk_addr & IRR & LWS for each hotness_entry
	struct hotness_entry *he, *tmp;
	rcu_read_lock();
	list_for_each_entry_rcu(he, &hc_list_ptr->ilist, list){
		kernel_write(fp, &he->blk_addr, sizeof(he->blk_addr), &pos);
		kernel_write(fp, &he->IRR, sizeof(he->IRR), &pos);
		kernel_write(fp, &he->LWS, sizeof(he->LWS), &pos);
		// printk("%u, 0x%x\n", he->blk_addr, he->blk_addr);
	}
	rcu_read_unlock();
	printk("pos = 0x%x\n", pos);
	
	filp_close(fp, NULL);
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

	// f2fs_bug_on(sbi, hc_list_ptr->count);
	// f2fs_bug_on(sbi, !list_empty(&hc_list_ptr->ilist));
}