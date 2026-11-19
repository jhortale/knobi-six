// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: the core's C headers for C++. knobi_core.h includes the firmware's params.h, seq.h and globals.h
// outside its extern "C" (they have none of their own); the plugin also calls the firmware's pure record functions
// (program_pack / program_unpack / program_crc16, seq_pack / seq_unpack: the packed records the firmware stores, used
// for the set state and, plan Task 6, the library file), so they are declared here with C linkage first. Include this,
// never core/knobi_core.h directly, from the plugin's C++.
#pragma once
extern "C" {
#include "params/params.h"
#include "params/globals.h"
#include "synth/seq.h"
}
#include "core/knobi_core.h"
