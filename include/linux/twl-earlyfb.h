/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Nintendo TWL (DSi) sub-screen early framebuffer interface.
 *
 * The early framebuffer is set up before C runtime starts so DEBUG_LL
 * printk output is visible on the bottom screen from the very first
 * message.  Once the full fbdev driver in drivers/video/fbdev/twl-fb.c
 * registers, it calls twl_early_fb_shutdown() to disable the earlyfb
 * writer before reprogramming the LCD pixel format.
 */
#ifndef _LINUX_TWL_EARLYFB_H
#define _LINUX_TWL_EARLYFB_H

void twl_early_fb_init(void);
void twl_early_putc(int c);
void twl_early_fb_shutdown(void);

#endif /* _LINUX_TWL_EARLYFB_H */
