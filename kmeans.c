#include <linux/fs.h>
#include <linux/module.h>
#include <linux/f2fs_fs.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "hc.h"
#include "kmeans.h"

#define diff(a, b) (a) < (b) ? ((b) - (a)) : ((a) - (b))
#define MIN_3(a, b, c) ((a) < (b)) ? (((a) < (c)) ? CURSEG_HOT_DATA : CURSEG_COLD_DATA) : (((c) > (b)) ? CURSEG_WARM_DATA : CURSEG_COLD_DATA)
#define MIN_2(a, b) ((a) < (b)) ? CURSEG_HOT_DATA : CURSEG_WARM_DATA

int f2fs_hc(struct hc_list *hc_list_ptr, struct f2fs_sb_info *sbi)
{
    printk("Doing f2fs_hc...\n");
    return 0;
}

int kmeans_get_type(struct f2fs_io_info *fio)
{
    printk("Doing kmeans_get_type...\n");
    unsigned int old_IRR, old_LWS;
    unsigned int type;
    int err;
    err = lookup_hotness_entry(fio->sbi, fio->old_blkaddr, &old_IRR, &old_LWS);
    if (err) {
        printk("fail to lookup hotness_entry\n");
        return err;
    }
    if(fio->sbi->n_clusters == 3) {
        type = MIN_3(diff(old_IRR, fio->sbi->centers[0]),
                     diff(old_IRR, fio->sbi->centers[1]),
                     diff(old_IRR, fio->sbi->centers[2]));
    } else {
        type = MIN_2(diff(old_IRR, fio->sbi->centers[0]),
                     diff(old_IRR, fio->sbi->centers[1]));
    }
    
    return type;
}