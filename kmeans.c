#include <linux/fs.h>
#include <linux/module.h>
#include <linux/f2fs_fs.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "hc.h"
#include "kmeans.h"

int f2fs_hc(struct hc_list *hc_list_ptr, struct f2fs_sb_info *sbi)
{
    printk("Doing f2fs_hc...\n");
    return 0;
}