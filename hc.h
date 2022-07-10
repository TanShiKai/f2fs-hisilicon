#ifndef _LINUX_HC_H
#define _LINUX_HC_H

/* 热度定义 */
struct hotness_entry
{
	block_t blk_addr;/* 块地址 */
	unsigned int IRR;/* 最近两次更新间隔时间 */
	unsigned int LWS;/* 最后一次更新时间 */
	struct list_head list;
	struct radix_tree_node rt_node;
};

/* 热度元数据组织 */
struct hc_list {
	struct list_head ilist; // 16 bytes
	struct radix_tree_root iroot; // 16 bytes
	unsigned int count;
};
extern struct hc_list *hc_list_ptr;

/* 热度聚类 */
struct f2fs_hc_kthread {
	struct task_struct *f2fs_hc_task;

	/* for hc sleep time */
	unsigned int min_sleep_time;
	unsigned int max_sleep_time;
	unsigned int no_hc_sleep_time;
};

void insert_hotness_entry(
    struct f2fs_sb_info *sbi, 
    block_t blkaddr, 
    unsigned int *IRR, 
    unsigned int *LWS);
int lookup_hotness_entry(
    struct f2fs_sb_info *sbi, 
    block_t blkaddr, 
    unsigned int *IRR, 
    unsigned int *LWS);
void delete_hotness_entry(
    struct f2fs_sb_info *sbi, 
    block_t blkaddr);

#endif