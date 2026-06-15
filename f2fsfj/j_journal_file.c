/**
 * @file j_journal_file.c
 * @author leslie.cui (10033908@github.com)
 * @brief implementation for journal file operations
 * @version 0.1
 * @date 2023-10
 *
 * @copyright Copyright (c) 2023
 */
#include "j_journal_file.h"
#include "node.h"
#include "segment.h"
#include "j_recovery.h"
#include <linux/stat.h>

// alloc memory for log_entry_info, the memory for log contents is allocated from mmaped journal file
static struct kmem_cache* j_log_entry_info_slab = NULL;
static j_file_mapping_t j_file_mmap[4] = {0};
static j_jsb_info_t g_jsb = {0};
static j_on_disk_file_into_t g_on_disk_j_file = {0};

int init_journal_file_info(struct super_block *sb)
{
    j_jsb_info_t * j_sb_blk_ptr = NULL;
    struct buffer_head *bh = NULL;
    bh = sb_bread(sb, F2FSJ_SB_BLOCK1_ADDR);
    if (!bh)
    {
        INFO_REPORT("sb_bread journal file superblock failed\n");
        return F2FSJ_ERROR;
    }
    else
    {
        j_sb_blk_ptr = (j_jsb_info_t *)bh->b_data;

        // journal superblock
        g_jsb.j_magic_num    = j_sb_blk_ptr->j_magic_num;
        g_jsb.j_start_addr   = j_sb_blk_ptr->j_start_addr;
        g_jsb.j_file_size    = j_sb_blk_ptr->j_file_size;
    }

    if (g_jsb.j_magic_num != JOURNAL_FILE_MAGIC_NUMBER)
    {
        // init journal file superblock
        INFO_REPORT("Init journal file superblock\n");
        j_sb_blk_ptr->j_magic_num    = JOURNAL_FILE_MAGIC_NUMBER;
        j_sb_blk_ptr->j_start_addr   = F2FSJ_SB_BLOCK1_ADDR;
        j_sb_blk_ptr->j_file_size    = JOURNAL_FILE_SIZE;
        j_sb_blk_ptr->j_current_small_file = F2FSJ_J_FILE_0;
        j_sb_blk_ptr->j_current_free_log_entry = JOURNAL_BLK_PER_SMALL_FILE;
        mark_buffer_dirty(bh);
        sync_dirty_buffer(bh);
        INFO_REPORT("Init journal file superblock end");
    }
    else
    {
        INFO_REPORT("Journal file magic number is valid - %x\n", g_jsb.j_magic_num);
    }

    // init spin lock for log entry allocation
    spin_lock_init(&g_jsb.j_file_memap_lock);

    // init slab for log entry info
    j_log_entry_info_slab = kmem_cache_create("f2fsj_log_entry_info_cache_heap", sizeof(j_log_entry_t), 0,
                                                        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD, NULL);
    if (!j_log_entry_info_slab)
    {
        STATUS_LOG(STATUS_FATAL, "init log entry info cache heap fail\n");
        return F2FSJ_ERROR;
    }

    // memory map journal file
    INFO_REPORT("memory map journal file begin\n");
    mmap_journal_file();
    INFO_REPORT("memory map journal file end\n");


    // init in-memory journal file info
    g_jsb.j_current_small_file     = F2FSJ_J_FILE_0;
    g_jsb.j_current_free_log_entry = JOURNAL_BLK_PER_SMALL_FILE;

    // init on-disk journal file info
    g_on_disk_j_file.total_file_size = JOURNAL_FILE_SIZE; // MB
    g_on_disk_j_file.used_file_size  = 0;

    return F2FSJ_OK;
}

int journal_recovery(struct super_block *sb)
{
    int ret = 0;
    // read logs from journal file
    INFO_REPORT("begin to read journal file\n");
    ret = recover_read_journal(sb);
    INFO_REPORT("read journal file end\n");

    // clear journal file
    INFO_REPORT("Begin to clear journal file\n");
    ret = clear_journal_file_after_recovery(sb);
    INFO_REPORT("Clear journal file end\n");

    return ret;
}


int mmap_journal_file()
{
    int i, j;
    struct page *p = NULL;

    for (i = 0; i < 1; i++)
    {
        j_file_mmap[i].j_file_state = J_FILE_IDLE;
        j_file_mmap[i].j_cur_file = i;
        j_file_mmap[i].j_cur_log_entry_idx = 0;
        for (j = 0; j < JOURNAL_BLK_PER_SMALL_FILE; j++)
        {
            p = alloc_page(GFP_KERNEL); //GFP_KERNEL
            if (p)
            {
                j_file_mmap[i].j_pages[j] = p;
                j_file_mmap[i].j_pages_buf[j] = page_address(p);
                if (j == 0)
                    INFO_REPORT("jfile-%d, %d page addr is %p\n", i, j, j_file_mmap[i].j_pages_buf[j]);
            }
            else
            {
                STATUS_LOG(STATUS_ERROR, "memmap journal file, alloc page fail\n");
            }
        }
    }

    j_file_mmap[0].j_cur_file_start_blk = JORNAL_FILE_0_START_BLK;
    j_file_mmap[0].j_cur_file_end_blk = JORNAL_FILE_0_START_BLK + JOURNAL_BLK_PER_SMALL_FILE;
    j_file_mmap[0].j_cur_file_free_blk = JORNAL_FILE_0_START_BLK;

}

int j_alloc_journal_block(struct f2fs_sb_info *sbi, uint32_t nr_blk, uint32_t *start_blk)
{
    spin_lock(&g_jsb.j_file_memap_lock);

    if (j_file_mmap[0].j_cur_file_free_blk + nr_blk - 1 < j_file_mmap[0].j_cur_file_end_blk)
    {
        *start_blk = j_file_mmap[0].j_cur_file_free_blk;
        j_file_mmap[0].j_cur_file_free_blk += nr_blk;
    }
    else
    {
        // trigger CKPT to recliam journal space
        set_sbi_flag(sbi, SBI_NEED_CP);
        f2fs_issue_checkpoint(sbi);
        INFO_REPORT("issue CKPT to reclaim fsync jornal space\n");
        j_file_mmap[0].j_cur_file_free_blk = j_file_mmap[0].j_cur_file_start_blk;
        *start_blk = j_file_mmap[0].j_cur_file_free_blk;
        j_file_mmap[0].j_cur_file_free_blk += nr_blk;
    }

    // trigger CKPT to recliam journal space
    if (j_file_mmap[0].j_cur_file_free_blk >= j_file_mmap[0].j_cur_file_end_blk)
    {
        set_sbi_flag(sbi, SBI_NEED_CP);
        f2fs_issue_checkpoint(sbi);
        INFO_REPORT("issue CKPT to reclaim fsync jornal space\n");
        j_file_mmap[0].j_cur_file_free_blk = j_file_mmap[0].j_cur_file_start_blk;
        *start_blk = j_file_mmap[0].j_cur_file_free_blk;
        j_file_mmap[0].j_cur_file_free_blk += nr_blk;
    }

    spin_unlock(&g_jsb.j_file_memap_lock);
}


j_jsb_info_t* get_current_jouranl_sb()
{
    return &g_jsb;
}


static void j_write_end_io(struct bio *bio)
{
    struct f2fs_sb_info *sbi;
    struct bio_vec *bvec;
    struct bvec_iter_all iter_all;

    sbi = bio->bi_private;

#if 1
    bio_for_each_segment_all(bvec, bio, iter_all)
    {
        struct page *page = bvec->bv_page;

        // if (!PageUptodate(page))
        // {
        //   SetPageUptodate(page);
        // }

        // unlock pages
        if (PageLocked(page))
        {
          unlock_page(page);
        }

        // if (PageWriteback(page))
        // {
        //     end_page_writeback(page);
        // }


        if (unlikely(bio->bi_status))
        {
            STATUS_LOG(STATUS_ERROR, "Journal IO happens err\n");
        }

        //INFO_REPORT("Journal file complete one bio\n");
    }
#endif


    bio_put(bio);
}

int j_alloc_bio_write(struct f2fs_sb_info *sbi, struct bio **b_out, uint32_t start_blk_addr, int pre_alloc_iovecs)
{
    struct bio *b = NULL;

    struct block_device *bdev = sbi->sb->s_bdev;

    b = bio_alloc(GFP_KERNEL, pre_alloc_iovecs); // The second parameter is num of iovecs to pre-allocated
    if (!b)
    {
        STATUS_LOG(STATUS_ERROR, "bio allocate fail\n");
        return F2FSJ_ERROR;
    }
    else
    {
        bio_set_dev(b, bdev);
        bio_set_op_attrs(b, REQ_OP_WRITE, 0); //REQ_SYNC
        b->bi_end_io = j_write_end_io;
        b->bi_private = sbi;
        b->bi_iter.bi_sector = J_SECTOR_FROM_BLOCK(start_blk_addr);
        *b_out = b;
    }

    return F2FSJ_OK;
}

int j_alloc_bio_read(struct f2fs_sb_info *sbi, struct bio **b_out, uint32_t start_blk_addr)
{
    struct bio *b = NULL;

    struct block_device *bdev = sbi->sb->s_bdev;

    b = bio_alloc(GFP_KERNEL, 256); // The second parameter is num of iovecs to pre-allocated
    if (!b)
    {
        STATUS_LOG(STATUS_ERROR, "bio allocate fail\n");
        return F2FSJ_ERROR;
    }
    else
    {
        bio_set_dev(b, bdev);
        bio_set_op_attrs(b, REQ_OP_READ, REQ_SYNC);
        b->bi_end_io = j_write_end_io;
        b->bi_private = sbi;
        b->bi_iter.bi_sector = J_SECTOR_FROM_BLOCK(start_blk_addr);
        *b_out = b;
    }

    return F2FSJ_OK;
}

int add_journal_page_2_bio(struct page *p_log, struct bio *b)
{
    // add a page into bio
    if (bio_add_page(b, p_log, JOURNAL_BLOCK_SIZE, 0) != JOURNAL_BLOCK_SIZE)
    {
        STATUS_LOG(STATUS_ERROR, "add page into bio err\n");
        return F2FSJ_ERROR;
    }
    return F2FSJ_OK;
}

int clear_journal_file_after_recovery(struct super_block *sb)
{
    struct bio *b = NULL;
    struct page * j_p = NULL;
    uint8_t * j_p_b = NULL;

    int i = 0, j = 0, nr_page = 0;
    uint32_t start_blk = 0;
    uint32_t end_blk = 0;

    for (i = 0; i < 1; i++)
    {
        // clear mmap journal page contents to 0
        start_blk = j_file_mmap[i].j_cur_file_start_blk;
        end_blk = j_file_mmap[i].j_cur_file_end_blk;
        nr_page = 0;
        j_alloc_bio_write(F2FS_SB(sb), &b, start_blk, 256);
        for (j = 0; j <= JOURNAL_BLK_PER_SMALL_FILE; j++)
        {
            if (nr_page % 256 == 0 && nr_page != 0)
            {
                if (nr_page != JOURNAL_BLK_PER_SMALL_FILE)
                {
                    submit_bio(b);
                }
                else
                {
                    submit_bio_wait(b);
                    break;
                }

                if (nr_page != JOURNAL_BLK_PER_SMALL_FILE)
                {
                    j_alloc_bio_write(F2FS_SB(sb), &b, start_blk, 256);
                }
            }
            j_p = j_file_mmap[i].j_pages[j];
            j_p_b = j_file_mmap[i].j_pages_buf[j];
            memzero_page(j_p, 0, JOURNAL_BLOCK_SIZE);
            add_journal_page_2_bio(j_p, b);
            nr_page ++;
            start_blk ++;
        }
        INFO_REPORT("Clear %d-th j-file with %d pages\n", i, j);
    }
}

int recover_read_journal(struct super_block *sb)
{
    struct bio *b = NULL;
    // find journal file tagged with J_WHOLE_FILE_WAIT_COMMIT
    int i = 0, j = 0, nr_page = 0;
    uint32_t total_pages = 0;
    uint32_t start_blk = 0;
    uint32_t end_blk = 0;
    uint32_t start_page_idx = 0;
    int ret = F2FSJ_OK;

    INFO_REPORT("Begin to read journals\n");
    // Only read and do not change mmaped journal file status
    //for (i = 0; i < NR_JOUNRAL_SMALL_FILE; i++)
    for (i = 0; i < 1; i++)
    {
        start_blk = j_file_mmap[i].j_cur_file_start_blk;
        end_blk = j_file_mmap[i].j_cur_file_end_blk;
        total_pages = end_blk - start_blk;
        start_page_idx = 0;
        nr_page = 0;

        j_alloc_bio_read(F2FS_SB(sb), &b, start_blk);
        for (j = start_page_idx; j <= total_pages; j ++)
        {
            if (nr_page % 256 == 0 && nr_page != 0)
            {
                if (nr_page == total_pages)
                {
                    submit_bio_wait(b);
                    break;
                }
                else
                {
                    submit_bio(b);
                }

                if (nr_page != total_pages)
                {
                    j_alloc_bio_read(F2FS_SB(sb), &b, start_blk);
                }
            }
            ret = add_journal_page_2_bio(j_file_mmap[i].j_pages[j], b);
            if (ret != F2FSJ_OK)
            {
                INFO_REPORT("add page-[%d] to bio err, bio_max_vec %d, read journal exit\n", j, b->bi_max_vecs);
                return F2FSJ_ERROR;
            }
            nr_page ++;
            start_blk ++;
        }
        INFO_REPORT("submit bio to read %d journal pages\n", total_pages);
    }

    INFO_REPORT("Read journal end\n");
    if (b->bi_status)
    {
        STATUS_LOG(STATUS_ERROR, "Read journal file by bio happends ERR-%d\n",b->bi_status);
    }
    else
    {
        // read journal file contents and do recovery
        INFO_REPORT("read journal file by bio succuessfully, then to recover\n");
        iterate_journal(sb, 0);
    }
}

int is_invalid_log_type(log_type_e log_type)
{
    if (log_type == CREATE_LOG || log_type == MKDIR_LOG || log_type == UNLINK_LOG
    || log_type == RENAME_LOG || log_type == DATA_WRITE_LOG)
    {
        return 0;
    }
    else
    {
        INFO_REPORT("Invalid log type, read journal teiminated\n");
        return 1;
    }
}


int iterate_journal(struct super_block *sb, int j_file_idx)
{
    int i = 0;
    int j = 0;
    uint8_t * p_addr = NULL;
    uint8_t * log_en = NULL;
    struct page * p = NULL;
    struct page * p1 = NULL;
    struct page * p2 = NULL;

    struct f2fs_node * node_info = NULL;

    struct f2fs_node * node_info1 = NULL;
    struct f2fs_node * node_info2 = NULL;
    __le64 cp_ver1 = 0;
    __le64 cp_ver2 = 0;

    p1 = j_file_mmap[j_file_idx].j_pages[0];
    p2 = j_file_mmap[j_file_idx].j_pages[1];
    node_info1 = F2FS_NODE(p1);
    node_info2 = F2FS_NODE(p2);
    cp_ver1 = node_info1->footer.cp_ver;
    cp_ver2 = node_info1->footer.cp_ver;

    if (cp_ver1 != cp_ver2)
    {
        INFO_REPORT("invalid journals\n");
        return -1;
    }
    else
    {
        INFO_REPORT("cp_ver is match %d\n", cp_ver1);
    }

    uint64_t time1, time2;
    time1 = get_current_time_ms();

    for (i = 0; i < JOURNAL_BLK_PER_SMALL_FILE; i ++)
    {
        p = j_file_mmap[j_file_idx].j_pages[i];
        node_info = F2FS_NODE(p);
        //if (node_info->footer.cp_ver == cp_ver2)

        if (node_info->footer.ino > F2FS_ROOT_INO(F2FS_SB(sb)) && node_info->footer.ino < 24586)
        {
                INFO_REPORT("recover inode %d, nid %d\n", node_info->footer.ino, node_info->footer.nid);
                do_recover_from_journal(sb, p, node_info->footer.nid == node_info->footer.ino, node_info->footer.ino);
        }
        else
        {
            INFO_REPORT("Recovery end, skip invalid journals\n");
            break;
        }
    }

end:
    time2 = get_current_time_ms();
    INFO_REPORT("recover files cost %llu ms\n", time2 - time1);
}

int do_recover_from_journal(struct super_block *sb, struct page * node_page, bool is_inode, int ino)
{
    // if it is an inode, try to recover direntry
    if (is_inode)
    {
        j_recover_direntry(sb, node_page);
    }

    // then, recover inode/dnode page
    j_recover_dnode(sb, node_page);

    return F2FSJ_OK;
}