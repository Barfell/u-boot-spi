/*
 * Copyright (C) 2017 Jagan Teki <jagan@openedev.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <asm/state.h>
#include <dm/test.h>
#include <test/ut.h>

#include <mtd.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-nor.h>

DECLARE_GLOBAL_DATA_PTR;

/* Test that we can find mtd devices without probing them */
static int dm_test_mtd_find(struct unit_test_state *uts)
{
	struct udevice *mtd, *spinor, *dev;

	ut_assertok(mtd_create_device(mtd, "spinor_mtd", "test",
				      MTD_IF_TYPE_SPI_NOR, &spinor));

	ut_asserteq(-ENODEV, mtd_find_device(MTD_IF_TYPE_SPI_NOR, 1, &dev));
	ut_assertok(mtd_find_device(MTD_IF_TYPE_SPI_NOR, 0, &dev));
	ut_asserteq_ptr(spinor, dev);

	return 0;
}
DM_TEST(dm_test_mtd_find, DM_TESTF_SCAN_PDATA | DM_TESTF_SCAN_FDT);
