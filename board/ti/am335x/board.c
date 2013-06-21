/*
 * board.c
 *
 * Board functions for TI AM335X based boards
 *
 * Copyright (C) 2011, Texas Instruments, Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR /PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <common.h>
#include <errno.h>
#include <spl.h>
#include <asm/arch/cpu.h>
#include <asm/arch/hardware.h>
#include <asm/arch/omap.h>
#include <asm/arch/ddr_defs.h>
#include <asm/arch/clock.h>
#include <asm/arch/gpio.h>
#include <asm/arch/mmc_host_def.h>
#include <asm/arch/sys_proto.h>
#include <asm/io.h>
#include <asm/emif.h>
#include <asm/gpio.h>
#include <i2c.h>
#include <miiphy.h>
#include <cpsw.h>
#include "board.h"
#include "pmic.h"
#include "tps65217.h"

DECLARE_GLOBAL_DATA_PTR;

static struct wd_timer *wdtimer = (struct wd_timer *)WDT_BASE;
#if defined(CONFIG_SPL_BUILD) || (CONFIG_NOR_BOOT)
static struct uart_sys *uart_base = (struct uart_sys *)DEFAULT_UART_BASE;
#endif

/* MII mode defines */
#define MII_MODE_ENABLE		0x0
#define RGMII_MODE_ENABLE	0x3A

/* GPIO that controls power to DDR on EVM-SK */
#define GPIO_DDR_VTT_EN		7

static struct ctrl_dev *cdev = (struct ctrl_dev *)CTRL_DEVICE_BASE;

static struct am335x_baseboard_id __attribute__((section (".data"))) header;

static inline int board_is_bone(void)
{
	return !strncmp(header.name, "A335BONE", HDR_NAME_LEN);
}

static inline int board_is_bone_lt(void)
{
	return !strncmp(header.name, "A335BNLT", HDR_NAME_LEN);
}

static inline int board_is_evm_sk(void)
{
	return !strncmp("A335X_SK", header.name, HDR_NAME_LEN);
}

static inline int board_is_idk(void)
{
	return !strncmp(header.config, "SKU#02", 6);
}

static int __maybe_unused board_is_gp_evm(void)
{
	return !strncmp("A33515BB", header.name, 8);
}

int board_is_evm_15_or_later(void)
{
	return (!strncmp("A33515BB", header.name, 8) &&
		strncmp("1.5", header.version, 3) <= 0);
}

/*
 * Read header information from EEPROM into global structure.
 */
static int read_eeprom(void)
{
	/* Check if baseboard eeprom is available */
	if (i2c_probe(CONFIG_SYS_I2C_EEPROM_ADDR)) {
		puts("Could not probe the EEPROM; something fundamentally "
			"wrong on the I2C bus.\n");
		return -ENODEV;
	}

	/* read the eeprom using i2c */
	if (i2c_read(CONFIG_SYS_I2C_EEPROM_ADDR, 0, 2, (uchar *)&header,
							sizeof(header))) {
		puts("Could not read the EEPROM; something fundamentally"
			" wrong on the I2C bus.\n");
		return -EIO;
	}

	if (header.magic != 0xEE3355AA) {
		/*
		 * read the eeprom using i2c again,
		 * but use only a 1 byte address
		 */
		if (i2c_read(CONFIG_SYS_I2C_EEPROM_ADDR, 0, 1,
					(uchar *)&header, sizeof(header))) {
			puts("Could not read the EEPROM; something "
				"fundamentally wrong on the I2C bus.\n");
			return -EIO;
		}

		if (header.magic != 0xEE3355AA) {
			printf("Incorrect magic number (0x%x) in EEPROM\n",
					header.magic);
			return -EINVAL;
		}
	}

	return 0;
}

/* UART Defines */
#if defined(CONFIG_SPL_BUILD) || defined(CONFIG_NOR_BOOT)
/**
 * tps65217_reg_read() - Generic function that can read a TPS65217 register
 * @src_reg:          Source register address
 * @src_val:          Address of destination variable
 */

unsigned char tps65217_reg_read(uchar src_reg, uchar *src_val)
{
        if (i2c_read(TPS65217_CHIP_PM, src_reg, 1, src_val, 1))
                return 1;
        return 0;
}

/**
 *  tps65217_reg_write() - Generic function that can write a TPS65217 PMIC
 *                         register or bit field regardless of protection
 *                         level.
 *
 *  @prot_level:        Register password protection.
 *                      use PROT_LEVEL_NONE, PROT_LEVEL_1, or PROT_LEVEL_2
 *  @dest_reg:          Register address to write.
 *  @dest_val:          Value to write.
 *  @mask:              Bit mask (8 bits) to be applied.  Function will only
 *                      change bits that are set in the bit mask.
 *
 *  @return:            0 for success, 1 for failure.
 */
int tps65217_reg_write(uchar prot_level, uchar dest_reg,
        uchar dest_val, uchar mask)
{
        uchar read_val;
        uchar xor_reg;

        /* if we are affecting only a bit field, read dest_reg and apply the mask */
        if (mask != MASK_ALL_BITS) {
                if (i2c_read(TPS65217_CHIP_PM, dest_reg, 1, &read_val, 1))
                        return 1;
                read_val &= (~mask);
                read_val |= (dest_val & mask);
                dest_val = read_val;
        }

        if (prot_level > 0) {
                xor_reg = dest_reg ^ PASSWORD_UNLOCK;
                if (i2c_write(TPS65217_CHIP_PM, PASSWORD, 1, &xor_reg, 1))
                        return 1;
        }

        if (i2c_write(TPS65217_CHIP_PM, dest_reg, 1, &dest_val, 1))
                return 1;

        if (prot_level == PROT_LEVEL_2) {
                if (i2c_write(TPS65217_CHIP_PM, PASSWORD, 1, &xor_reg, 1))
                        return 1;

                if (i2c_write(TPS65217_CHIP_PM, dest_reg, 1, &dest_val, 1))
                        return 1;
        }

        return 0;
}

int tps65217_voltage_update(unsigned char dc_cntrl_reg, unsigned char volt_sel)
{
        if ((dc_cntrl_reg != DEFDCDC1) && (dc_cntrl_reg != DEFDCDC2)
                && (dc_cntrl_reg != DEFDCDC3))
                return 1;

        /* set voltage level */
        if (tps65217_reg_write(PROT_LEVEL_2, dc_cntrl_reg, volt_sel, MASK_ALL_BITS))
                return 1;

        /* set GO bit to initiate voltage transition */
        if (tps65217_reg_write(PROT_LEVEL_2, DEFSLEW, DCDC_GO, DCDC_GO))
                return 1;

        return 0;
}

/*
 * voltage switching for MPU frequency switching.
 * @module = mpu - 0, core - 1
 * @vddx_op_vol_sel = vdd voltage to set
 */

#define MPU     0
#define CORE    1

int voltage_update(unsigned int module, unsigned char vddx_op_vol_sel)
{
        uchar buf[4];
        unsigned int reg_offset;

        if(module == MPU)
                reg_offset = PMIC_VDD1_OP_REG;
        else
                reg_offset = PMIC_VDD2_OP_REG;

        /* Select VDDx OP   */
        if (i2c_read(PMIC_CTRL_I2C_ADDR, reg_offset, 1, buf, 1))
                return 1;

        buf[0] &= ~PMIC_OP_REG_CMD_MASK;

        if (i2c_write(PMIC_CTRL_I2C_ADDR, reg_offset, 1, buf, 1))
                return 1;

        /* Configure VDDx OP  Voltage */
        if (i2c_read(PMIC_CTRL_I2C_ADDR, reg_offset, 1, buf, 1))
                return 1;

        buf[0] &= ~PMIC_OP_REG_SEL_MASK;
        buf[0] |= vddx_op_vol_sel;

        if (i2c_write(PMIC_CTRL_I2C_ADDR, reg_offset, 1, buf, 1))
                return 1;

        if (i2c_read(PMIC_CTRL_I2C_ADDR, reg_offset, 1, buf, 1))
                return 1;

        if ((buf[0] & PMIC_OP_REG_SEL_MASK ) != vddx_op_vol_sel)
                return 1;

        return 0;
}

#define UART_RESET		(0x1 << 1)
#define UART_CLK_RUNNING_MASK	0x1
#define UART_SMART_IDLE_EN	(0x1 << 0x3)

static void rtc32k_enable(void)
{
	struct rtc_regs *rtc = (struct rtc_regs *)AM335X_RTC_BASE;

	/*
	 * Unlock the RTC's registers.  For more details please see the
	 * RTC_SS section of the TRM.  In order to unlock we need to
	 * write these specific values (keys) in this order.
	 */
	writel(0x83e70b13, &rtc->kick0r);
	writel(0x95a4f1e0, &rtc->kick1r);

	/* Enable the RTC 32K OSC by setting bits 3 and 6. */
	writel((1 << 3) | (1 << 6), &rtc->osc);
}

static const struct ddr_data ddr2_data = {
	.datardsratio0 = ((MT47H128M16RT25E_RD_DQS<<30) |
			  (MT47H128M16RT25E_RD_DQS<<20) |
			  (MT47H128M16RT25E_RD_DQS<<10) |
			  (MT47H128M16RT25E_RD_DQS<<0)),
	.datawdsratio0 = ((MT47H128M16RT25E_WR_DQS<<30) |
			  (MT47H128M16RT25E_WR_DQS<<20) |
			  (MT47H128M16RT25E_WR_DQS<<10) |
			  (MT47H128M16RT25E_WR_DQS<<0)),
	.datawiratio0 = ((MT47H128M16RT25E_PHY_WRLVL<<30) |
			 (MT47H128M16RT25E_PHY_WRLVL<<20) |
			 (MT47H128M16RT25E_PHY_WRLVL<<10) |
			 (MT47H128M16RT25E_PHY_WRLVL<<0)),
	.datagiratio0 = ((MT47H128M16RT25E_PHY_GATELVL<<30) |
			 (MT47H128M16RT25E_PHY_GATELVL<<20) |
			 (MT47H128M16RT25E_PHY_GATELVL<<10) |
			 (MT47H128M16RT25E_PHY_GATELVL<<0)),
	.datafwsratio0 = ((MT47H128M16RT25E_PHY_FIFO_WE<<30) |
			  (MT47H128M16RT25E_PHY_FIFO_WE<<20) |
			  (MT47H128M16RT25E_PHY_FIFO_WE<<10) |
			  (MT47H128M16RT25E_PHY_FIFO_WE<<0)),
	.datawrsratio0 = ((MT47H128M16RT25E_PHY_WR_DATA<<30) |
			  (MT47H128M16RT25E_PHY_WR_DATA<<20) |
			  (MT47H128M16RT25E_PHY_WR_DATA<<10) |
			  (MT47H128M16RT25E_PHY_WR_DATA<<0)),
	.datauserank0delay = MT47H128M16RT25E_PHY_RANK0_DELAY,
	.datadldiff0 = PHY_DLL_LOCK_DIFF,
};

static const struct cmd_control ddr2_cmd_ctrl_data = {
	.cmd0csratio = MT47H128M16RT25E_RATIO,
	.cmd0dldiff = MT47H128M16RT25E_DLL_LOCK_DIFF,
	.cmd0iclkout = MT47H128M16RT25E_INVERT_CLKOUT,

	.cmd1csratio = MT47H128M16RT25E_RATIO,
	.cmd1dldiff = MT47H128M16RT25E_DLL_LOCK_DIFF,
	.cmd1iclkout = MT47H128M16RT25E_INVERT_CLKOUT,

	.cmd2csratio = MT47H128M16RT25E_RATIO,
	.cmd2dldiff = MT47H128M16RT25E_DLL_LOCK_DIFF,
	.cmd2iclkout = MT47H128M16RT25E_INVERT_CLKOUT,
};

static const struct emif_regs ddr2_emif_reg_data = {
	.sdram_config = MT47H128M16RT25E_EMIF_SDCFG,
	.ref_ctrl = MT47H128M16RT25E_EMIF_SDREF,
	.sdram_tim1 = MT47H128M16RT25E_EMIF_TIM1,
	.sdram_tim2 = MT47H128M16RT25E_EMIF_TIM2,
	.sdram_tim3 = MT47H128M16RT25E_EMIF_TIM3,
	.emif_l3_config = REG_PR_OLD_COUNT_EN,
	.emif_ddr_phy_ctlr_1 = MT47H128M16RT25E_EMIF_READ_LATENCY,
};

static const struct ddr_data ddr3_data = {
	.datardsratio0 = MT41J128MJT125_RD_DQS,
	.datawdsratio0 = MT41J128MJT125_WR_DQS,
	.datafwsratio0 = MT41J128MJT125_PHY_FIFO_WE,
	.datawrsratio0 = MT41J128MJT125_PHY_WR_DATA,
	.datadldiff0 = PHY_DLL_LOCK_DIFF,
};

static const struct ddr_data ddr3_beagleblack_data = {
	.datardsratio0 = MT41K256M16HA125E_RD_DQS,
 	.datawdsratio0 = MT41K256M16HA125E_WR_DQS,
	.datafwsratio0 = MT41K256M16HA125E_PHY_FIFO_WE,
	.datawrsratio0 = MT41K256M16HA125E_PHY_WR_DATA,
	.datadldiff0 = PHY_DLL_LOCK_DIFF,
};

static const struct ddr_data ddr3_evm_data = {
	.datardsratio0 = MT41J512M8RH125_RD_DQS,
	.datawdsratio0 = MT41J512M8RH125_WR_DQS,
	.datafwsratio0 = MT41J512M8RH125_PHY_FIFO_WE,
	.datawrsratio0 = MT41J512M8RH125_PHY_WR_DATA,
	.datadldiff0 = PHY_DLL_LOCK_DIFF,
};

static const struct cmd_control ddr3_cmd_ctrl_data = {
	.cmd0csratio = MT41J128MJT125_RATIO,
	.cmd0dldiff = MT41J128MJT125_DLL_LOCK_DIFF,
	.cmd0iclkout = MT41J128MJT125_INVERT_CLKOUT,

	.cmd1csratio = MT41J128MJT125_RATIO,
	.cmd1dldiff = MT41J128MJT125_DLL_LOCK_DIFF,
	.cmd1iclkout = MT41J128MJT125_INVERT_CLKOUT,

	.cmd2csratio = MT41J128MJT125_RATIO,
	.cmd2dldiff = MT41J128MJT125_DLL_LOCK_DIFF,
	.cmd2iclkout = MT41J128MJT125_INVERT_CLKOUT,
};

static const struct cmd_control ddr3_beagleblack_cmd_ctrl_data = {
	.cmd0csratio = MT41K256M16HA125E_RATIO,
	.cmd0dldiff = MT41K256M16HA125E_DLL_LOCK_DIFF,
	.cmd0iclkout = MT41K256M16HA125E_INVERT_CLKOUT,

	.cmd1csratio = MT41K256M16HA125E_RATIO,
	.cmd1dldiff = MT41K256M16HA125E_DLL_LOCK_DIFF,
	.cmd1iclkout = MT41K256M16HA125E_INVERT_CLKOUT,

	.cmd2csratio = MT41K256M16HA125E_RATIO,
	.cmd2dldiff = MT41K256M16HA125E_DLL_LOCK_DIFF,
	.cmd2iclkout = MT41K256M16HA125E_INVERT_CLKOUT,
};

static const struct cmd_control ddr3_evm_cmd_ctrl_data = {
	.cmd0csratio = MT41J512M8RH125_RATIO,
	.cmd0dldiff = MT41J512M8RH125_DLL_LOCK_DIFF,
	.cmd0iclkout = MT41J512M8RH125_INVERT_CLKOUT,

	.cmd1csratio = MT41J512M8RH125_RATIO,
	.cmd1dldiff = MT41J512M8RH125_DLL_LOCK_DIFF,
	.cmd1iclkout = MT41J512M8RH125_INVERT_CLKOUT,

	.cmd2csratio = MT41J512M8RH125_RATIO,
	.cmd2dldiff = MT41J512M8RH125_DLL_LOCK_DIFF,
	.cmd2iclkout = MT41J512M8RH125_INVERT_CLKOUT,
};

static struct emif_regs ddr3_emif_reg_data = {
	.sdram_config = MT41J128MJT125_EMIF_SDCFG,
	.ref_ctrl = MT41J128MJT125_EMIF_SDREF,
	.sdram_tim1 = MT41J128MJT125_EMIF_TIM1,
	.sdram_tim2 = MT41J128MJT125_EMIF_TIM2,
	.sdram_tim3 = MT41J128MJT125_EMIF_TIM3,
	.emif_l3_config = REG_PR_OLD_COUNT_EN,
	.zq_config = MT41J128MJT125_ZQ_CFG,
	.emif_ddr_phy_ctlr_1 = MT41J128MJT125_EMIF_READ_LATENCY |
				PHY_EN_DYN_PWRDN,
};

static struct emif_regs ddr3_beagleblack_emif_reg_data = {
	.sdram_config = MT41K256M16HA125E_EMIF_SDCFG,
	.ref_ctrl = MT41K256M16HA125E_EMIF_SDREF,
	.sdram_tim1 = MT41K256M16HA125E_EMIF_TIM1,
	.sdram_tim2 = MT41K256M16HA125E_EMIF_TIM2,
	.sdram_tim3 = MT41K256M16HA125E_EMIF_TIM3,
	.zq_config = MT41K256M16HA125E_ZQ_CFG,
	.emif_ddr_phy_ctlr_1 = MT41K256M16HA125E_EMIF_READ_LATENCY |
				PHY_EN_DYN_PWRDN,
};

static struct emif_regs ddr3_evm_emif_reg_data = {
	.sdram_config = MT41J512M8RH125_EMIF_SDCFG,
	.ref_ctrl = MT41J512M8RH125_EMIF_SDREF,
	.sdram_tim1 = MT41J512M8RH125_EMIF_TIM1,
	.sdram_tim2 = MT41J512M8RH125_EMIF_TIM2,
	.sdram_tim3 = MT41J512M8RH125_EMIF_TIM3,
	.emif_l3_config = REG_PR_OLD_COUNT_EN,
	.zq_config = MT41J512M8RH125_ZQ_CFG,
	.emif_ddr_phy_ctlr_1 = MT41J512M8RH125_EMIF_READ_LATENCY |
				PHY_EN_DYN_PWRDN,
};

void am33xx_spl_board_init(void)
{
	int mpu_vdd, mpu_pll, sil_rev;

	/* Assume PG 1.0 */
	mpu_pll = MPUPLL_M_720;

	sil_rev = readl(&cdev->deviceid) >> 28;
	if (sil_rev == 1)
		/* PG 2.0, efuse may not be set. */
		mpu_pll = MPUPLL_M_800;
	else if (sil_rev >= 2) {
		/* Check what the efuse says our max speed is. */
		int efuse_arm_mpu_max_freq;
		efuse_arm_mpu_max_freq = readl(&cdev->efuse_sma);
		if ((efuse_arm_mpu_max_freq & DEVICE_ID_MASK) ==
				AM335X_ZCZ_1000)
			mpu_pll = MPUPLL_M_1000;
		else if ((efuse_arm_mpu_max_freq & DEVICE_ID_MASK) ==
				AM335X_ZCZ_800)
			mpu_pll = MPUPLL_M_800;
	}

	/*
	 * HACK: PG 2.0 should have max of 800MHz but Beaglebone Black
	 * can work at 1GHz.
	 */
	if (board_is_bone_lt())
		mpu_pll = MPUPLL_M_1000;

	if (board_is_bone() || board_is_bone_lt()) {
		/* BeagleBone PMIC Code */
		uchar pmic_status_reg;
		int usb_cur_lim;

		/* Only perform PMIC configurations if board rev > A1 */
		if (board_is_bone() && !strncmp(header.version, "00A1", 4))
			return;

		if (i2c_probe(TPS65217_CHIP_PM))
			return;

		if (tps65217_reg_read(STATUS, &pmic_status_reg))
			return;

		/*
		 * Increase USB current limit to 1300mA or 1800mA and set
		 * the MPU voltage controller as needed.
		 */
		if (mpu_pll == MPUPLL_M_1000) {
			usb_cur_lim = USB_INPUT_CUR_LIMIT_1800MA;
			mpu_vdd = DCDC_VOLT_SEL_1325MV;
		} else {
			usb_cur_lim = USB_INPUT_CUR_LIMIT_1300MA;
			mpu_vdd = DCDC_VOLT_SEL_1275MV;
		}

		if (tps65217_reg_write(PROT_LEVEL_NONE, POWER_PATH,
				       usb_cur_lim, USB_INPUT_CUR_LIMIT_MASK))
			printf("tps65217_reg_write failure\n");

		/* Set DCDC3 (CORE) voltage to 1.125V */
		if (tps65217_voltage_update(DEFDCDC3, DCDC_VOLT_SEL_1125MV)) {
			printf("tps65217_voltage_update failure\n");
			return;
		}

		/* Set CORE Frequency to what we detected */
		core_pll_config(OPP_100);

		/* Set DCDC2 (MPU) voltage to 1.275V */
		if (tps65217_voltage_update(DEFDCDC2, mpu_vdd)) {
			printf("tps65217_voltage_update failure\n");
			return;
		}

		/* Set MPU Frequency to what we detected */
		mpu_pll_config(mpu_pll);

		/*
		 * Set LDO3, LDO4 output voltage to 3.3V for Beaglebone.
		 * Set LDO3 to 1.8V and LDO4 to 3.3V for Beaglebone Black.
		 */
		if (board_is_bone()) {
			if (tps65217_reg_write(PROT_LEVEL_2, DEFLS1,
				       LDO_VOLTAGE_OUT_3_3, LDO_MASK))
				printf("tps65217_reg_write failure\n");
		} else {
			if (tps65217_reg_write(PROT_LEVEL_2, DEFLS1,
				       LDO_VOLTAGE_OUT_1_8, LDO_MASK))
				printf("tps65217_reg_write failure\n");
		}

		if (tps65217_reg_write(PROT_LEVEL_2, DEFLS2,
				       LDO_VOLTAGE_OUT_3_3, LDO_MASK))
			printf("tps65217_reg_write failure\n");

		/* Only Beaglebone needs the AC power, not Beaglebone Black */
		if (board_is_bone() &&
				 !(pmic_status_reg & PWR_SRC_AC_BITMASK)) {
			printf("No AC power, disabling frequency switch\n");
			return;
		}
	} else {
		uchar buf[4];

		/*
		 * The GP EVM, IDK and EVM SK use a TPS65910 PMIC.  For all
		 * MPU frequencies we support we use a CORE voltage of
		 * 1.1375V.  For 1GHz we need to use an MPU voltage of
		 * 1.3250V and for 720MHz or 800MHz we use 1.2625V.
		 */
		if (i2c_probe(PMIC_CTRL_I2C_ADDR))
			return;

		/* VDD1/2 voltage selection register access by control i/f */
		if (i2c_read(PMIC_CTRL_I2C_ADDR, PMIC_DEVCTRL_REG, 1, buf, 1))
			return;

		buf[0] |= PMIC_DEVCTRL_REG_SR_CTL_I2C_SEL_CTL_I2C;

		if (i2c_write(PMIC_CTRL_I2C_ADDR, PMIC_DEVCTRL_REG, 1, buf, 1))
			return;

		/*
		 * Unless we're running at 1GHz we use thesame VDD for
		 * all other frequencies we switch to (currently 720MHz,
		 * 800MHz or 1GHz).
		 */
		if (mpu_pll == MPUPLL_M_1000)
			mpu_vdd = PMIC_OP_REG_SEL_1_3_2_5;
		else
			mpu_vdd = PMIC_OP_REG_SEL_1_2_6;

		if (!voltage_update(CORE, PMIC_OP_REG_SEL_1_1_3))
			core_pll_config(OPP_100);
		if (!voltage_update(MPU, mpu_vdd))
			mpu_pll_config(mpu_pll);
	}
}
#endif

/*
 * early system init of muxing and clocks.
 */
void s_init(void)
{
	__maybe_unused struct am335x_baseboard_id header;
#ifdef CONFIG_NOR_BOOT
	asm("stmfd      sp!, {r2 - r4}");
	asm("movw       r4, #0x8A4");
	asm("movw       r3, #0x44E1");
	asm("orr        r4, r4, r3, lsl #16");
	asm("mov        r2, #9");
	asm("mov        r3, #8");
	asm("gpmc_mux:  str     r2, [r4], #4");
	asm("subs       r3, r3, #1");
	asm("bne        gpmc_mux");
	asm("ldmfd      sp!, {r2 - r4}");
#endif

	/* WDT1 is already running when the bootloader gets control
	 * Disable it to avoid "random" resets
	 */
	writel(0xAAAA, &wdtimer->wdtwspr);
	while (readl(&wdtimer->wdtwwps) != 0x0)
		;
	writel(0x5555, &wdtimer->wdtwspr);
	while (readl(&wdtimer->wdtwwps) != 0x0)
		;

#if defined(CONFIG_SPL_BUILD) || defined(CONFIG_NOR_BOOT)
	/* Setup the PLLs and the clocks for the peripherals */
	pll_init();

	/* Enable RTC32K clock */
	rtc32k_enable();

	/* UART softreset */
	u32 regVal;

#ifdef CONFIG_SERIAL1
	enable_uart0_pin_mux();
#endif /* CONFIG_SERIAL1 */
#ifdef CONFIG_SERIAL2
	enable_uart1_pin_mux();
#endif /* CONFIG_SERIAL2 */
#ifdef CONFIG_SERIAL3
	enable_uart2_pin_mux();
#endif /* CONFIG_SERIAL3 */
#ifdef CONFIG_SERIAL4
	enable_uart3_pin_mux();
#endif /* CONFIG_SERIAL4 */
#ifdef CONFIG_SERIAL5
	enable_uart4_pin_mux();
#endif /* CONFIG_SERIAL5 */
#ifdef CONFIG_SERIAL6
	enable_uart5_pin_mux();
#endif /* CONFIG_SERIAL6 */

	regVal = readl(&uart_base->uartsyscfg);
	regVal |= UART_RESET;
	writel(regVal, &uart_base->uartsyscfg);
	while ((readl(&uart_base->uartsyssts) &
		UART_CLK_RUNNING_MASK) != UART_CLK_RUNNING_MASK)
		;

	/* Disable smart idle */
	regVal = readl(&uart_base->uartsyscfg);
	regVal |= UART_SMART_IDLE_EN;
	writel(regVal, &uart_base->uartsyscfg);

#if defined(CONFIG_NOR_BOOT)
	gd = (gd_t *) ((CONFIG_SYS_INIT_SP_ADDR) & ~0x07);
	gd->baudrate = CONFIG_BAUDRATE;
	serial_init();
	gd->have_console = 1;
#else
	gd = &gdata;

	preloader_console_init();
#endif

	/* Initalize the board header */
	enable_i2c0_pin_mux();
	i2c_init(CONFIG_SYS_I2C_SPEED, CONFIG_SYS_I2C_SLAVE);
#ifndef CONFIG_NOR_BOOT
	if (read_eeprom() < 0)
		puts("Could not get board ID.\n");
#endif

	/* Check if baseboard eeprom is available */
	if (i2c_probe(CONFIG_SYS_I2C_EEPROM_ADDR)) {
		puts("Could not probe the EEPROM; something fundamentally "
			"wrong on the I2C bus.\n");
	}

	/* read the eeprom using i2c */
	if (i2c_read(CONFIG_SYS_I2C_EEPROM_ADDR, 0, 2, (uchar *)&header,
							sizeof(header))) {
		puts("Could not read the EEPROM; something fundamentally"
			" wrong on the I2C bus.\n");
	}

	if (header.magic != 0xEE3355AA) {
		/*
		 * read the eeprom using i2c again,
		 * but use only a 1 byte address
		 */
		if (i2c_read(CONFIG_SYS_I2C_EEPROM_ADDR, 0, 1,
					(uchar *)&header, sizeof(header))) {
			puts("Could not read the EEPROM; something "
				"fundamentally wrong on the I2C bus.\n");
			hang();
		}

		if (header.magic != 0xEE3355AA) {
			printf("Incorrect magic number (0x%x) in EEPROM\n",
					header.magic);
			hang();
		}
	}

	enable_board_pin_mux(&header);
	if (!strncmp("A335X_SK", header.name, HDR_NAME_LEN)) {
		/*
		 * EVM SK 1.2A and later use gpio0_7 to enable DDR3.
		 * This is safe enough to do on older revs.
		 */
		gpio_request(GPIO_DDR_VTT_EN, "ddr_vtt_en");
		gpio_direction_output(GPIO_DDR_VTT_EN, 1);
	}

#ifdef CONFIG_NOR_BOOT
	am33xx_spl_board_init();
#endif

	if (!strncmp("A335X_SK", header.name, HDR_NAME_LEN))
		config_ddr(303, MT41J128MJT125_IOCTRL_VALUE, &ddr3_data,
			   &ddr3_cmd_ctrl_data, &ddr3_emif_reg_data);
	else if  (!strncmp("A335BNLT", header.name, 8))
		config_ddr(400, MT41K256M16HA125E_IOCTRL_VALUE,
			   &ddr3_beagleblack_data,
			   &ddr3_beagleblack_cmd_ctrl_data,
			   &ddr3_beagleblack_emif_reg_data);
	else if (!strncmp("A33515BB", header.name, 8) &&
				strncmp("1.5", header.version, 3) <= 0)
		config_ddr(303, MT41J512M8RH125_IOCTRL_VALUE, &ddr3_evm_data,
			   &ddr3_evm_cmd_ctrl_data, &ddr3_evm_emif_reg_data);
	else
		config_ddr(266, MT47H128M16RT25E_IOCTRL_VALUE, &ddr2_data,
			   &ddr2_cmd_ctrl_data, &ddr2_emif_reg_data);
#endif
}

/*
 * Basic board specific setup.  Pinmux has been handled already.
 */
int board_init(void)
{
	i2c_init(CONFIG_SYS_I2C_SPEED, CONFIG_SYS_I2C_SLAVE);
	if (read_eeprom() < 0)
		puts("Could not get board ID.\n");

	gd->bd->bi_boot_params = PHYS_DRAM_1 + 0x100;

	gpmc_init();

	return 0;
}

#ifdef CONFIG_BOARD_LATE_INIT
int board_late_init(void)
{
#ifdef CONFIG_ENV_VARS_UBOOT_RUNTIME_CONFIG
	char safe_string[HDR_NAME_LEN + 1];

	/* Now set variables based on the header. */
	strncpy(safe_string, (char *)header.name, sizeof(header.name));
	safe_string[sizeof(header.name)] = 0;
	setenv("board_name", safe_string);

	strncpy(safe_string, (char *)header.version, sizeof(header.version));
	safe_string[sizeof(header.version)] = 0;
	setenv("board_rev", safe_string);
#endif

	return 0;
}
#endif

#if (defined(CONFIG_DRIVER_TI_CPSW) && !defined(CONFIG_SPL_BUILD)) || \
	(defined(CONFIG_SPL_ETH_SUPPORT) && defined(CONFIG_SPL_BUILD))
static void cpsw_control(int enabled)
{
	/* VTP can be added here */

	return;
}

static struct cpsw_slave_data cpsw_slaves[] = {
	{
		.slave_reg_ofs	= 0x208,
		.sliver_reg_ofs	= 0xd80,
		.phy_id		= 0,
	},
	{
		.slave_reg_ofs	= 0x308,
		.sliver_reg_ofs	= 0xdc0,
		.phy_id		= 1,
	},
};

static struct cpsw_platform_data cpsw_data = {
	.mdio_base		= AM335X_CPSW_MDIO_BASE,
	.cpsw_base		= AM335X_CPSW_BASE,
	.mdio_div		= 0xff,
	.channels		= 8,
	.cpdma_reg_ofs		= 0x800,
	.slaves			= 1,
	.slave_data		= cpsw_slaves,
	.ale_reg_ofs		= 0xd00,
	.ale_entries		= 1024,
	.host_port_reg_ofs	= 0x108,
	.hw_stats_reg_ofs	= 0x900,
	.mac_control		= (1 << 5),
	.control		= cpsw_control,
	.host_port_num		= 0,
	.version		= CPSW_CTRL_VERSION_2,
};
#endif

#if defined(CONFIG_DRIVER_TI_CPSW) || \
	(defined(CONFIG_USB_ETHER) && defined(CONFIG_MUSB_GADGET))
int board_eth_init(bd_t *bis)
{
	int rv, n = 0;
	uint8_t mac_addr[6];
	uint32_t mac_hi, mac_lo;

	/* try reading mac address from efuse */
	mac_lo = readl(&cdev->macid0l);
	mac_hi = readl(&cdev->macid0h);
	mac_addr[0] = mac_hi & 0xFF;
	mac_addr[1] = (mac_hi & 0xFF00) >> 8;
	mac_addr[2] = (mac_hi & 0xFF0000) >> 16;
	mac_addr[3] = (mac_hi & 0xFF000000) >> 24;
	mac_addr[4] = mac_lo & 0xFF;
	mac_addr[5] = (mac_lo & 0xFF00) >> 8;

#if (defined(CONFIG_DRIVER_TI_CPSW) && !defined(CONFIG_SPL_BUILD)) || \
	(defined(CONFIG_SPL_ETH_SUPPORT) && defined(CONFIG_SPL_BUILD))
	if (!getenv("ethaddr")) {
		printf("<ethaddr> not set. Validating first E-fuse MAC\n");

		if (is_valid_ether_addr(mac_addr))
			eth_setenv_enetaddr("ethaddr", mac_addr);
	}

	if (board_is_bone() || board_is_bone_lt() || board_is_idk()) {
		writel(MII_MODE_ENABLE, &cdev->miisel);
		cpsw_slaves[0].phy_if = cpsw_slaves[1].phy_if =
				PHY_INTERFACE_MODE_MII;
	} else {
		writel(RGMII_MODE_ENABLE, &cdev->miisel);
		cpsw_slaves[0].phy_if = cpsw_slaves[1].phy_if =
				PHY_INTERFACE_MODE_RGMII;
	}

	rv = cpsw_register(&cpsw_data);
	if (rv < 0)
		printf("Error %d registering CPSW switch\n", rv);
	else
		n += rv;

	/*
	 *
	 * CPSW RGMII Internal Delay Mode is not supported in all PVT
	 * operating points.  So we must set the TX clock delay feature
	 * in the AR8051 PHY.  Since we only support a single ethernet
	 * device in U-Boot, we only do this for the first instance.
	 */
#define AR8051_PHY_DEBUG_ADDR_REG	0x1d
#define AR8051_PHY_DEBUG_DATA_REG	0x1e
#define AR8051_DEBUG_RGMII_CLK_DLY_REG	0x5
#define AR8051_RGMII_TX_CLK_DLY		0x100

	if (board_is_evm_sk() || board_is_gp_evm()) {
		const char *devname;
		devname = miiphy_get_current_dev();

		miiphy_write(devname, 0x0, AR8051_PHY_DEBUG_ADDR_REG,
				AR8051_DEBUG_RGMII_CLK_DLY_REG);
		miiphy_write(devname, 0x0, AR8051_PHY_DEBUG_DATA_REG,
				AR8051_RGMII_TX_CLK_DLY);
	}
#endif
#if defined(CONFIG_USB_ETHER) && \
	(!defined(CONFIG_SPL_BUILD) || defined(CONFIG_SPL_USBETH_SUPPORT))
	if (is_valid_ether_addr(mac_addr))
		eth_setenv_enetaddr("usbnet_devaddr", mac_addr);

	rv = usb_eth_initialize(bis);
	if (rv < 0)
		printf("Error %d registering USB_ETHER\n", rv);
	else
		n += rv;
#endif
	return n;
}
#endif
