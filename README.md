# U-Boot with fastboot for the BeagleBone Black
This is a fork of U-Boot which adds support for the Android fastboot
protocol. The changes are somewhat specific to the BeagleBone Black.

# Build from source

Clone this repository:

```
$ git clone https://github.com/csimmonds/u-boot.git
$ cd u-boot
$ git checkout am335x-v2013.01.01-bbb-fb
```
Since you are likely to be building this along with AOSP, I suggest using
the Android cross compiler from prebuilts, but probably any recent arm eabi
toolchain will do. If you have sourced build/envsetup.sh and selected the
lunch combo the path will be set up already. If not, set it now,
substituting ${AOSP} with the place where you installed your AOSP. I used
Android 4.4, which has gcc v4.7:

```
$ PATH=${AOSP}/prebuilts/gcc/linux-x86/arm/arm-eabi-4.7/bin:$PATH
```
Then configure and build U-Boot:

```
$ make CROSS_COMPILE=arm-eabi- distclean
$ make CROSS_COMPILE=arm-eabi- am335x_evm_config
$ make CROSS_COMPILE=arm-eabi- 
```
This produces the two files: MLO and u-boot.img

# Create a bootable micro SD card

Take a micro-SD card and connect it to your PC, either using a direct SD slot
if available, in which case the card will appear as "/dev/mmcblk0" or, using
a memory card reader in which case the card wil be seen as "/dev/sdb",
or "/dev/sdc", etc

Now type the command below to partition the micro-SD card, assuming that
the card is seen as "/dev/mmcblk0"

```
sudo sfdisk -D -H 255 -S 63 /dev/mmcblk0 << EOF
,9,0x0C,*
,,,-
EOF
```
Format the first partition as FAT32

```
sudo mkfs.vfat -F 32 -n boot /dev/mmcblk0p1
```
Remove the card and insert it again. It should automatically be mounted as
"/media/boot".

Now, copy the files to this partition:

```
cp MLO u-boot.img /media/boot
```
Finally, umount it.

# Flash U-Boot to the BeagleBone Black

You will need

- A BeagelBone Black rev A/B/C
- A micro SD card of any capacity since you are only going to use the first
  70 MiB to write a small flasher image
- The mini USB to USB A cable supplied with the BeagleBone
- A 5V power supply because the current used when writing to the eMMC chip may
  exceed that supplied by a typical USB port.

The procedure is:

1. With no power on the BeagleBone, insert the microSD card

2. Press and hold the 'Boot button' on the BeagleBone, power up the board
   using the external 5V power connector and release the button after the
   fastboot LED (USER 0 led) lights up, showing that it is running this
   version of U-Boot and is ready to accept fastboot commands

3. Plug in the USB cable between the mini USB port on the BeagleBone and the
   PC. Then, using the fastboot command from the Android SDK or an AOSP build,
   check that the BeagleBone has been detected by typing (on the PC)

```
$ fastboot devices
90:59:af:5e:94:81	fastboot
```

4. If instead you see

```
$ fastboot devices
no permissions	fastboot
```

Add this line to /etc/udev/rules.d/51-android.rules

```
SUBSYSTEM =="usb", ATTRS{idVendor}=="0451", ATTRS{idProduct}=="d022" , MODE="0666"

```
Then unplug the mini USB cable and plug it back in again. Check that the permissions problem has gone away.

5. Use fastboot to format the eMMC chip and then flash the images

```
$ fastboot oem format
$ fastboot flash spl MLO
$ fastboot flash bootloader u-boot.img
```

6. Power off the board and remove the SD card

7. Power on again. Your BeagleBone will boot into U-Boot and it will be
  once again ready to accept fastboot commands
