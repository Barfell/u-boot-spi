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

#include <dm/device-internal.h>
#include <dm/lists.h>

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

int mtd_dprotect(struct mtd *mtd, loff_t ofs, uint64_t len, bool prot)
{
	struct udevice *dev = mtd->dev;
	const struct mtd_ops *ops = mtd_get_ops(dev);

	if (!ops->lock || !ops->unlock)
		return -ENOSYS;

	if (ofs < 0 || ofs > mtd->size || len > mtd->size - ofs)
		return -EINVAL;
	if (!len)
		return 0;

	if (prot)
		return ops->lock(dev, ofs, len);
	else
		return ops->unlock(dev, ofs, len);
}

int mtd_find_device(int mtd_if_type, int devnum, struct udevice **devp)
{
	struct uclass *uc;
	struct udevice *dev;
	int ret;

	ret = uclass_get(UCLASS_MTD, &uc);
	if (ret)
		return ret;
	uclass_foreach_dev(dev, uc) {
		struct mtd *mtd = dev_get_uclass_platdata(dev);

		debug("%s: mtd_if_type=%d, devnum=%d: %s, %d, %d\n", __func__,
		      mtd_if_type, devnum, dev->name, mtd->mtd_if_type, mtd->devnum);
		if (mtd->mtd_if_type == mtd_if_type && mtd->devnum == devnum) {
			*devp = dev;
			return 0;
		}
	}

	return -ENODEV;
}

int mtd_get_device(int mtd_if_type, int devnum, struct udevice **devp)
{
	int ret;

	ret = mtd_find_device(mtd_if_type, devnum, devp);
	if (ret)
		return ret;

	return device_probe(*devp);
}

int mtd_select_devnum(enum mtd_if_type mtd_if_type, int devnum)
{
	struct udevice *dev;

	return mtd_get_device(mtd_if_type, devnum, &dev);
}

int mtd_find_max_devnum(enum mtd_if_type mtd_if_type)
{
	struct udevice *dev;
	int max_devnum = -ENODEV;
	struct uclass *uc;
	int ret;

	ret = uclass_get(UCLASS_MTD, &uc);
	if (ret)
		return ret;
	uclass_foreach_dev(dev, uc) {
		struct mtd *mtd = dev_get_uclass_platdata(dev);

		if (mtd->mtd_if_type == mtd_if_type && mtd->devnum > max_devnum)
			max_devnum = mtd->devnum;
	}

	return max_devnum;
}

static int mtd_next_free_devnum(enum mtd_if_type mtd_if_type)
{
	int ret;

	ret = mtd_find_max_devnum(mtd_if_type);
	if (ret == -ENODEV)
		return 0;
	if (ret < 0)
		return ret;

	return ret + 1;
}

int mtd_create_device(struct udevice *parent, const char *drv_name,
		      const char *name, int mtd_if_type, struct udevice **devp)
{
	struct mtd *mtd;
	struct udevice *dev;
	int devnum, ret;

	devnum = mtd_next_free_devnum(mtd_if_type);
	if (devnum < 0)
		return devnum;
	ret = device_bind_driver(parent, drv_name, name, &dev);
	if (ret)
		return ret;
	mtd = dev_get_uclass_platdata(dev);
	mtd->mtd_if_type = mtd_if_type;
	mtd->dev = dev;
	mtd->devnum = devnum;
	*devp = dev;

	return 0;
}

int mtd_create_devicef(struct udevice *parent, const char *drv_name,
		       const char *name, int mtd_if_type,
		       struct udevice **devp)
{
	char dev_name[30], *str;
	int ret;

	snprintf(dev_name, sizeof(dev_name), "%s.%s", parent->name, name);
	str = strdup(dev_name);
	if (!str)
		return -ENOMEM;

	ret = mtd_create_device(parent, drv_name, str, mtd_if_type, devp);
	if (ret) {
		free(str);
		return ret;
	}
	device_set_name_alloced(*devp);

	return ret;
}

/*
 * Implement a MTD uclass which should include most flash drivers.
 * The uclass private is pointed to mtd_info.
 */

UCLASS_DRIVER(mtd) = {
	.id		= UCLASS_MTD,
	.name		= "mtd",
	.per_device_platdata_auto_alloc_size = sizeof(struct mtd),
};
