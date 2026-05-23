// SPDX-License-Identifier: GPL-2.0
//
// Nintendo TWL (DSi) clocksource + clockevent driver.
//
// The DSi ARM9 has four 16-bit timers at 0x04000100..0x0400010F running
// from a fixed 33.513982 MHz source, independent of CPU clock.
//
// Clocksource: Timers 0+1 chained into a 32-bit free-running counter.
//   T0 counts at /1 (33513982 Hz); T1 increments on every T0 overflow.
//   Composite 32-bit counter wraps every ~128 s. Drives sched_clock().
//
// Clockevent: Timer 2 with /64 prescaler (~523656 Hz, ~1.9 µs resolution,
//   125 ms max single-shot). Uses IRQ bit 5 (Timer 2 overflow).
//
// DT binding: compatible = "nintendo,twl-timer",
//             reg = <0x04000100 0x10>,
//             interrupt-parent = <&intc>, interrupts = <5>,
//             clock-frequency = <33513982>.

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>

/* Per-timer register offsets (16-bit wide, 4-byte stride) */
#define TM_CNT_L(n)  ((n) * 4 + 0)   /* counter/reload (16-bit) */
#define TM_CNT_H(n)  ((n) * 4 + 2)   /* control (16-bit) */

/* Control register bits */
#define TMCNT_H_PSC_1      0x0000u  /* clock /1   */
#define TMCNT_H_PSC_64     0x0001u  /* clock /64  */
#define TMCNT_H_COUNT_UP   BIT(2)   /* increment on previous timer overflow */
#define TMCNT_H_IRQ        BIT(6)   /* fire IRQ on overflow */
#define TMCNT_H_ENABLE     BIT(7)   /* start timer */

static void __iomem *twl_timer_base;
static u32 twl_timer_rate;  /* source clock in Hz, from DT clock-frequency */

/* ---- 32-bit clocksource (T0 + T1 chained) -------------------------------- */

/*
 * Race-free 32-bit read: sample hi, lo, hi again.
 * If hi changed between the two samples, T0 overflowed mid-read; retry.
 */
static inline u32 twl_read32(void)
{
	u16 hi, lo, hi2;

	do {
		hi  = readw(twl_timer_base + TM_CNT_L(1));
		lo  = readw(twl_timer_base + TM_CNT_L(0));
		hi2 = readw(twl_timer_base + TM_CNT_L(1));
	} while (hi != hi2);

	return ((u32)hi2 << 16) | lo;
}

static u64 notrace twl_sched_clock_read(void)
{
	return twl_read32();
}

static u64 twl_clocksource_read(struct clocksource *cs)
{
	return twl_read32();
}

static struct clocksource twl_cs = {
	.name  = "twl",
	.rating = 250,
	.read  = twl_clocksource_read,
	.mask  = CLOCKSOURCE_MASK(32),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

/* ---- clockevent (T2 at source_rate/64) ------------------------------------ */

static void twl_ce_disable(void)
{
	writew(0, twl_timer_base + TM_CNT_H(2));
}

static void twl_ce_program(unsigned long cycles)
{
	writew((u16)(0x10000u - cycles), twl_timer_base + TM_CNT_L(2));
	writew(TMCNT_H_PSC_64 | TMCNT_H_IRQ | TMCNT_H_ENABLE,
	       twl_timer_base + TM_CNT_H(2));
}

static int twl_clkevt_shutdown(struct clock_event_device *ce)
{
	twl_ce_disable();
	return 0;
}

static int twl_clkevt_set_periodic(struct clock_event_device *ce)
{
	twl_ce_disable();
	twl_ce_program(DIV_ROUND_CLOSEST(twl_timer_rate / 64, HZ));
	return 0;
}

static int twl_clkevt_set_oneshot(struct clock_event_device *ce)
{
	twl_ce_disable();
	return 0;
}

static int twl_clkevt_next_event(unsigned long evt,
				  struct clock_event_device *ce)
{
	twl_ce_disable();
	twl_ce_program(evt);
	return 0;
}

static irqreturn_t twl_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	/* Auto-reload fires again without this; stop for oneshot events */
	if (clockevent_state_oneshot(evt))
		twl_ce_disable();

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct clock_event_device twl_ce = {
	.name               = "twl",
	.rating             = 250,
	.features           = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown = twl_clkevt_shutdown,
	.set_state_periodic = twl_clkevt_set_periodic,
	.set_state_oneshot  = twl_clkevt_set_oneshot,
	.tick_resume        = twl_clkevt_shutdown,
	.set_next_event     = twl_clkevt_next_event,
};

/* ---- OF init -------------------------------------------------------------- */

static int __init twl_timer_of_init(struct device_node *node)
{
	int irq, ret;

	twl_timer_base = of_iomap(node, 0);
	if (!twl_timer_base) {
		pr_err("unable to map registers\n");
		return -ENXIO;
	}

	if (of_property_read_u32(node, "clock-frequency", &twl_timer_rate))
		twl_timer_rate = 33513982;

	/* Stop all three timers before configuring */
	writew(0, twl_timer_base + TM_CNT_H(0));
	writew(0, twl_timer_base + TM_CNT_H(1));
	writew(0, twl_timer_base + TM_CNT_H(2));

	/* T0: free-running at /1, no IRQ */
	writew(0, twl_timer_base + TM_CNT_L(0));
	writew(TMCNT_H_PSC_1 | TMCNT_H_ENABLE, twl_timer_base + TM_CNT_H(0));

	/* T1: count-up from T0 overflow, no IRQ */
	writew(0, twl_timer_base + TM_CNT_L(1));
	writew(TMCNT_H_COUNT_UP | TMCNT_H_ENABLE, twl_timer_base + TM_CNT_H(1));

	sched_clock_register(twl_sched_clock_read, 32, twl_timer_rate);
	clocksource_register_hz(&twl_cs, twl_timer_rate);

	irq = irq_of_parse_and_map(node, 0);
	if (!irq) {
		pr_err("no IRQ in DT\n");
		return -EINVAL;
	}

	ret = request_irq(irq, twl_timer_interrupt,
			  IRQF_TIMER | IRQF_IRQPOLL, "twl-timer", &twl_ce);
	if (ret) {
		pr_err("request_irq %d failed: %d\n", irq, ret);
		return ret;
	}

	twl_ce.cpumask = cpu_possible_mask;
	twl_ce.irq = irq;
	clockevents_config_and_register(&twl_ce, twl_timer_rate / 64, 1, 0xffff);

	pr_info("T0+T1 clocksource @ %u Hz, T2 clockevent @ %u Hz\n",
		twl_timer_rate, twl_timer_rate / 64);
	return 0;
}

TIMER_OF_DECLARE(twl_timer, "nintendo,twl-timer", twl_timer_of_init);
