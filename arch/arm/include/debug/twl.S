/* SPDX-License-Identifier: GPL-2.0
 *
 * arch/arm/include/debug/twl.S — Nintendo TWL (DSi) sub-screen early debug.
 *
 * Plugs into arch/arm/kernel/debug.S via CONFIG_DEBUG_LL_INCLUDE.
 * Provides the macros expected by that framework:
 *
 *   addruart    rp, rv, tmp  — load "port" address (VRAM_C base placeholder)
 *   waituartcts rd, rx       — no-op (no flow-control lines on a framebuffer)
 *   waituarttxrdy rd, rx     — no-op (framebuffer writes are immediate)
 *   senduart    rd, rx       — output one character via twl_early_putc()
 *   busyuart    rd, rx       — no-op
 *
 * senduart delegates to twl_early_putc() in arch/arm/mach-twl/earlyfb.c,
 * which handles cursor tracking, glyph rasterization, and scrolling.
 *
 * In no-MMU mode, debug.S expands addruart_current as:
 *   addruart <rx>, <tmp1>, <tmp2>
 * so rp=rx is the only output that matters; rv is unused.
 */

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =0x06200000		@ TWL sub-screen VRAM base
		.endm

		/* Framebuffer writes complete synchronously. */
		.macro	waituartcts, rd, rx
		.endm

		.macro	waituarttxrdy, rd, rx
		.endm

		.macro	busyuart, rd, rx
		.endm

		/*
		 * senduart rd, rx — rasterize character \rd onto the sub-screen.
		 *
		 * debug.S calls:  senduart r1, r3
		 * where r1 = character byte, r3 = value from addruart.
		 *
		 * Push r0-r3 + lr around the C call so printascii's loop (which
		 * uses r0 as the string pointer and r3 as the base address) is
		 * intact on return.
		 */
		.macro	senduart, rd, rx
		stmfd	sp!, {r0-r3, lr}
		and	r0, \rd, #0xff		@ zero-extend char to int arg
		bl	twl_early_putc
		ldmfd	sp!, {r0-r3, lr}
		.endm
