// SPDX-License-Identifier: GPL-2.0
//
// IRQ controller driver for Nintendo TWL (DSi).
//
// The ARM9-side interrupt controller uses a single pair of 32-bit registers:
//   IME @ 0x04000208  master enable (bit 0)
//   IE  @ 0x04000210  interrupt enable (R/W)
//   IF  @ 0x04000214  interrupt flag (W1C)
//
// IE/IF bit 0..31 map directly to hwirq numbers. Timer 0..3 are bits 3..6.
// All interrupts are treated as edge-triggered (IF is W1C, line does not
// stay asserted after the flag is cleared).
//
// DT binding: compatible = "nintendo,twl-intc",
//             reg = <0x04000208 0x10>,
//             interrupt-controller, #interrupt-cells = <1>.

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/exception.h>

/* Offsets from the mapped base (first reg = 0x04000208) */
#define TWL_IME  0x00  /* master interrupt enable, bit 0 */
#define TWL_IE   0x08  /* interrupt enable */
#define TWL_IF   0x0c  /* interrupt flag (write-1-to-clear) */

#define TWL_NR_IRQS  32

static void __iomem *twl_intc_base;
static struct irq_domain *twl_intc_domain;

static void twl_intc_mask(struct irq_data *d)
{
	writel(readl(twl_intc_base + TWL_IE) & ~BIT(d->hwirq),
	       twl_intc_base + TWL_IE);
}

static void twl_intc_unmask(struct irq_data *d)
{
	writel(readl(twl_intc_base + TWL_IE) | BIT(d->hwirq),
	       twl_intc_base + TWL_IE);
}

static void twl_intc_ack(struct irq_data *d)
{
	writel(BIT(d->hwirq), twl_intc_base + TWL_IF);
}

static struct irq_chip twl_intc_chip = {
	.name       = "twl-intc",
	.irq_ack    = twl_intc_ack,
	.irq_mask   = twl_intc_mask,
	.irq_unmask = twl_intc_unmask,
};

static int twl_intc_domain_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &twl_intc_chip, handle_edge_irq);
	irq_set_probe(irq);
	return 0;
}

static const struct irq_domain_ops twl_intc_ops = {
	.map   = twl_intc_domain_map,
	.xlate = irq_domain_xlate_onecell,
};

static void __exception_irq_entry twl_handle_irq(struct pt_regs *regs)
{
	u32 pending;

	while ((pending = readl(twl_intc_base + TWL_IE) &
			  readl(twl_intc_base + TWL_IF))) {
		do {
			unsigned int bit = __ffs(pending);

			pending &= ~BIT(bit);
			generic_handle_domain_irq(twl_intc_domain, bit);
		} while (pending);
	}
}

static int __init twl_intc_of_init(struct device_node *node,
				    struct device_node *parent)
{
	twl_intc_base = of_iomap(node, 0);
	if (!twl_intc_base) {
		pr_err("twl-intc: unable to map registers\n");
		return -ENXIO;
	}

	/* Disable all IRQs, clear all pending, enable master */
	writel(0, twl_intc_base + TWL_IE);
	writel(~0U, twl_intc_base + TWL_IF);
	writel(1, twl_intc_base + TWL_IME);

	twl_intc_domain = irq_domain_add_linear(node, TWL_NR_IRQS,
						 &twl_intc_ops, NULL);
	if (!twl_intc_domain) {
		pr_err("twl-intc: failed to add irq domain\n");
		return -ENOMEM;
	}

	set_handle_irq(twl_handle_irq);

	pr_info("twl-intc: registered %u IRQs\n", TWL_NR_IRQS);
	return 0;
}

IRQCHIP_DECLARE(twl_intc, "nintendo,twl-intc", twl_intc_of_init);
