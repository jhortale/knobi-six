/* SPDX-License-Identifier: GPL-3.0-only */
/* alloc_hook.c: count allocations and locks (any thread, or one thread) while armed */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
__attribute__((visibility("default"))) void alloc_hook_arm(int on);
/* armed for the calling thread only (0: off): counts that thread's calls, not the others' (plan Task 6
 * reload_off_audio_thread: the library reloads on its own thread meanwhile) */
__attribute__((visibility("default"))) void alloc_hook_arm_thread(int on);
__attribute__((visibility("default"))) long alloc_hook_allocs(void);
__attribute__((visibility("default"))) long alloc_hook_locks(void);
#ifdef __cplusplus
}
#endif
