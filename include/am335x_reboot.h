/*
 * (C) Copyright 2014
 * Chris Simmonds, 2net Ltd. chris@2net.co.uk
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
 */

/* Here we define an address in SRAM that can contain a flag to indicate
   the reason for a reboot.

   I chose an address 16 bytes from the end of SRAM in the hope that
   nobody will clobber it between Linux reboot and U-Boot starting */
#define REBOOT_REASON_PA 0x4030fff0

#define REBOOT_FLAG_RECOVERY    0x52564352
#define REBOOT_FLAG_FASTBOOT    0x54534146
#define REBOOT_FLAG_NORMAL      0x4D524F4E
#define REBOOT_FLAG_POWER_OFF   0x46464F50

