/* (Copied from jhortale/smk37-analog, spike/device-link, spike/cpu/, by João Hortale.) Entry for the emulator harness (after fm1-emu's probes/harness/crt0.S). */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	rts
1:	cli
	sp = 0x01C7A000
	ssp = 0x01C7C000
	call boot
	goto .
