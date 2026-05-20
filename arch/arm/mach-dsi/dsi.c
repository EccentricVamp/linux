// SPDX-License-Identifier: GPL-2.0
//
// arch/arm/mach-dsi/dsi.c — Nintendo DSi machine support (skeleton).
//
// What this file *will* eventually do:
//   * Configure the DSi clock tree (PLL, peripheral gates) on early init.
//   * Map the I/O hole at 0x04000000 and expose registers.
//   * Register the DSi interrupt controller with genirq.
//   * Bring up an early-printk console on the bottom screen framebuffer
//     so we have a way to see panics before fbcon is ready.
//   * Register IPC channels with the ARM7 (FIFO at 0x04000180 etc.) so
//     drivers (touchscreen, sound, SD on TMIO, RTC, wifi) can talk to the
//     ARM7 firmware.
//
// What this file does today:
//   * Provides a MACHINE_START block so the kernel will link.
//
// Everything below the boilerplate is TODO. See STATUS.md.

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>

/* Fallback stubs when earlyfb.c is not compiled in. */
#ifndef CONFIG_DEBUG_DSI_EARLYFB
#include "dsi-earlyfb.h"
void dsi_early_fb_init(void) {}
void dsi_early_putc(int c) {}
#endif

static const char *const dsi_dt_compat[] __initconst = {
    "nintendo,dsi",
    NULL,
};

static void __init dsi_init_early(void) {
    /* TODO: bring up clocks, identify DSi vs DSi-XL, gate unused
     * peripherals to save power. */
}

static void __init dsi_init_irq(void) {
    /* TODO: register IRQ controller (REG_IE / REG_IF at 0x04000210/0x04000214,
     * plus DSi-only auxiliary IRQs at 0x04004010/0x04004014). */
}

static void __init dsi_init_machine(void) {
    /* TODO: map MMIO, register platform devices for screen fbdev,
     * touchscreen, MMC (TMIO), wifi-ipc. */
    of_platform_default_populate(NULL, NULL, NULL);
}

DT_MACHINE_START(NINTENDO_DSI, "Nintendo DSi")
	.dt_compat = dsi_dt_compat,
	.init_early = dsi_init_early, .init_irq = dsi_init_irq,
	.init_machine = dsi_init_machine,
MACHINE_END
