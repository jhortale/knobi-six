/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX: the factory bank header, if any (docs/FACTORY_BANK.md). Moved out of store_prog.c unchanged (the same
 * tokens at the same place: the firmware build is the same) so the host core (firmware/src/core/knobi_core.c), which
 * has no program store of its own, reads the same bank the same way. firmware/gen/factory_bank.h when it exists at
 * build time (never committed), else none; -DKNOBI_NO_FACTORY: none; -DKNOBI_FACTORY_H='"file.h"': that bank (a path
 * from this directory). Defines FACTORY_COUNT (0: no bank). factory.c reads it. */
#ifndef KNOBI_FACTORY_INC_H
#define KNOBI_FACTORY_INC_H
#if defined KNOBI_FACTORY_H
#include KNOBI_FACTORY_H
#elif !defined KNOBI_NO_FACTORY && defined __has_include
#if __has_include("factory_bank.h")
#include "factory_bank.h"
#endif
#endif
#ifndef FACTORY_COUNT
#define FACTORY_COUNT 0u                               /* no factory bank: the init program everywhere */
#endif
#endif
