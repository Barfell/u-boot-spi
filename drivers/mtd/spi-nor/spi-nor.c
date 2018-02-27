/*
 * SPI NOR Core framework.
 *
 * Copyright (C) 2016 Jagan Teki <jagan@openedev.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <mapmem.h>
#include <mtd.h>

#include <dm/device-internal.h>
#include <linux/math64.h>
#include <linux/types.h>
#include <linux/mtd/spi-nor.h>

DECLARE_GLOBAL_DATA_PTR;

/* Set write enable latch with Write Enable command */
static inline int write_enable(struct udevice *dev)
{
	return spi_nor_get_ops(dev)->write_reg(dev, SNOR_OP_WREN, NULL, 0);
}

/* Re-set write enable latch with Write Disable command */
static inline int write_disable(struct udevice *dev)
{
	return spi_nor_get_ops(dev)->write_reg(dev, SNOR_OP_WRDI, NULL, 0);
}

static int read_sr(struct udevice *dev)
{
	u8 sr;
	int ret;

	ret = spi_nor_get_ops(dev)->read_reg(dev, SNOR_OP_RDSR, &sr, 1);
	if (ret < 0) {
		debug("spi-nor: fail to read status register\n");
		return ret;
	}

	return sr;
}

static int read_fsr(struct udevice *dev)
{
	u8 fsr;
	int ret;

	ret = spi_nor_get_ops(dev)->read_reg(dev, SNOR_OP_RDFSR, &fsr, 1);
	if (ret < 0) {
		debug("spi-nor: fail to read flag status register\n");
		return ret;
	}

	return fsr;
}

static int write_sr(struct udevice *dev, u8 ws)
{
	struct spi_nor *nor = spi_nor_get_spi_nor_dev(dev);
	const struct spi_nor_ops *ops = spi_nor_get_ops(dev);

	nor->cmd_buf[0] = ws;
	return ops->write_reg(dev, SNOR_OP_WRSR, nor->cmd_buf, 1);
}

#if defined(CONFIG_SPI_NOR_SPANSION) || defined(CONFIG_SPI_NOR_WINBOND)
static int read_cr(struct udevice *dev)
{
	u8 cr;
	int ret;

	ret = spi_nor_get_ops(dev)->read_reg(dev, SNOR_OP_RDCR, &cr, 1);
	if (ret < 0) {
		debug("spi-nor: fail to read config register\n");
		return ret;
	}

	return cr;
}

/*
 * Write status Register and configuration register with 2 bytes
 * - First byte will be written to the status register.
 * - Second byte will be written to the configuration register.
 * Return negative if error occurred.
 */
static int write_sr_cr(struct udevice *dev, u16 val)
{
	struct spi_nor *nor = spi_nor_get_spi_nor_dev(dev);
	const struct spi_nor_ops *ops = spi_nor_get_ops(dev);

	nor->cmd_buf[0] = val & 0xff;
	nor->cmd_buf[1] = (val >> 8);

	return ops->write_reg(dev, SNOR_OP_WRSR, nor->cmd_buf, 2);
}
#endif

static int spi_nor_sr_ready(struct udevice *dev)
{
	int sr = read_sr(dev);

	if (sr < 0)
		return sr;
	else
		return !(sr & SR_WIP);
}

static int spi_nor_fsr_ready(struct udevice *dev)
{
	int fsr = read_fsr(dev);

	if (fsr < 0)
		return fsr;
	else
		return fsr & FSR_READY;
}

static int spi_nor_ready(struct udevice *dev)
{
	struct spi_nor *nor = spi_nor_get_spi_nor_dev(dev);
	int sr, fsr;

	sr = spi_nor_sr_ready(dev);
	if (sr < 0)
		return sr;

	fsr = 1;
	if (nor->flags & SNOR_F_USE_FSR) {
		fsr = spi_nor_fsr_ready(dev);
		if (fsr < 0)
			return fsr;
	}

	return sr && fsr;
}

static int spi_nor_wait_till_ready(struct udevice *dev, unsigned long timeout)
{
	int timebase, ret;

	timebase = get_timer(0);

	while (get_timer(timebase) < timeout) {
		ret = spi_nor_ready(dev);
		if (ret < 0)
			return ret;
		if (ret)
			return 0;
	}

	printf("spi-nor: Timeout!\n");

	return -ETIMEDOUT;
}

static const struct spi_nor_info *spi_nor_id(struct udevice *dev)
{
	int				tmp;
	u8				id[SPI_NOR_MAX_ID_LEN];
	const struct spi_nor_info	*info;
	const struct spi_nor_ops	*ops = spi_nor_get_ops(dev);

	tmp = ops->read_reg(dev, SNOR_OP_RDID, id, SPI_NOR_MAX_ID_LEN);
	if (tmp < 0) {
		printf("spi-nor: error %d reading JEDEC ID\n", tmp);
		return ERR_PTR(tmp);
	}

	info = spi_nor_ids;
	for (; info->name != NULL; info++) {
		if (info->id_len) {
			if (!memcmp(info->id, id, info->id_len))
				return info;
		}
	}

	printf("spi-nor: unrecognized JEDEC id bytes: %02x, %2x, %2x\n",
	       id[0], id[1], id[2]);
	return ERR_PTR(-ENODEV);
}

int spi_nor_merase(struct udevice *dev, loff_t addr, size_t len)
{
	struct mtd *mtd = dev_get_uclass_platdata(dev);
	const struct mtd_ops *mops = mtd_get_ops(mtd->dev);
	int devnum = mtd->devnum;
	struct spi_nor *nor;
	const struct spi_nor_ops *ops;
	u32 erase_addr, rem;
	int ret = -1;

	nor = find_spi_nor_device(devnum);
	if (!nor)
		return -ENODEV;
	ops = spi_nor_get_ops(nor->dev);

	div_u64_rem(len, mtd->erasesize, &rem);
	if (rem)
		return -EINVAL;

	if (mops->is_locked) {
		ret = mops->is_locked(mtd->dev, addr, len);
		if (ret > 0) {
			printf("spi-nor: offset 0x%llx is locked, cannot be erased\n", addr);
			return -EINVAL;
		}
	}

	while (len) {
		erase_addr = addr;

		write_enable(nor->dev);

		ret = ops->write(nor->dev, erase_addr, 0, NULL);
		if (ret < 0)
			break;

		ret = spi_nor_wait_till_ready(nor->dev, SNOR_READY_WAIT_ERASE);
		if (ret < 0)
			return ret;

		addr += mtd->erasesize;
		len -= mtd->erasesize;
	}

	write_disable(nor->dev);

	return ret;
}

static int spi_nor_mwrite(struct udevice *dev, loff_t to, size_t len,
			  size_t *retlen, const u_char *buf)
{
	struct mtd *mtd = dev_get_uclass_platdata(dev);
	const struct mtd_ops *mops = mtd_get_ops(mtd->dev);
	int devnum = mtd->devnum;
	struct spi_nor *nor;
	const struct spi_nor_ops *ops;
	size_t addr, byte_addr;
	size_t chunk_len, actual;
	u32 page_size;
	int ret = -1;

	nor = find_spi_nor_device(devnum);
	if (!nor)
		return -ENODEV;
	ops = spi_nor_get_ops(nor->dev);

	page_size = mtd->writebufsize;

	if (mops->is_locked) {
		ret = mops->is_locked(mtd->dev, to, len);
		if (ret > 0) {
			printf("spi-nor: offset 0x%llx is locked, cannot be written\n", to);
			return -EINVAL;
		}
	}

	for (actual = 0; actual < len; actual += chunk_len) {
		addr = to;

		byte_addr = addr % page_size;
		chunk_len = min(len - actual, (size_t)(page_size - byte_addr));

		write_enable(nor->dev);

		ret = ops->write(nor->dev, addr, chunk_len, buf + actual);
		if (ret < 0)
			break;

		ret = spi_nor_wait_till_ready(nor->dev, SNOR_READY_WAIT_PROG);
		if (ret < 0)
			return ret;

		to += chunk_len;
		*retlen += chunk_len;
	}

	return ret;
}

int spi_nor_mread(struct udevice *dev, loff_t from, size_t len,
		  size_t *retlen, u_char *buf)
{
	struct mtd *mtd = dev_get_uclass_platdata(dev);
	int devnum = mtd->devnum;
	struct spi_nor *nor;
	const struct spi_nor_ops *ops;
	int ret;

	nor = find_spi_nor_device(devnum);
	if (!nor)
		return -ENODEV;
	ops = spi_nor_get_ops(nor->dev);

	/* Handle memory-mapped SPI */
	if (nor->memory_map) {
		ret = ops->read(nor->dev, from, len, buf);
		if (ret) {
			debug("spi-nor: mmap read failed\n");
			return ret;
		}

		return ret;
	}

	ret = ops->read(nor->dev, from, len, buf);
	if (ret < 0) {
		printf("%s ret = %d\n", __func__, ret);
		return ret;
	}

	*retlen += len;

	return ret;
}

#ifdef CONFIG_SPI_NOR_SST
static int sst_byte_write(struct spi_nor *nor, u32 addr, const void *buf,
			  size_t *retlen)
{
	const struct spi_nor_ops *ops = spi_nor_get_ops(nor->dev);
	int ret;

	ret = write_enable(nor->dev);
	if (ret)
		return ret;

	nor->program_opcode = SNOR_OP_BP;

	ret = ops->write(nor->dev, addr, 1, buf);
	if (ret)
		return ret;

	*retlen += 1;

	return spi_nor_wait_till_ready(nor->dev, SNOR_READY_WAIT_PROG);
}

static int sst_mwrite_wp(struct udevice *dev, loff_t to, size_t len,
			 size_t *retlen, const u_char *buf)
{
	struct mtd *mtd = mtd_get_info(dev);
	int devnum = mtd->devnum;
	const struct spi_nor_ops *ops;
	struct spi_nor *nor;
	size_t actual;
	int ret;

	nor = find_spi_nor_device(devnum);
	if (!nor)
		return -ENODEV;
	ops = spi_nor_get_ops(nor->dev);

	/* If the data is not word aligned, write out leading single byte */
	actual = to % 2;
	if (actual) {
		ret = sst_byte_write(nor, to, buf, retlen);
		if (ret)
			goto done;
	}
	to += actual;

	ret = write_enable(nor->dev);
	if (ret)
		goto done;

	for (; actual < len - 1; actual += 2) {
		nor->program_opcode = SNOR_OP_AAI_WP;

		ret = ops->write(nor->dev, to, 2, buf + actual);
		if (ret) {
			debug("spi-nor: sst word program failed\n");
			break;
		}

		ret = spi_nor_wait_till_ready(nor->dev, SNOR_READY_WAIT_PROG);
		if (ret)
			break;

		to += 2;
		*retlen += 2;
	}

	if (!ret)
		ret = write_disable(nor->dev);

	/* If there is a single trailing byte, write it out */
	if (!ret && actual != len)
		ret = sst_byte_write(nor, to, buf + actual, retlen);

 done:
	return ret;
}

static int sst_mwrite_bp(struct udevice *dev, loff_t to, size_t len,
			 size_t *retlen, const u_char *buf)
{
	struct mtd *mtd = mtd_get_info(dev);
	int devnum = mtd->devnum;
	struct spi_nor *nor;
	size_t actual;
	int ret;

	nor = find_spi_nor_device(devnum);
	if (!nor)
		return -ENODEV;

	for (actual = 0; actual < len; actual++) {
		ret = sst_byte_write(nor, to, buf + actual, retlen);
		if (ret) {
			debug("spi-nor: sst byte program failed\n");
			break;
		}
		to++;
	}

	if (!ret)
		ret = write_disable(nor->dev);

	return ret;
}
#endif

#if defined(CONFIG_SPI_NOR_STMICRO) || defined(CONFIG_SPI_NOR_SST)
static void stm_get_locked_range(struct mtd *mtd, u8 sr, loff_t *ofs,
				 u64 *len)
{
	u8 mask = SR_BP2 | SR_BP1 | SR_BP0;
	int shift = ffs(mask) - 1;
	int pow;

	if (!(sr & mask)) {
		/* No protection */
		*ofs = 0;
		*len = 0;
	} else {
		pow = ((sr & mask) ^ mask) >> shift;
		*len = mtd->size >> pow;
		*ofs = mtd->size - *len;
	}
}

/* Return 1 if the entire region is locked, 0 otherwise */
static int stm_is_locked_sr(struct mtd *mtd, loff_t ofs, u64 len,
			    u8 sr)
{
	loff_t lock_offs;
	u64 lock_len;

	stm_get_locked_range(mtd, sr, &lock_offs, &lock_len);

	return (ofs + len <= lock_offs + lock_len) && (ofs >= lock_offs);
}

/*
 * Check if a region of the flash is (completely) locked. See stm_lock() for
 * more info.
 *
 * Returns 1 if entire region is locked, 0 if any portion is unlocked, and
 * negative on errors.
 */
static int stm_is_locked(struct udevice *dev, loff_t ofs, uint64_t len)
{
	struct mtd *mtd = dev_get_uclass_platdata(dev);
	int devnum = mtd->devnum;
	struct spi_nor *nor;
	int status;

	nor = find_spi_nor_device(devnum);
	if (!nor)
		return -ENODEV;

	status = read_sr(nor->dev);
	if (status < 0)
		return status;

	return stm_is_locked_sr(mtd, ofs, len, status);
}

/*
 * Lock a region of the flash. Compatible with ST Micro and similar flash.
 * Supports only the block protection bits BP{0,1,2} in the status register
 * (SR). Does not support these features found in newer SR bitfields:
 *   - TB: top/bottom protect - only handle TB=0 (top protect)
 *   - SEC: sector/block protect - only handle SEC=0 (block protect)
 *   - CMP: complement protect - only support CMP=0 (range is not complemented)
 *
 * Sample table portion for 8MB flash (Winbond w25q64fw):
 *
 *   SEC  |  TB   |  BP2  |  BP1  |  BP0  |  Prot Length  | Protected Portion
 *  --------------------------------------------------------------------------
 *    X   |   X   |   0   |   0   |   0   |  NONE         | NONE
 *    0   |   0   |   0   |   0   |   1   |  128 KB       | Upper 1/64
 *    0   |   0   |   0   |   1   |   0   |  256 KB       | Upper 1/32
 *    0   |   0   |   0   |   1   |   1   |  512 KB       | Upper 1/16
 *    0   |   0   |   1   |   0   |   0   |  1 MB         | Upper 1/8
 *    0   |   0   |   1   |   0   |   1   |  2 MB         | Upper 1/4
 *    0   |   0   |   1   |   1   |   0   |  4 MB         | Upper 1/2
 *    X   |   X   |   1   |   1   |   1   |  8 MB         | ALL
 *
 * Returns negative on errors, 0 on success.
 */
static int stm_lock(struct udevice *dev, loff_t ofs, uint64_t len)
{
	struct mtd *mtd = dev_get_uclass_platdata(dev);
	int devnum = mtd->devnum;
	struct spi_nor *nor;
	u8 status_old, status_new;
	u8 mask = SR_BP2 | SR_BP1 | SR_BP0;
	u8 shift = ffs(mask) - 1, pow, val;

	nor = find_spi_nor_device(devnum);
	if (!nor)
		return -ENODEV;

	status_old = read_sr(nor->dev);
	if (status_old < 0)
		return status_old;

	/* SPI NOR always locks to the end */
	if (ofs + len != mtd->size) {
		/* Does combined region extend to end? */
		if (!stm_is_locked_sr(mtd, ofs + len, mtd->size - ofs - len,
				      status_old))
			return -EINVAL;
		len = mtd->size - ofs;
	}

	/*
	 * Need smallest pow such that:
	 *
	 *   1 / (2^pow) <= (len / size)
	 *
	 * so (assuming power-of-2 size) we do:
	 *
	 *   pow = ceil(log2(size / len)) = log2(size) - floor(log2(len))
	 */
	pow = ilog2(mtd->size) - ilog2(len);
	val = mask - (pow << shift);
	if (val & ~mask)
		return -EINVAL;

	/* Don't "lock" with no region! */
	if (!(val & mask))
		return -EINVAL;

	status_new = (status_old & ~mask) | val;

	/* Only modify protection if it will not unlock other areas */
	if ((status_new & mask) <= (status_old & mask))
		return -EINVAL;

	write_sr(nor->dev, status_new);

	return 0;
}

/*
 * Unlock a region of the flash. See stm_lock() for more info
 *
 * Returns negative on errors, 0 on success.
 */
static int stm_unlock(struct udevice *dev, loff_t ofs, uint64_t len)
{
	struct mtd *mtd = dev_get_uclass_platdata(dev);
	int devnum = mtd->devnum;
	struct spi_nor *nor;
	uint8_t status_old, status_new;
	u8 mask = SR_BP2 | SR_BP1 | SR_BP0;
	u8 shift = ffs(mask) - 1, pow, val;

	nor = find_spi_nor_device(devnum);
	if (!nor)
		return -ENODEV;

	status_old = read_sr(nor->dev);
	if (status_old < 0)
		return status_old;

	/* Cannot unlock; would unlock larger region than requested */
	if (stm_is_locked_sr(mtd, ofs - mtd->erasesize, mtd->erasesize,
			     status_old))
		return -EINVAL;
	/*
	 * Need largest pow such that:
	 *
	 *   1 / (2^pow) >= (len / size)
	 *
	 * so (assuming power-of-2 size) we do:
	 *
	 *   pow = floor(log2(size / len)) = log2(size) - ceil(log2(len))
	 */
	pow = ilog2(mtd->size) - order_base_2(mtd->size - (ofs + len));
	if (ofs + len == mtd->size) {
		val = 0; /* fully unlocked */
	} else {
		val = mask - (pow << shift);
		/* Some power-of-two sizes are not supported */
		if (val & ~mask)
			return -EINVAL;
	}

	status_new = (status_old & ~mask) | val;

	/* Only modify protection if it will not lock other areas */
	if ((status_new & mask) >= (status_old & mask))
		return -EINVAL;

	write_sr(nor->dev, status_new);

	return 0;
}
#endif

#ifdef CONFIG_SPI_NOR_MACRONIX
static int macronix_quad_enable(struct udevice *dev)
{
	int ret, val;

	val = read_sr(dev);
	if (val < 0)
		return val;

	if (val & SR_QUAD_EN_MX)
		return 0;

	write_enable(dev);

	ret = write_sr(dev, val | SR_QUAD_EN_MX);
	if (ret < 0)
		return ret;

	if (spi_nor_wait_till_ready(dev, SNOR_READY_WAIT_PROG))
		return 1;

	ret = read_sr(dev);
	if (!(ret > 0 && (ret & SR_QUAD_EN_MX))) {
		printf("spi-nor: Macronix Quad bit not set\n");
		return -EINVAL;
	}

	return 0;
}
#endif

#if defined(CONFIG_SPI_NOR_SPANSION) || defined(CONFIG_SPI_NOR_WINBOND)
static int spansion_quad_enable(struct udevice *dev)
{
	int ret, val;

	val = read_cr(dev);
	if (val < 0)
		return val;

	if (val & CR_QUAD_EN_SPAN)
		return 0;

	write_enable(dev);

	ret = write_sr_cr(dev, val | CR_QUAD_EN_SPAN);
	if (ret < 0)
		return ret;

	if (spi_nor_wait_till_ready(dev, SNOR_READY_WAIT_PROG))
		return 1;

	/* read back and check it */
	ret = read_cr(dev);
	if (!(ret > 0 && (ret & CR_QUAD_EN_SPAN))) {
		printf("spi-nor: Spansion Quad bit not set\n");
		return -EINVAL;
	}

	return 0;
}
#endif

static int set_quad_mode(struct udevice *dev, const struct spi_nor_info *info)
{
	switch (JEDEC_MFR(info)) {
#ifdef CONFIG_SPI_NOR_MACRONIX
	case SNOR_MFR_MACRONIX:
		return macronix_quad_enable(dev);
#endif
#if defined(CONFIG_SPI_NOR_SPANSION) || defined(CONFIG_SPI_NOR_WINBOND)
	case SNOR_MFR_SPANSION:
	case SNOR_MFR_WINBOND:
		return spansion_quad_enable(dev);
#endif
#ifdef CONFIG_SPI_NOR_STMICRO
	case SNOR_MFR_MICRON:
		return 0;
#endif
	default:
		printf("spi-nor: Need set QEB func for %02x flash\n",
		       JEDEC_MFR(info));
		return -1;
	}
}

#if CONFIG_IS_ENABLED(OF_CONTROL)
int spi_nor_decode_fdt(const void *blob, struct spi_nor *nor)
{
	struct udevice *dev = nor->dev;
	struct mtd *mtd = dev_get_uclass_priv(dev);
	fdt_addr_t addr;
	fdt_size_t size;
	int node;

	/* If there is no node, do nothing */
	node = fdtdec_next_compatible(blob, 0, COMPAT_GENERIC_SPI_FLASH);
	if (node < 0)
		return 0;

	addr = fdtdec_get_addr_size(blob, node, "memory-map", &size);
	if (addr == FDT_ADDR_T_NONE) {
		debug("%s: Cannot decode address\n", __func__);
		return 0;
	}

	if (mtd->size != size) {
		debug("%s: Memory map must cover entire device\n", __func__);
		return -1;
	}
	nor->memory_map = map_sysmem(addr, size);

	return 0;
}
#endif /* CONFIG_IS_ENABLED(OF_CONTROL) */

int spi_nor_scan(struct spi_nor *nor)
{
	struct mtd *mtd = spi_nor_get_mtd(nor);
	struct mtd_ops *ops = mtd_get_ops(mtd->dev);
	const struct spi_nor_info *info = NULL;
	int ret;

	struct spi_nor_uclass_priv *upriv = dev_get_uclass_priv(nor->dev);
	upriv->spi_nor = nor;

	if (nor->init_done)
		return 0;

	info = spi_nor_id(nor->dev);
	if (IS_ERR_OR_NULL(info)) {
		ret = -ENOENT;
		goto err;
	}

	/*
	 * Flash powers up read-only, so clear BP# bits.
	 *
	 * Note on some flash (like Macronix), QE (quad enable) bit is in the
	 * same status register as BP# bits, and we need preserve its original
	 * value during a reboot cycle as this is required by some platforms
	 * (like Intel ICH SPI controller working under descriptor mode).
	 */
	if (JEDEC_MFR(info) == SNOR_MFR_ATMEL ||
	   (JEDEC_MFR(info) == SNOR_MFR_SST) ||
	   (JEDEC_MFR(info) == SNOR_MFR_MACRONIX)) {
		u8 sr = 0;

		if (JEDEC_MFR(info) == SNOR_MFR_MACRONIX)
			sr = read_sr(nor->dev) & SR_QUAD_EN_MX;
		write_sr(nor->dev, sr);
	}

	mtd->name = info->name;
	mtd->priv = nor;
	mtd->type = MTD_NORFLASH;
	mtd->writesize = 1;
	mtd->flags = MTD_CAP_NORFLASH;

	if (info->flags & E_FSR)
		nor->flags |= SNOR_F_USE_FSR;

	if (info->flags & SST_WR)
		nor->flags |= SNOR_F_SST_WRITE;

	ops->write = spi_nor_mwrite;
#if defined(CONFIG_SPI_NOR_SST)
	if (nor->flags & SNOR_F_SST_WRITE) {
		if (nor->mode & SNOR_WRITE_1_1_BYTE)
			ops->write = sst_mwrite_bp;
		else
			ops->write = sst_mwrite_wp;
	}
#endif

#if defined(CONFIG_SPI_NOR_STMICRO) || defined(CONFIG_SPI_NOR_SST)
	/* NOR protection support for STmicro/Micron chips and similar */
	if (JEDEC_MFR(info) == SNOR_MFR_MICRON ||
	    JEDEC_MFR(info) == SNOR_MFR_SST) {
		ops->lock = stm_lock;
		ops->unlock = stm_unlock;
		ops->is_locked = stm_is_locked;
	}
#endif
	/* compute the flash size */
	nor->page_size = info->page_size;
	/*
	 * The Spansion S25FL032P and S25FL064P have 256b pages, yet use the
	 * 0x4d00 Extended JEDEC code. The rest of the Spansion flashes with
	 * the 0x4d00 Extended JEDEC code have 512b pages. All of the others
	 * have 256b pages.
	 */
	if (JEDEC_EXT(info) == 0x4d00) {
		if ((JEDEC_ID(info) != 0x0215) &&
		    (JEDEC_ID(info) != 0x0216))
			nor->page_size = 512;
	}
	mtd->writebufsize = nor->page_size;
	mtd->size = info->sector_size * info->n_sectors;

#ifdef CONFIG_MTD_SPI_NOR_USE_4K_SECTORS
	/* prefer "small sector" erase if possible */
	if (info->flags & SECT_4K) {
		nor->erase_opcode = SNOR_OP_BE_4K;
		mtd->erasesize = 4096;
	} else
#endif
	{
		nor->erase_opcode = SNOR_OP_SE;
		mtd->erasesize = info->sector_size;
	}

	/* Look for read opcode */
	nor->read_opcode = SNOR_OP_READ_FAST;
	if (nor->mode & SNOR_READ)
		nor->read_opcode = SNOR_OP_READ;
	else if (nor->mode & SNOR_READ_1_1_4 && info->flags & RD_QUAD)
		nor->read_opcode = SNOR_OP_READ_1_1_4;
	else if (nor->mode & SNOR_READ_1_1_2 && info->flags & RD_DUAL)
		nor->read_opcode = SNOR_OP_READ_1_1_2;

	/* Look for program opcode */
	if (info->flags & WR_QPP && nor->mode & SNOR_WRITE_1_1_4)
		nor->program_opcode = SNOR_OP_QPP;
	else
		/* Go for default supported write cmd */
		nor->program_opcode = SNOR_OP_PP;

	/* Set the quad enable bit - only for quad commands */
	if ((nor->read_opcode == SNOR_OP_READ_1_1_4) ||
	    (nor->read_opcode == SNOR_OP_READ_1_1_4_IO) ||
	    (nor->program_opcode == SNOR_OP_QPP)) {
		ret = set_quad_mode(nor->dev, info);
		if (ret) {
			debug("spi-nor: quad mode not supported for %02x\n",
			      JEDEC_MFR(info));
			goto err;
		}
	}

	nor->addr_width = 3;

	/* Dummy cycles for read */
	switch (nor->read_opcode) {
	case SNOR_OP_READ_1_1_4_IO:
		nor->read_dummy = 16;
		break;
	case SNOR_OP_READ:
		nor->read_dummy = 0;
		break;
	default:
		nor->read_dummy = 8;
	}

#if CONFIG_IS_ENABLED(OF_CONTROL)
	ret = spi_nor_decode_fdt(gd->fdt_blob, nor);
	if (ret) {
		debug("spi-nor: FDT decode error\n");
		goto err;
	}
#endif

	nor->init_done = 1;
	return 0;
err:
	nor->init_done = 0;
	return ret;
}
