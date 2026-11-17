/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM1_INLINE: register helpers that must inline into their caller, so the
 * SFR access sequence (and its timing, in ISRs and IRQ-off sections) is the
 * one written at the call site; -Os would otherwise out-line a helper with
 * several callers. RAM-resident code (.ram_text) may only use these, never
 * a called helper (build.py checks .ram_text for calls). */
#pragma once

#define FM1_INLINE static inline __attribute__((always_inline))

/* Target board: 0 = M-Vave FM-1 (default), 1 = M-Vave SMK-37 Pro (same AC791N, SPL and
 * update loader; different panel wiring). build.py sets it from BOARD=smk37. */
#ifndef BOARD_SMK37
#define BOARD_SMK37 0
#endif
/* SMK_MODEL: the model name the SMK build reports (its identity, its update loader's): the SMK-37 Pro,
 * SMK-37 Elite and MKE-P37 are the same keyboard under three names, and M-Vave's updater only takes a
 * unit's own factory package. build.py sets it from MODEL=elite / mke. */
#if BOARD_SMK37 && !defined(SMK_MODEL)
#define SMK_MODEL "SMK-37 Pro"
#endif

/* KNOBI_CPU1: 1 = a build with the second core (CPU1: fm1_cpu1.h, tools/build.py KNOBI_CPU1=1). 0, the default (CI
 * and every release), builds exactly the single-core image: CPU1 is never started, held or checked. FM1_CPU1 is
 * felucca-bsp's name for the same switch (fm1_flash_access.h, fm1_cpu1.h). */
#ifndef KNOBI_CPU1
#define KNOBI_CPU1 0
#endif
#ifndef FM1_CPU1
#define FM1_CPU1 KNOBI_CPU1
#endif
