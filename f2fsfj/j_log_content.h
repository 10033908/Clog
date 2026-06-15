/**
 * @file j_log_content.h
 * @author leslie.cui (10033908@github.com)
 * @brief 
 * @version 0.1
 * @date 2023-07
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifndef J_LOG_CONTENT_H
#define J_LOG_CONTENT_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/smp.h>
#include <linux/f2fs_fs.h>

typedef enum __return_value_e
{
    F2FSJ_OK  = 0,
    F2FSJ_ERROR = -1,
    F2FSJ_FATAL = -99,
}return_value_e;

typedef enum __print_log_status_e
{
    STATUS_TRACE   = 0,
    STATUS_INFO    = 1,
    STATUS_WARNING = 2,
    STATUS_ERROR   = 3,
    STATUS_FATAL   = 4,
}print_log_status_e;

#define DEFAULT_LOG_OUTPUT_LEVEL (STATUS_TRACE)    ///< Now, default log output level is Trace

#ifndef STATUS_LOG
#define STATUS_LOG(status_level, fmt, args...)                                        \
do                                                                                  \
{                                                                                   \
    /** TRACE and INFO LOG*/                                                        \
    if (status_level <= STATUS_INFO && status_level >= DEFAULT_LOG_OUTPUT_LEVEL)    \
        {pr_info("[f2fsj-cpu-%d]\t%s:%d,\t" fmt, get_cpu(), __FUNCTION__, __LINE__, ##args);}\
    /** WARNING LOG*/                                                               \
    if (status_level == STATUS_WARNING && status_level >= DEFAULT_LOG_OUTPUT_LEVEL) \
        {pr_warn("[f2fsj-cpu-%d]\t%s:%d,\t" fmt, get_cpu(), __FUNCTION__, __LINE__, ##args);}\
    /** ERROR LOG*/                                                                 \
    if (status_level == STATUS_ERROR && status_level >= DEFAULT_LOG_OUTPUT_LEVEL)   \
        {pr_err("[f2fsj-cpu-%d]\t%s:%d,\t" fmt, get_cpu(), __FUNCTION__, __LINE__, ##args);}\
    /** FATAL LOG*/                                                                 \
    if (status_level == STATUS_FATAL && status_level >= DEFAULT_LOG_OUTPUT_LEVEL)   \
        {pr_emerg("[f2fsj-cpu-%d]\t%s:%d,\t" fmt, get_cpu(), __FUNCTION__, __LINE__, ##args);}\
} while (0);                                                                        
#endif

#ifndef INFO_REPORT
#define INFO_REPORT(fmt, args...)                                              \
do                                                                             \
{                                                                              \
    if (DEFAULT_LOG_OUTPUT_LEVEL <= STATUS_INFO)                               \
        {pr_info("[f2fsj-cpu-%d]\t%s:%d,\t" fmt, get_cpu(), __FUNCTION__, __LINE__, ##args);}\
} while (0);        
#endif

static inline unsigned long get_current_time_ms(void)
{
    unsigned long ns;
    unsigned long ms;

    struct timespec64 cur_time;

    ktime_get_ts64(&cur_time);

    ns = timespec64_to_ns(&cur_time);
    ms = ns / 1000000;

    return ms;
}

static inline unsigned long get_current_time_ns(void)
{
    unsigned long ns;

    struct timespec64 cur_time;

    ktime_get_ts64(&cur_time);

    ns = timespec64_to_ns(&cur_time);

    return ns;
}

static inline log2(int n)
{
    int cnt = 0;
    if (n == 1)
    {
        return 0;
    }

    return 1 + log2(n >> 1);
}

///< define log type base on file operations
typedef enum __log_type
{
    ///< meta drived in-frequent log
    CREATE_LOG  = 0,
    MKDIR_LOG   = 1,
    UNLINK_LOG  = 2,
    LINK_LOG    = 3,
    RENAME_LOG  = 4,
    SYMLINK_LOG = 5,
    CHOWN_LOG   = 6,
    DIR_LOG     = 7,

    ///< meta drived frequent log
    READ_FILE_DATA_LOG    = 8,
    READ_DIR_LOG          = 9,
    STAT_LOG              = 10,

    ///< data drived frequent log
    DATA_WRITE_LOG    = 11
}log_type_e;

///< define log head
typedef struct __j_log_head
{
    log_type_e       log_type;
    //uint32_t         checksum;
    uint32_t         log_size;
}j_log_head_t;



#endif // !J_LOG_CONTENT_H