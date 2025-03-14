// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

/*
 * NOTE:
 * The modification is appended to initialization of image sensor.
 * After sensor initialization, use the function
 * bool otp_update_wb(unsigned char golden_rg, unsigned char golden_bg)
 * and
 * bool otp_update_lenc(void)
 * and then the calibration of AWB & LSC & BLC will be applied.
 * After finishing the OTP written, we will provide you the typical
 * value of golden sample.
 */

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/slab.h>

//#ifndef VENDOR_EDIT
//#include "kd_camera_hw.h"
/*Caohua.Lin@Camera.Drv, 20180126 remove to adapt with mt6771*/
//#endif
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"
#include "kd_camera_typedef.h"

#include "s5k4h7_sunny_mipi_raw_Sensor.h"
#include "s5k4h7_sunny_otp.h"


/******************Modify Following Strings for Debug*******************/
#define PFX "S5K4H7_SUNNYOTP"
#define LOG_1 SENSORDB("S5K4H7_SUNNY,MIPI CAM\n")
#define LOG_INF(format, args...) \
	pr_debug(PFX "[%s] " format, __func__, ##args)
/*********************   Modify end    *********************************/

#define USHORT        unsigned short
#define BYTE          unsigned char
#define I2C_ID        0x5a
#define S5K4H7_SUNNY_MIFAWB_GROUP_CNT 3
//#define S5K4H7_SUNNY_LSC_GROUP_CNT 3

unsigned char sunny_ucMIFAWB_data[64] = {0x00};
static kal_uint16 read_cmos_sensor_8(kal_uint16 addr)
{
	kal_uint16 get_byte = 0;
	char pusendcmd[2] = { (char)(addr >> 8), (char)(addr & 0xFF) };

	iReadRegI2C(pusendcmd, 2, (u8 *) &get_byte, 1, I2C_ID);
	return get_byte;
}



static void write_cmos_sensor_8(kal_uint16 addr, kal_uint8 para)
{
	char pusendcmd[4] = {
		(char)(addr >> 8),
		(char)(addr & 0xFF),
		(char)(para & 0xFF) };

	iWriteRegI2C(pusendcmd, 3, I2C_ID);
}

bool Sunny_Write_success(void)
{
	unsigned int mask = 1;
	unsigned int retry = 10;

	do
	{
		mDELAY(1);
		if(read_cmos_sensor_8(0x0A01)&(mask<<0))
		{
			LOG_INF("Current page ready to read otp data");
			return 1;
		}
		retry--;
	} while (retry > 0);
	return ERROR_NONE;
}
EXPORT_SYMBOL(Sunny_Write_success);
int sunny_power(int base, int exponent)
{
    int result = 1;
	int i = 0;
    for(i=0; i < exponent; i++) {
        result *= base;
    }
    return result;
}

int Sunny_GetCRC(const BYTE *data, const int len)
{
	int crc[16] = { 0 };
	int do_invert = 0, i = 0, j = 0;
	int mycrc16x = 0;
	for (i = 0; i < len; i++)
	{
		const int hexonly = data[i];
		for (j = 7; j >= 0; j--)
		{
			do_invert = hexonly &(int) (sunny_power(2, j));
			do_invert = do_invert / sunny_power(2, j);
			do_invert = do_invert ^ crc[15];

			crc[15] = crc[14] ^ do_invert;
			crc[14] = crc[13];
			crc[13] = crc[12];
			crc[12] = crc[11];
			crc[11] = crc[10];
			crc[10] = crc[9];
			crc[9] = crc[8];
			crc[8] = crc[7];
			crc[7] = crc[6];
			crc[6] = crc[5];
			crc[5] = crc[4];
			crc[4] = crc[3];
			crc[3] = crc[2];
			crc[2] = crc[1] ^ do_invert;
			crc[1] = crc[0];
			crc[0] = do_invert;
		}
	}

	for (i = 0; i < 16; i++)
	{
		mycrc16x = mycrc16x + (crc[i] * sunny_power(2, i));
	}
	return mycrc16x;
}

/*************************************************************************
 * Function    :  sunny_wb_gain_set
 * Description :  Set WB ratio to register gain setting  512x
 * Parameters  :  [int] r_ratio : R ratio data compared with golden module R
 *                b_ratio : B ratio data compared with golden module B
 * Return      :  [bool] 0 : set wb fail 1 : WB set success
 *************************************************************************/
bool sunny_wb_gain_set(kal_uint32 r_ratio, kal_uint32 b_ratio)
{

	kal_uint32 R_GAIN = 0;
	kal_uint32 B_GAIN = 0;
	kal_uint32 Gr_GAIN = 0;
	kal_uint32 Gb_GAIN = 0;
	kal_uint32 G_GAIN = 0;
	kal_uint32 GAIN_DEFAULT = 0x0100;

	if (!r_ratio || !b_ratio) {
		LOG_INF(" OTP WB ratio Data Err!");
		return 0;
	}
	if (r_ratio >= 1023) {
		if (b_ratio >= 1023) {
			R_GAIN = (USHORT)(GAIN_DEFAULT * r_ratio / 1023);
			G_GAIN = GAIN_DEFAULT;
			B_GAIN = (USHORT)(GAIN_DEFAULT * b_ratio / 1023);
		}
		else {
			R_GAIN = (USHORT)(GAIN_DEFAULT * r_ratio / b_ratio);
			G_GAIN = (USHORT)(GAIN_DEFAULT * 1023 / b_ratio);
			B_GAIN = GAIN_DEFAULT;
		}
	}
	else {
		if (b_ratio >= 1023) {
			R_GAIN = GAIN_DEFAULT;
			G_GAIN = (USHORT)(GAIN_DEFAULT * 1023 / r_ratio);
			B_GAIN = (USHORT)(GAIN_DEFAULT * b_ratio / r_ratio);
		}
		else {
			Gr_GAIN = (USHORT)(GAIN_DEFAULT * 1023 / r_ratio);
			Gb_GAIN = (USHORT)(GAIN_DEFAULT * 1023 / b_ratio);
			if (Gr_GAIN >= Gb_GAIN) {
				R_GAIN = GAIN_DEFAULT;
				G_GAIN = (USHORT)
					(GAIN_DEFAULT * 1023 / r_ratio);
				B_GAIN = (USHORT)
					(GAIN_DEFAULT * b_ratio / r_ratio);
			}
			else {
				R_GAIN = (USHORT)
					(GAIN_DEFAULT * r_ratio / b_ratio);
				G_GAIN = (USHORT)
					(GAIN_DEFAULT * 1023 / b_ratio);
				B_GAIN = GAIN_DEFAULT;
			}
		}
	}

	write_cmos_sensor_8(0x3C0F, 0x00);
	if (R_GAIN > GAIN_DEFAULT) {
		write_cmos_sensor_8(0x0210, (R_GAIN >> 8) & 0x0F);
		write_cmos_sensor_8(0x0211, R_GAIN & 0xFF);
	}
	if (B_GAIN > GAIN_DEFAULT) {
		write_cmos_sensor_8(0x0212, (B_GAIN >> 8) & 0x0F);
		write_cmos_sensor_8(0x0213, B_GAIN & 0xFF);
	}
	if (G_GAIN > GAIN_DEFAULT) {
		write_cmos_sensor_8(0x020E, (G_GAIN >> 8) & 0x0F);
		write_cmos_sensor_8(0x020F, G_GAIN & 0xFF);
		write_cmos_sensor_8(0x0214, (G_GAIN >> 8) & 0x0F);
		write_cmos_sensor_8(0x0215, G_GAIN & 0xFF);
	}
	return 1;
}

/*********************************************************
 *s5k4h7_sunny_apply_otp_lsc
 * ******************************************************/
bool s5k4h7_sunny_apply_otp_lsc(void)
{
    LOG_INF("OTP enable lsc\n");
    write_cmos_sensor_8(0x3400, 0x00);
    write_cmos_sensor_8(0x0B00, 0x01);
	return 1;
}

bool s5k4h7_sunny_update_awb(unsigned char page)
{
	kal_uint32 r_ratio;
	kal_uint32 b_ratio;


	kal_uint32 golden_rg = 0;
	kal_uint32 golden_bg = 0;

	kal_uint32 unit_rg = 0;
	kal_uint32 unit_bg = 0;

	page+=1;
	write_cmos_sensor_8(0x0A02, page);
	write_cmos_sensor_8(0x0A00, 0x01);

	if (Sunny_Write_success() ==1){
		golden_rg = read_cmos_sensor_8(0x0A06)| read_cmos_sensor_8(0x0A05) << 8;
		golden_bg = read_cmos_sensor_8(0x0A08)| read_cmos_sensor_8(0x0A07) << 8;

		unit_rg = read_cmos_sensor_8(0x0A22) | read_cmos_sensor_8(0x0A21) << 8;
		unit_bg = read_cmos_sensor_8(0x0A24) | read_cmos_sensor_8(0x0A23) << 8;
	}
	LOG_INF(
		"updata wb golden_rg=0x%02x golden_bg=0x%02x unit_rg=0x%02x unit_bg =0x%02x\n",
		golden_rg, golden_bg, unit_rg, unit_bg);

	if (!golden_rg || !golden_bg || !unit_rg || !unit_bg) {
		LOG_INF("updata wb err");
		return 0;
	}
	r_ratio = 1023 * (golden_rg) / (unit_rg);
	b_ratio = 1023 * (golden_bg) / (unit_bg);
	sunny_wb_gain_set(r_ratio, b_ratio);
	return 1;
}

void s5k4h7_sunny_get_module_info(unsigned char* sunny_ucMIFAWB_data)
{
	int i = 0;
	LOG_INF("Part Number:");
	for(i= 4; i < 12; i++)
	{
		LOG_INF("%02x",sunny_ucMIFAWB_data[i]);
	}

	LOG_INF("VCM ID = 0x%02x",sunny_ucMIFAWB_data[12]);
	LOG_INF("Lens ID = 0x%02x",sunny_ucMIFAWB_data[13]);
	LOG_INF("Manufacturer ID[0] = 0x%02x",sunny_ucMIFAWB_data[14]);
	LOG_INF("Manufacturer ID[1] = 0x%02x",sunny_ucMIFAWB_data[15]);
	LOG_INF("Year = %d",sunny_ucMIFAWB_data[19]+2000);
	LOG_INF("Month = %d",sunny_ucMIFAWB_data[20]);
	LOG_INF("day = %d",sunny_ucMIFAWB_data[21]);

	// LOG_INF("Mirror/Flip Status = 0x%02x",sunny_ucMIFAWB_data[11]);
	// LOG_INF("IR filter ID = 0x%02x",sunny_ucMIFAWB_data[12]);
}

bool s5k4h7_sunny_mifawb_datacheck(unsigned char page)
{
	kal_uint8 sunny_ucMIFAWB_data[60] = {0x00};
	kal_uint32 uiChecksum_cal = 0;
	unsigned int uiChecksum_read = 0;

	int i = 0;
	kal_uint32 gloden_R=0,gloden_Gr=0,gloden_Gb=0,gloden_B=0;

	write_cmos_sensor_8(0x0A02, page);
	write_cmos_sensor_8(0x0A00, 0x01);

	if (Sunny_Write_success() ==1)
	{
		for( i=0; i < 8; i++)
		{
			sunny_ucMIFAWB_data[i] = read_cmos_sensor_8(0x0A3C + i);
			LOG_INF("sunny_ucMIFAWB_data[%d] = 0x%02x",i,read_cmos_sensor_8(0x0A3C + i));
		}
		page+=1;
	}

	write_cmos_sensor_8(0x0A02, page);
	write_cmos_sensor_8(0x0A00, 0x01);

	if (Sunny_Write_success() ==1)
	{
		for( i=0; i < 51; i++)
		{
			sunny_ucMIFAWB_data[8+i] = read_cmos_sensor_8(0x0A04 + i);
			LOG_INF("sunny_ucMIFAWB_data[%d] = 0x%02x",8+i,read_cmos_sensor_8(0x0A04 + i));
			uiChecksum_cal = Sunny_GetCRC(sunny_ucMIFAWB_data, 57);
			// LOG_INF("MIF&AWB calculate checksum = 0x%04x\n",uiChecksum_cal);
		}
	}
	gloden_R=(sunny_ucMIFAWB_data[2])|(sunny_ucMIFAWB_data[1]<<8);
	gloden_Gr=(sunny_ucMIFAWB_data[4])|(sunny_ucMIFAWB_data[3]<<8);
	gloden_Gb=(sunny_ucMIFAWB_data[6])|(sunny_ucMIFAWB_data[5]<<8);
	gloden_B=(sunny_ucMIFAWB_data[8])|(sunny_ucMIFAWB_data[7]<<8);

	LOG_INF("gloden_R = 0x%04x,gloden_Gr= 0x%04x,gloden_Gb= 0x%04x,gloden_B= 0x%04x\n",gloden_R,gloden_Gr,gloden_Gb,gloden_B);

	uiChecksum_read = (sunny_ucMIFAWB_data[58])|(sunny_ucMIFAWB_data[57]<<8);;

	LOG_INF("MIF&AWB calculate checksum = 0x%04x\n",uiChecksum_cal);
	LOG_INF("MIF&AWB read checksum = 0x%04x\n",uiChecksum_read);
	if(uiChecksum_cal != uiChecksum_read)
	{
		LOG_INF("MIF&AWB read checksum not match calculated checksum!");
		return 0;
	}else{
		LOG_INF("S5K4HT AWB checksum success!");
	}
	return 1;
}

bool s5k4h7_sunny_lsc_datacheck(unsigned int group)
{
	unsigned char ucLSCData[1872] = {0x00};
	unsigned int uiBatchData = 0;
	unsigned int uiChecksum_cal = 0;
	unsigned int uiChecksum_read = 0;
	int i = 0;
	int page = 0;

	if(0 == group)
	{
		LOG_INF("LSC group 1 valied!\n");
		for(page = 36; page < 66; page++ )
		{
			write_cmos_sensor_8(0x0A02, page);
			write_cmos_sensor_8(0x0A00, 0x01);

			if (Sunny_Write_success() ==1)
			{
				if (page !=65)
				{
					for(i = 0; i < 64; i++)  //1 page = 64
					{
						ucLSCData[uiBatchData] = read_cmos_sensor_8(0x0A04 + i);
						LOG_INF("LSC data[%d] = 0x%02x\n",uiBatchData,ucLSCData[uiBatchData]);
						uiBatchData++;
					}
				}else
				{
					for(i = 0; i < 13; i++)  //1 page = 13
					{
						ucLSCData[uiBatchData] = read_cmos_sensor_8(0x0A04 + i);
						LOG_INF("LSC data[%d] = 0x%02x\n",uiBatchData,ucLSCData[uiBatchData]);
						uiBatchData++;
					}
				}
			}
		}
		uiChecksum_read = read_cmos_sensor_8(0x0A12)|(read_cmos_sensor_8(0x0A11)<<8);
	}
	else if(1 == group)
	{
		LOG_INF("LSC group 2 valied!\n");
		for(page = 65; page < 95; page++ )
		{
			write_cmos_sensor_8(0x0A02, page);
			write_cmos_sensor_8(0x0A00, 0x01);
			if (Sunny_Write_success() ==1)
			{
				// if (page !=94)
				if (page ==65)
				{
					for(i = 0; i < 49; i++)  //1 page = 49
					{
						ucLSCData[uiBatchData] = read_cmos_sensor_8(0x0A13 + i);
						LOG_INF("LSC data[%d] = 0x%02x\n",uiBatchData,ucLSCData[uiBatchData]);
						uiBatchData++;
					}
				}
				else if ((page>65) && (page<94))
				{
					for(i = 0; i < 64; i++)  //1 page = 64
				{
					ucLSCData[uiBatchData] = read_cmos_sensor_8(0x0A04 + i);
					LOG_INF("LSC data[%d] = 0x%02x\n",uiBatchData,ucLSCData[uiBatchData]);
					uiBatchData++;
				}
		}
				else{
					for(i = 0; i < 28; i++)  //1 page = 28
					{
						ucLSCData[uiBatchData] = read_cmos_sensor_8(0x0A04 + i);
						LOG_INF("LSC data[%d] = 0x%02x\n",uiBatchData,ucLSCData[uiBatchData]);
						uiBatchData++;
					}
				}
			}
		}
		uiChecksum_read = read_cmos_sensor_8(0x0A21)|(read_cmos_sensor_8(0x0A20)<<8);
	}
	else
	{
		LOG_INF("LSC group 3 valied!\n");
		for(page = 94; page < 124; page++ )
		{
			write_cmos_sensor_8(0x0A02, page);
			write_cmos_sensor_8(0x0A00, 0x01);
			if (Sunny_Write_success() ==1)
			{
				if (page ==94)
				{
					for(i = 0; i < 34; i++)  //1 page = 34
					{
					ucLSCData[uiBatchData] = read_cmos_sensor_8(0x0A22 + i);
					LOG_INF("LSC data[%d] = 0x%02x\n",uiBatchData,ucLSCData[uiBatchData]);
					uiBatchData++;
				}
				}
				else if ((page>94) && (page<123))
				{
					for(i = 0; i < 64; i++)  //1 page = 64
					{
						ucLSCData[uiBatchData] = read_cmos_sensor_8(0x0A04 + i);
						LOG_INF("LSC data[%d] = 0x%02x\n",uiBatchData,ucLSCData[uiBatchData]);
						uiBatchData++;
					}
				}
				else
				{
					for(i = 0; i < 43; i++)  //1 page = 43
					{
						ucLSCData[uiBatchData] = read_cmos_sensor_8(0x0A04 + i);
						LOG_INF("LSC data[%d] = 0x%02x\n",uiBatchData,ucLSCData[uiBatchData]);
						uiBatchData++;
					}
				}
			}
		}
		uiChecksum_read = read_cmos_sensor_8(0x0A30)|(read_cmos_sensor_8(0x0A2F)<<8);
	}

	for(i = 0; i < 1869; i++)
	{
		// LOG_INF("LSC data[%d] = 0x%02x\n",i,ucLSCData[i]);
		uiChecksum_cal = Sunny_GetCRC(ucLSCData, 1869);
		// LOG_INF("LSC  calculate checksum = 0x%04x\n",uiChecksum_cal);
	}


	LOG_INF("LSC calculate checksum = 0x%02x\n",uiChecksum_cal);
	LOG_INF("LSC read checksum = 0x%02x\n",uiChecksum_read);
	if(uiChecksum_cal != uiChecksum_read)
	{
		LOG_INF("LSC read checksum not match calculated checksum!");
		return 0;
	}
	else
	{
		LOG_INF("s5k4h7_sunny LSC  checksum success!");
		return 1;
	}
}

#if 0
bool s5k4h7_sunny_get_module_lsc(void)
{
	unsigned char flag[3] = {0x00};
	unsigned char page = 0x24;   //page 36
	unsigned int group = 0;
	write_cmos_sensor_8(0x0A02, page);
	write_cmos_sensor_8(0x0A00, 0x01);
	mDELAY(10);
	flag[0] = read_cmos_sensor_8(0x0A04);
	page =0x41;
	flag[1] = read_cmos_sensor_8(0x0A13);
	page =0x5E;
	flag[2] = read_cmos_sensor_8(0x0A22);

	if(0x00 == flag[0])
	{
		LOG_INF("group 1 is enpty!\n");
                return 0;
	}
	else if(0x40 == flag[0]||(0xC0 == flag[0]))
	{
		LOG_INF("group 1 is valied!\n");
		group = 1;
	}
	else if(0x40 == flag[1]||(0xC0 == flag[1]))
	{
		LOG_INF("group 2 is valied!\n");
		group = 2;
	}
	else if(0x40 == flag[2]||(0xC0 == flag[2]))
	{
		LOG_INF("group 3 is valied!\n");
		group = 3;
	}
	else if((0x00 == flag[0])&&(0x00 == flag[1])&&(0x00 == flag[2]))
	{
		LOG_INF("group 1\2\3 is invalied\n");
		return 0;
	}

	return s5k4h7_sunny_lsc_datacheck(group);
}

void s5k4h7_sunny_get_chip_id(void)
{
	char chip_id[8] = {0x00};
	int i = 0;

	write_cmos_sensor_8(0x0136, 0x18);	// 24MHz
	write_cmos_sensor_8(0x0137, 0x00);
	write_cmos_sensor_8(0x0305, 0x06);	// PLL pre div
	write_cmos_sensor_8(0x0306, 0x00);	//PLL multiplier
	write_cmos_sensor_8(0x0307, 0x8C);

	write_cmos_sensor_8(0x030D, 0x06);	// second_pre_pll_clk_div

	write_cmos_sensor_8(0x030E, 0x00);	// second_pll_multiplier
	write_cmos_sensor_8(0x030F, 0xAF);	// second_pll_multiplier
	write_cmos_sensor_8(0x0301, 0x04);	// vt_pix_clk_div

	//Streaming ON
	write_cmos_sensor_8(0x0100, 0x01);	// Streaming ON
	mDELAY(10);

	write_cmos_sensor_8(0x0A02, 0x15);	// page 21
	write_cmos_sensor_8(0x0A00, 0x01);
	mDELAY(10);

	LOG_INF("S5K4H7_SUNNY chip_id:");
	for(i = 0; i < 8; i++)
	{
		chip_id[i] = read_cmos_sensor_8(0x0A04 + i);
		LOG_INF("%02x",chip_id[i]);
	}
	LOG_INF("\n");
}
#endif
/***************************************************************************
 * Function    :  otp_update_wb
 * Description :  Update white balance settings from OTP
 * Parameters  :  [in] golden_rg : R/G of golden camera module
 [in] golden_bg : B/G of golden camera module
 * Return      :  1, success; 0, fail
 ***************************************************************************/
bool s5k4h7_sunny_otp_read(void)
{
	unsigned int flag = 0;
	unsigned char page[3] = {21,26,31};
	unsigned int group = 0;
	// unsigned char ucMIFAWB_addr[64] = {0x00};
	int i = 0;
	int nRet = 0;

	// s5k4h7_sunny_get_chip_id();
	write_cmos_sensor_8(0x0136, 0x18);	// 24MHz
	write_cmos_sensor_8(0x0137, 0x00);
	write_cmos_sensor_8(0x0305, 0x06);	// PLL pre div
	write_cmos_sensor_8(0x0306, 0x00);	//PLL multiplier
	write_cmos_sensor_8(0x0307, 0x8C);

	write_cmos_sensor_8(0x030D, 0x06);	// second_pre_pll_clk_div

	write_cmos_sensor_8(0x030E, 0x00);	// second_pll_multiplier
	write_cmos_sensor_8(0x030F, 0xAF);	// second_pll_multiplier
	write_cmos_sensor_8(0x0301, 0x04);	// vt_pix_clk_div

	//Streaming ON
	write_cmos_sensor_8(0x0100, 0x01);	// Streaming ON
	mDELAY(10);

	write_cmos_sensor_8(0x0A02, 0x15);	// page 21
	write_cmos_sensor_8(0x0A00, 0x01);
	if (Sunny_Write_success() ==1)
	flag = read_cmos_sensor_8(0x0A04);
	// ucMIFAWB_data[0] = flag;
	// ucMIFAWB_addr[0] = 0x00;
	// page = 0x15;
	for(group = 0;group < S5K4H7_SUNNY_MIFAWB_GROUP_CNT;group++)
	{
		switch(flag)
		{
		case 0x00:
			LOG_INF("group %d MIF&AWB is enpty!\n",group + 1);
			break;
		case 0x40:
			LOG_INF("group %d MIF&AWB is valied!\n",group + 1);
			for(i = 0; i < 64; i++)
			{
				sunny_ucMIFAWB_data[i] = read_cmos_sensor_8(0x0A04 + i);
				//ucMIFAWB_addr[i] = i;
			}
			s5k4h7_sunny_get_module_info(sunny_ucMIFAWB_data);
			nRet = s5k4h7_sunny_mifawb_datacheck(page[group]);
			nRet = s5k4h7_sunny_lsc_datacheck(group);
			return nRet;
		case 0xC0:
			LOG_INF("group %d MIF&AWB is invalied!\n",group + 1);
			//page += 1;
			write_cmos_sensor_8(0x0A02, page[group+1]);     // page 26/31
			write_cmos_sensor_8(0x0A00, 0x01);
			if (Sunny_Write_success() ==1)
			flag = read_cmos_sensor_8(0x0A04);
			break;
		default:
			break;
		}
	}

	return 0;
}
bool s5k4h7_sunny_otp_update(void)
{
	unsigned char page[3] = {21,26,31};
	unsigned int group = 0;
	int nRet = 0;
	int flag = 0;
	write_cmos_sensor_8(0x0136, 0x18);	// 24MHz
	write_cmos_sensor_8(0x0137, 0x00);
	write_cmos_sensor_8(0x0305, 0x06);	// PLL pre div
	write_cmos_sensor_8(0x0306, 0x00);	//PLL multiplier
	write_cmos_sensor_8(0x0307, 0x8C);

	write_cmos_sensor_8(0x030D, 0x06);	// second_pre_pll_clk_div

	write_cmos_sensor_8(0x030E, 0x00);	// second_pll_multiplier
	write_cmos_sensor_8(0x030F, 0xAF);	// second_pll_multiplier
	write_cmos_sensor_8(0x0301, 0x04);	// vt_pix_clk_div

	//Streaming ON
	write_cmos_sensor_8(0x0100, 0x01);	// Streaming ON
	mDELAY(10);

	write_cmos_sensor_8(0x0A02, 0x15);	// page 21
	write_cmos_sensor_8(0x0A00, 0x01);
	if (Sunny_Write_success() ==1)
	flag = read_cmos_sensor_8(0x0A04);

	for(group = 0;group < S5K4H7_SUNNY_MIFAWB_GROUP_CNT;group++)
	{
		switch(flag)
		{
		case 0x00:
			LOG_INF("group %d MIF&AWB is enpty!\n",group + 1);
			break;
		case 0x40:
			LOG_INF("group %d MIF&AWB is valied!\n",group + 1);
			nRet = s5k4h7_sunny_update_awb(page[group]);
			nRet = s5k4h7_sunny_apply_otp_lsc();
			return nRet;
		case 0xC0:
			LOG_INF("group %d moduleinfo is invalied!\n",group + 1);
			write_cmos_sensor_8(0x0A02, page[group+1]);     // page 26/31
			write_cmos_sensor_8(0x0A00, 0x01);
			if (Sunny_Write_success() ==1)
			flag = read_cmos_sensor_8(0x0A04);
			break;
		default:
			break;
		}
	}	
	return flag;
}

unsigned char s5k4h7_sunny_get_module_id(void)
{
	unsigned char module_id = 0;

	write_cmos_sensor_8(0x0136, 0x18);	// 24MHz
	write_cmos_sensor_8(0x0137, 0x00);
	write_cmos_sensor_8(0x0305, 0x06);	// PLL pre div
	write_cmos_sensor_8(0x0306, 0x00);	//PLL multiplier
	write_cmos_sensor_8(0x0307, 0x8C);

	write_cmos_sensor_8(0x030D, 0x06);	// second_pre_pll_clk_div

	write_cmos_sensor_8(0x030E, 0x00);	// second_pll_multiplier
	write_cmos_sensor_8(0x030F, 0xAF);	// second_pll_multiplier
	write_cmos_sensor_8(0x0301, 0x04);	// vt_pix_clk_div

	//Streaming ON
	write_cmos_sensor_8(0x0100, 0x01);	// Streaming ON
	mDELAY(10);

	write_cmos_sensor_8(0x0A02, 0x15);	// page 21
	write_cmos_sensor_8(0x0A00, 0x01);
	if (Sunny_Write_success() ==1)
	module_id = read_cmos_sensor_8(0x0A13);
	LOG_INF("S5K4H7_SUNNY module id = 0x%02x\n", module_id);
	return module_id;
}

unsigned int s5k4h7_sunny_read_otpdata(struct i2c_client *client, unsigned int addr, unsigned char *data, unsigned int size)
{
	int i = 0,group = 0,flag = 0;
	unsigned char page[3] = {21,26,31};
	write_cmos_sensor_8(0x0136, 0x18);	// 24MHz
	write_cmos_sensor_8(0x0137, 0x00);
	write_cmos_sensor_8(0x0305, 0x06);	// PLL pre div
	write_cmos_sensor_8(0x0306, 0x00);	//PLL multiplier
	write_cmos_sensor_8(0x0307, 0x8C);
	write_cmos_sensor_8(0x030D, 0x06);	// second_pre_pll_clk_div
	write_cmos_sensor_8(0x030E, 0x00);	// second_pll_multiplier
	write_cmos_sensor_8(0x030F, 0xAF);	// second_pll_multiplier
	write_cmos_sensor_8(0x0301, 0x04);	// vt_pix_clk_div
	//Streaming ON
	write_cmos_sensor_8(0x0100, 0x01);	// Streaming ON
	mDELAY(10);
	write_cmos_sensor_8(0x0A02, 0x15);	// page 21
	write_cmos_sensor_8(0x0A00, 0x01);
	if (Sunny_Write_success() ==1)
	flag = read_cmos_sensor_8(0x0A04);

	for(group = 0;group < 3;group++)
	{
		switch(flag)
		{
		case 0x00:
			LOG_INF("group %d moduleinfo is enpty!\n",group + 1);
			break;
		case 0x40:
			LOG_INF("group %d moduleinfo is valied!\n",group + 1);
			for(i = 0; i < 22; i++)
			{
				data[i] = read_cmos_sensor_8(0X0A0C + i);
				LOG_INF("s5k4h7 module info reg[%d] = %x", i, data[i]);
			}
			return size;
		case 0xC0:
			LOG_INF("group %d moduleinfo is invalied!\n",group + 1);
			write_cmos_sensor_8(0x0A02, page[group+1]);     // page 26/31
			write_cmos_sensor_8(0x0A00, 0x01);
			if (Sunny_Write_success() ==1)
			flag = read_cmos_sensor_8(0x0A04);
			break;
		default:
			break;
		}
	}
	return size;
}
EXPORT_SYMBOL(s5k4h7_sunny_read_otpdata);
