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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 * Part of the rx_handler were copied from the Android project.
 * Specifically rx command parsing in the  usb_rx_data_complete
 * function of the file bootable/bootloader/legacy/usbloader/usbloader.c
 *
 * The logical naming of flash comes from the Android project
 * Thse structures and functions that look like fastboot_flash_*
 * They come from bootable/bootloader/legacy/libboot/flash.c
 *
 * This is their Copyright:
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

/* The 64 defined bytes plus \0 */
#define RESPONSE_LEN	(64 + 1)

static struct fastboot_config *fb_cfg;

/* To support the Android-style naming of flash */
#define MAX_PTN 16
static fastboot_ptentry ptable[MAX_PTN];
static unsigned int pcount;
static int static_pcount = -1;

void set_fb_config (struct fastboot_config *cfg)
{
	fb_cfg = cfg;
}


/*
 * Android style flash utilties */
void fastboot_flash_reset_ptn(void)
{
       FBTINFO("fastboot flash reset partition..!!");
       pcount = 0;
}

void fastboot_flash_add_ptn(fastboot_ptentry *ptn)
{
       if(pcount < MAX_PTN){
               memcpy(ptable + pcount, ptn, sizeof(*ptn));
               pcount++;
       }
}

void fastboot_flash_dump_ptn(void)
{
       unsigned int n;
       for(n = 0; n < pcount; n++) {
               fastboot_ptentry *ptn = ptable + n;
               FBTINFO("ptn %d name='%s' start=%d len=%d\n",
                               n, ptn->name, ptn->start, ptn->length);
       }
}

fastboot_ptentry *fastboot_flash_find_ptn(const char *name)
{
       unsigned int n;

       for(n = 0; n < pcount; n++) {
               /* Make sure a substring is not accepted */
               if (strlen(name) == strlen(ptable[n].name))
               {
                       if(0 == strcmp(ptable[n].name, name))
                               return ptable + n;
               }
       }
       return 0;
}

static int fastboot_tx_write_str(const char *buffer)
{
	return fastboot_tx_write(buffer, strlen(buffer));
}

static void compl_do_reset(struct usb_ep *ep, struct usb_request *req)
{
	do_reset(NULL, 0, 0, NULL);
}

static void cb_reboot(struct usb_ep *ep, struct usb_request *req)
{
	req_in->complete = compl_do_reset;
	fastboot_tx_write_str("OKAY");
}

static int strcmp_l1(const char *s1, const char *s2)
{
	return strncmp(s1, s2, strlen(s1));
}

static void cb_getvar(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;
	char response[RESPONSE_LEN];
	const char *s;

	strcpy(response, "OKAY");
	strsep(&cmd, ":");
	if (!cmd) {
		fastboot_tx_write_str("FAILmissing var");
		return;
	}

	if (!strcmp_l1("version", cmd)) {
		strncat(response, FASTBOOT_VERSION, sizeof(response));

	} else if (!strcmp_l1("downloadsize", cmd)) {
		char str_num[12];

		sprintf(str_num, "%08x", fb_cfg->transfer_buffer_size);
		strncat(response, str_num, sizeof(response));

	} else if (!strcmp_l1("product", cmd)) {

		s = fb_find_usb_string(FB_STR_PRODUCT_IDX);
		if (s)
			strncat(response, s, sizeof(response));
		else
			strcpy(response, "FAILValue not set");

	} else if (!strcmp_l1("serialno", cmd)) {

		s = fb_find_usb_string(FB_STR_SERIAL_IDX);
		if (s)
			strncat(response, s, sizeof(response));
		else
			strcpy(response, "FAILValue not set");

	} else if (!strcmp_l1("cpurev", cmd)) {

		s = fb_find_usb_string(FB_STR_PROC_REV_IDX);
		if (s)
			strncat(response, s, sizeof(response));
		else
			strcpy(response, "FAILValue not set");
	} else if (!strcmp_l1("secure", cmd)) {

		s = fb_find_usb_string(FB_STR_PROC_TYPE_IDX);
		if (s)
			strncat(response, s, sizeof(response));
		else
			strcpy(response, "FAILValue not set");
	} else {
		strcpy(response, "FAILVariable not implemented");
	}
	fastboot_tx_write_str(response);
}

static unsigned int rx_bytes_expected(void)
{
	int rx_remain = fb_cfg->download_size - fb_cfg->download_bytes;
	if (rx_remain < 0)
		return 0;
	if (rx_remain > EP_BUFFER_SIZE)
		return EP_BUFFER_SIZE;
	return rx_remain;
}

#define BYTES_PER_DOT	32768
static void rx_handler_dl_image(struct usb_ep *ep, struct usb_request *req)
{
	char response[RESPONSE_LEN];
	unsigned int transfer_size = fb_cfg->download_size - fb_cfg->download_bytes;
	const unsigned char *buffer = req->buf;
	unsigned int buffer_size = req->actual;
	int dnl_complete = 0;

	if (req->status != 0) {
		printf("Bad status: %d\n", req->status);
		return;
	}

	if (buffer_size < transfer_size)
		transfer_size = buffer_size;

	memcpy(fb_cfg->transfer_buffer + fb_cfg->download_bytes,
			buffer, transfer_size);

	fb_cfg->download_bytes += transfer_size;

	/* Check if transfer is done */
	if (fb_cfg->download_bytes >= fb_cfg->download_size) {
		/*
		 * Reset global transfer variable, keep fb_cfg->download_bytes because
		 * it will be used in the next possible flashing command
		 */
		fb_cfg->download_size = 0;
		req->complete = rx_handler_command;
		req->length = EP_BUFFER_SIZE;
		dnl_complete = 1;
		printf("\ndownloading of %d bytes finished\n",
				fb_cfg->download_bytes);
	} else
		req->length = rx_bytes_expected();

	if (fb_cfg->download_bytes && !(fb_cfg->download_bytes % BYTES_PER_DOT)) {
		printf(".");
		if (!(fb_cfg->download_bytes % (74 * BYTES_PER_DOT)))
				printf("\n");

	}
	if (dnl_complete)
	{
		fastboot_tx_write_str("OKAY");
	}
	req->actual = 0;
	usb_ep_queue(ep, req, 0);
}

static void cb_download(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;
	char response[RESPONSE_LEN];

	strsep(&cmd, ":");
	fb_cfg->download_size = simple_strtoul(cmd, NULL, 16);
	fb_cfg->download_bytes = 0;

	printf("Starting download of %d bytes\n",
			fb_cfg->download_size);

	if (0 == fb_cfg->download_size) {
		sprintf(response, "FAILdata invalid size");
	} else if (fb_cfg->download_size >
			fb_cfg->transfer_buffer_size) {
		fb_cfg->download_size = 0;
		sprintf(response, "FAILdata too large");
	} else {
		sprintf(response, "DATA%08x", fb_cfg->download_size);
		req->complete = rx_handler_dl_image;
		req->length = rx_bytes_expected();
	}
	fastboot_tx_write_str(response);
}

static char boot_addr_start[32];
static char *bootm_args[] = { "bootm", boot_addr_start, NULL };

static void do_bootm_on_complete(struct usb_ep *ep, struct usb_request *req)
{
	req->complete = NULL;
	fastboot_shutdown();
	printf("Booting kernel..\n");

	do_bootm(NULL, 0, 2, bootm_args);

	/* This only happens if image is somehow faulty so we start over */
	do_reset(NULL, 0, 0, NULL);
}

static void cb_boot(struct usb_ep *ep, struct usb_request *req)
{
	sprintf(boot_addr_start, "0x%p", fb_cfg->transfer_buffer);

	req_in->complete = do_bootm_on_complete;
	fastboot_tx_write_str("OKAY");
	return;
}


int fastboot_oem(const char *cmd)
{
	printf("fastboot_oem:%s", cmd);
	if (!strcmp(cmd, "format"))
		return do_format();
	return -1;
}


static void cb_oem(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;

	printf ("calling fastboot oem!! : %s\n", cmd);
	int r = fastboot_oem(cmd + 4);
	if (r < 0) {
		fastboot_tx_write_str("FAIL");
	} else {
		fastboot_tx_write_str("OKAY");
	}
}

static void cb_flash(struct usb_ep *ep, struct usb_request *req)
{
	char *cmdbuf = req->buf;
	char response[32];
	char part_name[20]={0,};
	strncpy (part_name, cmdbuf + 6, req->actual - 6);
	handle_flash(part_name, response);
	fastboot_tx_write_str(response);
}
struct cmd_dispatch_info {
	char *cmd;
	void (*cb)(struct usb_ep *ep, struct usb_request *req);
};

static struct cmd_dispatch_info cmd_dispatch_info[] = {
	{
		.cmd = "reboot",
		.cb = cb_reboot,
	}, {
		.cmd = "getvar:",
		.cb = cb_getvar,
	}, {
		.cmd = "download:",
		.cb = cb_download,
	}, {
		.cmd = "boot",
		.cb = cb_boot,
	}, {
		.cmd = "oem",
		.cb = cb_oem,
	},{
		.cmd = "flash:",
		.cb = cb_flash,
	},
};

void rx_handler_command(struct usb_ep *ep, struct usb_request *req)
{
	char response[RESPONSE_LEN];
	char *cmdbuf = req->buf;
	void (*func_cb)(struct usb_ep *ep, struct usb_request *req) = NULL;
	int i;
	sprintf(response, "FAIL");

	*(cmdbuf + req->actual) = '\0';
	FBTINFO ("Recieved command : %s : req len : %d \n", cmdbuf, req->actual);

	for (i = 0; i < ARRAY_SIZE(cmd_dispatch_info); i++) {
		if (!strcmp_l1(cmd_dispatch_info[i].cmd, cmdbuf)) {
			func_cb = cmd_dispatch_info[i].cb;
			break;
		}
	}

	if (!func_cb)
		fastboot_tx_write_str("FAILunknown command");
	else
		func_cb(ep, req);

	if (req->status == 0) {
		*cmdbuf = '\0';
		req->actual = 0;
		usb_ep_queue(ep, req, 0);
	}
}
