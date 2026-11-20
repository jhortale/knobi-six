/* SPDX-License-Identifier: GPL-3.0-only */
/* The plugin tests' allocation and lock counter (rt_no_alloc): a dylib linked into the test program, loaded at launch,
 * that interposes (dyld __interpose) the C allocator's entry points and the common locks for the whole process (JUCE,
 * libc++'s operator new, the core). Armed: every call counted, whatever the thread; armed for a thread: that thread's.
 * macOS only. */
#include <stdatomic.h>
#include <stdlib.h>
#include <malloc/malloc.h>
#include <pthread.h>
#include <os/lock.h>
#include "alloc_hook.h"

static atomic_int armed;
static atomic_long allocs, locks;
static _Atomic(pthread_t) armed_thread;          /* (pthread_self compared: no thread-local, which may allocate) */

void alloc_hook_arm(int on) { atomic_store(&armed, on); }
void alloc_hook_arm_thread(int on) { atomic_store(&armed_thread, on ? pthread_self() : (pthread_t)0); }
long alloc_hook_allocs(void) { return atomic_load(&allocs); }
long alloc_hook_locks(void) { return atomic_load(&locks); }
static inline void count(atomic_long *c)
{
    pthread_t t = atomic_load_explicit(&armed_thread, memory_order_relaxed);
    if (atomic_load_explicit(&armed, memory_order_relaxed) || (t && pthread_equal(t, pthread_self())))
        atomic_fetch_add(c, 1);
}

#if defined __has_feature
#if __has_feature(thread_sanitizer)               /* (a ThreadSanitizer build: TSan intercepts these itself; interposing
                                                   * them too crashes at launch. Nothing counted there: the rt tests
                                                   * are for the normal build, TSan runs the threading tests) */
#define ALLOC_HOOK_INERT 1
#endif
#endif
#ifdef ALLOC_HOOK_INERT
#define INTERPOSE(rep, orig) __attribute__((used)) static const void *unused_##orig = (const void *)(unsigned long)&rep
#else
#define INTERPOSE(rep, orig) \
    __attribute__((used)) static const struct { const void *r, *o; } interpose_##orig \
    __attribute__((section("__DATA,__interpose"))) = {(const void *)(unsigned long)&rep, (const void *)(unsigned long)&orig}
#endif

static void *h_malloc(size_t n) { count(&allocs); return malloc(n); }
static void *h_calloc(size_t a, size_t b) { count(&allocs); return calloc(a, b); }
static void *h_realloc(void *p, size_t n) { count(&allocs); return realloc(p, n); }
static void *h_valloc(size_t n) { count(&allocs); return valloc(n); }
static int h_posix_memalign(void **p, size_t a, size_t n) { count(&allocs); return posix_memalign(p, a, n); }
static void *h_aligned_alloc(size_t a, size_t n) { count(&allocs); return aligned_alloc(a, n); }
static void *h_zmalloc(malloc_zone_t *z, size_t n) { count(&allocs); return malloc_zone_malloc(z, n); }
static void *h_zcalloc(malloc_zone_t *z, size_t a, size_t b) { count(&allocs); return malloc_zone_calloc(z, a, b); }
static void *h_zrealloc(malloc_zone_t *z, void *p, size_t n) { count(&allocs); return malloc_zone_realloc(z, p, n); }
static void *h_zmemalign(malloc_zone_t *z, size_t a, size_t n) { count(&allocs); return malloc_zone_memalign(z, a, n); }
INTERPOSE(h_malloc, malloc);
INTERPOSE(h_calloc, calloc);
INTERPOSE(h_realloc, realloc);
INTERPOSE(h_valloc, valloc);
INTERPOSE(h_posix_memalign, posix_memalign);
INTERPOSE(h_aligned_alloc, aligned_alloc);
INTERPOSE(h_zmalloc, malloc_zone_malloc);
INTERPOSE(h_zcalloc, malloc_zone_calloc);
INTERPOSE(h_zrealloc, malloc_zone_realloc);
INTERPOSE(h_zmemalign, malloc_zone_memalign);
#if defined __has_include
#if __has_include(<malloc/_malloc_type.h>)       /* (typed allocation, macOS 15 SDKs: what new code may call) */
#include <malloc/_malloc_type.h>
static void *h_tmalloc(size_t n, malloc_type_id_t t) { count(&allocs); return malloc_type_malloc(n, t); }
static void *h_tcalloc(size_t a, size_t b, malloc_type_id_t t) { count(&allocs); return malloc_type_calloc(a, b, t); }
static void *h_trealloc(void *p, size_t n, malloc_type_id_t t) { count(&allocs); return malloc_type_realloc(p, n, t); }
static int h_tposix(void **p, size_t a, size_t n, malloc_type_id_t t) { count(&allocs); return malloc_type_posix_memalign(p, a, n, t); }
INTERPOSE(h_tmalloc, malloc_type_malloc);
INTERPOSE(h_tcalloc, malloc_type_calloc);
INTERPOSE(h_trealloc, malloc_type_realloc);
INTERPOSE(h_tposix, malloc_type_posix_memalign);
#endif
#endif
static int h_mlock(pthread_mutex_t *m) { count(&locks); return pthread_mutex_lock(m); }
static int h_mtrylock(pthread_mutex_t *m) { count(&locks); return pthread_mutex_trylock(m); }
static int h_rdlock(pthread_rwlock_t *l) { count(&locks); return pthread_rwlock_rdlock(l); }
static int h_wrlock(pthread_rwlock_t *l) { count(&locks); return pthread_rwlock_wrlock(l); }
static void h_unfair(os_unfair_lock_t l) { count(&locks); os_unfair_lock_lock(l); }
INTERPOSE(h_mlock, pthread_mutex_lock);
INTERPOSE(h_mtrylock, pthread_mutex_trylock);
INTERPOSE(h_rdlock, pthread_rwlock_rdlock);
INTERPOSE(h_wrlock, pthread_rwlock_wrlock);
INTERPOSE(h_unfair, os_unfair_lock_lock);
