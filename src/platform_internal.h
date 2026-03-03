#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BH_PLATFORM_PLAYDATE
#define BH_PLATFORM_PLAYDATE
#endif

#define BH_APPLET_PRESERVED_STACK_SIZE (4 * BH_KB)
#define BH_THREAD_DEFAULT_PRIORITY 0

typedef uint32_t korp_tid;
typedef struct korp_mutex {
    int placeholder;
} korp_mutex;
typedef struct korp_cond {
    int placeholder;
} korp_cond;
typedef struct korp_thread {
    int placeholder;
} korp_thread;
typedef struct korp_rwlock {
    int placeholder;
} korp_rwlock;
typedef struct korp_sem {
    int value;
} korp_sem;

#define os_thread_local_attribute __thread

typedef int os_file_handle;
typedef void *os_dir_stream;
typedef int os_raw_file_handle;
typedef int os_poll_file_handle;
typedef unsigned int os_nfds_t;
typedef struct timespec os_timespec;

static inline int
os_getpagesize(void)
{
    return 4096;
}

static inline os_file_handle
os_get_invalid_handle(void)
{
    return -1;
}

#endif /* _PLATFORM_INTERNAL_H */
