/*
 * $Id$
 *
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Raymond Ingles
 */

/*
 * Trident 4DWave and Aureal Vortex gameport driver for Linux
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gameport.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Trident 4DWave and Aureal Vortex gameport driver");
MODULE_LICENSE("GPL");

#define PCI_VENDOR_ID_AUREAL	0x12eb

#define PCIGAME_DATA_WAIT	20	/* 20 ms */

#define PCIGAME_4DWAVE		0
#define PCIGAME_VORTEX		1
#define PCIGAME_VORTEX2		2

struct pcigame_data {
	int gcr;	/* Gameport control register */
	int legacy;	/* Legacy port location */
	int axes;	/* Axes start */
	int axsize;	/* Axis field size */
	int axmax;	/* Axis field max value */
	int adcmode;	/* Value to enable ADC mode in GCR */
};

static struct pcigame_data pcigame_data[] __devinitdata =
{{ 0x00030, 0x00031, 0x00034, 2, 0xffff, 0x80 },
 { 0x1100c, 0x11008, 0x11010, 4, 0x1fff, 0x40 },
 { 0x2880c, 0x28808, 0x28810, 4, 0x1fff, 0x40 },
 { 0 }};

struct pcigame {
	struct gameport gameport;
	struct pci_dev *dev;
        unsigned char *base;
	struct pcigame_data *data;
	char phys[32];
};

static unsigned char pcigame_read(struct gameport *gameport)
{
	struct pcigame *pcigame = gameport->private;
	return readb(pcigame->base + pcigame->data->legacy);
}

static void pcigame_trigger(struct gameport *gameport)
{
	struct pcigame *pcigame = gameport->private;
	writeb(0xff, pcigame->base + pcigame->data->legacy);
}

static int pcigame_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
        struct pcigame *pcigame = gameport->private;
	int i;

	*buttons = (~readb(pcigame->base + pcigame->data->legacy) >> 4) & 0xf;

	for (i = 0; i < 4; i++) {
		axes[i] = readw(pcigame->base + pcigame->data->axes + i * pcigame->data->axsize);
		if (axes[i] == pcigame->data->axmax) axes[i] = -1;
	}
        
        return 0;
}

static int pcigame_open(struct gameport *gameport, int mode)
{
	struct pcigame *pcigame = gameport->private;

	switch (mode) {
		case GAMEPORT_MODE_COOKED:
			writeb(pcigame->data->adcmode, pcigame->base + pcigame->data->gcr);
			wait_ms(PCIGAME_DATA_WAIT);
			return 0;
		case GAMEPORT_MODE_RAW:
			writeb(0, pcigame->base + pcigame->data->gcr);
			return 0;
		default:
			return -1;
	}

	return 0;
}

static int __devinit pcigame_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct pcigame *pcigame;
	int i;

	if (!(pcigame = kmalloc(sizeof(struct pcigame), GFP_KERNEL)))
		return -1;
        memset(pcigame, 0, sizeof(struct pcigame));


	pcigame->data = pcigame_data + id->driver_data;
	pcigame->dev = dev;
	sprintf(pcigame->phys, "pci%s/gameport0", dev->slot_name);

	dev->driver_data = pcigame;

	pcigame->gameport.private = pcigame;
	pcigame->gameport.fuzz = 64;
	
	pcigame->gameport.read = pcigame_read;
	pcigame->gameport.trigger = pcigame_trigger;
	pcigame->gameport.cooked_read = pcigame_cooked_read;
	pcigame->gameport.open = pcigame_open;

	pcigame->gameport.name = dev->name;
	pcigame->gameport.phys = pcigame->phys;
	pcigame->gameport.idbus = BUS_PCI;
	pcigame->gameport.idvendor = dev->vendor;
	pcigame->gameport.idproduct = dev->device;

	for (i = 0; i < 6; i++)
		if (~pci_resource_flags(dev, i) & IORESOURCE_IO)
			break;

	pci_enable_device(dev);

	pcigame->base = ioremap(pci_resource_start(pcigame->dev, i),
				pci_resource_len(pcigame->dev, i));

	gameport_register_port(&pcigame->gameport);
	
	printk(KERN_INFO "gameport: %s at pci%s speed %d kHz\n",
		dev->name, dev->slot_name, pcigame->gameport.speed);

	return 0;
}

static void __devexit pcigame_remove(struct pci_dev *dev)
{
	struct pcigame *pcigame = dev->driver_data;
	gameport_unregister_port(&pcigame->gameport);
	iounmap(pcigame->base);
	kfree(pcigame);
}

static struct pci_device_id pcigame_id_table[] __devinitdata =
{{ PCI_VENDOR_ID_TRIDENT, 0x2000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCIGAME_4DWAVE  },
 { PCI_VENDOR_ID_TRIDENT, 0x2001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCIGAME_4DWAVE  },
 { PCI_VENDOR_ID_AUREAL,  0x0001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCIGAME_VORTEX  },
 { PCI_VENDOR_ID_AUREAL,  0x0002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCIGAME_VORTEX2 },
 { 0 }};

static struct pci_driver pcigame_driver = {
	name:		"pcigame",
	id_table:	pcigame_id_table,
	probe:		pcigame_probe,
	remove:		pcigame_remove,
};

int __init pcigame_init(void)
{
	return pci_module_init(&pcigame_driver);
}

void __exit pcigame_exit(void)
{
	pci_unregister_driver(&pcigame_driver);
}

module_init(pcigame_init);
module_exit(pcigame_exit);
