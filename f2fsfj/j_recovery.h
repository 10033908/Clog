#ifndef __J_RECOVERY_H__
#define __J_RECOVERY_H__

// #include "f2fs.h"
#include "j_log_content.h"


int j_recover_direntry(struct super_block *sb, struct page * node_page);

int j_recover_dnode(struct super_block *sb, struct page * node_page);

#endif // !__J_RECOVERY_H__