/*
 * Command for accessing SPI-NOR device.
 *
 * Copyright (C) 2016 Jagan Teki <jagan@openedev.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <mtd.h>

#include <asm/io.h>
#include <jffs2/jffs2.h>
#include <linux/mtd/spi-nor.h>

static int curr_device = 0;

static int do_spinor_list(void)
{
	print_spi_nor_devices('\n');
	return CMD_RET_SUCCESS;
}

static struct spi_nor *init_spinor_device(int dev, bool force_init)
{
	struct spi_nor *nor;

	nor = find_spi_nor_device(dev);
	if (!nor) {
		printf("No SPI-NOR device found! %x\n", dev);
		return NULL;
	}

	if (force_init)
		nor->init_done = 0;
	if (spi_nor_scan(nor))
		return NULL;

	return nor;
}

static void print_spinor_info(struct spi_nor *nor)
{
	struct mtd *mtd = spi_nor_get_mtd(nor);

	printf("bus: %s: %d\n", nor->dev->name, mtd->devnum);
	printf("device: %s\n", mtd->name);
	printf("page size: %d B\nerase size: ", mtd->writebufsize);
	print_size(mtd->erasesize, "\nsize: ");
	print_size(mtd->size, "");
	if (nor->memory_map)
		printf(", mapped at %p", nor->memory_map);
	printf("\n");
}

static int do_spinor_info(void)
{
	struct spi_nor *nor;

	if (curr_device < 0) {
		if (get_spi_nor_num() > 0)
			curr_device = 0;
		else {
			puts("No SPI-NOR device available\n");
			return 1;
		}
	}

	nor = init_spinor_device(curr_device, false);
	if (!nor)
		return CMD_RET_FAILURE;

	print_spinor_info(nor);
	return CMD_RET_SUCCESS;
}

static int do_spinor_dev(int argc, char * const argv[])
{
	struct spi_nor *nor;
	int devnum = 0;
	int ret;

	if (argc == 2)
		devnum = curr_device;
	else if (argc == 3)
		devnum = simple_strtoul(argv[2], NULL, 10);

	nor = init_spinor_device(devnum, true);
	if (!nor)
		return CMD_RET_FAILURE;

	ret = mtd_select_devnum(MTD_IF_TYPE_SPI_NOR, devnum);
	printf("switch to dev #%d, %s\n", devnum, (!ret) ? "OK" : "ERROR");
	if (ret)
		return CMD_RET_FAILURE;

	curr_device = devnum;
	printf("spinor%d is current device\n", curr_device);

	return CMD_RET_SUCCESS;
}

static int do_spinor_write_read(int argc, char * const argv[])
{
	struct mtd *mtd;
	struct spi_nor *nor;
	loff_t offset, addr, len, maxsize;
	u_char *buf;
	char *endp;
	int idx = 0;
	int ret = CMD_RET_FAILURE;

	if (argc != 4)
		return CMD_RET_USAGE;

	nor = init_spinor_device(curr_device, false);
	if (!nor)
		return CMD_RET_FAILURE;

	addr = simple_strtoul(argv[1], &endp, 16);
	if (*argv[1] == 0 || *endp != 0)
		return CMD_RET_FAILURE;

	mtd = spi_nor_get_mtd(nor);
	if (mtd_arg_off_size(argc - 2, &argv[2], &idx, &offset, &len,
			     &maxsize, MTD_DEV_TYPE_NOR, mtd->size))
		return CMD_RET_FAILURE;

	buf = map_physmem(addr, len, MAP_WRBACK);
	if (!buf) {
		puts("failed to map physical memory\n");
		return 1;
	}

	if (strcmp(argv[0], "write") == 0)
		ret = mtd_dwrite(mtd, offset, len, (size_t *)&len, buf);
	else if (strcmp(argv[0], "read") == 0)
		ret = mtd_dread(mtd, offset, len, (size_t *)&len, buf);

	printf("SPI-NOR: %zu bytes @ %#llx %s: ", (size_t)len, offset,
	       (strcmp(argv[0], "read") == 0) ? "Read" : "Written");
	if (ret)
		printf("ERROR %d\n", ret);
	else
		printf("OK\n");

	unmap_physmem(buf, len);

	return ret == 0 ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

static int do_spinor_protect(int argc, char * const argv[])
{
	struct mtd *mtd;
	struct spi_nor *nor;
	loff_t sector, len, maxsize;
	char *endp;
	int idx = 0;
	bool prot = false;
	int ret = CMD_RET_FAILURE;

	if (argc != 4)
		return CMD_RET_USAGE;

	nor = init_spinor_device(curr_device, false);
	if (!nor)
		return CMD_RET_FAILURE;

	sector = simple_strtoul(argv[2], &endp, 16);
	if (*argv[2] == 0 || *endp != 0)
		return CMD_RET_FAILURE;

	mtd = spi_nor_get_mtd(nor);
	if (mtd_arg_off_size(argc - 3, &argv[3], &idx, &sector, &len,
			     &maxsize, MTD_DEV_TYPE_NOR, mtd->size))
		return CMD_RET_FAILURE;

	if (strcmp(argv[1], "lock") == 0)
		prot = true;
	else if (strcmp(argv[1], "unlock") == 0)
		prot = false;
	else
		return -1;  /* Unknown parameter */

	ret = mtd_dprotect(mtd, sector, len, prot);

	return ret == 0 ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

static int mtd_parse_len_arg(struct mtd *mtd, char *arg, loff_t *len)
{
	char *ep;
	char round_up_len; /* indicates if the "+length" form used */
	ulong len_arg;

	round_up_len = 0;
	if (*arg == '+') {
		round_up_len = 1;
		++arg;
	}

	len_arg = simple_strtoul(arg, &ep, 16);
	if (ep == arg || *ep != '\0')
		return -1;

	if (round_up_len && mtd->erasesize > 0)
		*len = ROUND(len_arg, mtd->erasesize);
	else
		*len = len_arg;

	return 1;
}

static int do_spinor_erase(int argc, char * const argv[])
{
	struct mtd *mtd;
	struct spi_nor *nor;
	loff_t addr, len, maxsize;
	int idx = 0;
	int ret;

	if (argc != 3)
		return CMD_RET_USAGE;

	nor = init_spinor_device(curr_device, false);
	if (!nor)
		return CMD_RET_FAILURE;

	mtd = spi_nor_get_mtd(nor);
	if (mtd_arg_off(argv[1], &idx, &addr, &len, &maxsize,
			MTD_DEV_TYPE_NOR, mtd->size))
		return CMD_RET_FAILURE;

	ret = mtd_parse_len_arg(mtd, argv[2], &len);
	if (ret != 1)
		return CMD_RET_FAILURE;

	ret = mtd_derase(mtd, addr, len);
	printf("SPI-NOR: %zu bytes @ %#llx Erased: %s\n", (size_t)len, addr,
	       ret ? "ERROR" : "OK");

	return ret == 0 ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

static int do_spinor(cmd_tbl_t *cmdtp, int flag, int argc,
			char * const argv[])
{
	const char *cmd;
	int ret = 0;

	cmd = argv[1];
	if (strcmp(cmd, "list") == 0) {
		if (argc > 2)
			goto usage;

		ret = do_spinor_list();
		goto done;
	}

	if (strcmp(cmd, "dev") == 0) {
		if (argc > 3)
			goto usage;

		ret = do_spinor_dev(argc, argv);
		goto done;
	}

	if (strcmp(cmd, "info") == 0) {
		if (argc > 2)
			goto usage;

		ret = do_spinor_info();
		goto done;
	}

	if (argc < 3)
		goto usage;

	--argc;
	++argv;

	if (strcmp(cmd, "erase") == 0) {
		ret = do_spinor_erase(argc, argv);
		goto done;
	}

	if (strcmp(cmd, "write") == 0 || strcmp(cmd, "read") == 0) {
		ret = do_spinor_write_read(argc, argv);
		goto done;
	}

	if (strcmp(cmd, "protect") == 0) {
		ret = do_spinor_protect(argc, argv);
		goto done;
	}

done:
	if (ret != -1)
		return ret;

usage:
	return CMD_RET_USAGE;
}

static char spinor_help_text[] =
	"list			- show list of spinor devices\n"
	"spinor info			- show current spinor device info\n"
	"spinor dev [devnum]		- show or set current spinor device\n"
	"spinor erase offset len         - erase 'len' bytes from 'offset'\n"
	"spinor write addr to len	- write 'len' bytes to 'to' from 'addr'\n"
	"spinor read addr from len	- read 'len' bytes from 'from' to 'addr'\n"
	"spinor protect lock/unlock sector len - protect/unprotect 'len' bytes starting\n"
	"				  at address 'sector'";

U_BOOT_CMD(
	spinor, 5, 1, do_spinor,
	"SPI-NOR Sub-system",
	spinor_help_text
);
