#include <common.h>
#include <command.h>
#include <usb/fastboot.h>
#include <asm/sizes.h>

static char serial_number[28] = "001234";

static struct usb_string def_usb_fb_strings[] = {
	{ FB_STR_PRODUCT_IDX,		"AM335xevm" },
	{ FB_STR_SERIAL_IDX,		serial_number },
	{ FB_STR_PROC_REV_IDX,		"ES2.x" },
	{ FB_STR_PROC_TYPE_IDX,		"ARMv7" },
	{ FB_STR_MANUFACTURER_IDX,	"Texas Instruments" },
	{  }
};

static struct usb_gadget_strings def_fb_strings = {
	.language	= 0x0409, /* en-us */
	.strings	= def_usb_fb_strings,
};

/*
 * Hardcoded memory region to stash data which comes over USB before it is
 * stored on media
 */
DECLARE_GLOBAL_DATA_PTR;
#define CFG_FASTBOOT_TRANSFER_BUFFER (void *)(gd->bd->bi_dram[0].start + SZ_16M)

static void set_serial_number(void)
{
	/* use ethaddr for fastboot serial no. */
	char *ethaddr = getenv("ethaddr");

	if (ethaddr != NULL) {
		int len;

		memset(&serial_number[0], 0, 28);
		len = strlen(ethaddr);
		if (len > 28)
			len = 26;

		strncpy(&serial_number[0], ethaddr, len);
	}
}

int fastboot_board_init(struct fastboot_config *interface,
			struct usb_gadget_strings **str)
{
	/* Initialize board serial no.  */
	set_serial_number();

	interface->transfer_buffer = CFG_FASTBOOT_TRANSFER_BUFFER;
	interface->transfer_buffer_size = CONFIG_FASTBOOT_MAX_TRANSFER_SIZE;

	*str = &def_fb_strings;
	return 0;
}

static int do_fastboot(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
	int ret = 1;

	if (!fastboot_init()) {
		printf("Fastboot entered...\n");

		ret = 0;

		while (1) {
			if (fastboot_poll())
			{
				printf("Fastboot poll break...\n");
				break;
			}
		}
	}

	fastboot_shutdown();
	return ret;
}

U_BOOT_CMD(
	fastboot,	1,	1,	do_fastboot,
	"fastboot- use USB Fastboot protocol\n",
	""
);
