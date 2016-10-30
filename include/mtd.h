/*
 * Copyright (C) 2015 Thomas Chou <thomas@wytron.com.tw>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef _MTD_H_
#define _MTD_H_

#include <linux/mtd/mtd.h>

/*
 * Get mtd_info structure of the dev, which is stored as uclass private.
 *
 * @dev: The MTD device
 * @return: pointer to mtd_info, NULL on error
 */
static inline struct mtd_info *mtd_get_info(struct udevice *dev)
{
	return dev_get_uclass_priv(dev);
}

struct mtd {
	struct udevice		*dev;		/* mtd device */
	u32			flags;		/* mtd device flags */
	uint64_t		size;		/* mtd device size */
};

struct mtd_ops {
	int (*erase)(struct udevice *dev, loff_t addr, size_t len);
	int (*read)(struct udevice *dev, loff_t from, size_t len,
		    size_t *retlen, u_char *buf);
	int (*write)(struct udevice *dev, loff_t to, size_t len,
		     size_t *retlen, const u_char *buf);
};

/* Access the serial operations for a device */
#define mtd_get_ops(dev) ((struct mtd_ops *)(dev)->driver->ops)

/**
 * mtd_dread() - read data from MTD device
 *
 * @mtd:	MTD device
 * @from:	offset into device in bytes to read from
 * @len:	length of bytes to read
 * @retlen:	length of return bytes read to
 * @buf:	buffer to put the data that is read
 * @return 0 if OK, -ve on error
 */
int mtd_dread(struct mtd *mtd, loff_t from, size_t len, size_t *retlen,
	      u_char *buf);

/**
 * mtd_dwrite() - write data to MTD device
 *
 * @mtd:	MTD device
 * @to:		offset into device in bytes to write to
 * @len:	length of bytes to write
 * @retlen:	length of return bytes to write to
 * @buf:	buffer containing bytes to write
 * @return 0 if OK, -ve on error
 */
int mtd_dwrite(struct mtd *mtd, loff_t to, size_t len, size_t *retlen,
	       const u_char *buf);

/**
 * mtd_derase() - erase blocks of the MTD device
 *
 * @mtd:	MTD device
 * @to:		offset into device in bytes to erase to
 * @len:	length of bytes to erase
 * @return 0 if OK, -ve on error
 */
int mtd_derase(struct mtd *mtd, loff_t addr, size_t len);

#endif	/* _MTD_H_ */
