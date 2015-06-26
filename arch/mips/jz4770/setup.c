/*
 * linux/arch/mips/jz4770/common/setup.c
 *
 * JZ4770 common setup routines.
 *
 * Copyright (C) 2011 Ingenic Semiconductor Inc.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 */

#include <linux/init.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>

#include <uapi/linux/serial_reg.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/page.h>

#include <asm/mach-jz4770/jz4770cpm.h>
#include <asm/mach-jz4770/jz4770dmac.h>

#ifdef CONFIG_PC_KEYB
#include <asm/keyboard.h>
#endif

#include "reset.h"
#include "uart.h"


/* XXX: We really should just merge with jz4740. */
void jz4740_serial_out(struct uart_port *p, int offset, int value)
{
	switch (offset) {
	case UART_FCR:
		value |= UART_JZ_FCR_UME; /* enable UART module */
		break;
	/* XXX: Define a port type, set UART_CAP_RTOIE. */
	case UART_IER:
		value |= (value & UART_IER_RLSI) << 2;
		break;
	default:
		break;
	}
	writeb(value, p->membase + (offset << p->regshift));
}

static void __init jz_serial_setup(void)
{
#ifdef CONFIG_SERIAL_8250
	struct uart_port s;

	REG8(UART0_FCR) |= UART_JZ_FCR_UME; /* enable UART module */
	memset(&s, 0, sizeof(s));
	s.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST;
	s.iotype = SERIAL_IO_MEM;
	s.regshift = 2;
	s.uartclk = JZ_EXTAL;
	s.serial_out = jz4740_serial_out;

	s.line = 0;
	s.membase = (u8 *)UART0_BASE;
	s.irq = IRQ_UART0;
	if (early_serial_setup(&s) != 0) {
		printk(KERN_ERR "Serial ttyS0 setup failed!\n");
	}

	s.line = 1;
	s.membase = (u8 *)UART1_BASE;
	s.irq = IRQ_UART1;
	if (early_serial_setup(&s) != 0) {
		printk(KERN_ERR "Serial ttyS1 setup failed!\n");
	}

	s.line = 2;
	s.membase = (u8 *)UART2_BASE;
	s.irq = IRQ_UART2;
	if (early_serial_setup(&s) != 0) {
		printk(KERN_ERR "Serial ttyS2 setup failed!\n");
	}

	s.line = 3;
	s.membase = (u8 *)UART3_BASE;
	s.irq = IRQ_UART3;
	if (early_serial_setup(&s) != 0) {
		printk(KERN_ERR "Serial ttyS3 setup failed!\n");
	}
#endif
}

void __init plat_mem_setup(void)
{
	__asm__ (
		"li    $2, 0xa9000000 \n\t"
		"mtc0  $2, $5, 4      \n\t"
		"nop                  \n\t"
		::"r"(2));

	/* IO/MEM resources. Which will be the addtion value in `inX' and
	 * `outX' macros defined in asm/io.h */
	set_io_port_base(0);
	ioport_resource.start	= 0x00000000;
	ioport_resource.end	= 0xffffffff;
	iomem_resource.start	= 0x00000000;
	iomem_resource.end	= 0xffffffff;

	jz_reset_init();
	__dt_setup_arch(__dtb_start);

	jz_serial_setup();
}

/*
 * We have seen MMC DMA transfers read corrupted data from SDRAM when a burst
 * interval ends at physical address 0x10000000. To avoid this problem, we
 * remove the final page of low memory from the memory map.
 */
void __init jz4770_reserve_unsafe_for_dma(void)
{
	int i;
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		struct boot_mem_map_entry *entry = boot_mem_map.map + i;

		if (entry->type != BOOT_MEM_RAM)
			continue;

		if (entry->addr + entry->size != 0x10000000)
			continue;

		entry->size -= PAGE_SIZE;
		break;
	}
}

void __init device_tree_init(void)
{
	if (!initial_boot_params)
		return;

	unflatten_and_copy_device_tree();
}

static int __init populate_machine(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	return 0;
}
arch_initcall(populate_machine);
