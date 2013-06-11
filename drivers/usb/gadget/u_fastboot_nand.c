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

#include <nand.h>
#include <environment.h>

#define MAX_PTN                 5

/* Initialize the name of fastboot flash name mappings */
fastboot_ptentry nand_ptn[MAX_PTN] = {
	{
		.name   = "spl",
		.start  = 0x0000000,
		.length = 0x0020000, /* 128 K */
		/* Written into the first 4 0x20000 blocks
		   Use HW ECC */
		.flags  = FASTBOOT_PTENTRY_FLAGS_WRITE_I |
			  FASTBOOT_PTENTRY_FLAGS_WRITE_HW_BCH8_ECC |
			  FASTBOOT_PTENTRY_FLAGS_REPEAT_4,
	},
	{
		.name   = "uboot",
		.start  = 0x0080000,
		.length = 0x01E0000, /* 1.875 M */
		/* Skip bad blocks on write
		   Use HW ECC */
		.flags  = FASTBOOT_PTENTRY_FLAGS_WRITE_I |
			  FASTBOOT_PTENTRY_FLAGS_WRITE_HW_BCH8_ECC,
	},
	{
		.name   = "environment",
		.start  = NAND_ENV_OFFSET,  /* set in config file */
		.length = 0x0020000,
		.flags  = FASTBOOT_PTENTRY_FLAGS_WRITE_ENV |
			  FASTBOOT_PTENTRY_FLAGS_WRITE_HW_ECC,
	},
	{
		.name   = "kernel",
		.start  = 0x0280000,
		.length = 0x0500000, /* 5 M */
		.flags  = FASTBOOT_PTENTRY_FLAGS_WRITE_I |
			  FASTBOOT_PTENTRY_FLAGS_WRITE_HW_BCH8_ECC,
	},
	{
		.name   = "filesystem",
		.start  = 0x0780000,
		.length = 0xF880000, /* 248.5 M */
		.flags  = FASTBOOT_PTENTRY_FLAGS_WRITE_I |
			  FASTBOOT_PTENTRY_FLAGS_WRITE_HW_BCH8_ECC,
	},
};

struct fastboot_config fastboot_cfg;

extern int do_env_save (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);
extern int do_switch_ecc(cmd_tbl_t * cmdtp, int flag, int argc, char * const argv[]);

static void set_env(char *var, char *val)
{
	char *setenv[4]  = { "setenv", NULL, NULL, NULL, };

	setenv[1] = var;
	setenv[2] = val;

	do_env_set(NULL, 0, 3, setenv);
}

static void set_ecc(int hw)
{
	char ecc_type[2];
	char *ecc[4]     = { "nandecc", "hw", "0" , NULL };
	ecc[2] = ecc_type;
	if (hw)
	{
		/*for hardware ecc : set BCH8*/
		sprintf(ecc_type, "2");
	}
	else
	{
		sprintf(ecc_type, "0");
	}

	do_switch_ecc(NULL, 0, 3, ecc);
}

static void save_env(struct fastboot_ptentry *ptn,
		     char *var, char *val)
{
	char *saveenv[2] = { "setenv", NULL, };
	set_env (var, val);

	if ((ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_HW_ECC) &&
	    (ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_SW_ECC))	{
		/* Both can not be true */
		FBTWARN("can not do hw and sw ecc for partition '%s'\n", ptn->name);
		FBTWARN("Ignoring these flags\n");
	} else if (ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_HW_ECC) {
		set_ecc(1);
	} else if (ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_SW_ECC) {
		set_ecc(0);
	}
	do_env_save(NULL, 0, 1, saveenv);
}

static void save_block_values(struct fastboot_ptentry *ptn,
			      unsigned int offset,
			      unsigned int size)
{
	struct fastboot_ptentry *env_ptn;

	char var[64], val[32];
	char start[32], length[32];
	char *setenv[4]  = { "setenv", NULL, NULL, NULL, };
	char *saveenv[2] = { "setenv", NULL, };

	setenv[1] = var;
	setenv[2] = val;

	FBTINFO ("saving it..\n");

	if (size == 0) {
		/* The error case, where the variables are being unset */

		sprintf (var, "%s_nand_offset", ptn->name);
		sprintf (val, "");
		do_env_set (NULL, 0, 3, setenv);

		sprintf (var, "%s_nand_size", ptn->name);
		sprintf (val, "");
		do_env_set (NULL, 0, 3, setenv);
	} else {
		/* Normal case */

		sprintf (var, "%s_nand_offset", ptn->name);
		sprintf (val, "0x%x", offset);

		FBTINFO("%s %s %s\n", setenv[0], setenv[1], setenv[2]);

		do_env_set (NULL, 0, 3, setenv);

		sprintf(var, "%s_nand_size", ptn->name);

		sprintf (val, "0x%x", size);

		FBTINFO("%s %s %s\n", setenv[0], setenv[1], setenv[2]);

		do_env_set (NULL, 0, 3, setenv);
	}


	/* Warning :
	   The environment is assumed to be in a partition named 'enviroment'.
	   It is very possible that your board stores the enviroment
	   someplace else. */
	env_ptn = fastboot_flash_find_ptn("environment");

	if (env_ptn)
	{
		/* Some flashing requires the nand's ecc to be set */
		if ((env_ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_HW_ECC) &&
		    (env_ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_SW_ECC))
		{
			/* Both can not be true */
			FBTWARN("can not do hw and sw ecc for partition '%s'\n", ptn->name);
			FBTWARN("Ignoring these flags\n");
		}
		else if (env_ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_HW_ECC)
		{
			set_ecc(1);
		}
		else if (env_ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_SW_ECC)
		{
			set_ecc(0);
		}

		sprintf (start, "0x%x", env_ptn->start);
		sprintf (length, "0x%x", env_ptn->length);

	}

	do_env_save (NULL, 0, 1, saveenv);
}

/* When save = 0, just parse.  The input is unchanged
   When save = 1, parse and do the save.  The input is changed */
static int parse_env(void *ptn, char *err_string, int save, int debug)
{
	int ret = 1;
	unsigned int sets = 0;
	unsigned int comment_start = 0;
	char *var = NULL;
	char *var_end = NULL;
	char *val = NULL;
	char *val_end = NULL;
	unsigned int i;

	char *buff = (char *)fastboot_cfg.transfer_buffer;
	unsigned int size = fastboot_cfg.download_bytes_unpadded;

	/* The input does not have to be null terminated.
	   This will cause a problem in the corner case
	   where the last line does not have a new line.
	   Put a null after the end of the input.

	   WARNING : Input buffer is assumed to be bigger
	   than the size of the input */
	if (save)
		buff[size] = 0;

	for (i = 0; i < size; i++) {

		if (NULL == var) {

			/*
			 * Check for comments, comment ok only on
			 * mostly empty lines
			 */
			if (buff[i] == '#')
				comment_start = 1;

			if (comment_start) {
				if  ((buff[i] == '\r') ||
				     (buff[i] == '\n')) {
					comment_start = 0;
				}
			} else {
				if (!((buff[i] == ' ') ||
				      (buff[i] == '\t') ||
				      (buff[i] == '\r') ||
				      (buff[i] == '\n'))) {
					/*
					 * Normal whitespace before the
					 * variable
					 */
					var = &buff[i];
				}
			}

		} else if (((NULL == var_end) || (NULL == val)) &&
			   ((buff[i] == '\r') || (buff[i] == '\n'))) {

			/* This is the case when a variable
			   is unset. */

			if (save) {
				/* Set the var end to null so the
				   normal string routines will work

				   WARNING : This changes the input */
				buff[i] = '\0';

				save_env(ptn, var, val);

				FBTDBG("Unsetting %s\n", var);
			}

			/* Clear the variable so state is parse is back
			   to initial. */
			var = NULL;
			var_end = NULL;
			sets++;
		} else if (NULL == var_end) {
			if ((buff[i] == ' ') ||
			    (buff[i] == '\t'))
				var_end = &buff[i];
		} else if (NULL == val) {
			if (!((buff[i] == ' ') ||
			      (buff[i] == '\t')))
				val = &buff[i];
		} else if (NULL == val_end) {
			if ((buff[i] == '\r') ||
			    (buff[i] == '\n')) {
				/* look for escaped cr or ln */
				if ('\\' == buff[i - 1]) {
					/* check for dos */
					if ((buff[i] == '\r') &&
					    (buff[i+1] == '\n'))
						buff[i + 1] = ' ';
					buff[i - 1] = buff[i] = ' ';
				} else {
					val_end = &buff[i];
				}
			}
		} else {
			sprintf(err_string, "Internal Error");

			FBTDBG("Internal error at %s %d\n",
				       __FILE__, __LINE__);
			return 1;
		}
		/* Check if a var / val pair is ready */
		if (NULL != val_end) {
			if (save) {
				/* Set the end's with nulls so
				   normal string routines will
				   work.

				   WARNING : This changes the input */
				*var_end = '\0';
				*val_end = '\0';

				save_env(ptn, var, val);

				FBTDBG("Setting %s %s\n", var, val);
			}

			/* Clear the variable so state is parse is back
			   to initial. */
			var = NULL;
			var_end = NULL;
			val = NULL;
			val_end = NULL;

			sets++;
		}
	}

	/* Corner case
	   Check for the case that no newline at end of the input */
	if ((NULL != var) &&
	    (NULL == val_end)) {
		if (save) {
			/* case of val / val pair */
			if (var_end)
				*var_end = '\0';
			/* else case handled by setting 0 past
			   the end of buffer.
			   Similar for val_end being null */
			save_env(ptn, var, val);

			if (var_end)
				FBTDBG("Trailing Setting %s %s\n", var, val);
			else
				FBTDBG("Trailing Unsetting %s\n", var);
		}
		sets++;
	}
	/* Did we set anything ? */
	if (0 == sets)
		sprintf(err_string, "No variables set");
	else
		ret = 0;

	return ret;
}

static int saveenv_to_ptn(struct fastboot_ptentry *ptn, char *err_string)
{
	int ret = 1;
	int save = 0;
	int debug = 0;

	/* err_string is only 32 bytes
	   Initialize with a generic error message. */
	sprintf(err_string, "%s", "Unknown Error");

	/* Parse the input twice.
	   Only save to the enviroment if the entire input if correct */
	save = 0;
	if (0 == parse_env(ptn, err_string, save, debug)) {
		save = 1;
		ret = parse_env(ptn, err_string, save, debug);
	}
	return ret;
}

static void set_ptn_ecc(struct fastboot_ptentry *ptn)
{
	if (((ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_HW_BCH8_ECC)  ||
	     (ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_HW_ECC)) &&
	     (ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_SW_ECC)) {
		/* Both can not be true */
		FBTERR("can not do hw and sw ecc for partition '%s'\n",
		       ptn->name);
		FBTERR("Ignoring these flags\n");
	} else if (ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_HW_BCH8_ECC) {
		set_ecc(1);
	} else if (ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_HW_ECC) {
		set_ecc(1);
	} else if (ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_SW_ECC) {
		set_ecc(0);
	}
}

static int write_to_ptn(struct fastboot_ptentry *ptn)
{
	int ret = 1;
	char start[32], length[32];
	char wstart[32], wlength[32], addr[32];
	char write_type[32];
	int repeat, repeat_max;

	char *write[6]  = { "nand", "write",  NULL, NULL, NULL, NULL, };
	char *erase[5]  = { "nand", "erase",  NULL, NULL, NULL, };

	erase[2] = start;
	erase[3] = length;

	write[1] = write_type;
	write[2] = addr;
	write[3] = wstart;
	write[4] = wlength;

	FBTINFO("flashing '%s'\n", ptn->name);

	/* Which flavor of write to use */
	if (ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_I)
		sprintf(write_type, "write.i");
	else if (ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_JFFS2)
		sprintf(write_type, "write.jffs2");
	else
		sprintf(write_type, "write");

	/* Some flashing requires writing the same data in multiple,
	   consecutive flash partitions */
	repeat_max = 1;
	if (FASTBOOT_PTENTRY_FLAGS_REPEAT_MASK(ptn->flags)) {
		if (ptn->flags &
		    FASTBOOT_PTENTRY_FLAGS_WRITE_CONTIGUOUS_BLOCK) {
			FBTWARN("can not do both 'contiguous block' and 'repeat' writes for for partition '%s'\n", ptn->name);
			FBTWARN("Ignoring repeat flag\n");
		} else {
			repeat_max = (FASTBOOT_PTENTRY_FLAGS_REPEAT_MASK(ptn->flags));
		}
	}

	sprintf(length, "0x%x", ptn->length);

	for (repeat = 0; repeat < repeat_max; repeat++) {

		set_ptn_ecc(ptn);
		sprintf(start, "0x%x", ptn->start + (repeat * ptn->length));

		do_nand(NULL, 0, 4, erase);

		if ((ptn->flags &
		     FASTBOOT_PTENTRY_FLAGS_WRITE_NEXT_GOOD_BLOCK) &&
		    (ptn->flags &
		     FASTBOOT_PTENTRY_FLAGS_WRITE_CONTIGUOUS_BLOCK)) {
			/* Both can not be true */
			FBTWARN("can not do 'next good block' and 'contiguous block' for partition '%s'\n", ptn->name);
			FBTWARN("Ignoring these flags\n");
		} else if (ptn->flags &
			   FASTBOOT_PTENTRY_FLAGS_WRITE_NEXT_GOOD_BLOCK) {
			/* Keep writing until you get a good block
			   transfer_buffer should already be aligned */
			if (fastboot_cfg.nand_block_size) {
				unsigned int blocks = fastboot_cfg.download_bytes /
					fastboot_cfg.nand_block_size;
				unsigned int i = 0;
				unsigned int offset = 0;

				sprintf(wlength, "0x%x",
					fastboot_cfg.nand_block_size);
				while (i < blocks) {
					/* Check for overflow */
					if (offset >= ptn->length)
						break;

					/* download's address only advance
					   if last write was successful */
					sprintf(addr, "0x%x",
						fastboot_cfg.transfer_buffer +
						(i * fastboot_cfg.nand_block_size));

					/* nand's address always advances */
					sprintf(wstart, "0x%x",
						ptn->start + (repeat * ptn->length) + offset);

					ret = do_nand(NULL, 0, 5, write);
					if (ret)
						break;
					else
						i++;

					/* Go to next nand block */
					offset += fastboot_cfg.nand_block_size;
				}
			} else {
				FBTWARN("nand block size can not be 0 when using 'next good block' for partition '%s'\n", ptn->name);
				FBTWARN("Ignoring write request\n");
			}
		} else if (ptn->flags &
			 FASTBOOT_PTENTRY_FLAGS_WRITE_CONTIGUOUS_BLOCK) {
			/* Keep writing until you get a good block
			   transfer_buffer should already be aligned */
			if (fastboot_cfg.nand_block_size) {
				if (0 == nand_curr_device) {
					nand_info_t *nand;
					unsigned long off;
					unsigned int ok_start;

					nand = &nand_info[nand_curr_device];

					FBTINFO("\nDevice %d bad blocks:\n",
					       nand_curr_device);

					/* Initialize the ok_start to the
					   start of the partition
					   Then try to find a block large
					   enough for the download */
					ok_start = ptn->start;

					/* It is assumed that the start and
					   length are multiples of block size */
					for (off = ptn->start;
					     off < ptn->start + ptn->length;
					     off += nand->erasesize) {
						if (nand_block_isbad(nand, off)) {
							/* Reset the ok_start
							   to the next block */
							ok_start = off +
								nand->erasesize;
						}

						/* Check if we have enough
						   blocks */
						if ((ok_start - off) >=
						    fastboot_cfg.download_bytes)
							break;
					}

					/* Check if there is enough space */
					if (ok_start + fastboot_cfg.download_bytes <=
					    ptn->start + ptn->length) {
						sprintf(addr,    "0x%x", fastboot_cfg.transfer_buffer);
						sprintf(wstart,  "0x%x", ok_start);
						sprintf(wlength, "0x%x", fastboot_cfg.download_bytes);

						ret = do_nand(NULL, 0, 5, write);

						/* Save the results into an
						   environment variable on the
						   format
						   ptn_name + 'offset'
						   ptn_name + 'size'  */
						if (ret) {
							/* failed */
							save_block_values(ptn, 0, 0);
						} else {
							/* success */
							save_block_values(ptn, ok_start, fastboot_cfg.download_bytes);
						}
					} else {
						FBTERR("could not find enough contiguous space in partition '%s' \n", ptn->name);
						FBTERR("Ignoring write request\n");
					}
				} else {
					/* TBD : Generalize flash handling */
					FBTERR("only handling 1 NAND per board");
					FBTERR("Ignoring write request\n");
				}
			} else {
				FBTWARN("nand block size can not be 0 when using 'continuous block' for partition '%s'\n", ptn->name);
				FBTWARN("Ignoring write request\n");
			}
		} else {
			/* Normal case */
			sprintf(addr,    "0x%x", fastboot_cfg.transfer_buffer);
			sprintf(wstart,  "0x%x", ptn->start +
				(repeat * ptn->length));
			sprintf(wlength, "0x%x", fastboot_cfg.download_bytes);
			if (ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_JFFS2)
				sprintf(wlength, "0x%x",
					fastboot_cfg.download_bytes_unpadded);

			ret = do_nand(NULL, 0, 5, write);

			if (0 == repeat) {
				if (ret) /* failed */
					save_block_values(ptn, 0, 0);
				else     /* success */
					save_block_values(ptn, ptn->start,
							  fastboot_cfg.download_bytes);
			}
		}


		if (ret)
			break;
	}

	return ret;
}

int handle_flash(char *part_name , char *response)
{
	int status = 0;


	fastboot_cfg.download_bytes_unpadded = fastboot_cfg.download_size;
	/* XXX: Revisit padding handling */
	if (fastboot_cfg.nand_block_size) {
		if (fastboot_cfg.download_bytes % fastboot_cfg.nand_block_size) {
			unsigned int pad = fastboot_cfg.nand_block_size - (fastboot_cfg.download_bytes % fastboot_cfg.nand_block_size);
			unsigned int i;

			for (i = 0; i < pad; i++) {
				if (fastboot_cfg.download_bytes >= fastboot_cfg.transfer_buffer_size)
					break;

				fastboot_cfg.transfer_buffer[fastboot_cfg.download_bytes] = 0;
				fastboot_cfg.download_bytes++;
			}
		}
	}


	if (fastboot_cfg.download_bytes) {
		struct fastboot_ptentry *ptn;

		ptn = fastboot_flash_find_ptn(part_name);
		if (ptn == 0) {
			sprintf(response, "FAILpartition does not exist");
		} else if ((fastboot_cfg.download_bytes > ptn->length) &&
				!(ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_ENV)) {
			sprintf(response, "FAILimage too large for partition");
		} else {
			/* Check if this is not really a flash write
			   but rather a saveenv */
			if (ptn->flags & FASTBOOT_PTENTRY_FLAGS_WRITE_ENV) {
				/* Since the response can only be 64 bytes,
				   there is no point in having a large error message. */
				char err_string[32];

				if (saveenv_to_ptn(ptn, &err_string[0])) {
					FBTINFO("savenv '%s' failed : %s\n", ptn->name, err_string);
					sprintf(response, "FAIL%s", err_string);
				} else {
					FBTINFO("partition '%s' saveenv-ed\n", ptn->name);
					sprintf(response, "OKAY");
				}
			} else {
				/* Normal case */
				if (write_to_ptn(ptn)) {
					FBTINFO("flashing '%s' failed\n", ptn->name);
					sprintf(response, "FAILfailed to flash partition");
				} else {
					FBTINFO("partition '%s' flashed\n", ptn->name);
					sprintf(response, "OKAY");
				}
			}
		}
	} else {
		sprintf(response, "FAILno image downloaded");
	}

	return status;
}

/*This function does nothing with NAND partitioning.
It only adds partition info to fastboot partition table.
*/
int do_format()
{
	int indx;

	fastboot_flash_reset_ptn();

	for (indx = 0; indx < MAX_PTN; indx++)
	{
		fastboot_flash_add_ptn(&nand_ptn[indx]);
	}
	return 0;
}

