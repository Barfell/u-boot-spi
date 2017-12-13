/*
 * MTD SPI-NOR driver for ST M25Pxx (and similar) serial flash chips
 *
 * Copyright (C) 2016 Jagan Teki <jagan@openedev.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <dma.h>
#include <errno.h>
#include <mtd.h>
#include <spi.h>

#include <dm/device-internal.h>
#include <linux/mtd/spi-nor.h>

#define MAX_CMD_SIZE		6

struct m25p_platdata {
	struct spi_nor		spi_nor;
};

struct m25p_priv {
	struct spi_slave	*spi;
	u8			command[MAX_CMD_SIZE];
};

static void m25p_addr2cmd(struct spi_nor *nor, unsigned int addr, u8 *cmd)
{
	/* opcode is in cmd[0] */
	cmd[1] = addr >> (nor->addr_width * 8 -  8);
	cmd[2] = addr >> (nor->addr_width * 8 - 16);
	cmd[3] = addr >> (nor->addr_width * 8 - 24);
}

static int m25p_cmdsz(struct spi_nor *nor)
{
	return 1 + nor->addr_width;
}

static int m25p_read_reg(struct udevice *dev, u8 opcode, u8 *val, int len)
{
	struct m25p_priv *priv = dev_get_priv(dev);
	struct spi_slave *spi = priv->spi;
	int ret;

	ret = spi_claim_bus(spi);
	if (ret) {
		debug("m25p: failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	ret = spi_write_then_read(spi, &opcode, 1, NULL, val, len);
	if (ret < 0) {
		debug("m25p: error %d reading register %x\n", ret, opcode);
		return ret;
	}

	spi_release_bus(spi);
	return ret;
}

static int m25p_write_reg(struct udevice *dev, u8 opcode, u8 *buf, int len)
{
	struct m25p_priv *priv = dev_get_priv(dev);
	struct spi_slave *spi = priv->spi;
	int ret;

	ret = spi_claim_bus(spi);
	if (ret) {
		debug("m25p: failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	ret = spi_write_then_read(spi, &opcode, 1, buf, NULL, len);
	if (ret < 0) {
		debug("m25p: error %d writing register %x\n", ret, opcode);
		return ret;
	}

	spi_release_bus(spi);
	return ret;
}

/*
 * TODO: remove the weak after all the other spi_flash_copy_mmap
 * implementations removed from drivers
 */
void __weak flash_copy_mmap(void *data, void *offset, size_t len)
{
#ifdef CONFIG_DMA
	if (!dma_memcpy(data, offset, len))
		return;
#endif
	memcpy(data, offset, len);
}

static int m25p_read(struct udevice *dev, loff_t from, size_t len,
		       u_char *buf)
{
	struct m25p_priv *priv = dev_get_priv(dev);
	struct spi_slave *spi = priv->spi;
	struct spi_nor *nor = spi_nor_get_spi_nor_dev(dev);
	unsigned int dummy;
	int ret;

	ret = spi_claim_bus(spi);
	if (ret) {
		debug("m25p: failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	if (nor->memory_map) {
		spi_xfer(spi, 0, NULL, NULL, SPI_XFER_MMAP);
		flash_copy_mmap(buf, nor->memory_map + from, len);
		spi_xfer(spi, 0, NULL, NULL, SPI_XFER_MMAP_END);
		spi_release_bus(spi);
		return 0;
	}

	/* convert the dummy cycles to the number of bytes */
	dummy = nor->read_dummy;
	dummy /= 8;

	priv->command[0] = nor->read_opcode;
	m25p_addr2cmd(nor, from, priv->command);

	if (spi->max_read_size)
		len = min(len, spi->max_read_size);

	debug("m25p: (read) %2x %2x %2x %2x %2x (%llx)\n",
	      priv->command[0], priv->command[1], priv->command[2],
	      priv->command[3], priv->command[4], from);
	ret = spi_write_then_read(spi, priv->command, m25p_cmdsz(nor) + dummy,
				  NULL, buf, len);
	if (ret < 0) {
		debug("m25p: error %d reading %x\n", ret, priv->command[0]);
		return ret;
	}

	spi_release_bus(spi);
	return ret;
}

static int m25p_write(struct udevice *dev, loff_t to, size_t len,
			const u_char *buf)
{
	struct m25p_priv *priv = dev_get_priv(dev);
	struct spi_slave *spi = priv->spi;
	struct spi_nor *nor = spi_nor_get_spi_nor_dev(dev);
	int cmd_sz, ret;

	ret = spi_claim_bus(spi);
	if (ret) {
		debug("m25p: failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	cmd_sz = m25p_cmdsz(nor);
	if ((nor->program_opcode == SNOR_OP_AAI_WP) && (buf != NULL))
		cmd_sz = 1;

	priv->command[0] = nor->program_opcode;
	if (buf == NULL)
		priv->command[0] = nor->erase_opcode;
	m25p_addr2cmd(nor, to, priv->command);

	if (spi->max_write_size)
		len = min(len, spi->max_write_size - cmd_sz);

	debug("m25p: (%s) %2x %2x %2x %2x %2x (%llx)\n",
	      (buf == NULL) ? "erase" : "write", priv->command[0],
	      priv->command[1], priv->command[2], priv->command[3],
	      priv->command[4], to);
	ret = spi_write_then_read(spi, priv->command, cmd_sz, buf, NULL, len);
	if (ret < 0) {
		debug("m25p: error %d writing %x\n", ret, priv->command[0]);
		return ret;
	}

	spi_release_bus(spi);
	return ret;
}

const struct spi_nor_ops m25p_ops = {
	.read		= m25p_read,
	.write		= m25p_write,
	.read_reg	= m25p_read_reg,
	.write_reg	= m25p_write_reg,
};

static int m25p_probe(struct udevice *dev)
{
	struct m25p_platdata *plat = dev_get_platdata(dev);
	struct m25p_priv *priv = dev_get_priv(dev);
	struct spi_nor_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct spi_slave *spi = dev_get_parent_priv(dev);
	int ret;

	ret = spi_claim_bus(spi);
	if (ret) {
		debug("m25p: failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	if (spi->mode & SPI_RX_SLOW)
		plat->spi_nor.mode = SNOR_READ;
	else if (spi->mode & SPI_RX_DUAL)
		plat->spi_nor.mode = SNOR_READ_1_1_2;
	else if (spi->mode & SPI_RX_QUAD)
		plat->spi_nor.mode = SNOR_READ_1_1_4;

	if (spi->mode & SPI_TX_BYTE)
		plat->spi_nor.mode |= SNOR_WRITE_1_1_BYTE;
	else if (spi->mode & SPI_TX_QUAD)
		plat->spi_nor.mode |= SNOR_WRITE_1_1_4;

	plat->spi_nor.memory_map = spi->memory_map;

	priv->spi = spi;
	upriv->spi_nor = &plat->spi_nor;

	return 0;
}

static int m25p_bind(struct udevice *dev)
{
	struct m25p_platdata *plat = dev_get_platdata(dev);

	return spi_nor_bind(dev, &plat->spi_nor);
}

static const struct udevice_id m25p_ids[] = {
	/*
	 * Generic compatibility for SPI NOR that can be identified by the
	 * JEDEC READ ID opcode (0x9F). Use this, if possible.
	 */
	{ .compatible = "jedec,spi-nor" },
	{ }
};

U_BOOT_DRIVER(m25p) = {
	.name		= "m25p",
	.id		= UCLASS_SPI_NOR,
	.of_match	= m25p_ids,
	.ops		= &m25p_ops,
	.bind		= m25p_bind,
	.probe		= m25p_probe,
	.priv_auto_alloc_size = sizeof(struct m25p_priv),
	.platdata_auto_alloc_size = sizeof(struct m25p_platdata),
};
