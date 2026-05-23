// SPDX-License-Identifier: GPL-2.0
//
// drivers/video/fbdev/twl-fb.c - Nintendo TWL (DSi) sub-screen fbdev driver.
//
// The DSi sub-engine drives the bottom 256x192 LCD.  This driver configures
// BG2 as a 16bpp direct-color (BGR555) extended-rotation bitmap backed by
// VRAM bank C at 0x06200000, then registers it as /dev/fb0 so fbcon can
// render a virtual terminal there.
//
// On probe it tells the early framebuffer to stop poking VRAM via
// twl_early_fb_shutdown(); afterward DEBUG_LL output continues to reach the
// melonDS no$gba debug register through earlycon=ntr_nocash.

#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/twl-earlyfb.h>

/* DSi sub-engine MMIO. */
#define TWL_VRAMCNT_C		0x04000242u
#define TWL_DISPCNT_SUB		0x04001000u
#define TWL_BG2CNT_SUB		0x0400100Cu
#define TWL_BG2PA_SUB		0x04001020u
#define TWL_BG2PB_SUB		0x04001022u
#define TWL_BG2PC_SUB		0x04001024u
#define TWL_BG2PD_SUB		0x04001026u
#define TWL_BG2X_SUB		0x04001028u
#define TWL_BG2Y_SUB		0x0400102Cu
#define TWL_VRAM_C_BASE		0x06200000u
#define TWL_VRAM_C_SIZE		0x20000u	/* 128 KB bank */

/* VRAMCNT_C: enable | MST=4 (sub-engine BG). */
#define VRAMCNT_C_SUB_BG	0x84u
/* DISPCNT_SUB: mode 5 | BG2 enable | graphics display. */
#define DISPCNT_VAL		(5u | (1u << 10) | (1u << 16))
/*
 * BG2CNT: priority 0, 8.8 fractional palette (n/a in direct mode), mosaic 0,
 * direct color (bit 7), screen base 0, area-overflow wrap 0, size = 256x256
 * (bits 14:15 = 01).  Result: 0x4084.
 */
#define BG2CNT_BMP_DIRECT_256	0x4084u

#define FB_W	256
#define FB_H	192
#define FB_BPP	16
#define FB_LINELEN	(FB_W * FB_BPP / 8)
#define FB_SIZE	(FB_LINELEN * FB_H)

#define TWL_FB_PALETTE_ENTRIES	16

struct twl_fb_par {
	u32 pseudo_palette[TWL_FB_PALETTE_ENTRIES];
};

static const struct fb_fix_screeninfo twl_fb_fix = {
	.id		= "twl-fb",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.accel		= FB_ACCEL_NONE,
	.line_length	= FB_LINELEN,
	.smem_start	= TWL_VRAM_C_BASE,
	.smem_len	= FB_SIZE,
};

static const struct fb_var_screeninfo twl_fb_var = {
	.xres		= FB_W,
	.yres		= FB_H,
	.xres_virtual	= FB_W,
	.yres_virtual	= FB_H,
	.bits_per_pixel	= FB_BPP,
	/* BGR555 with the high bit as a (forced) alpha flag. */
	.red		= { .offset = 0,  .length = 5 },
	.green		= { .offset = 5,  .length = 5 },
	.blue		= { .offset = 10, .length = 5 },
	.transp		= { .offset = 15, .length = 1 },
	.activate	= FB_ACTIVATE_NOW,
	.vmode		= FB_VMODE_NONINTERLACED,
	.width		= -1,
	.height		= -1,
};

static int twl_fb_setcolreg(unsigned int regno, unsigned int red,
			    unsigned int green, unsigned int blue,
			    unsigned int transp, struct fb_info *info)
{
	u32 *pal = info->pseudo_palette;
	u32 r, g, b;

	if (regno >= TWL_FB_PALETTE_ENTRIES)
		return -EINVAL;

	r = red   >> (16 - info->var.red.length);
	g = green >> (16 - info->var.green.length);
	b = blue  >> (16 - info->var.blue.length);

	pal[regno] = (r << info->var.red.offset) |
		     (g << info->var.green.offset) |
		     (b << info->var.blue.offset) |
		     /* always opaque: BGR555's high bit is the LCD enable */
		     (1u << info->var.transp.offset);
	return 0;
}

static const struct fb_ops twl_fb_ops = {
	.owner		= THIS_MODULE,
	FB_DEFAULT_IOMEM_OPS,
	.fb_setcolreg	= twl_fb_setcolreg,
};

static void twl_fb_program_hw(void)
{
	void __iomem *vramcnt_c = (void __iomem *)TWL_VRAMCNT_C;
	void __iomem *dispcnt   = (void __iomem *)TWL_DISPCNT_SUB;
	void __iomem *bg2cnt    = (void __iomem *)TWL_BG2CNT_SUB;

	/* Stop the earlyfb writer; we're about to reinterpret VRAM as 16bpp. */
	twl_early_fb_shutdown();

	writeb(VRAMCNT_C_SUB_BG, vramcnt_c);
	writel(DISPCNT_VAL, dispcnt);
	writew(BG2CNT_BMP_DIRECT_256, bg2cnt);

	/* Identity affine matrix + zero reference. */
	writew(0x0100, (void __iomem *)TWL_BG2PA_SUB);
	writew(0x0000, (void __iomem *)TWL_BG2PB_SUB);
	writew(0x0000, (void __iomem *)TWL_BG2PC_SUB);
	writew(0x0100, (void __iomem *)TWL_BG2PD_SUB);
	writel(0x00000000, (void __iomem *)TWL_BG2X_SUB);
	writel(0x00000000, (void __iomem *)TWL_BG2Y_SUB);
}

static int twl_fb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct twl_fb_par *par;
	struct fb_info *info;
	void __iomem *vram;
	int ret;

	info = framebuffer_alloc(sizeof(*par), dev);
	if (!info)
		return -ENOMEM;
	par = info->par;

	vram = devm_ioremap(dev, TWL_VRAM_C_BASE, TWL_VRAM_C_SIZE);
	if (!vram) {
		ret = -ENOMEM;
		goto err_release;
	}

	twl_fb_program_hw();
	memset_io(vram, 0, FB_SIZE);

	info->fbops		= &twl_fb_ops;
	info->fix		= twl_fb_fix;
	info->var		= twl_fb_var;
	info->screen_base	= vram;
	info->screen_size	= FB_SIZE;
	info->pseudo_palette	= par->pseudo_palette;

	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(dev, "register_framebuffer failed: %d\n", ret);
		goto err_release;
	}

	platform_set_drvdata(pdev, info);
	dev_info(dev, "TWL framebuffer at %pa, %ux%u %ubpp\n",
		 &info->fix.smem_start, info->var.xres, info->var.yres,
		 info->var.bits_per_pixel);
	return 0;

err_release:
	framebuffer_release(info);
	return ret;
}

static void twl_fb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);

	unregister_framebuffer(info);
	framebuffer_release(info);
}

static const struct of_device_id twl_fb_of_match[] = {
	{ .compatible = "nintendo,twl-fb" },
	{ }
};
MODULE_DEVICE_TABLE(of, twl_fb_of_match);

static struct platform_driver twl_fb_driver = {
	.probe	= twl_fb_probe,
	.remove	= twl_fb_remove,
	.driver	= {
		.name		= "twl-fb",
		.of_match_table	= twl_fb_of_match,
	},
};
module_platform_driver(twl_fb_driver);

MODULE_DESCRIPTION("Nintendo DSi (TWL) sub-engine framebuffer");
MODULE_LICENSE("GPL");
