/* SPDX-License-Identifier: GPL-2.0
 *
 * arch/arm/include/debug/dsi.S — Nintendo DSi sub-screen early debug.
 *
 * Plugs into arch/arm/kernel/debug.S via CONFIG_DEBUG_LL_INCLUDE.
 * Provides the macros expected by that framework:
 *
 *   addruart    rp, rv, tmp  — load "port" address (VRAM_C base placeholder)
 *   waituartcts rd, rx       — no-op (no flow-control lines on a framebuffer)
 *   waituarttxrdy rd, rx     — no-op (framebuffer writes are immediate)
 *   senduart    rd, rx       — output one character via dsi_early_putc()
 *   busyuart    rd, rx       — no-op
 *
 * senduart delegates to dsi_early_putc() in arch/arm/mach-dsi/earlyfb.c,
 * which handles cursor tracking, glyph rasterization, and scrolling.
 *
 * Stack contract: head-dsi.S sets up init_thread_union's stack before
 * jumping to start_kernel, so stmfd/ldmfd is safe by the time printascii
 * is ever called.
 *
 * In no-MMU mode, debug.S expands addruart_current as:
 *   addruart <rx>, <tmp1>, <tmp2>
 * so rp=rx is the only output that matters; rv is unused.
 */

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =0x06200000		@ DSi sub-screen VRAM base
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
		 * intact on return.  The stack is live at this point because
		 * head-dsi.S sets sp to init_thread_union + THREAD_START_SP
		 * before ever reaching start_kernel.
		 */
		.macro	senduart, rd, rx
		stmfd	sp!, {r0-r3, lr}
		and	r0, \rd, #0xff		@ zero-extend char to int arg
		bl	dsi_early_putc
		ldmfd	sp!, {r0-r3, lr}
		.endm
