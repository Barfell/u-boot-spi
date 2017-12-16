/*
 * SPI-NOR Environment.
 *
 * Copyright (C) 2017 Jagan Teki <jagan@openedev.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <environment.h>
#include <memalign.h>
#include <mtd.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-nor.h>

DECLARE_GLOBAL_DATA_PTR;

static struct spi_nor *env_init_spinor(void)
{
	struct spi_nor *nor;
	int devnum = CONFIG_ENV_SPI_NOR_DEVNUM;

	nor = find_spi_nor_device(devnum);
	if (!nor)
		return NULL;

	if (spi_nor_scan(nor))
		return NULL;

	return nor;
}

static int env_spinor_save(void)
{
	ALLOC_CACHE_ALIGN_BUFFER(env_t, env_new, 1);
	struct spi_nor *nor;
	struct mtd *mtd;
	loff_t len, sector;
	int ret;

	nor = env_init_spinor();
	if (!nor)
		return -EIO;

	ret = env_export(env_new);
	if (ret)
		return -EINVAL;

	mtd = spi_nor_get_mtd(nor);
	if (!mtd)
		return -EIO;

	sector = DIV_ROUND_UP(CONFIG_ENV_SIZE, CONFIG_ENV_SECT_SIZE);
	len = sector * CONFIG_ENV_SECT_SIZE;

	puts("erasing spinor flash...\n");
        ret = mtd_derase(mtd, CONFIG_ENV_OFFSET, len);
	if (ret)
		return -EIO;

	len = CONFIG_ENV_SIZE;
	puts("writing spinor flash...\n");
	ret = mtd_dwrite(mtd, CONFIG_ENV_OFFSET, len,
			 (size_t *)&len, (u_char *)env_new);
	if (ret)
		return -EIO;

	puts("done\n");
	return ret;
}

static int env_spinor_load(void)
{
	ALLOC_CACHE_ALIGN_BUFFER(char, buf, CONFIG_ENV_SIZE);
	struct spi_nor *nor;
	struct mtd *mtd;
	loff_t offset, len;
	int ret;

	nor = env_init_spinor();
	if (!nor)
		return -EIO;

	offset = CONFIG_ENV_OFFSET;
	len = CONFIG_ENV_SIZE;

	mtd = spi_nor_get_mtd(nor);
	if (!mtd)
		return -EIO;

	ret = mtd_dread(mtd, offset, len, (size_t *)&len, (uchar *)buf);
	if (ret)
		return -EIO;

	ret = env_import(buf, 1);
	if (ret)
		gd->env_valid = ENV_VALID;

	return ret;
}

U_BOOT_ENV_LOCATION(spinor) = {
	.location	= ENVL_SPI_NOR,
	ENV_NAME("SPI-NOR Flash")
	.load		= env_spinor_load,
	.save		= env_save_ptr(env_spinor_save),
};
