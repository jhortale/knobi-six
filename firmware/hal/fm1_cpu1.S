/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio): from Melodee (github.com/keremimo/melodee), crt0.S's CPU1 entry,
 *   and the resume after a fault of the second core's (fm1_vec.S), under MPL-2.0 by agreement
 *   (https://github.com/hugelton/felucca-bsp/issues/5)
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Written against the public JieLi AC79 SDK (Apache-2.0); see NOTICE.
 * KNOBI-SIX: from hugelton/felucca-bsp fbc254d chip/fm1_cpu1.S; fm1_cpu1_resume moved here from that commit's
 * chip/fm1_vec.S (hal/fm1_vec.S jumps to it in a KNOBI_CPU1 build). Assembled only with KNOBI_CPU1 (tools/build.py). */
/* Entry of the second core (CPU1), as the SDK's cpu.a/startup.S.o: interrupts off, no interrupt configuration, its
 * own user and supervisor stacks, then out of the start-up interrupt into fm1_cpu1_main (in RAM, fm1_cpu1.h), which
 * never returns. fm1_cpu1_start puts this address in the word the chip starts CPU1 from (0x01C7FFF8). */
#ifndef FM1_CPU1_USTACK_BYTES
#define FM1_CPU1_USTACK_BYTES 2816	/* the jobs' stack (fm1_cpu1.h reads the same macro); a multiple of 8 */
#endif
	.section .text.fm1_cpu1,"ax",@progbits
	.balign 4
	.globl fm1_cpu1_entry
fm1_cpu1_entry:
	cli
	icfg = 0
	usp = fm1_cpu1_ustack_top
	sp = fm1_cpu1_sstack_top
	ssp = fm1_cpu1_sstack_top
	reti = fm1_cpu1_main
	rti

	/* fm1_fault_c (fm1_irq.h) returns only after a fault of the second core's taken on CPU0 (CPU1 now held:
	 * fm1_cpu1.h): resume as an ISR wrapper does. fm1_fatal_common (fm1_vec.S) jumps here with sp at its frame.
	 * reti, the interrupted rets and psr move to the frame's last three words (f[23..25]), where
	 * {psr, rets, reti} = [sp++] reads them; that also leaves sp where the exception found it (Melodee's, keremimo;
	 * MPL-2.0 by agreement: https://github.com/hugelton/felucca-bsp/issues/5) */
	.section .text.fm1_cpu1_resume,"ax",@progbits
	.balign 2
	.globl fm1_cpu1_resume
fm1_cpu1_resume:
	r0 = [sp+64]			/* f[16] reti */
	r1 = [sp+100]			/* f[25] the interrupted rets */
	r2 = [sp+80]			/* f[20] psr */
	[sp+92] = r0
	[sp+96] = r1
	[sp+100] = r2
	{r15-r0} = [sp++]
	sp += 28			/* f[16..22] */
	{psr, rets, reti} = [sp++]
	csync
	rti

	.section .bss.fm1_cpu1_stack,"aw",@nobits
	.balign 8
	.globl fm1_cpu1_ustack
fm1_cpu1_ustack:		/* after rti CPU1 runs on this one: the jobs' (FM1_CPU1_USTACK_WORDS; marked at start) */
	.space FM1_CPU1_USTACK_BYTES
	.globl fm1_cpu1_ustack_top
fm1_cpu1_ustack_top:
	.space 256		/* exceptions only (no interrupts on this core) */
	.globl fm1_cpu1_sstack_top
fm1_cpu1_sstack_top:
