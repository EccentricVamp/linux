// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/io.h>

#define NTR_NOCASH_DEBUG_PUTC	((void __iomem *)0x04fffa1cu)

static void
ntr_nocash_debugport_early_putc(struct uart_port *port, unsigned char ch)
{
	iowrite8(ch, NTR_NOCASH_DEBUG_PUTC);
}

static void
ntr_nocash_debugport_early_write(struct console *con, const char *s,
				 unsigned int count)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, count, ntr_nocash_debugport_early_putc);
}

static int __init
ntr_nocash_debugport_early_setup(struct earlycon_device *dev, const char *opt)
{
	dev->con->write = ntr_nocash_debugport_early_write;
	return 0;
}
EARLYCON_DECLARE(ntr_nocash, ntr_nocash_debugport_early_setup);
