/*
 * SPI NOR Core framework.
 *
 * Copyright (C) 2016 Jagan Teki <jagan@openedev.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <mtd.h>

#include <dm/device-internal.h>
#include <linux/mtd/spi-nor.h>

DECLARE_GLOBAL_DATA_PTR;

int spi_nor_init(void)
{
	struct udevice *bus;

	for (uclass_first_device(UCLASS_SPI_NOR, &bus);
	     bus;
	     uclass_next_device(&bus)) {
		;
	}

#ifndef CONFIG_SPL_BUILD
	print_spi_nor_devices(',');
#endif

	return 0;
}

struct spi_nor *spi_nor_get_spi_nor_dev(struct udevice *dev)
{
	struct spi_nor_uclass_priv *upriv;

	if (!device_active(dev))
		return NULL;
	upriv = dev_get_uclass_priv(dev);
	return upriv->spi_nor;
}

struct spi_nor *find_spi_nor_device(int dev_num)
{
	struct udevice *dev, *spi_nor_dev;
	int ret;

	ret = mtd_find_device(MTD_IF_TYPE_SPI_NOR, dev_num, &dev);
	if (ret) {
		printf("SPI-NOR Device %d not found\n", dev_num);
		return NULL;
	}

	spi_nor_dev = dev_get_parent(dev);

	struct spi_nor *nor = spi_nor_get_spi_nor_dev(spi_nor_dev);

	return nor;
}

int get_spi_nor_num(void)
{
	return max((mtd_find_max_devnum(MTD_IF_TYPE_SPI_NOR) + 1), 0);
}

struct mtd *spi_nor_get_mtd(struct spi_nor *nor)
{
	struct mtd *mtd;
	struct udevice *dev;

	device_find_first_child(nor->dev, &dev);
	if (!dev)
		return NULL;
	mtd = dev_get_uclass_platdata(dev);

	return mtd;
}

void print_spi_nor_devices(char separator)
{
	struct udevice *dev;
	bool first = true;

	for (uclass_first_device(UCLASS_SPI_NOR, &dev);
	     dev;
	     uclass_next_device(&dev), first = false) {
		struct spi_nor *nor = spi_nor_get_spi_nor_dev(dev);

		if (!first) {
			printf("%c", separator);
			if (separator != '\n')
				puts(" ");
		}

		printf("%s: %d", dev->name, spi_nor_get_mtd(nor)->devnum);
	}

	printf("\n");
}

int spi_nor_bind(struct udevice *dev, struct spi_nor *nor)
{
	struct udevice *mdev;
	int ret;

	if (!spi_nor_get_ops(dev))
		return -ENOSYS;

	ret = mtd_create_devicef(dev, "spinor_mtd", "mtd", MTD_IF_TYPE_SPI_NOR,
				 &mdev);
	if (ret) {
		debug("Cannot create mtd device\n");
		return ret;
	}
	nor->dev = dev;

	return 0;
}

static int spi_nor_mtd_probe(struct udevice *dev)
{
	struct udevice *spi_nor_dev = dev_get_parent(dev);
	struct spi_nor_uclass_priv *upriv = dev_get_uclass_priv(spi_nor_dev);
	struct spi_nor *nor = upriv->spi_nor;
	int ret;

	ret = spi_nor_scan(nor);
	if (ret) {
		debug("%s: spi_nor_scan() failed (err=%d)\n", __func__, ret);
		return ret;
	}

	return 0;
}

static const struct mtd_ops spi_nor_mtd_ops = {
	.read	= spi_nor_mread,
	.erase	= spi_nor_merase,
};

U_BOOT_DRIVER(spinor_mtd) = {
	.name		= "spinor_mtd",
	.id		= UCLASS_MTD,
	.ops		= &spi_nor_mtd_ops,
	.probe		= spi_nor_mtd_probe,
};

U_BOOT_DRIVER(spinor) = {
	.name	= "spinor",
	.id	= UCLASS_SPI_NOR,
};

UCLASS_DRIVER(spinor) = {
	.id		= UCLASS_SPI_NOR,
	.name		= "spinor",
	.flags		= DM_UC_FLAG_SEQ_ALIAS,
	.per_device_auto_alloc_size = sizeof(struct spi_nor_uclass_priv),
};
