/*
 * (C) Copyright 2016 Jagan Teki <jagan@openedev.com>
 *
 * Xilinx Zynq Quad-SPI(QSPI) NOR controller driver
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <malloc.h>
#include <mtd.h>
#include <asm/io.h>

#include <linux/mtd/spi-nor.h>

DECLARE_GLOBAL_DATA_PTR;

/* zynq qspi register bit masks ZYNQ_QSPI_<REG>_<BIT>_MASK */
#define ZYNQ_QSPI_CR_IFMODE_MASK	BIT(31)	/* Flash intrface mode*/
#define ZYNQ_QSPI_CR_MSA_MASK		BIT(15)	/* Manual start enb */
#define ZYNQ_QSPI_CR_MCS_MASK		BIT(14)	/* Manual chip select */
#define ZYNQ_QSPI_CR_PCS_MASK		BIT(10)	/* Peri chip select */
#define ZYNQ_QSPI_CR_FW_MASK		GENMASK(7, 6)	/* FIFO width */
#define ZYNQ_QSPI_CR_SS_MASK		GENMASK(13, 10)	/* Slave Select */
#define ZYNQ_QSPI_CR_BAUD_MASK		GENMASK(5, 3)	/* Baud rate div */
#define ZYNQ_QSPI_CR_CPHA_MASK		BIT(2)	/* Clock phase */
#define ZYNQ_QSPI_CR_CPOL_MASK		BIT(1)	/* Clock polarity */
#define ZYNQ_QSPI_CR_MSTREN_MASK	BIT(0)	/* Mode select */
#define ZYNQ_QSPI_IXR_RXNEMPTY_MASK	BIT(4)	/* RX_FIFO_not_empty */
#define ZYNQ_QSPI_IXR_TXOW_MASK		BIT(2)	/* TX_FIFO_not_full */
#define ZYNQ_QSPI_IXR_ALL_MASK		GENMASK(6, 0)	/* All IXR bits */
#define ZYNQ_QSPI_ENR_SPI_EN_MASK	BIT(0)	/* SPI Enable */
#define ZYNQ_QSPI_LQSPICFG_LQMODE_MASK	BIT(31) /* Linear QSPI Mode */

/* zynq qspi Transmit Data Register */
#define ZYNQ_QSPI_TXD_00_00_OFFSET	0x1C	/* Transmit 4-byte inst */
#define ZYNQ_QSPI_TXD_00_01_OFFSET	0x80	/* Transmit 1-byte inst */
#define ZYNQ_QSPI_TXD_00_10_OFFSET	0x84	/* Transmit 2-byte inst */
#define ZYNQ_QSPI_TXD_00_11_OFFSET	0x88	/* Transmit 3-byte inst */

#define ZYNQ_QSPI_XFER_BEGIN		BIT(0)
#define ZYNQ_QSPI_XFER_END		BIT(1)
#define ZYNQ_QSPI_TXFIFO_THRESHOLD	1	/* Tx FIFO threshold level*/
#define ZYNQ_QSPI_RXFIFO_THRESHOLD	32	/* Rx FIFO threshold level */

#define ZYNQ_QSPI_CR_BAUD_MAX		8	/* Baud rate divisor max val */
#define ZYNQ_QSPI_CR_BAUD_SHIFT		3	/* Baud rate divisor shift */
#define ZYNQ_QSPI_CR_SS_SHIFT		10	/* Slave select shift */
#define ZYNQ_QSPI_MAX_CMDSZ		4	/* 1 byte opcode,3 byte addr */

#define ZYNQ_QSPI_FIFO_DEPTH		63
#ifndef CONFIG_SYS_ZYNQ_QSPI_WAIT
#define CONFIG_SYS_ZYNQ_QSPI_WAIT	CONFIG_SYS_HZ/100	/* 10 ms */
#endif

/* zynq qspi register set */
struct zynq_qspinor_regs {
	u32 cr;		/* 0x00 */
	u32 isr;	/* 0x04 */
	u32 ier;	/* 0x08 */
	u32 idr;	/* 0x0C */
	u32 imr;	/* 0x10 */
	u32 enr;	/* 0x14 */
	u32 dr;		/* 0x18 */
	u32 txd0r;	/* 0x1C */
	u32 drxr;	/* 0x20 */
	u32 sicr;	/* 0x24 */
	u32 txftr;	/* 0x28 */
	u32 rxftr;	/* 0x2C */
	u32 gpior;	/* 0x30 */
	u32 reserved0[19];
	u32 txd1r;	/* 0x80 */
	u32 txd2r;	/* 0x84 */
	u32 txd3r;	/* 0x88 */
	u32 reserved1[5];
	u32 lqspicfg;	/* 0xA0 */
	u32 lqspists;	/* 0xA4 */
};

/* zynq qspi platform data */
struct zynq_qspinor_platdata {
	struct spi_nor spi_nor;
};

/* zynq qspi priv */
struct zynq_qspinor_priv {
	struct zynq_qspinor_regs *regs;
	u8 cs;
	u8 mode;
	u8 fifo_depth;
	u32 freq;		/* required frequency */
	u8 cmd[4];		/* 1 byte opcode + 3-byte address */
	const void *tx_buf;
	void *rx_buf;
	unsigned len;
	int bytes_to_transfer;
	int bytes_to_receive;
	unsigned int is_inst;
	unsigned cs_change:1;
};

static void zynq_qspinor_addr(u32 addr, u8 *cmd)
{
	/* opcode is in cmd[0] */
	cmd[1] = addr >> 16;
	cmd[2] = addr >> 8;
	cmd[3] = addr >> 0;
}

/*
 * zynq_qspinor_read_data - Copy data to RX buffer
 * @zqspi:	Pointer to the zynq_qspinor structure
 * @data:	The 32 bit variable where data is stored
 * @size:	Number of bytes to be copied from data to RX buffer
 */
static void zynq_qspinor_read_data(struct zynq_qspinor_priv *priv,
				   u32 data, u8 size)
{
	u8 byte3;

	debug("%s: data 0x%04x rx_buf addr: 0x%08x size %d\n", __func__ ,
	      data, (unsigned)(priv->rx_buf), size);

	if (priv->rx_buf) {
		switch (size) {
		case 1:
			*((u8 *)priv->rx_buf) = data;
			priv->rx_buf += 1;
			break;
		case 2:
			*((u16 *)priv->rx_buf) = data;
			priv->rx_buf += 2;
			break;
		case 3:
			*((u16 *)priv->rx_buf) = data;
			priv->rx_buf += 2;
			byte3 = (u8)(data >> 16);
			*((u8 *)priv->rx_buf) = byte3;
			priv->rx_buf += 1;
			break;
		case 4:
			/* Can not assume word aligned buffer */
			memcpy(priv->rx_buf, &data, size);
			priv->rx_buf += 4;
			break;
		default:
			/* This will never execute */
			break;
		}
	}
	priv->bytes_to_receive -= size;
	if (priv->bytes_to_receive < 0)
		priv->bytes_to_receive = 0;
}

/*
 * zynq_qspinor_write_data - Copy data from TX buffer
 * @zqspi:	Pointer to the zynq_qspinor structure
 * @data:	Pointer to the 32 bit variable where data is to be copied
 * @size:	Number of bytes to be copied from TX buffer to data
 */
static void zynq_qspinor_write_data(struct  zynq_qspinor_priv *priv,
		u32 *data, u8 size)
{
	if (priv->tx_buf) {
		switch (size) {
		case 1:
			*data = *((u8 *)priv->tx_buf);
			priv->tx_buf += 1;
			*data |= 0xFFFFFF00;
			break;
		case 2:
			*data = *((u16 *)priv->tx_buf);
			priv->tx_buf += 2;
			*data |= 0xFFFF0000;
			break;
		case 3:
			*data = *((u16 *)priv->tx_buf);
			priv->tx_buf += 2;
			*data |= (*((u8 *)priv->tx_buf) << 16);
			priv->tx_buf += 1;
			*data |= 0xFF000000;
			break;
		case 4:
			/* Can not assume word aligned buffer */
			memcpy(data, priv->tx_buf, size);
			priv->tx_buf += 4;
			break;
		default:
			/* This will never execute */
			break;
		}
	} else {
		*data = 0;
	}

	debug("%s: data 0x%08x tx_buf addr: 0x%08x size %d\n", __func__,
	      *data, (u32)priv->tx_buf, size);

	priv->bytes_to_transfer -= size;
	if (priv->bytes_to_transfer < 0)
		priv->bytes_to_transfer = 0;
}

static void zynq_qspinor_chipselect(struct  zynq_qspinor_priv *priv, int is_on)
{
	u32 confr;
	struct zynq_qspinor_regs *regs = priv->regs;

	confr = readl(&regs->cr);

	if (is_on) {
		/* Select the slave */
		confr &= ~ZYNQ_QSPI_CR_SS_MASK;
		confr |= (~(1 << priv->cs) << ZYNQ_QSPI_CR_SS_SHIFT) &
					ZYNQ_QSPI_CR_SS_MASK;
	} else
		/* Deselect the slave */
		confr |= ZYNQ_QSPI_CR_SS_MASK;

	writel(confr, &regs->cr);
}

/*
 * zynq_qspinor_fill_tx_fifo - Fills the TX FIFO with as many bytes as possible
 * @zqspi:	Pointer to the zynq_qspinor structure
 */
static void zynq_qspinor_fill_tx_fifo(struct zynq_qspinor_priv *priv, u32 size)
{
	u32 data = 0;
	u32 fifocount = 0;
	unsigned len, offset;
	struct zynq_qspinor_regs *regs = priv->regs;
	static const unsigned offsets[4] = {
		ZYNQ_QSPI_TXD_00_00_OFFSET, ZYNQ_QSPI_TXD_00_01_OFFSET,
		ZYNQ_QSPI_TXD_00_10_OFFSET, ZYNQ_QSPI_TXD_00_11_OFFSET };

	while ((fifocount < size) &&
			(priv->bytes_to_transfer > 0)) {
		if (priv->bytes_to_transfer >= 4) {
			if (priv->tx_buf) {
				memcpy(&data, priv->tx_buf, 4);
				priv->tx_buf += 4;
			} else {
				data = 0;
			}
			writel(data, &regs->txd0r);
			priv->bytes_to_transfer -= 4;
			fifocount++;
		} else {
			/* Write TXD1, TXD2, TXD3 only if TxFIFO is empty. */
			if (!(readl(&regs->isr)
					& ZYNQ_QSPI_IXR_TXOW_MASK) &&
					!priv->rx_buf)
				return;
			len = priv->bytes_to_transfer;
			zynq_qspinor_write_data(priv, &data, len);
			offset = (priv->rx_buf) ? offsets[0] : offsets[len];
			writel(data, &regs->cr + (offset / 4));
		}
	}
}

/*
 * zynq_qspinor_irq_poll - Interrupt service routine of the QSPI controller
 * @zqspi:	Pointer to the zynq_qspinor structure
 *
 * This function handles TX empty and Mode Fault interrupts only.
 * On TX empty interrupt this function reads the received data from RX FIFO and
 * fills the TX FIFO if there is any data remaining to be transferred.
 * On Mode Fault interrupt this function indicates that transfer is completed,
 * the SPI subsystem will identify the error as the remaining bytes to be
 * transferred is non-zero.
 *
 * returns:	0 for poll timeout
 *		1 transfer operation complete
 */
static int zynq_qspinor_irq_poll(struct zynq_qspinor_priv *priv)
{
	struct zynq_qspinor_regs *regs = priv->regs;
	u32 rxindex = 0;
	u32 rxcount;
	u32 status, timeout;

	/* Poll until any of the interrupt status bits are set */
	timeout = get_timer(0);
	do {
		status = readl(&regs->isr);
	} while ((status == 0) &&
		(get_timer(timeout) < CONFIG_SYS_ZYNQ_QSPI_WAIT));

	if (status == 0) {
		printf("zynq_qspinor_irq_poll: Timeout!\n");
		return -ETIMEDOUT;
	}

	writel(status, &regs->isr);

	/* Disable all interrupts */
	writel(ZYNQ_QSPI_IXR_ALL_MASK, &regs->idr);
	if ((status & ZYNQ_QSPI_IXR_TXOW_MASK) ||
	    (status & ZYNQ_QSPI_IXR_RXNEMPTY_MASK)) {
		/*
		 * This bit is set when Tx FIFO has < THRESHOLD entries. We have
		 * the THRESHOLD value set to 1, so this bit indicates Tx FIFO
		 * is empty
		 */
		rxcount = priv->bytes_to_receive - priv->bytes_to_transfer;
		rxcount = (rxcount % 4) ? ((rxcount/4)+1) : (rxcount/4);
		while ((rxindex < rxcount) &&
				(rxindex < ZYNQ_QSPI_RXFIFO_THRESHOLD)) {
			/* Read out the data from the RX FIFO */
			u32 data;
			data = readl(&regs->drxr);

			if (priv->bytes_to_receive >= 4) {
				if (priv->rx_buf) {
					memcpy(priv->rx_buf, &data, 4);
					priv->rx_buf += 4;
				}
				priv->bytes_to_receive -= 4;
			} else {
				zynq_qspinor_read_data(priv, data,
						       priv->bytes_to_receive);
			}
			rxindex++;
		}

		if (priv->bytes_to_transfer) {
			/* There is more data to send */
			zynq_qspinor_fill_tx_fifo(priv,
						  ZYNQ_QSPI_RXFIFO_THRESHOLD);

			writel(ZYNQ_QSPI_IXR_ALL_MASK, &regs->ier);
		} else {
			/*
			 * If transfer and receive is completed then only send
			 * complete signal
			 */
			if (!priv->bytes_to_receive) {
				/* return operation complete */
				writel(ZYNQ_QSPI_IXR_ALL_MASK,
				       &regs->idr);
				return 1;
			}
		}
	}

	return 0;
}

/*
 * zynq_qspinor_start_transfer - Initiates the QSPI transfer
 * @qspi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provide information
 *		about next transfer parameters
 *
 * This function fills the TX FIFO, starts the QSPI transfer, and waits for the
 * transfer to be completed.
 *
 * returns:	Number of bytes transferred in the last transfer
 */
static int zynq_qspinor_start_transfer(struct zynq_qspinor_priv *priv)
{
	u32 data = 0;
	struct zynq_qspinor_regs *regs = priv->regs;

	debug("%s: qspi: 0x%08x transfer: 0x%08x len: %d\n", __func__,
	      (u32)priv, (u32)priv, priv->len);

	priv->bytes_to_transfer = priv->len;
	priv->bytes_to_receive = priv->len;

	if (priv->len < 4)
		zynq_qspinor_fill_tx_fifo(priv, priv->len);
	else
		zynq_qspinor_fill_tx_fifo(priv, priv->fifo_depth);

	writel(ZYNQ_QSPI_IXR_ALL_MASK, &regs->ier);

	/* wait for completion */
	do {
		data = zynq_qspinor_irq_poll(priv);
	} while (data == 0);

	return (priv->len) - (priv->bytes_to_transfer);
}

static int zynq_qspinor_transfer(struct zynq_qspinor_priv *priv)
{
	unsigned cs_change = 1;
	int status = 0;

	while (1) {
		/* Select the chip if required */
		if (cs_change)
			zynq_qspinor_chipselect(priv, 1);

		cs_change = priv->cs_change;

		if (!priv->tx_buf && !priv->rx_buf && priv->len) {
			status = -1;
			break;
		}

		/* Request the transfer */
		if (priv->len) {
			status = zynq_qspinor_start_transfer(priv);
			priv->is_inst = 0;
		}

		if (status != priv->len) {
			if (status > 0)
				status = -EMSGSIZE;
			debug("zynq_qspinor_transfer:%d len:%d\n",
			      status, priv->len);
			break;
		}
		status = 0;

		if (cs_change)
			/* Deselect the chip */
			zynq_qspinor_chipselect(priv, 0);

		break;
	}

	return 0;
}

static int zynq_qspinor_xfer(struct zynq_qspinor_priv *priv, unsigned int bitlen,
		const void *dout, void *din, unsigned long flags)
{
	priv->tx_buf = dout;
	priv->rx_buf = din;
	priv->len = bitlen / 8;

	/*
	 * Festering sore.
	 * Assume that the beginning of a transfer with bits to
	 * transmit must contain a device command.
	 */
	if (dout && flags & ZYNQ_QSPI_XFER_BEGIN)
		priv->is_inst = 1;
	else
		priv->is_inst = 0;

	if (flags & ZYNQ_QSPI_XFER_END)
		priv->cs_change = 1;
	else
		priv->cs_change = 0;

	zynq_qspinor_transfer(priv);

	return 0;
}

static int zynq_qspinor_tx_then_rx(struct zynq_qspinor_priv *priv,
				   const u8 *opcode, size_t n_opcode,
				   const u8 *txbuf, u8 *rxbuf, size_t n_buf)
{
	struct zynq_qspinor_regs *regs = priv->regs;
	unsigned long flags = ZYNQ_QSPI_XFER_BEGIN;
	int ret;

	/* enable spi */
	writel(ZYNQ_QSPI_ENR_SPI_EN_MASK, &regs->enr);

	if (n_buf == 0)
		flags |= ZYNQ_QSPI_XFER_END;

	ret = zynq_qspinor_xfer(priv, n_opcode * 8, opcode, NULL, flags);
	if (ret) {
		debug("%s: failed to send command (%zu bytes): %d\n",
		      __func__, n_opcode, ret);
	} else if (n_buf != 0) {
		ret = zynq_qspinor_xfer(priv, n_buf * 8, txbuf, rxbuf,
				     ZYNQ_QSPI_XFER_END);
		if (ret)
			debug("%s: failed to transfer %zu bytes of data: %d\n",
			      __func__, n_buf, ret);
	}

	/* disable spi */
	writel(~ZYNQ_QSPI_ENR_SPI_EN_MASK, &regs->enr);

	return ret;
}

static int zynq_qspinor_read_reg(struct udevice *dev, u8 opcode,
				 u8 *val, int len)
{
	struct zynq_qspinor_priv *priv = dev_get_priv(dev);

	return zynq_qspinor_tx_then_rx(priv, &opcode, 1, NULL, val, len);
}

static int zynq_qspinor_write_reg(struct udevice *dev, u8 opcode,
				  u8 *buf, int len)
{
	struct zynq_qspinor_priv *priv = dev_get_priv(dev);

	return zynq_qspinor_tx_then_rx(priv, &opcode, 1, buf, NULL, len);
}

static int zynq_qspinor_read(struct udevice *dev, loff_t from,
			     size_t len, u_char *buf)
{
	struct zynq_qspinor_priv *priv = dev_get_priv(dev);
	unsigned int cmd_sz;
	struct spi_nor *nor = spi_nor_get_spi_nor_dev(dev);

	priv->cmd[0] = nor->read_opcode;
	zynq_qspinor_addr(from, priv->cmd);
	cmd_sz = sizeof(priv->cmd) + (nor->read_dummy / 8);

	debug("zynq_qspinor: (read) %2x %2x %2x %2x (%llx)\n", priv->cmd[0],
	      priv->cmd[1], priv->cmd[2], priv->cmd[3], from);
	return zynq_qspinor_tx_then_rx(priv, priv->cmd, cmd_sz, NULL, buf, len);
}

static int zynq_qspinor_write(struct udevice *dev, loff_t to,
			      size_t len, const u_char *buf)
{
	struct zynq_qspinor_priv *priv = dev_get_priv(dev);
	struct spi_nor *nor = spi_nor_get_spi_nor_dev(dev);

	priv->cmd[0] = nor->program_opcode;
	if (buf == NULL)
		priv->cmd[0] = nor->erase_opcode;

	zynq_qspinor_addr(to, priv->cmd);

	debug("zynq_qspinor: (%s) %2x %2x %2x %2x (%llx)\n",
	      (buf == NULL) ? "erase" : "write", priv->cmd[0], priv->cmd[1],
	      priv->cmd[2], priv->cmd[3], to);
	return zynq_qspinor_tx_then_rx(priv, priv->cmd, sizeof(priv->cmd),
				       buf, NULL, len);
}

const struct spi_nor_ops zynq_qspinor_ops = {
	.read		= zynq_qspinor_read,
	.write		= zynq_qspinor_write,
	.read_reg	= zynq_qspinor_read_reg,
	.write_reg	= zynq_qspinor_write_reg,
};

static void zynq_qspinor_init_hw(struct zynq_qspinor_priv *priv)
{
	struct zynq_qspinor_regs *regs = priv->regs;
	u32 confr;

	/* disable QSPI */
	writel(~ZYNQ_QSPI_ENR_SPI_EN_MASK, &regs->enr);

	/* disable Interrupts */
	writel(ZYNQ_QSPI_IXR_ALL_MASK, &regs->idr);

	/* clear the TX and RX threshold reg */
	writel(ZYNQ_QSPI_TXFIFO_THRESHOLD, &regs->txftr);
	writel(ZYNQ_QSPI_RXFIFO_THRESHOLD, &regs->rxftr);

	/* clear the RX FIFO */
	while (readl(&regs->isr) & ZYNQ_QSPI_IXR_RXNEMPTY_MASK)
		readl(&regs->drxr);

	/* clear Interrupts */
	writel(ZYNQ_QSPI_IXR_ALL_MASK, &regs->isr);

	/* manual slave select and Auto start */
	confr = readl(&regs->cr);
	confr &= ~ZYNQ_QSPI_CR_MSA_MASK;
	confr |= ZYNQ_QSPI_CR_IFMODE_MASK | ZYNQ_QSPI_CR_MCS_MASK |
		ZYNQ_QSPI_CR_PCS_MASK | ZYNQ_QSPI_CR_FW_MASK |
		ZYNQ_QSPI_CR_MSTREN_MASK;
	writel(confr, &regs->cr);

	/* enable SPI */
	writel(ZYNQ_QSPI_ENR_SPI_EN_MASK, &regs->enr);
}

static int zynq_qspinor_probe(struct udevice *dev)
{
	struct zynq_qspinor_platdata *plat = dev_get_platdata(dev);
	struct zynq_qspinor_priv *priv = dev_get_priv(dev);
	struct spi_nor_uclass_priv *upriv = dev_get_uclass_priv(dev);

	priv->regs = (void *)dev_read_addr(dev);
	priv->fifo_depth = ZYNQ_QSPI_FIFO_DEPTH;

	/* init the zynq spi hw */
	zynq_qspinor_init_hw(priv);

	upriv->spi_nor = &plat->spi_nor;

	return 0;
}

static int zynq_qspinor_bind(struct udevice *dev)
{
	struct zynq_qspinor_platdata *plat = dev_get_platdata(dev);

	return spi_nor_bind(dev, &plat->spi_nor);
}

static const struct udevice_id zynq_qspinor_ids[] = {
	{ .compatible = "xlnx,zynq-qspinor-1.0" },
	{ }
};

U_BOOT_DRIVER(zynq_qspinor) = {
	.name   = "zynq_qspinor",
	.id     = UCLASS_SPI_NOR,
	.of_match = zynq_qspinor_ids,
	.ops	= &zynq_qspinor_ops,
	.bind	= zynq_qspinor_bind,
	.probe  = zynq_qspinor_probe,
	.priv_auto_alloc_size = sizeof(struct zynq_qspinor_priv),
	.platdata_auto_alloc_size = sizeof(struct zynq_qspinor_platdata),
};
