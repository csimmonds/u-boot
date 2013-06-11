/*
 * (C) Copyright 2008 - 2009
 * Windriver, <www.windriver.com>
 * Tom Rix <Tom.Rix at windriver.com>
 *
 * Copyright (c) 2011 Sebastian Andrzej Siewior <bigeasy at linutronix.de>
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
 * The logical naming of flash comes from the Android project
 * Thse structures and functions that look like fastboot_flash_*
 * They come from bootloader/legacy/include/boot/flash.h
 *
 * The boot_img_hdr structure and associated magic numbers also
 * come from the Android project.  They are from
 * bootloader/legacy/include/boot/bootimg.h
 *
 * Here are their copyrights
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
 *
 */

#ifndef FASTBOOT_H
#define FASTBOOT_H

#include <common.h>
#include <command.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#ifdef DEBUG
#define FBTDBG(fmt,args...)\
        printf("DEBUG: [%s]: %d: \n"fmt, __FUNCTION__, __LINE__,##args)
#else
#define FBTDBG(fmt,args...) do{}while(0)
#endif

#ifdef INFO
#define FBTINFO(fmt,args...)\
        printf("INFO: [%s]: "fmt, __FUNCTION__, ##args)
#else
#define FBTINFO(fmt,args...) do{}while(0)
#endif

#ifdef WARN
#define FBTWARN(fmt,args...)\
        printf("WARNING: [%s]: "fmt, __FUNCTION__, ##args)
#else
#define FBTWARN(fmt,args...) do{}while(0)
#endif

#ifdef ERR
#define FBTERR(fmt,args...)\
        printf("ERROR: [%s]: "fmt, __FUNCTION__, ##args)
#else
#define FBTERR(fmt,args...) do{}while(0)
#endif

struct fastboot_config {

	/*
	 * Transfer buffer for storing data sent by the client. It should be
	 * able to hold a kernel image and flash partitions. Care should be
	 * take so it does not overrun bootloader memory
	 */
	unsigned char *transfer_buffer;

	/* Size of the buffer mentioned above */
	unsigned int transfer_buffer_size;

	/* Total data to be downloaded */
	unsigned int download_size;

	/* Data downloaded so far */
	unsigned int download_bytes;

	unsigned int nand_block_size;

	unsigned int nand_oob_size;

	unsigned int download_bytes_unpadded;
};

#define FB_STR_PRODUCT_IDX      1
#define FB_STR_SERIAL_IDX       2
#define FB_STR_CONFIG_IDX       3
#define FB_STR_INTERFACE_IDX    4
#define FB_STR_MANUFACTURER_IDX 5
#define FB_STR_PROC_REV_IDX     6
#define FB_STR_PROC_TYPE_IDX    7

#ifdef CONFIG_CMD_FASTBOOT

int fastboot_init(void);
void fastboot_shutdown(void);
int fastboot_poll(void);

int fastboot_board_init(struct fastboot_config *interface,
		struct usb_gadget_strings **str);


/* Android-style flash naming */
typedef struct fastboot_ptentry fastboot_ptentry;

/* flash partitions are defined in terms of blocks
 ** (flash erase units)
 */
struct fastboot_ptentry{

	/* The logical name for this partition, null terminated */
	char name[16];
	/* The start wrt the nand part, must be multiple of nand block size */
	unsigned int start;
	/* The length of the partition, must be multiple of nand block size */
	unsigned int length;
	/* Controls the details of how operations are done on the partition
	   See the FASTBOOT_PTENTRY_FLAGS_*'s defined below */
	unsigned int flags;
};

/* Lower byte shows if the read/write/erase operation in
   repeated.  The base address is incremented.
   Either 0 or 1 is ok for a default */

#define FASTBOOT_PTENTRY_FLAGS_REPEAT_MASK(n)   (n & 0x0f)
#define FASTBOOT_PTENTRY_FLAGS_REPEAT_4         0x00000004

/* Writes happen a block at a time.
   If the write fails, go to next block
   NEXT_GOOD_BLOCK and CONTIGOUS_BLOCK can not both be set */
#define FASTBOOT_PTENTRY_FLAGS_WRITE_NEXT_GOOD_BLOCK  0x00000010

/* Find a contiguous block big enough for a the whole file
   NEXT_GOOD_BLOCK and CONTIGOUS_BLOCK can not both be set */
#define FASTBOOT_PTENTRY_FLAGS_WRITE_CONTIGUOUS_BLOCK 0x00000020

/* Sets the ECC to software before writing
   HW and SW ECC should not both be set. */
#define FASTBOOT_PTENTRY_FLAGS_WRITE_SW_ECC           0x00000040

/* Sets the ECC to hardware before writing
   HW and SW ECC should not both be set. */
#define FASTBOOT_PTENTRY_FLAGS_WRITE_HW_ECC           0x00000080

/* Sets the ECC to hardware before writing
   HW and SW ECC should not both be set. */
#define FASTBOOT_PTENTRY_FLAGS_WRITE_HW_BCH4_ECC      0x00000100

/* Sets the ECC to hardware before writing
   HW and SW ECC should not both be set. */
#define FASTBOOT_PTENTRY_FLAGS_WRITE_HW_BCH8_ECC      0x00000200

/* Sets the ECC to hardware before writing
   HW and SW ECC should not both be set. */
#define FASTBOOT_PTENTRY_FLAGS_WRITE_HW_BCH16_ECC     0x00000400

/* Write the file with write.i */
#define FASTBOOT_PTENTRY_FLAGS_WRITE_I                0x00000800

/* Write the file with write.jffs2 */
#define FASTBOOT_PTENTRY_FLAGS_WRITE_JFFS2            0x00001000

/* Write the file as a series of variable/value pairs
   using the setenv and saveenv commands */
#define FASTBOOT_PTENTRY_FLAGS_WRITE_ENV              0x00002000


/* The Android-style flash handling */

/* tools to populate and query the partition table */
extern void fastboot_flash_add_ptn(fastboot_ptentry *ptn);
extern fastboot_ptentry *fastboot_flash_find_ptn(const char *name);
extern fastboot_ptentry *fastboot_flash_get_ptn(unsigned n);
extern unsigned int fastboot_flash_get_ptn_count(void);
extern void fastboot_flash_dump_ptn(void);
extern int fastboot_oem(const char *cmd);


#endif
#endif
