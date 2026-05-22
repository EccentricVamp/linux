// SPDX-License-Identifier: GPL-2.0

#include <asm/mach/arch.h>
#include <linux/init.h>
#include <linux/of_platform.h>

#ifdef CONFIG_DEBUG_TWL_EARLYFB
#include "twl-earlyfb.h"

static void __init twl_init_early(void)
{
	twl_early_fb_init();
}
#endif

static const char *const twl_dt_compat[] __initconst = {
	"nintendo,twl",
	NULL,
};

static void __init twl_init_machine(void)
{
	of_platform_default_populate(NULL, NULL, NULL);
}

DT_MACHINE_START(NINTENDO_TWL, "Nintendo DSi")
	.dt_compat	= twl_dt_compat,
#ifdef CONFIG_DEBUG_TWL_EARLYFB
	.init_early	= twl_init_early,
#endif
	.init_machine	= twl_init_machine,
MACHINE_END
