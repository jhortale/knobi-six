/* SPDX-License-Identifier: GPL-3.0-only */
/* Written by tools/core_state.py: the per-instance state blocks (core/state/<block>.h), in knobi_core.c's order */
#define KC_STATE_BLOCKS(X) X(prog_seq) X(engine) X(controls) X(controls_seq) X(midi_cc) X(midi_known) X(midi_arp)
#define KC_SLOTS 8
