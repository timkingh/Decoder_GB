#ifndef _MDIN241_H_
#define _MDIN241_H_

#define SYSTEM_USE_ARM_LINUX

#ifdef SYSTEM_USE_ARM_LINUX
#define stPACKED			__attribute__((packed))
#else
#define stPACKED
#endif

#define MDIN241_IOCTL_SET_RESOLUTION		0
#define MDIN241_IOCTL_SET_PARAMETER		1

//typedef unsigned short WORD;
//typedef unsigned char BYTE;
typedef struct {
	BYTE	operation;		//
	WORD	RegAddr;		//
	WORD	 RegVal;		//
}stPACKED _RegisterOperation_;

typedef struct {
	WORD vi;		//
	WORD vo;		//
}stPACKED _VideoResolution_;

typedef struct {
	BYTE	brightness;		// default = 128, range: from 0 to 255
	BYTE	contrast;		// default = 128, range: from 0 to 255
	BYTE	saturation;		// default = 128, range: from 0 to 255
	BYTE	hue;			// default = 128, range: from 0 to 255
	BYTE	sharpness;		// default = 128, range: from 0 to 255
	
	// for detail adjust
	BYTE	r_gain;			// default = 128, range: from 0 to 255
	BYTE	g_gain;			// default = 128, range: from 0 to 255
	BYTE	b_gain;			// default = 128, range: from 0 to 255
	BYTE	r_offset;		// default = 128, range: from 0 to 255
	BYTE	g_offset;		// default = 128, range: from 0 to 255
	BYTE	b_offset;		// default = 128, range: from 0 to 255
}stPACKED _VideoAdjust_;
typedef struct {
	_RegisterOperation_ RegOp;
	_VideoResolution_ Vres;
	_VideoAdjust_ Vadj;
}stPACKED _mdin240ioctlpara_;

typedef enum _mdin240_vi_res_{
    MVI_720x576i50,		// PAL
    MVI_720x480i60,		// NTSC
    MVI_720p60,
    MVI_1080i60,
    MVI_1080p60,
    MVI_800x600p60,
    MVI_1024x768p60,
    MVI_1280x1024p60,
    MVI_1360x768p60,
    MVI_1440x900p60,
    MVI_1440x960i60,		// 4x NTSC
    MVI_1440x1152i50,	// 4x PAL
    MVI_1440x960p60,	// 4x NTSC
    MVI_1440x1152p50	// 4x PAL
}mdin240_vi_res_t;

typedef enum _mdin240_vo_res_{
    MVO_1080p60,
    MVO_1080p50,
    MVO_720p60,
    MVO_720p50,
    MVO_1080i60,
    MVO_1080i50,
    MVO_1440x900p60,
    MVO_1280x1024p60,
    MVO_1280x1024p75,
    MVO_1024x768p60,
    MVO_1024x768p75
}mdin240res_vo_t;



#endif

