/*
 * Copyright (C) 2013 Texas Instruments
 *
 * Author : Pankaj Bharadiya <pankaj.bharadiya@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 *
 * Fastboot is implemented using gadget stack, many of the ideas are
 * derived from fastboot implemented in OmapZoom by
 * Tom Rix <Tom.Rix@windriver.com> and Sitara 2011 u-boot by
 * Mohammed Afzal M A <afzal@ti.com>
 *
 * Part of OmapZoom was copied from Android project, Android source
 * (legacy bootloader) was used indirectly here by using OmapZoom.
 *
 * This is Android's Copyright:
 *
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <common.h>
#include <command.h>
#include <usb/fastboot.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include "g_fastboot.h"
#include <environment.h>
#include <mmc.h>

int do_format(void)
{
	/* Maybe one day this will write a new partition table... */
	printf("do_format - not implemented\n");
	return -1;
}

struct fastboot_config fastboot_cfg;

/* Map fastboot partition names to MMC partition numbers, like so

  P2: boot       Contains Android boot image
  P3: recovery   Contains Android recovery image
[ P4: extended   Contains logical partitions L5..L7 ]
  L5: system
  L6: userdata
  L7: cache

  P1 Contains MLO, u-boot.img and uEnv.txt as usual and
  is vfat format with the boot flag set. We don't mess with this partition
 */

struct partition {
	const char *name;
	unsigned int part;
};

static struct partition partitions[] = {
	{ "boot", 2},
	{ "recovery", 3},
	{ "system", 5},
	{ "userdata", 6},
	{ "cache", 7}
};

int fastboot_find_partition_number(const char *name)
{
	unsigned int n;

	for (n = 0; n < sizeof(partitions)/sizeof(struct partition); n++) {
	if (strcmp(partitions[n].name, name) == 0)
		return partitions[n].part;
	}
	return 0;
}

int handle_flash(char *part_name, char *response)
{
	int status = 0;
	int dev = CONFIG_MMC_FASTBOOT_DEV;
	int part;
	int ret;
	int n;
	unsigned int block_count;
	struct mmc *mmc;
	block_dev_desc_t *blk_dev;
	disk_partition_t partinfo;

	if (fastboot_cfg.download_bytes == 0)
		return -1;

	part = fastboot_find_partition_number(part_name);
	if (part == 0) {
		printf("Partition:[%s] does not exist\n", part_name);
		sprintf(response, "FAILpartition does not exist");
		return -1;
	}

	mmc = find_mmc_device(dev);
	if (mmc == NULL || mmc_init(mmc)) {
		printf("%s: could not find mmc device #%d!\n",
			__func__, dev);
		sprintf(response, "FAILmmc device does not exist");
		return -ENODEV;
	}
	blk_dev = &mmc->block_dev;

	ret = get_partition_info(blk_dev, part, &partinfo);
	if (ret != 0) {
		printf("%s: could not find partition %s [%d] on device\n",
                        __func__, part_name, part);
		sprintf(response, "FAILpartition does not exist");
		return -1;

	}
	block_count = ((fastboot_cfg.download_bytes - 1)/512) + 1;

	if (block_count > partinfo.size) {
		printf("Image too large for the partition\n");
		printf("Download size %d, partition size %lu\n",
			block_count * 512, partinfo.size * 512);
		sprintf(response, "FAILimage too large for partition");
		return -1;
	}

	printf("Writing to MMC device %d partition %d from block %lu count %d\n",
		dev, part, partinfo.start, block_count);
	n = mmc->block_dev.block_write(dev, partinfo.start, block_count,
		fastboot_cfg.transfer_buffer);
	if (n != block_count) {
		printf("Writing '%s' FAILED!\n", part_name);
		sprintf(response, "FAIL: Write partition");
	} else {
		printf("Writing '%s' DONE!\n", part_name);
		sprintf(response, "OKAY");
	}
	return 0;
}

int board_mmc_fbtptn_init(void)
{
	/* Do any pre-initialisation here. None needed at the moment */
	return 0;
}

