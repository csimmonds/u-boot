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
 */

#include <common.h>
#include <command.h>
#include <nand.h>
#include <asm/arch/sys_proto.h>

int do_switch_ecc(cmd_tbl_t * cmdtp, int flag, int argc, char * const argv[])
{
        int type = 0;
        if (argc < 2)
                goto usage;

        if (strncmp(argv[1], "hw", 2) == 0) {
                        type = simple_strtoul(argv[2], NULL, 10);
			omap_nand_switch_ecc(type);
        }
        else
                goto usage;

        return 0;

usage:
        printf("Usage: nandecc %s\n", cmdtp->usage);
        return 1;
}

U_BOOT_CMD(
        nandecc, 3, 1,  do_switch_ecc,
	"Switch between NAND hardware (hw) or software (sw) ecc algorithm",
	"nandecc hw <hw_type>"
	"   hw_type- 0 for sofware\n"
	"            1 for Hamming code\n"
	"            2 for bch8\n"
);


