/*
 * SPI NOR Core header file.
 *
 * Copyright (C) 2016 Jagan Teki <jagan@openedev.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __MTD_SPI_NOR_H
#define __MTD_SPI_NOR_H

#include <common.h>

/*
 * Manufacturer IDs
 *
 * The first byte returned from the flash after sending opcode SPINOR_OP_RDID.
 * Sometimes these are the same as CFI IDs, but sometimes they aren't.
 */
#define SNOR_MFR_ATMEL		0x1f
#define SNOR_MFR_MACRONIX	0xc2
#define SNOR_MFR_MICRON		0x20	/* ST Micro <--> Micron */
#define SNOR_MFR_SPANSION	0x01
#define SNOR_MFR_SST		0xbf
#define SNOR_MFR_WINBOND	0xef

/**
 * SPI NOR opcodes.
 *
 * Note on opcode nomenclature: some opcodes have a format like
 * SNOR_OP_FUNCTION{4,}_x_y_z. The numbers x, y, and z stand for the number
 * of I/O lines used for the opcode, address, and data (respectively). The
 * FUNCTION has an optional suffix of '4', to represent an opcode which
 * requires a 4-byte (32-bit) address.
 */
#define SNOR_OP_WRDI		0x04	/* Write disable */
#define SNOR_OP_WREN		0x06	/* Write enable */
#define SNOR_OP_RDSR		0x05	/* Read status register */
#define SNOR_OP_WRSR		0x01	/* Write status register 1 byte */
#define SNOR_OP_READ		0x03	/* Read data bytes (low frequency) */
#define SNOR_OP_READ_FAST	0x0b	/* Read data bytes (high frequency) */
#define SNOR_OP_READ_1_1_2	0x3b	/* Read data bytes (Dual SPI) */
#define SNOR_OP_READ_1_1_2_IO	0xbb	/* Read data bytes (Dual IO SPI) */
#define SNOR_OP_READ_1_1_4	0x6b	/* Read data bytes (Quad SPI) */
#define SNOR_OP_READ_1_1_4_IO	0xeb	/* Read data bytes (Quad IO SPI) */
#define SNOR_OP_BRWR		0x17	/* Bank register write */
#define SNOR_OP_BRRD		0x16	/* Bank register read */
#define SNOR_OP_WREAR		0xC5	/* Write extended address register */
#define SNOR_OP_RDEAR		0xC8	/* Read extended address register */
#define SNOR_OP_PP		0x02	/* Page program (up to 256 bytes) */
#define SNOR_OP_QPP		0x32	/* Quad Page program */
#define SNOR_OP_BE_4K		0x20	/* Erase 4KiB block */
#define SNOR_OP_BE_4K_PMC	0xd7    /* Erase 4KiB block on PMC chips */
#define SNOR_OP_BE_32K		0x52    /* Erase 32KiB block */
#define SPINOR_OP_CHIP_ERASE	0xc7    /* Erase whole flash chip */
#define SNOR_OP_SE		0xd8	/* Sector erase (usually 64KiB) */
#define SNOR_OP_RDID		0x9f	/* Read JEDEC ID */
#define SNOR_OP_RDCR		0x35	/* Read configuration register */
#define SNOR_OP_RDFSR		0x70	/* Read flag status register */

/* Used for SST flashes only. */
#define SNOR_OP_BP		0x02	/* Byte program */
#define SNOR_OP_AAI_WP		0xad	/* Auto addr increment word program */

/* Status Register bits. */
#define SR_WIP			BIT(0)	/* Write in progress */
#define SR_WEL			BIT(1)	/* Write enable latch */

/* meaning of other SR_* bits may differ between vendors */
#define SR_BP0			BIT(2)	/* Block protect 0 */
#define SR_BP1			BIT(3)	/* Block protect 1 */
#define SR_BP2			BIT(4)	/* Block protect 2 */
#define SR_SRWD			BIT(7)	/* SR write protect */

#define SR_QUAD_EN_MX		BIT(6)	/* Macronix Quad I/O */

/* Flag Status Register bits */
#define FSR_READY		BIT(7)

/* Configuration Register bits. */
#define CR_QUAD_EN_SPAN		BIT(1) /* Spansion/Winbond Quad I/O */

/* Flash timeout values */
#define SNOR_READY_WAIT_PROG	(2 * CONFIG_SYS_HZ)
#define SNOR_READY_WAIT_ERASE	(5 * CONFIG_SYS_HZ)
#define SNOR_MAX_CMD_SIZE	4
#define SNOR_16MB_BOUN		0x1000000

/**
 * struct spi_nor_uclass_priv - Holds information about a device used by the uclass
 */
struct spi_nor_uclass_priv {
	struct spi_nor *spi_nor;
};

enum snor_option_flags {
	SNOR_F_SST_WRITE	= BIT(0),
	SNOR_F_USE_FSR		= BIT(1),
	SNOR_F_U_PAGE		= BIT(1),
};

enum mode {
	SNOR_READ		= BIT(0),
	SNOR_READ_1_1_2		= BIT(1),
	SNOR_READ_1_1_4		= BIT(2),
	SNOR_READ_1_1_2_IO	= BIT(3),
	SNOR_READ_1_1_4_IO	= BIT(4),
	SNOR_WRITE_1_1_BYTE	= BIT(5),
	SNOR_WRITE_1_1_4	= BIT(6),
};

#define JEDEC_MFR(info)		((info)->id[0])
#define JEDEC_ID(info)		(((info)->id[1]) << 8 | ((info)->id[2]))
#define JEDEC_EXT(info)		(((info)->id[3]) << 8 | ((info)->id[4]))
#define SPI_NOR_MAX_ID_LEN	6

struct spi_nor_info {
	char		*name;

	/*
	 * This array stores the ID bytes.
	 * The first three bytes are the JEDIC ID.
	 * JEDEC ID zero means "no ID" (mostly older chips).
	 */
	u8		id[SPI_NOR_MAX_ID_LEN];
	u8		id_len;

	/* The size listed here is what works with SNOR_OP_SE, which isn't
	 * necessarily called a "sector" by the vendor.
	 */
	unsigned int	sector_size;
	u16		n_sectors;

	u16		page_size;

	u16		flags;
#define SECT_4K			BIT(0)
#define E_FSR			BIT(1)
#define SST_WR			BIT(2)
#define WR_QPP			BIT(3)
#define RD_QUAD			BIT(4)
#define RD_DUAL			BIT(5)
#define RD_QUADIO		BIT(6)
#define RD_DUALIO		BIT(7)
#define RD_FULL			(RD_QUAD | RD_DUAL | RD_QUADIO | RD_DUALIO)
};

extern const struct spi_nor_info spi_nor_ids[];

/**
 * struct spi_nor - Structure for defining a the SPI NOR layer
 *
 * @dev:		SPI NOR device
 * @name:		name of the SPI NOR device
 * @page_size:		the page size of the SPI NOR
 * @addr_width:		number of address bytes
 * @erase_opcode:	the opcode for erasing a sector
 * @read_opcode:	the read opcode
 * @read_dummy:		the dummy bytes needed by the read operation
 * @program_opcode:	the program opcode
 * @flags:		flag options for the current SPI-NOR (SNOR_F_*)
 * @mode:		read, write mode or any other mode bits.
 * @read_mode:		read mode.
 * @cmd_buf:		used by the write_reg
 * @read_reg:		[DRIVER-SPECIFIC] read out the register
 * @write_reg:		[DRIVER-SPECIFIC] write data to the register
 * @read:		[DRIVER-SPECIFIC] read data from the SPI NOR
 * @write:		[DRIVER-SPECIFIC] write data to the SPI NOR
 * @memory_map:	address of read-only SPI NOR access
 */
struct spi_nor {
	struct udevice		*dev;
	const char		*name;
	u8			init_done;
	u32			page_size;
	u8			addr_width;
	u8			erase_opcode;
	u8			read_opcode;
	u8			read_dummy;
	u8			program_opcode;
	u32			flags;
	u8			mode;
	u8			read_mode;
	u8			cmd_buf[SNOR_MAX_CMD_SIZE];

	void *memory_map;
};

struct spi_nor_ops {
	int (*read_reg)(struct udevice *dev, u8 cmd, u8 *val, int len);
	int (*write_reg)(struct udevice *dev, u8 cmd, u8 *data, int len);

	int (*read)(struct udevice *dev, loff_t from, size_t len,
		    u_char *buf);
	int (*write)(struct udevice *dev, loff_t to, size_t len,
		     const u_char *buf);
};

#define spi_nor_get_ops(dev)	((struct spi_nor_ops *)(dev)->driver->ops)

int spi_nor_merase(struct udevice *dev, loff_t addr, size_t len);
int spi_nor_mread(struct udevice *dev, loff_t from, size_t len,
		  size_t *retlen, u_char *buf);

int spi_nor_scan(struct spi_nor *nor);
int spi_nor_bind(struct udevice *dev, struct spi_nor *nor);

struct spi_nor *find_spi_nor_device(int dev_num);
int get_spi_nor_num(void);
struct spi_nor *spi_nor_get_spi_nor_dev(struct udevice *dev);
struct mtd *spi_nor_get_mtd(struct spi_nor *nor);

#endif /* __MTD_SPI_NOR_H */
