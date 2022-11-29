/*
 * Copyright 2016 SiliconMotion Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 */

#include <drm/drmP.h>

#include "ddk768/ddk768_mode.h"
#include "ddk768/ddk768_help.h"
#include "ddk768/ddk768_reg.h"	
#include "ddk768/ddk768_display.h"
#include "ddk768/ddk768_2d.h"
#include "ddk768/ddk768_power.h"
#include "ddk768/ddk768_cursor.h"
#include "ddk768/ddk768_video.h"
#include "ddk768/ddk768_hdmi.h"

struct smi_768_register{
	uint32_t clock_enable, pll_ctrl[3];
	
	uint32_t primary_display_ctrl[10], lvds_ctrl, primary_hwcurs_ctrl[4];
	uint32_t secondary_display_ctrl[10], secondary_hwcurs_ctrl[4];
};

void hw768_load_lut(int path, int offset, u32 rgb)
{
	if(path == 0)
		pokeRegisterDWord(CHANNEL0_PALETTE_RAM + offset,rgb);		
	else
		pokeRegisterDWord(CHANNEL1_PALETTE_RAM + offset,rgb);		
	
}

void hw768_enable_gamma(unsigned dispCtrl,unsigned long enableGammaCtrl)
{

	unsigned long value;
	unsigned long regCtrl;

	regCtrl = (dispCtrl == CHANNEL0_CTRL)? DISPLAY_CTRL : (DISPLAY_CTRL+CHANNEL_OFFSET);

	value = peekRegisterDWord(regCtrl);
	
	if (enableGammaCtrl)
		value = FIELD_SET(value, DISPLAY_CTRL, GAMMA, ENABLE);
	else
		value = FIELD_SET(value, DISPLAY_CTRL, GAMMA, DISABLE);
		
	pokeRegisterDWord(regCtrl, value);	  


}


mode_parameter_t convert_drm_mode_to_ddk_mode(struct drm_display_mode mode)
{
	mode_parameter_t modeP;

    modeP.horizontal_total = mode.htotal;
    modeP.horizontal_display_end = mode.hdisplay;
    modeP.horizontal_sync_start = mode.hsync_start;
    modeP.horizontal_sync_width = mode.hsync_end - mode.hsync_start;
    modeP.horizontal_sync_polarity = mode.flags & DRM_MODE_FLAG_PHSYNC ? POS : NEG;

    /* Vertical timing. */
    modeP.vertical_total = mode.vtotal;
    modeP.vertical_display_end = mode.vdisplay;
    modeP.vertical_sync_start = mode.vsync_start;
    modeP.vertical_sync_height = mode.vsync_end - mode.vsync_start;
    modeP.vertical_sync_polarity = mode.flags & DRM_MODE_FLAG_PVSYNC ? POS : NEG;

    /* Refresh timing. */
    modeP.pixel_clock = mode.clock * 1000;
    modeP.horizontal_frequency = 0;
    modeP.vertical_frequency = 0;
    
    /* Clock Phase. This clock phase only applies to Panel. */
    modeP.clock_phase_polarity = POS;

	return modeP;
}



void hw768_enable_lvds(int channels)
{
	if(channels == 1){	
		pokeRegisterDWord(0x80020,0x31E30000);		
		pokeRegisterDWord(0x8002C,0x74001200);
	}else{
		unsigned long value = 0;
		pokeRegisterDWord(0x80020, 0x31E3F71D);
		pokeRegisterDWord(0x8002C,0x750FED02);
		value = peekRegisterDWord(DISPLAY_CTRL);
		value = FIELD_SET(value, DISPLAY_CTRL, LVDS_OUTPUT_FORMAT, CHANNEL0_48BIT);
		value = FIELD_SET(value, DISPLAY_CTRL, PIXEL_CLOCK_SELECT, HALF);
		value = FIELD_SET(value, DISPLAY_CTRL, DOUBLE_PIXEL_CLOCK, ENABLE);
		pokeRegisterDWord(DISPLAY_CTRL,value);

		value = peekRegisterDWord(DISPLAY_CTRL + CHANNEL_OFFSET);
		value = FIELD_SET(value, DISPLAY_CTRL, LVDS_OUTPUT_FORMAT, CHANNEL0_48BIT);
		pokeRegisterDWord(DISPLAY_CTRL + CHANNEL_OFFSET,value);
	}
}	



void hw768_suspend(struct smi_768_register * pSave)
{

#if 0
	int i;

	pSave->clock_enable = peekRegisterDWord(CLOCK_ENABLE);
	for (i = 0; i < 3; i++)
		pSave->pll_ctrl[i] = peekRegisterDWord(MCLK_PLL + i * 4);

	for (i = 0; i < 10; i++)
		pSave->primary_display_ctrl[i] = peekRegisterDWord(DISPLAY_CTRL + i * 4);
	pSave->lvds_ctrl = peekRegisterDWord(LVDS_CTRL2);
	for (i = 0; i < 4; i++)
		pSave->primary_hwcurs_ctrl[i] = peekRegisterDWord(HWC_CONTROL + i * 4);

	for (i = 0; i < 10; i++)
		pSave->secondary_display_ctrl[i] = peekRegisterDWord(0x8000 + DISPLAY_CTRL + i * 4);
	for (i = 0; i < 4; i++)
		pSave->secondary_hwcurs_ctrl[i] = peekRegisterDWord(0x8000 + HWC_CONTROL + i * 4);

#endif

}

void hw768_resume(struct smi_768_register * pSave)
{

#if 0
	int i;

	pokeRegisterDWord(CLOCK_ENABLE, pSave->clock_enable);
	for (i = 0; i < 3; i++)
		pokeRegisterDWord(MCLK_PLL + i * 4, pSave->pll_ctrl[i]);

	for (i = 0; i < 10; i++)
		pokeRegisterDWord(DISPLAY_CTRL + i * 4, pSave->primary_display_ctrl[i]);
	pokeRegisterDWord(LVDS_CTRL2, pSave->lvds_ctrl);
	for (i = 0; i < 4; i++)
		pokeRegisterDWord(HWC_CONTROL + i * 4, pSave->primary_hwcurs_ctrl[i]);

	for (i = 0; i < 10; i++)
		pokeRegisterDWord(0x8000 + DISPLAY_CTRL + i * 4, pSave->secondary_display_ctrl[i]);
	for (i = 0; i < 4; i++)
		pokeRegisterDWord(0x8000 + HWC_CONTROL + i * 4, pSave->secondary_hwcurs_ctrl[i]);

#endif
}
void hw768_set_base(int display,int pitch,int base_addr)
{	

	if(display == 0)
	{
		/* Frame buffer base */
	    pokeRegisterDWord((FB_ADDRESS),
	          FIELD_SET(0, FB_ADDRESS, STATUS, PENDING)
	        | FIELD_VALUE(0, FB_ADDRESS, ADDRESS, base_addr));

	    /* Pitch value (Hardware people calls it Offset) */
    	pokeRegisterDWord((FB_WIDTH), FIELD_VALUE(peekRegisterDWord(FB_WIDTH), FB_WIDTH, OFFSET, pitch));
	}
	else
	{
		/* Frame buffer base */
	    pokeRegisterDWord((FB_ADDRESS+CHANNEL_OFFSET),
	          FIELD_SET(0, FB_ADDRESS, STATUS, PENDING)
	        | FIELD_VALUE(0, FB_ADDRESS, ADDRESS, base_addr));

		
	    /* Pitch value (Hardware people calls it Offset) */	
	    pokeRegisterDWord((FB_WIDTH+CHANNEL_OFFSET),FIELD_VALUE(peekRegisterDWord(FB_WIDTH+CHANNEL_OFFSET), FB_WIDTH, OFFSET, pitch));

	}
}


void hw768_init_hdmi(void)
{
	HDMI_Init();
}

int hw768_set_hdmi_mode(logicalMode_t *pLogicalMode, struct drm_display_mode mode, bool isHDMI)
{
	int ret = 1;
	mode_parameter_t modeParam;
	
	if(pLogicalMode->x == 3840)
	{
		printk("Use 4K Mode!\n");
		pLogicalMode->hz = 30;
	}
	else
		pLogicalMode->hz = 60;
	// set HDMI parameters
	HDMI_Disable_Output();

	if(pLogicalMode->valid_edid)
		modeParam = convert_drm_mode_to_ddk_mode(mode);
	ret = HDMI_Set_Mode(pLogicalMode,&modeParam,isHDMI);
	return ret;
}

int hw768_en_dis_interrupt(int status, int pipe)
{
	if(status == 0)
	{
		pokeRegisterDWord(INT_MASK, 	(pipe == CHANNEL0_CTRL) ? 
		FIELD_SET(0, INT_MASK, CHANNEL1_VSYNC, DISABLE):
		FIELD_SET(0, INT_MASK, CHANNEL0_VSYNC, DISABLE));
	}
	else
	{
		pokeRegisterDWord(INT_MASK, 	(pipe == CHANNEL1_CTRL) ? 
		FIELD_SET(0, INT_MASK, CHANNEL1_VSYNC, ENABLE):
		FIELD_SET(0, INT_MASK, CHANNEL0_VSYNC, ENABLE));
	}
	return 0;
}
void hw768_HDMI_Enable_Output(void)
{
	HDMI_Enable_Output();
}

void hw768_HDMI_Disable_Output(void)
{
	HDMI_Disable_Output();
}


int hw768_get_hdmi_edid(unsigned char *pEDIDBuffer)
{
    int ret;
    enableHdmI2C(1);
    ret = HDMI_Read_Edid(pEDIDBuffer, 128);
    enableHdmI2C(0);

    return ret;
}

int hw768_check_iis_interrupt(void)
{

	unsigned long value;
		
	value = peekRegisterDWord(INT_STATUS);

	
    if (FIELD_GET(value, INT_STATUS, I2S) == INT_STATUS_I2S_ACTIVE)
		return true;
	else	
		return false;
}


int hw768_check_vsync_interrupt(int path)
{

	unsigned long value1,value2;
		
	value1 = peekRegisterDWord(RAW_INT);
	value2 = peekRegisterDWord(INT_MASK);

	if(path == CHANNEL0_CTRL)
	{
	    if ((FIELD_GET(value1, RAW_INT, CHANNEL0_VSYNC) == RAW_INT_CHANNEL0_VSYNC_ACTIVE)
			&&(FIELD_GET(value2, INT_MASK, CHANNEL0_VSYNC) == INT_MASK_CHANNEL0_VSYNC_ENABLE))
	    {
			return true;
		}
	}else{
		if ((FIELD_GET(value1, RAW_INT, CHANNEL1_VSYNC) == RAW_INT_CHANNEL1_VSYNC_ACTIVE)
			&&(FIELD_GET(value2, INT_MASK, CHANNEL1_VSYNC) == INT_MASK_CHANNEL1_VSYNC_ENABLE))
		{
			return true;
		}
	}
	
	return false;
}


void hw768_clear_vsync_interrupt(int path)
{
	
	unsigned long value;
	
	value = peekRegisterDWord(RAW_INT);

	if (path == CHANNEL0_CTRL)
	{
		pokeRegisterDWord(RAW_INT, FIELD_SET(value, RAW_INT, CHANNEL0_VSYNC, CLEAR));
	}
	else
	{
		pokeRegisterDWord(RAW_INT, FIELD_SET(value, RAW_INT, CHANNEL1_VSYNC, CLEAR));
	}
}

long hw768_setMode(logicalMode_t *pLogicalMode, struct drm_display_mode mode)
{
	
	if (!pLogicalMode->valid_edid)
		return ddk768_setMode(pLogicalMode);
	else
	{	
		mode_parameter_t ModeParam;
		ModeParam = convert_drm_mode_to_ddk_mode(mode);
		return ddk768_setCustomMode(pLogicalMode, &ModeParam);
	}
	
}


int hdmi_int_status = 0;

inline int hdmi_hotplug_detect(void)
{
	int ret = 0;
	unsigned int intMask = peekRegisterDWord(INT_MASK);
	intMask = FIELD_SET(intMask, INT_MASK, HDMI, ENABLE);
	pokeRegisterDWord(INT_MASK, intMask);

	ret = hdmi_detect();

	if (ret == 1)
	{
		hdmi_int_status = 1;
	}
	else if (ret == 0)
	{
		hdmi_int_status = 0;
	}
	else
	{
		hdmi_int_status = hdmi_int_status & ret;
	}

	intMask = peekRegisterDWord(INT_MASK);
	intMask = FIELD_SET(intMask, INT_MASK, HDMI, DISABLE);
	pokeRegisterDWord(INT_MASK, intMask);

	return hdmi_int_status;
}

void ddk768_disable_IntMask(void)
{
	
    pokeRegisterDWord(INT_MASK, 0);
}

void hw768_SetPixelClockFormat(disp_control_t dispControl,unsigned int is_half)
{
    unsigned long ulDispCtrlAddr;
    unsigned long ulDispCtrlReg;

    if (dispControl == CHANNEL0_CTRL)
    {
        ulDispCtrlAddr = DISPLAY_CTRL;
    }
    else
        return;
    
    ulDispCtrlReg = peekRegisterDWord(ulDispCtrlAddr);
    
	if(is_half)
		ulDispCtrlReg = FIELD_SET(ulDispCtrlReg, DISPLAY_CTRL, PIXEL_CLOCK_SELECT, HALF);
	else
		ulDispCtrlReg = FIELD_SET(ulDispCtrlReg, DISPLAY_CTRL, PIXEL_CLOCK_SELECT, SINGLE);


    pokeRegisterDWord(ulDispCtrlAddr, ulDispCtrlReg);
}

