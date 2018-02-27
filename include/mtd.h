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

enum mtd_if_type {
	MTD_IF_TYPE_UNKNOWN = 0,
	MTD_IF_TYPE_SPI_NOR,

	MTD_IF_TYPE_COUNT,
};

struct mtd {
	struct udevice		*dev;		/* mtd device */
	u32			flags;		/* mtd device flags */
	uint64_t		size;		/* mtd device size */
	enum mtd_if_type	mtd_if_type;	/* type of mtd interface */
	int			devnum;		/* device number */
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

/**
 * mtd_find_device() - find a mtd device
 *
 * This function does not activate the device. The device will be returned
 * whether or not it is activated.
 *
 * @mtd_if_type:	interface type (enum mtd_if_type_t)
 * @devnum:		device number (specific to each interface type)
 * @devp:		the device, if found
 * @return 0 if found, -ENODEV if no device found, or other -ve error value
 */
int mtd_find_device(int mtd_if_type, int devnum, struct udevice **devp);

/**
 * mtd_get_device() - find and probe a mtd device ready for use
 *
 * @mtd_if_type:	interface type (enum mtd_if_type_t)
 * @devnum:		device number (specific to each interface type)
 * @devp:		the device, if found
 * @return 0 if found, -ENODEV if no device found, or other -ve error value
 */
int mtd_get_device(int mtd_if_type, int devnum, struct udevice **devp);

/**
 * mtd_first_device() - find the first device for a given interface
 *
 * The device is probed ready for use
 *
 * @devnum:	device number (specific to each interface type)
 * @devp:	the device, if found
 * @return 0 if found, -ENODEV if no device, or other -ve error value
 */
int mtd_first_device(int mtd_if_type, struct udevice **devp);

/**
 * mtd_next_device() - find the next device for a given interface
 *
 * This can be called repeatedly after mtd_first_device() to iterate through
 * all devices of the given interface type.
 *
 * The device is probed ready for use
 *
 * @devp:	on entry, the previous device returned. On exit, the next
 *		device, if found
 * @return 0 if found, -ENODEV if no device, or other -ve error value
 */
int mtd_next_device(struct udevice **devp);

/**
 * mtd_create_device() - create a new mtd device
 *
 * @parent:		parent of the new device
 * @drv_name:		driver name to use for the mtd device
 * @name:		name for the device
 * @mtd_if_type:	Interface type (enum mtd_if_type_t)
 * @devp:		the new device (which has not been probed)
 */
int mtd_create_device(struct udevice *parent, const char *drv_name,
		      const char *name, int mtd_if_type,
		      struct udevice **devp);

/**
 * mtd_create_devicef() - Cceate a new named mtd device
 *
 * @parent:		parent of the new device
 * @drv_name:		driver name to use for the mtd device
 * @name:		name for the device (parent name is prepended)
 * @mtd_if_type:	interface type (enum mtd_if_type_t)
 * @devp:		the new device (which has not been probed)
 */
int mtd_create_devicef(struct udevice *parent, const char *drv_name,
		       const char *name, int mtd_if_type,
		       struct udevice **devp);

/**
 * mtd_prepare_device() - prepare a mtd device for use
 *
 * This reads partition information from the device if supported.
 *
 * @dev:	device to prepare
 * @return 0 if ok, -ve on error
 */
int mtd_prepare_device(struct udevice *dev);

/**
 * mtd_unbind_all() - unbind all device of the given interface type
 *
 * The devices are removed and then unbound.
 *
 * @mtd_if_type:	interface type to unbind
 * @return 0 if OK, -ve on error
 */
int mtd_unbind_all(int mtd_if_type);

/**
 * mtd_find_max_devnum() - find the maximum device number for an interface type
 *
 * Finds the last allocated device number for an interface type @mtd_if_type. The
 * next number is safe to use for a newly allocated device.
 *
 * @mtd_if_type:	interface type to scan
 * @return maximum device number found, or -ENODEV if none, or other -ve on
 * error
 */
int mtd_find_max_devnum(enum mtd_if_type mtd_if_type);

/**
 * mtd_select_devnum() - select the mtd device from device number
 *
 * @mtd_if_type:	interface type to scan
 * @devnum:		device number, specific to the interface type, or -1 to
 * @return maximum device number found, or -ENODEV if none, or other -ve on
 * error
 */
int mtd_select_devnum(enum mtd_if_type mtd_if_type, int devnum);

#endif	/* _MTD_H_ */
