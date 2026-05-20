/* SPDX-License-Identifier: GPL-2.0
 *
 * arch/arm/kernel/head-dsi.S
 *
 * DSinux replacement for arch/arm/kernel/head-nommu.S, used when
 * CONFIG_ARCH_DSI=y. Selected via MMUEXT in arch/arm/Makefile.
 *
 * The reason this is a board-replacement instead of patches into
 * head-nommu.S: head-nommu.S in mainline 6.x assumes Cortex-M (CPU_V7M
 * with NVIC, Thumb-2 only, BASEADDR_V7M_SCB lookups, no separate
 * vector relocation). Adding ARMv5TE+ARM946 paths to it would mean
 * touching a hot file with #ifdef CONFIG_CPU_ARM946E clauses; we
 * decided that maintaining a separate head-dsi.S is cheaper while
 * DSinux is the only ARMv5 nommu user.
 *
 * REVISIT: when DSinux is mature enough to consider upstreaming,
 * migrate the contents of this file into head-nommu.S guarded by
 * appropriate config symbols. See STATUS.md "head.S vs head-nommu.S".
 *
 * The flow follows DSLinux mach-nds/head.S (Malcolm Parsons et al.,
 * 2005) modernised for current Linux conventions:
 *   1. Force SVC mode, mask IRQ/FIQ.
 *   2. Disable cache + MPU (clean slate).
 *   3. Map DTCM at 0x027C0000 (16 KiB) and ITCM at 0x00000000 (32 KiB).
 *   4. Set up MPU regions for ITCM (vectors), main RAM, DTCM, and I/O.
 *   5. Enable I/D cache, ITCM, DTCM, MPU. Clear V bit (low vectors).
 *   6. Stash DTB pointer in __atags_pointer.
 *   7. Save processor id, clear BSS, set up sp.
 *   8. Jump to start_kernel (skips __lookup_processor_type since
 *      setup_processor() runs it later anyway based on processor_id).
 *
 * Boot protocol from the loader (bootloader/arm9/source/main.c):
 *   r0 = 0
 *   r1 = MACHINE_ID (ignored — DT compatible match used instead)
 *   r2 = DTB physical address (must end up in __atags_pointer)
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>

/* MPU region size encodings: bits[5:1] = log2(size) - 1, bit[0] = enable.
 * size = 2^(log2_size_minus1 + 1). */
#define PAGE_4K		(0b01011 << 1)
#define PAGE_8K		(0b01100 << 1)
#define PAGE_16K	(0b01101 << 1)
#define PAGE_32K	(0b01110 << 1)
#define PAGE_64K	(0b01111 << 1)
#define PAGE_1M		(0b10011 << 1)
#define PAGE_4M		(0b10101 << 1)
#define PAGE_16M	(0b10111 << 1)
#define PAGE_64M	(0b11001 << 1)

/* TCM size encoding: bits[5:1] = N where size = 512 << N bytes. */
#define TCM_SIZE_16K	(5 << 1)	/*  512 << 5 = 16 KiB */
#define TCM_SIZE_32K	(6 << 1)	/*  512 << 6 = 32 KiB */

/* DSi memory map (DSi mode). */
#define DRAM_BASE_DSI	0x02000000
#define DTCM_BASE_DSI	0x027C0000
#define IO_BASE		0x04000000

/* SCTLR (cp15 c1 c0 op2=0) bits we care about. */
#define SCTLR_M		(1 << 0)	/* MPU enable */
#define SCTLR_C		(1 << 2)	/* D-cache enable */
#define SCTLR_I		(1 << 12)	/* I-cache enable */
#define SCTLR_V		(1 << 13)	/* high vectors @ 0xFFFF0000 */
#define SCTLR_DTCM	(1 << 16)	/* DTCM enable */
#define SCTLR_ITCM	(1 << 18)	/* ITCM enable */

	__HEAD
ENTRY(stext)
	/* Force SVC mode, mask IRQ/FIQ. */
	mov	r0, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
	msr	cpsr_c, r0

	/* Save the DTB pointer the loader handed us in r2. We touch r2 in
	 * cp15 ops below; r10 is callee-saved-ish for our purposes. */
	mov	r10, r2

	/* Clean slate: disable MPU, caches, TCMs. SCTLR has implementation-
	 * defined reset state; force it to a known value before we touch
	 * anything cp15. */
	ldr	r1, =0x00002078		@ default-ish, MPU off, caches off
	mcr	p15, 0, r1, c1, c0, 0

	/* Flush instruction & data caches, drain write buffer. */
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0	@ I-cache invalidate all
	mcr	p15, 0, r0, c7, c6, 0	@ D-cache invalidate all
	mcr	p15, 0, r0, c7, c10, 4	@ drain write buffer

	/* Map DTCM at DTCM_BASE_DSI, 16 KiB. */
	ldr	r0, =(DTCM_BASE_DSI | TCM_SIZE_16K | 1)
	mcr	p15, 0, r0, c9, c1, 0

	/* Map ITCM at 0x00000000, 32 KiB. The kernel's vector copy code
	 * later places the vector table at 0x00000000, which now lands
	 * in fast on-chip ITCM rather than the bus's empty space. */
	ldr	r0, =(0x00000000 | TCM_SIZE_32K | 1)
	mcr	p15, 0, r0, c9, c1, 1

	/* MPU regions. Each: base | size_field | enable.
	 * Region 0: ITCM @ 0x00000000, 32 KiB — vectors land here. */
	ldr	r0, =(0x00000000 | PAGE_32K | 1)
	mcr	p15, 0, r0, c6, c0, 0
	/* Region 1: main RAM @ 0x02000000, 16 MiB (DSi mode). */
	ldr	r0, =(DRAM_BASE_DSI | PAGE_16M | 1)
	mcr	p15, 0, r0, c6, c1, 0
	/* Region 2: DTCM @ 0x027C0000, 16 KiB. */
	ldr	r0, =(DTCM_BASE_DSI | PAGE_16K | 1)
	mcr	p15, 0, r0, c6, c2, 0
	/* Region 3: I/O + VRAM @ 0x04000000, 64 MiB. */
	ldr	r0, =(IO_BASE | PAGE_64M | 1)
	mcr	p15, 0, r0, c6, c3, 0
	/* Regions 4..7: disabled (write 0). */
	mov	r0, #0
	mcr	p15, 0, r0, c6, c4, 0
	mcr	p15, 0, r0, c6, c5, 0
	mcr	p15, 0, r0, c6, c6, 0
	mcr	p15, 0, r0, c6, c7, 0

	/* Cacheable bits per region: only main RAM (region 1) is cacheable.
	 * I/O must NOT be cached. ITCM/DTCM bypass cache anyway. */
	mov	r0, #0b00000010		@ region 1 cacheable
	mcr	p15, 0, r0, c2, c0, 0	@ data cache cfg
	mcr	p15, 0, r0, c2, c0, 1	@ inst cache cfg

	/* Bufferable bits per region: same — only main RAM bufferable. */
	mov	r0, #0b00000010
	mcr	p15, 0, r0, c3, c0, 0

	/* Access permissions: 4 bits per region.
	 *   0x3 = privileged read/write, no user access (kernel only).
	 * Regions 0..3 = priv R/W → 0x3333. */
	ldr	r0, =0x00003333
	mcr	p15, 0, r0, c5, c0, 2	@ data access perms (extended)
	mcr	p15, 0, r0, c5, c0, 3	@ inst access perms (extended)

	/* Bring it all online. Read-modify-write SCTLR:
	 *   set:   M | C | I | DTCM | ITCM
	 *   clear: V (low vectors at 0x00000000) */
	mrc	p15, 0, r0, c1, c0, 0
	ldr	r1, =(SCTLR_M | SCTLR_C | SCTLR_I | SCTLR_DTCM | SCTLR_ITCM)
	orr	r0, r0, r1
	bic	r0, r0, #SCTLR_V
	mcr	p15, 0, r0, c1, c0, 0

	/* From here on, vectors fetched at 0x00000000 hit ITCM — but ITCM
	 * was just mapped and contains uninitialized garbage. Copy the
	 * runtime vectors + stubs from their LMA (where the linker stored
	 * them in Image) to their VMA before anything can trap.
	 *
	 * mainline's vmlinux.lds.S uses
	 *   OVERLAY 0xffff0000 : NOCROSSREFS AT(__vectors_lma) { ... }
	 * so the bytes ride along contiguously in the kernel image, and
	 * `__vectors_start` / `__vectors_end` / `__stubs_start` / `__stubs_end`
	 * resolve to the load addresses (in main RAM, after .text).
	 * early_trap_init() does this same copy later in setup_arch(),
	 * but anything that traps before then would crash; we have to
	 * land vectors before the first exception.
	 *
	 * V=0 above → runtime vector base is 0x00000000 (ITCM), stubs at
	 * 0x00001000. */
	ldr	r0, =__vectors_start
	ldr	r1, =__vectors_end
	mov	r2, #0x00000000
11:	cmp	r0, r1
	ldrcc	r3, [r0], #4
	strcc	r3, [r2], #4
	bcc	11b

	ldr	r0, =__stubs_start
	ldr	r1, =__stubs_end
	mov	r2, #0x00001000
12:	cmp	r0, r1
	ldrcc	r3, [r0], #4
	strcc	r3, [r2], #4
	bcc	12b

	/* ITCM bypasses D-cache, so no D-cache clean is needed for the
	 * stores. But the I-cache may have cached stale entries for the
	 * 0x00000000 region from before we mapped ITCM there — flush. */
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I-cache

	/* Set up the initial kernel stack. */
	ldr	sp, =init_thread_union + THREAD_START_SP

	/* Save the cpu id into processor_id. setup_processor() will use it
	 * later to find the matching proc_info (proc-arm946.S). */
	mrc	p15, 0, r3, c0, c0, 0
	ldr	r4, =processor_id
	str	r3, [r4]

	/* Save DTB pointer into __atags_pointer for setup_arch(). */
	ldr	r4, =__atags_pointer
	str	r10, [r4]

	/* Clear BSS. */
	ldr	r4, =__bss_start
	ldr	r5, =__bss_stop
	mov	r0, #0
1:	cmp	r4, r5
	strcc	r0, [r4], #4
	bcc	1b

	/* Set __current = &init_task so get_current() returns a valid pointer
	 * before the first context switch.  ARM with CONFIG_THREAD_INFO_IN_TASK
	 * uses the __current global (updated by set_current in __switch_to).
	 * BSS clear above zeroed it; fill it in now. */
	ldr	r4, =__current
	ldr	r5, =init_task
	str	r5, [r4]

	/* Initialize the bottom-screen framebuffer so DEBUG_LL printascii
	 * output is visible from the very first printk in start_kernel.
	 * BSS is clear and the stack is live, so calling C is safe here.
	 * Compiled in only when CONFIG_DEBUG_DSI_EARLYFB=y; otherwise the
	 * symbol is a no-op stub so the call always links cleanly. */
	mov	fp, #0
	bl	dsi_early_fb_init

	/* Off to C land. */
	b	start_kernel
ENDPROC(stext)

/* head-common.S provides __lookup_processor_type (asm), the C wrapper
 * lookup_processor_type, __error_p, __mmap_switched_data, etc. These are
 * referenced from setup.c and other parts of the kernel even when our
 * stext doesn't call them directly. head-nommu.S includes this same
 * file; we need to too because head-nommu.o is no longer linked. */
#include "head-common.S"
