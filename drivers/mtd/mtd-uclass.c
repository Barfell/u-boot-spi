/*
 * Copyright (C) 2016 Jagan Teki <jagan@openedev.com>
 * Copyright (C) 2015 Thomas Chou <thomas@wytron.com.tw>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <mtd.h>
#include <linux/log2.h>

int mtd_dread(struct mtd *mtd, loff_t from, size_t len, size_t *retlen,
	      u_char *buf)
{
	struct udevice *dev = mtd->dev;
	const struct mtd_ops *ops = mtd_get_ops(dev);

	if (!ops->read)
		return -ENOSYS;

	*retlen = 0;
	if (from < 0 || from > mtd->size || len > mtd->size - from)
		return -EINVAL;
	if (!len)
		return 0;

	return ops->read(dev, from, len, retlen, buf);
}

int mtd_derase(struct mtd *mtd, loff_t addr, size_t len)
{
	struct udevice *dev = mtd->dev;
	const struct mtd_ops *ops = mtd_get_ops(dev);

	if (!ops->erase)
		return -ENOSYS;

	if (addr > mtd->size || len > mtd->size - addr)
		return -EINVAL;
	if (!(mtd->flags & MTD_WRITEABLE))
		return -EROFS;
	if (!len)
		return 0;

	return ops->erase(dev, addr, len);
}

int mtd_dwrite(struct mtd *mtd, loff_t to, size_t len, size_t *retlen,
	       const u_char *buf)
{
	struct udevice *dev = mtd->dev;
	const struct mtd_ops *ops = mtd_get_ops(dev);

	if (!ops->write)
		return -ENOSYS;

	*retlen = 0;
	if (to < 0 || to > mtd->size || len > mtd->size - to)
		return -EINVAL;
	if (!ops->write || !(mtd->flags & MTD_WRITEABLE))
		return -EROFS;
	if (!len)
		return 0;

	return ops->write(dev, to, len, retlen, buf);
}

/*
 * Implement a MTD uclass which should include most flash drivers.
 * The uclass private is pointed to mtd_info.
 */

UCLASS_DRIVER(mtd) = {
	.id		= UCLASS_MTD,
	.name		= "mtd",
	.per_device_auto_alloc_size = sizeof(struct mtd),
};
