/* SPDX-License-Identifier: GPL-3.0-only */
/* alloc_hook.c: count allocations and locks (any thread) while armed */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
__attribute__((visibility("default"))) void alloc_hook_arm(int on);
__attribute__((visibility("default"))) long alloc_hook_allocs(void);
__attribute__((visibility("default"))) long alloc_hook_locks(void);
#ifdef __cplusplus
}
#endif
