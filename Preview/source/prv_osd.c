#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <linux/fb.h>

#include "hifb.h"
#include "disp_api.h"
#include "hi_common.h"
#include "hi_comm_sys.h"

#if defined(SN9234H1)
#include "hi_comm_vpp.h"
#include "loadbmp.h"
#include "mpi_vpp.h"
#else
#include "hi_comm_region.h"
#include "hi_comm_vpss.h"
#include "mpi_region.h"
#include "mpi_vpss.h"
#endif


#include "hi_tde_api.h"
#include "hi_tde_errcode.h"
#include "global_api.h"

#define JPEG_ON
#define TRACE_MOD_PRV 1
#define SUB_STREAM	1
#if defined(SN6104) || defined(SN8604D) || defined(SN8604M)
#define SECOND_DEV
#endif	
#if (TRACE_MOD_PRV)
#define printf(fmt, ...) TRACE(SCI_TRACE_HIGH, MOD_PRV, "%s:%d: " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define TRACE(lv, mod, fmt, ...) printf("%s:%d: " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define printf(fmt, ...) printf("%s:%d: " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif
#if defined(SN9234H1)
#define OPEN_REC_OSD
#endif
#define OPEN_PRV_OSD

#define ICON_X 600			//报警图标起始x位置
#define ICON_Y 20			//报警图标起始y位置

#define ICON_WIDTH 20		//录像时报警图标起始宽度
#define ICON_HEIGTH 20		//录像时报警图标起始高度
#define OSD_REC_TIME_X 200		//时间图标起始x位置
#define OSD_REC_TIME_Y 450		//时间图标起始y位置

#define OSD_NAME_X 50		//通道图标起始x位置
#define OSD_NAME_Y 50		//通道图标起始y位置
#define OSD_TIME_WIDTH		440//D1下时间宽度
#define OSD_TIME_HEIGTH		32//D1下时间高度
#define OSD_TIME_CIF_WIDTH		272//CIF下时间宽度
#define OSD_TIME_CIF_HEIGTH		20//CIF下时间高度
#define OSD_TIME_QCIF_WIDTH		144//QCIF下时间宽度
#define OSD_TIME_QCIF_HEIGTH		16//QCIF下时间高度


#define OSD_BG_ALPHAL	0		//录像背景透明度	
#define OSD_FG_ALPHAL	128		//录像前景透明度
#define OSD_BG_COLOR	0		//录像前景色


#define OSD_TIME_OFF 		0x01		//时间隐藏、显示标志
#define OSD_NAME_OFF 		0x02		//通道名称隐藏、显示标志
#define OSD_ALARM_OFF 		0x04		//报警图标隐藏、显示标志
#define OSD_REC_OFF 		0x08		//录像图标隐藏、显示标志
#define OSD_CLICKVISUAL_OFF	0x10		//一点即视
#define OSD_PB_OFF 			0x20		//回放隐藏、显示标志
#define OSD_FB_OFF 			0x40		//图形层隐藏、显示标志
#define OSD_FB_FLICKER_OFF 	0x80		//抗闪隐藏、显示标志

#define OSD_NOT_IN_MODE 	0x100		//不在当前状态下标志

#define OSD_CLEAR 		0x00			//全屏刷新标志

#define COVER_MAX_SIZE MAX_HIDE_AREA_NUM		//遮盖最大区域数
#define FB_BG_COLOR 0x00						//当前FB透明色
#define FB_BG_COLORKEY 0x0000					//当前FB透明色
#define SCREEN_DEF_WIDTH	720					//与GUI协定的OSD位置最大宽度
#define SCREEN_DEF_HEIGHT   575					//P制式下最大高度
#define SCREEN_DEF_N_HEIGHT   480					//N制式下最大高度
#define SCREEN_REC_WIDTH	352					//编码通道最大宽度
#define SCREEN_REC_HEIGHT   288					//编码通道最大高度

#define OSD_NAME_DEF_X			12		//通道名称默认位置
#define OSD_NAME_DEF_Y			20
#define OSD_OTHERNAME_DEF_Y		55
#define OSD_TIME_DEF_X			270		//时间默认位置
#define OSD_TIME_DEF_Y			528

#define SCREEN_D1_WIDTH   720					//D1最大宽度
#define SCREEN_4CIF_WIDTH   704					//4CIF最大宽度
#define SCREEN_4CIF_HEIGHT   576					//P制式下4CIF最大高度
#define SCREEN_4CIF_N_HEIGHT   480					//N制式下4CIF最大高度
#define SCREEN_CIF_WIDTH   352					//CIF最大宽度
#define SCREEN_CIF_HEIGHT	288					//P制式下CIF最大高度
#define SCREEN_CIF_N_HEIGHT   240					//N制式下CIF最大高度
#define SCREEN_QCIF_WIDTH   176					//qCIF最大宽度
#define SCREEN_QCIF_HEIGHT	144					//P制式下qCIF最大高度
#define SCREEN_QCIF_N_HEIGHT   120					//N制式下qCIF最大高度
#if SUB_STREAM
	#define REC_JPEG_START_CHN_ID	PRV_CHAN_NUM*2	//jpeg抓图起始编码通道号
#else
	#define REC_JPEG_START_CHN_ID	PRV_CHAN_NUM	//jpeg抓图起始编码通道号
#endif
#define OSD_X_OFFSET	2						//OSD位图的便宜横坐标数值
#define OSD_Y_OFFSET	2						//OSD位图的便宜纵坐标数值
#define PIXBYTE		2							//OSD位图的像素字节数

#define GUI_FONT_REC	0						//录像OSD字体 0-16号字体（8*20） 2-12号字体（6*16）
//#define GUI_FONT_REC_WIDTH	8					//录像OSD字体宽度
//#define GUI_FONT_REC_HEIGHT   20				//录像OSD字体高度

#define GUI_FONT_PRV	1						//预览OSD字体
//#define GUI_FONT_PRV_WIDTH	12					//预览OSD字体宽度
//#define GUI_FONT_PRV_HEIGHT   30				//预览OSD字体高度

#define GUI_DIS_RATIO 2							//OSD显示放大比例

#define OSD_INIT_ERR	-20						//预览OSD为初始化返回值

#define ENOUTALPHAFORM	TDE2_OUTALPHA_FROM_FOREGROUND//预览OSD 输出透明模式
#define ENCOLORKEYMODE	TDE2_COLORKEY_MODE_FOREGROUND//预览OSD关键色模式
#define ARGB_IGNORE		HI_FALSE//预览OSD 分量忽略


/* Font对应关系:1-20,2-12,3-16,4-24,5-28,6-32,7-36,8-40,其他-16 */
static const int s_as32FontH[] = {MMI_FONT_16, MMI_FONT_20, MMI_FONT_12, MMI_FONT_16, MMI_FONT_24, MMI_FONT_28, MMI_FONT_32, MMI_FONT_36, MMI_FONT_40};

#if defined(Hi3531)||defined(Hi3535)
static RGN_HANDLE cov_handle[PRV_CHAN_NUM][COVER_MAX_SIZE];			//遮盖区域句柄数组
static RGN_HANDLE rec_osd_handle[PRV_CHAN_NUM*3][REGION_NUM];		//录像OSD区域句柄数组，有3组通道，1组用于主码流、1组用于次码流，1组用于JPEG抓图
#else
static REGION_HANDLE cov_handle[PRV_CHAN_NUM][COVER_MAX_SIZE];		//遮盖区域句柄数组
static REGION_HANDLE rec_osd_handle[PRV_CHAN_NUM*3][REGION_NUM];	//录像OSD区域句柄数组，有3组通道，1组用于主码流、1组用于次码流，1组用于JPEG抓图
#endif

static unsigned char cov_flag[PRV_CHAN_NUM][COVER_MAX_SIZE];			//遮盖区域显示、隐藏标志

//static PRM_SCAPE_RECT  rec_osd_Rect[PRV_CHAN_NUM][2];
#define REC_CTRL_ALL	0xFF		//控制所有的录像OSD组，值为0xff

#define MAX_TIME_W		50		//28号字体的最大汉字宽度

#if 0
#define HANZI_MAX_28_W		29		//28号字体的最大汉字宽度
#define MAX_28_H		32		//28号字体的最大汉字高度
#define ZIMU_MAX_28_W		17		//28号字体的最大字母宽度

#define HANZI_MAX_16_W		17		//16号字体的最大汉字宽度
#define MAX_16_H		20		//16号字体的最大汉字高度
#define ZIMU_MAX_16_W		11		//16号字体的最大字母宽度

#define HANZI_MAX_12_W		14		//12号字体的最大汉字宽度
#define MAX_12_H		16		//12号字体的最大汉字高度
#define ZIMU_MAX_12_W		9		//12号字体的最大字母宽度
#endif

typedef struct 
{
	Rec_Osd_Time_Info	osd_time;
	unsigned char *pData[OSD_TIME_RES];//当前字符串的位图句柄
}Rec_Osd_Str_Info;
static unsigned int Rec_OSD_Bmp_Lenth=0;
Rec_Osd_Str_Info  Rec_Osd_Str[MAX_TIME_STR_LEN];//保存时间OSD需要用到的字符的字符信息
//static unsigned char OSD_QTime_Buf[MAX_TIME_STR_LEN]={0};
#if defined(Hi3531)||defined(Hi3535)
typedef union hiREGION_CTRL_PARAMETER_U
{
    /* for REGION_SET_LAYER */
    HI_U32 u32Layer;

    /* for REGION_SET_ALPHA0,REGION_SET_ALPHA1 or REGION_SET_GLOBAL_ALPHA */
    HI_U32 u32Alpha;

    /* for REGION_SET_COLOR */
    HI_U32 u32Color;

    /* for REGION_SET_POSTION */
    POINT_S stPoint;

    /* for REGION_SET_SIZE */
    SIZE_S stDimension;

    /* for REGION_SET_BITMAP */
    BITMAP_S stBitmap;
    
    /* for REGION_GET_SIGNLE_ATTR */
    RGN_ATTR_S stRegionAttr;

}REGION_CTRL_PARAM_U;
#endif
typedef struct
{
	REGION_CTRL_PARAM_U time_icon_param[REC_OSD_GROUP][PRV_CHAN_NUM];		//录像时间d1 OSD图标
	STRING_BMP_ST 		time_Bmp_param[OSD_TIME_RES];				//录像通道名称OSD图标保存数组
	unsigned char Time_Str[MAX_BMP_STR_LEN];//时间字符串
	unsigned char qTime_Str[MAX_BMP_STR_LEN];//qcif下的时间字符串
	unsigned char Change_flag[REC_OSD_GROUP][PRV_CHAN_NUM];	//	区域有修改的标志位
}Time_Rec_Info;
static Time_Rec_Info time_str_buf=
{
	.time_icon_param={{{0},{0},{0}}},
	.time_Bmp_param = {{0},{0},{0},{0},{0},{0}},
};//[PRV_CHAN_NUM];
static REGION_CTRL_PARAM_U name_icon_param[REC_OSD_GROUP][PRV_CHAN_NUM];				//录像通道名称OSD图标保存数组
static REGION_CTRL_PARAM_U name_icon_param_slave[REC_OSD_GROUP][PRV_CHAN_NUM];
static int TimeOsd_size = 0;
//录像通道名称OSD图标保存数组
//static STRING_BMP_ST name_Bmp_param[PRV_CHAN_NUM];				//录像通道名称OSD图标保存数组

//static REGION_CTRL_PARAM_U time_icon_param[PRV_CHAN_NUM];				//录像时间OSD图标保存数组
//static REGION_CTRL_PARAM_U rec_icon_param;				//录像OSD图标保存数组
//static REGION_CTRL_PARAM_U alarm_icon_param;				//录像报警OSD图标保存数组

#define ALARM_ICON_PATH 			"/res/osd_alarm1.bmp"
#define REC_ICON_PATH  				"/res/osd_rec1.bmp"
#if 1
#define ALARM_BITS_PATH 			"/res/alarm_novideo.bits"
#define ALARM_MD_BITS_PATH 			"/res/alarm_md.bits"
#define TIME_REC_BITS_PATH  		"/res/rec_time.bits"
#define MANUAL_REC_BITS_PATH  		"/res/rec_manual.bits"
#define ALARM_REC_BITS_PATH  		"/res/rec_alarm.bits"
#define CLICKVISUAL_BITS_PATH 		"/res/clickvisual.bits"
#else
#define ALARM_BITS_PATH 			"/mnt/mtd/alarm_novideo.bits"
#define ALARM_MD_BITS_PATH 			"/mnt/mtd/alarm_md.bits"
#define TIME_REC_BITS_PATH  		"/mnt/mtd/rec_time.bits"
#define MANUAL_REC_BITS_PATH  		"/mnt/mtd/rec_manual.bits"
#define ALARM_REC_BITS_PATH  		"/mnt/mtd/rec_alarm.bits"

#endif
//#define NOVIDEO_ICON_PATH  "novideo.bmp"

#ifdef VO_MAX_DEV_NUM
#undef VO_MAX_DEV_NUM
#endif

#if defined(Hi3531)||defined(Hi3535)
#define VO_MAX_DEV_NUM 	1//PRV_VO_DEV_NUM
static int Prv_fd[/*VO_MAX_DEV_NUM*/]={-1,-1,-1,-1,-1,-1,-1,-1};			//预览图形层FD
#else
#define VO_MAX_DEV_NUM 	PRV_VO_DEV_NUM
static int Prv_fd[/*VO_MAX_DEV_NUM*/]={-1,-1,-1};							//预览图形层FD
#endif

static int g_guifd = -1;										//GUI图形层FD
static int g1fd = -1;


#if !defined(USE_UI_OSD)
static unsigned int fb_memlen[VO_MAX_DEV_NUM];							//fb size
static unsigned int fb_phyaddr[VO_MAX_DEV_NUM]={0};				//预览图形层物理地址
static unsigned char *fb_mmap[VO_MAX_DEV_NUM];					//预览图形层映射地址
#endif
static TDE2_SURFACE_S g_stScreen[VO_MAX_DEV_NUM];				
static TDE2_SURFACE_S g_stImgSur[VO_MAX_DEV_NUM][REGION_NUM];

//抗闪操作变量
static TDE2_SURFACE_S g_fb_stScreen;		/* GUI层的SURFACE */
static TDE2_SURFACE_S g_fb_stScreen1;		/* GUI层的另一个SURFACE */
static TDE2_SURFACE_S g_fb_stScreen2;		/* G1层的SURFACE */
static unsigned int g_fb_phyaddr=0;
static unsigned int g_fb_flag=0;
static unsigned char *g_fb_mmap;

//static REGION_HANDLE OSD_handle[PRV_CHAN_NUM][REGION_NUM];
//
#if defined(SN9234H1)
static PRM_SCAPE_RECT  Mask_Rect[PRV_CHAN_NUM][COVER_MAX_SIZE];		//遮盖区域保存变量
static TDE2_RECT_S  OSD_Rect[PRV_VO_MAX_DEV][PRV_VO_CHN_NUM][REGION_NUM];	//记录OSD时间和通道名称位置
static TDE2_RECT_S  OSD_Jpeg_Rect[PRV_VO_MAX_DEV][PRV_CHAN_NUM][REGION_NUM];	//记录抓图上OSD位置
static TDE2_RECT_S  OSD_PRV_Time_Rect[PRV_VO_MAX_DEV];			//预览OSD时间位置
static unsigned int  OSD_PRV_Time_Width[OSD_TIME_RES];	//当前实时的预览时间宽度

static unsigned char OSD_Name_Buf[PRV_VO_CHN_NUM][MAX_BMP_STR_LEN];	//记录通道名称字符
static unsigned char OSD_GroupName_Buf[LINKAGE_MAX_GROUPNUM][MAX_BMP_STR_LEN];	//记录通道名称字符
static int OSD_Name_Type[DEV_CHANNEL_NUM];

//static unsigned char OSD_Time_Buf[MAX_BMP_STR_LEN]={0};
static unsigned int OSD_off_flag[VO_MAX_DEV_NUM][PRV_VO_CHN_NUM]={{0}};		//OSD是否显示标志
//static unsigned char First_InitFB=0;
unsigned int g_rec_srceen_h[REC_OSD_GROUP][PRV_CHAN_NUM];		//录像OSD高度
unsigned int g_rec_srceen_w[REC_OSD_GROUP][PRV_CHAN_NUM];		//录像OSD宽度
static unsigned char g_rec_type[PRV_VO_CHN_NUM][NAME_LEN];		//当前录像类型对应图片路径
static unsigned char g_alarm_type[PRV_VO_CHN_NUM][NAME_LEN];		//当前报警类型对应图片路径
static unsigned char g_clickvisual_type[DEV_CHANNEL_NUM][NAME_LEN];		//当前一点即视类型对应图片路径

#else
static PRM_SCAPE_RECT  Mask_Rect[PRV_CHAN_NUM][COVER_MAX_SIZE];		//遮盖区域保存变量
static TDE2_RECT_S  OSD_Rect[PRV_VO_MAX_DEV][DEV_CHANNEL_NUM][REGION_NUM];	//记录OSD时间和通道名称位置
static TDE2_RECT_S  OSD_Jpeg_Rect[PRV_VO_MAX_DEV][DEV_CHANNEL_NUM][REGION_NUM];	//记录抓图上OSD位置
static TDE2_RECT_S  OSD_PRV_Time_Rect[PRV_VO_MAX_DEV];			//预览OSD时间位置
static unsigned int  OSD_PRV_Time_Width[OSD_TIME_RES];	//当前实时的预览时间宽度

static unsigned char OSD_Name_Buf[DEV_CHANNEL_NUM][MAX_BMP_STR_LEN];	//记录通道名称字符
static unsigned char OSD_GroupName_Buf[LINKAGE_MAX_GROUPNUM][MAX_BMP_STR_LEN];	//记录通道名称字符
static int OSD_Name_Type[DEV_CHANNEL_NUM];
//static unsigned char OSD_Time_Buf[MAX_BMP_STR_LEN]={0};
static unsigned int OSD_off_flag[VO_MAX_DEV_NUM][DEV_CHANNEL_NUM]={{0}};		//OSD是否显示标志
//static unsigned char First_InitFB=0;
unsigned int g_rec_srceen_h[REC_OSD_GROUP][DEV_CHANNEL_NUM];		//录像OSD高度
unsigned int g_rec_srceen_w[REC_OSD_GROUP][DEV_CHANNEL_NUM];		//录像OSD宽度
static unsigned char g_rec_type[DEV_CHANNEL_NUM][NAME_LEN];		//当前录像类型对应图片路径
static unsigned char g_alarm_type[DEV_CHANNEL_NUM][NAME_LEN];		//当前报警类型对应图片路径
static unsigned char g_clickvisual_type[DEV_CHANNEL_NUM][NAME_LEN];		//当前一点即视类型对应图片路径

#endif


static int rectype[DEV_CHANNEL_NUM];		//当前录像类型对应图片路径
static int alarmtype[DEV_CHANNEL_NUM];		//当前报警类型对应图片路径
static int clickvisualtype[DEV_CHANNEL_NUM];

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_getbmp = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_setbmp = PTHREAD_MUTEX_INITIALIZER;

static HI_U32 s_width_unit;
static HI_U32 s_height_unit;
static unsigned char is_init_first=0;		//osd初始化信息
extern VO_DEV s_VoSecondDev;
typedef struct 
{
	HI_U32 name_def_x;//通道名称的默认位置
	HI_U32 name_def_y;
	HI_U32 osd_def_x;//通道名称1的默认位置
	HI_U32 osd_def_y;
	HI_U32 time_def_x;//时间的默认位置
	HI_U32 time_def_y;
}PRV_Def_Chn_Param;
static PRV_Def_Chn_Param osd_def_pos;	
	
typedef struct 
{
	unsigned char devid;
	int w;
	int h;
	unsigned char ch_num;
	enum PreviewMode_enum prv_mode;
	unsigned char ch_order[NINEINDEX];
}PRV_Cur_Param;

#if defined(Hi3520)
PRV_Cur_Param  preview_cur_param[/*VO_MAX_DEV_NUM*/]={
	{
		.devid = HD,
			.w = 1024,
			.h = 768,
			.ch_num = PRV_CHAN_NUM,
			.prv_mode = SixteenScene,
			
	},
	{
			.devid = AD,
				.w = 720-2*PRV_CVBS_EDGE_CUT_W,
				.h = 576-2*PRV_CVBS_EDGE_CUT_H,
				.ch_num = PRV_CHAN_NUM,
				.prv_mode = SixteenScene,
		},
		{
				.devid = SD,
					.w = 720-2*PRV_CVBS_EDGE_CUT_W,
					.h = 576-2*PRV_CVBS_EDGE_CUT_H,
					.ch_num = PRV_CHAN_NUM,
					.prv_mode = SixteenScene,
			}
};
#elif defined(Hi3535)
PRV_Cur_Param  preview_cur_param[/*VO_MAX_DEV_NUM*/]={
	{
		.devid = DHD0,
			.w = 1024,
			.h = 768,
			.ch_num = PRV_CHAN_NUM,
			.prv_mode = SixteenScene,
			
	},
	{
		.devid = DSD0,
			.w = 720-2*PRV_CVBS_EDGE_CUT_W,
			.h = 576-2*PRV_CVBS_EDGE_CUT_H,
			.ch_num = PRV_CHAN_NUM,
			.prv_mode = SixteenScene,
	},
	{
		.devid = DSD0,
			.w = 720-2*PRV_CVBS_EDGE_CUT_W,
			.h = 576-2*PRV_CVBS_EDGE_CUT_H,
			.ch_num = PRV_CHAN_NUM,
			.prv_mode = SixteenScene,
	}			
};

#else
PRV_Cur_Param  preview_cur_param[/*VO_MAX_DEV_NUM*/]={
	{
		.devid = DHD0,
			.w = 1024,
			.h = 768,
			.ch_num = PRV_CHAN_NUM,
			.prv_mode = SixteenScene,
			
	},
	{
			.devid = DSD0,
				.w = 720-2*PRV_CVBS_EDGE_CUT_W,
				.h = 576-2*PRV_CVBS_EDGE_CUT_H,
				.ch_num = PRV_CHAN_NUM,
				.prv_mode = SixteenScene,
		},
		{
				.devid = DSD1,
					.w = 720-2*PRV_CVBS_EDGE_CUT_W,
					.h = 576-2*PRV_CVBS_EDGE_CUT_H,
					.ch_num = PRV_CHAN_NUM,
					.prv_mode = SixteenScene,
			},
	{
				.devid = DSD2,
					.w = 720-2*PRV_CVBS_EDGE_CUT_W,
					.h = 576-2*PRV_CVBS_EDGE_CUT_H,
					.ch_num = PRV_CHAN_NUM,
					.prv_mode = SixteenScene,
			},
			{
				.devid = DSD3,
					.w = 720-2*PRV_CVBS_EDGE_CUT_W,
					.h = 576-2*PRV_CVBS_EDGE_CUT_H,
					.ch_num = PRV_CHAN_NUM,
					.prv_mode = SixteenScene,
			},{
				.devid = DSD4,
					.w = 720-2*PRV_CVBS_EDGE_CUT_W,
					.h = 576-2*PRV_CVBS_EDGE_CUT_H,
					.ch_num = PRV_CHAN_NUM,
					.prv_mode = SixteenScene,
			},
			{
				.devid = DSD5,
					.w = 720-2*PRV_CVBS_EDGE_CUT_W,
					.h = 576-2*PRV_CVBS_EDGE_CUT_H,
					.ch_num = PRV_CHAN_NUM,
					.prv_mode = SixteenScene,
			}
};
#endif
/************************************************************************/
/* 图形层alpha设置。
alpha:0~255,值越小越透明； 成功返回0，失败返回-1
*/
/************************************************************************/
/*static*/ int PRV_SetFbAlpha(int fd, unsigned char alpha)
{
	HIFB_ALPHA_S stAlpha;
	stAlpha.bAlphaChannel = HI_TRUE;
	stAlpha.bAlphaEnable = HI_TRUE;
	stAlpha.u8Alpha0 = 0xff;
	stAlpha.u8Alpha1 = 0xff;
	stAlpha.u8GlobalAlpha = alpha;
	stAlpha.u8Reserved = 0;
	CHECK_RET(ioctl(fd, FBIOPUT_ALPHA_HIFB, &stAlpha));
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%d   %#x **************************************************************************\n", FBIOPUT_ALPHA_HIFB, FBIOPUT_ALPHA_HIFB);
	RET_SUCCESS("");
}
/************************************************************************/
/* 设置图形层colorkey
*/
/************************************************************************/
/*static*/ int PRV_SetFbColorKey(int fd, unsigned int key, unsigned char r, unsigned char g, unsigned char b)
{

#if defined(SN9234H1)
	HIFB_COLORKEY_S stColorKey;
	stColorKey.bKeyEnable = HI_TRUE;
	stColorKey.bMaskEnable = HI_TRUE;
	stColorKey.u32Key = key;
	stColorKey.u8BlueMask = b;
	stColorKey.u8GreenMask = g;
	stColorKey.u8RedMask = r;
	stColorKey.u8Reserved = 0;
#else	
	HIFB_COLORKEY_S stColorKey;
	stColorKey.bKeyEnable = HI_TRUE;
//	stColorKey.bMaskEnable = HI_TRUE;
	stColorKey.u32Key = key;
//	stColorKey.u8BlueMask = b;
//	stColorKey.u8GreenMask = g;
//	stColorKey.u8RedMask = r;
//	stColorKey.u8Reserved = 0;
#endif
	CHECK(ioctl(fd, FBIOPUT_COLORKEY_HIFB, &stColorKey));
	
	RET_SUCCESS("");
}
/************************************************************************/
/* 设置FB图形层位置。
*/
/************************************************************************/
int PRV_SetFbStartXY(int fbfd, int x, int y)
{
	HIFB_POINT_S stPoint;
#if defined(SN9234H1)
	stPoint.u32PosX = x;
	stPoint.u32PosY = y;
#else
	stPoint.s32XPos= x;
	stPoint.s32YPos= y;
#endif	
	CHECK(ioctl(fbfd, FBIOPUT_SCREEN_ORIGIN_HIFB, &stPoint));
	return 0;
}
/************************************************************************/
/* 计算两个GUI分辨率的最小比例单位。
                                                                     */
/************************************************************************/
static HI_S32 PRV_CalcGuiResUnit(HI_VOID)
{
	int m,n,mm,nn;

	if (g_guifd>0 && g1fd>0)
	{
		m=g_fb_stScreen.u32Width;
		n=g_fb_stScreen2.u32Width;
		while(m!=n)
		{
			if(m>n)
				m=m-n;
			else
				n=n-m;
		}
		s_width_unit=g_fb_stScreen.u32Width/m;
		
		mm=g_fb_stScreen.u32Height;
		nn=g_fb_stScreen2.u32Height;
		while(mm!=nn)
		{
			if(mm>nn)
				mm=mm-nn;
			else
				nn=nn-mm;
		}
		s_height_unit=g_fb_stScreen.u32Height/mm;
	
		return 0;
	}
	else
	{
		printf(TEXT_COLOR_RED("gui fd %d, g1 fd %d.\n"), g_guifd, g1fd);
		return -1;
	}
}
#if defined (SN8616D_LE) || defined(SN8616M_LE)|| defined(SN9234H1)
static int PRV_Send_RecOsdTime2Slave(void)
{
    Prv_Slave_Update_Osd_Req *req = NULL;
    int bmp_data_size = 0, offset = 0;
    int i,j;

    for(i=0;i<OSD_TIME_RES;i++)
    {
        for(j=0;j<Rec_OSD_Bmp_Lenth;j++)
        {
            bmp_data_size += Rec_Osd_Str[j].osd_time.str_w_off[i] * Rec_Osd_Str[j].osd_time.str_h_off[i] * PIXBYTE;
        }
    }
    if (NULL != (req = SN_MALLOC(bmp_data_size + sizeof(Rec_Osd_Str) + sizeof(Prv_Slave_Update_Osd_Req))))
    {
        req->chn = 0;
        req->type = 0;
        req->reserve[0] = Rec_OSD_Bmp_Lenth;
        req->size = bmp_data_size + sizeof(Rec_Osd_Str);
        SN_MEMCPY(req->data, req->size, Rec_Osd_Str, sizeof(Rec_Osd_Str), sizeof(Rec_Osd_Str));
        offset += sizeof(Rec_Osd_Str);
    }
    else
    {
        TRACE(SCI_TRACE_HIGH,MOD_PRV,"%s:%d:: SN_MALLOC fail, bmp data size: %d", __FUNCTION__, __LINE__, bmp_data_size);
        return -1;
    }
    for(i=0;i<OSD_TIME_RES;i++)
    {
        for(j=0;j<Rec_OSD_Bmp_Lenth;j++)
        {
            int data_size = Rec_Osd_Str[j].osd_time.str_w_off[i] * Rec_Osd_Str[j].osd_time.str_h_off[i] * PIXBYTE;
            SN_MEMCPY(req->data+offset, req->size-offset, Rec_Osd_Str[j].pData[i], data_size, data_size);
            offset += data_size;
        }
    }
    
    SN_SendMccMessageEx(PRV_SLAVE_1,SUPER_USER_ID,MOD_PRV,MOD_PRV,0,0,MSG_ID_PRV_MCC_UPDATE_OSD_REQ,req,bmp_data_size + sizeof(Rec_Osd_Str) + sizeof(Prv_Slave_Update_Osd_Req) );
    //fprintf(stderr,"%s\n",__FUNCTION__);
    SN_FREE(req);
    return 0;
}
#endif
/*****************************************/
/* chn: 0 ~ PRV_CHAN_NUM-1,从片的通道序号 */
/*****************************************/
static int PRV_Send_RecOsdName2Slave(int chn)
{
    Prv_Slave_Update_Osd_Req *req = NULL;
    int bmp_data_size = 0, offset = 0;
    int i;//,j;
    REGION_CTRL_PARAM_U *p = NULL;

    if (chn >= PRV_CHAN_NUM)
    {
        TRACE(SCI_TRACE_HIGH,MOD_PRV,"bad param: chn = %d", chn);
        return -1;
    }
    for (i=0; i<REC_OSD_GROUP; i++)
    {
        bmp_data_size += name_icon_param_slave[i][chn].stBitmap.u32Width * name_icon_param_slave[i][chn].stBitmap.u32Height * PIXBYTE;
    }
    if (NULL != (req = SN_MALLOC(bmp_data_size + REC_OSD_GROUP*sizeof(REGION_CTRL_PARAM_U) + sizeof(Prv_Slave_Update_Osd_Req))))
    {
        req->chn = chn;
        req->type = 1;
        req->reserve[0] = Rec_OSD_Bmp_Lenth;
        req->size = bmp_data_size + REC_OSD_GROUP*sizeof(REGION_CTRL_PARAM_U);
    }
    else
    {
        TRACE(SCI_TRACE_HIGH,MOD_PRV,"%s:%d:: SN_MALLOC fail, bmp data size: %d", __FUNCTION__, __LINE__, bmp_data_size);
        return -1;
    }
    p = (REGION_CTRL_PARAM_U *)req->data;
    offset += sizeof(REGION_CTRL_PARAM_U) * REC_OSD_GROUP;
    for (i=0; i<REC_OSD_GROUP; i++)
    {
        SN_MEMCPY(p+i, sizeof(REGION_CTRL_PARAM_U), &name_icon_param_slave[i][chn], sizeof(REGION_CTRL_PARAM_U), sizeof(REGION_CTRL_PARAM_U));
    
        int data_size = name_icon_param_slave[i][chn].stBitmap.u32Width * name_icon_param_slave[i][chn].stBitmap.u32Height * PIXBYTE;
        SN_MEMCPY(req->data+offset, req->size-offset, name_icon_param_slave[i][chn].stBitmap.pData, data_size, data_size);
        offset += data_size;
    }
    SN_SendMccMessageEx(PRV_SLAVE_1,SUPER_USER_ID,MOD_PRV,MOD_PRV,0,0,MSG_ID_PRV_MCC_UPDATE_OSD_REQ,req,bmp_data_size + REC_OSD_GROUP*sizeof(REGION_CTRL_PARAM_U) + sizeof(Prv_Slave_Update_Osd_Req) );
    SN_FREE(req);
    return 0;
}

//***********************************************
//OSD_MASK
//
//
//****************************************************
//遮盖区域对比函数
//*****************************************************
static int do_compare(PRM_SCAPE_RECT *pstr0,const PRM_SCAPE_RECT *pstr1)
{
	int x_tmp,y_tmp,w_tmp,h_tmp;
	int x_tmp1,y_tmp1,w_tmp1,h_tmp1;
	x_tmp =  pstr0->left;
	y_tmp =  pstr0->top;
	w_tmp =  pstr0->width;
	h_tmp =  pstr0->height;
	x_tmp1 = pstr1->left;
	y_tmp1 = pstr1->top;
	w_tmp1 = pstr1->width;
	h_tmp1 = pstr1->height;
	TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,=======do_compare ======\n",__LINE__);
	if(x_tmp == x_tmp1)
	{
		if(y_tmp == y_tmp1)
		{
			if(w_tmp == w_tmp1)
			{
				if(h_tmp == h_tmp1)
				{
					return 0;
				}
			}
		}
	}
	return 1;
}
//****************************************************
//遮盖区域更新函数
//*****************************************************

int OSD_Mask_update(unsigned char ch,const PRM_SCAPE_RECT* prect,unsigned char cov_num)
{
	int i=0;
	HI_S32 s32Ret=0;
#if defined(SN9234H1)
	REGION_CRTL_CODE_E enCtrl;
	REGION_CTRL_PARAM_U unParam;
#endif
	const PRM_SCAPE_RECT *p = prect;
	unsigned int screen_h = 0;
	VI_CHN_ATTR_S vi_attr;
	int videv=0;
#if defined(Hi3531)||defined(Hi3535)	
	MPP_CHN_S stChn;
    RGN_CHN_ATTR_S stChnAttr;

    stChn.enModId = HI_ID_GROUP;
    stChn.s32DevId = 0;
    stChn.s32ChnId = ch;
#endif	
	if((g_rec_srceen_h[REC_MAINSTREAM][ch] == SCREEN_4CIF_HEIGHT)||(g_rec_srceen_h[REC_MAINSTREAM][ch] == SCREEN_CIF_HEIGHT) || (g_rec_srceen_h[REC_MAINSTREAM][ch] == SCREEN_QCIF_HEIGHT))
	{
		screen_h = SCREEN_4CIF_HEIGHT;
	}
	else
	{
		screen_h = SCREEN_4CIF_N_HEIGHT;
	}
	if(ch >= PRV_VI_CHN_NUM) /*2011-8-31 by caorh :  修正SN6116主片遮盖配置错误*/
	{
		videv = PRV_656_DEV;
	}
	else
	{
		videv = PRV_656_DEV_1;
	}
#if defined(SN9234H1)	
	if(HI_MPI_VI_GetChnAttr(videv,ch%PRV_VI_CHN_NUM,&vi_attr) == HI_FAILURE)
#endif
	{
		vi_attr.stCapRect.u32Width = 704;
		vi_attr.stCapRect.s32X = 8;
	}
	for(i=0;i<cov_num;i++)
	{
		if((p->width==0)||(p->height== 0))//如果长、宽为	0，那么退出
		{
			if((p->width==Mask_Rect[ch][i].width) || (p->height==Mask_Rect[ch][i].height))
				break;
		}
		if(do_compare(&Mask_Rect[ch][i],p))
		{//比较范围，如果位置不相同，那么更新位置
			TRACE(SCI_TRACE_NORMAL, MOD_PRV,"LINE:%d,=======do OSD_Mask_update== ch = %d, %d,%d,%d,%d  i=%d  handle = %d==\n",__LINE__,ch,p->left,p->top,p->width,p->height,i,cov_handle[ch][i]);
			if((p->width==0)||(p->height== 0))
			{//如果范围为0，那边隐藏区域
#if defined(SN9234H1)
				enCtrl = REGION_HIDE;
				s32Ret = HI_MPI_VPP_ControlRegion(cov_handle[ch][i],enCtrl,&unParam);

#else
				s32Ret = HI_MPI_RGN_GetDisplayAttr(cov_handle[ch][i], &stChn, &stChnAttr);
			    if(HI_SUCCESS != s32Ret)
			    {
			        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"HI_MPI_RGN_GetDisplayAttr (%d)) failed with %#x!\n",\
			               cov_handle[ch][i], s32Ret);
			        return HI_FAILURE;
			    }
				stChnAttr.bShow  = HI_FALSE;
				s32Ret = HI_MPI_RGN_SetDisplayAttr(cov_handle[ch][i],&stChn,&stChnAttr);
#endif				
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,show faild 0x%x!!!\n",__LINE__,s32Ret);
					return HI_FAILURE;
				}
			}
			else
			{
#if defined(SN9234H1)
				enCtrl = REGION_SET_POSTION;
				unParam.stPoint.s32X = p->left*vi_attr.stCapRect.u32Width/SCREEN_DEF_WIDTH+ vi_attr.stCapRect.s32X;//*704/720;//起始位置为采集起始位置加上704下的x位置
				unParam.stPoint.s32Y = p->top*screen_h/SCREEN_DEF_HEIGHT;
				s32Ret = HI_MPI_VPP_ControlRegion(cov_handle[ch][i],enCtrl,&unParam);
#else
				s32Ret = HI_MPI_RGN_GetDisplayAttr(cov_handle[ch][i], &stChn, &stChnAttr);
			    if(HI_SUCCESS != s32Ret)
			    {
			        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"HI_MPI_RGN_GetDisplayAttr (%d)) failed with %#x!\n",\
			               cov_handle[ch][i], s32Ret);
			        return HI_FAILURE;
			    }
				stChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = p->left*vi_attr.stCapRect.u32Width/SCREEN_DEF_WIDTH+ vi_attr.stCapRect.s32X;//*704/720;//起始位置为采集起始位置加上704下的x位置
				stChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = p->top*screen_h/SCREEN_DEF_HEIGHT;
				s32Ret = HI_MPI_RGN_SetDisplayAttr(cov_handle[ch][i],&stChn,&stChnAttr);
#endif				
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,OSD_Mask_update faild 0x%x!!!\n",__LINE__,s32Ret);
					return HI_FAILURE;
				}
#if defined(SN9234H1)
				enCtrl = REGION_SET_SIZE;
				unParam.stDimension.s32Height = p->height*screen_h/SCREEN_DEF_HEIGHT;
				unParam.stDimension.s32Width = p->width;//*704/720;
				s32Ret = HI_MPI_VPP_ControlRegion(cov_handle[ch][i],enCtrl,&unParam);
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,OSD_Mask_update faild 0x%x!!!\n",__LINE__,s32Ret);
					return HI_FAILURE;
				}
#endif
			}
			Mask_Rect[ch][i] = *p;
		}
		p++;
	}
	return 0;
}
#if defined(SN9234H1)
//****************************************************
//N\P切换时遮盖区域更新函数
//*****************************************************
int OSD_Mask_NPupdate(unsigned char ch)
{
	int i=0;
	HI_S32 s32Ret=0;
	REGION_CRTL_CODE_E enCtrl;
	REGION_CTRL_PARAM_U unParam;
	const PRM_SCAPE_RECT *p = Mask_Rect[ch];
	unsigned int screen_h = 0;
	VI_CHN_ATTR_S vi_attr;

	if((g_rec_srceen_h[REC_MAINSTREAM][ch] == SCREEN_4CIF_HEIGHT)||(g_rec_srceen_h[REC_MAINSTREAM][ch] == SCREEN_CIF_HEIGHT) || (g_rec_srceen_h[REC_MAINSTREAM][ch] == SCREEN_QCIF_HEIGHT))
	{
		screen_h = SCREEN_4CIF_HEIGHT;
	}
	else
	{
		screen_h = SCREEN_4CIF_N_HEIGHT;
	}
	if(HI_MPI_VI_GetChnAttr(ch/PRV_VI_CHN_NUM,ch%PRV_VI_CHN_NUM,&vi_attr) == HI_FAILURE)
	{
		vi_attr.stCapRect.u32Width = 704;
		vi_attr.stCapRect.s32X = 8;
	}
	for(i=0;i<COVER_MAX_SIZE;i++)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"LINE:%d,=======do OSD_Mask_update== ch = %d, %d,%d,%d,%d  i=%d  handle = %d==\n",__LINE__,ch,p->left,p->top,p->width,p->height,i,cov_handle[ch][i]);
		if((p->width==0)||(p->height== 0))//如果长、宽为0，那么退出
		{
			break;
		}
		
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV,"OSD_Mask_NPupdate\n");
			{
				enCtrl = REGION_SET_POSTION;
				unParam.stPoint.s32X = p->left*vi_attr.stCapRect.u32Width/SCREEN_DEF_WIDTH + vi_attr.stCapRect.s32X;//*704/720;//起始位置为采集起始位置加上704下的x位置
				unParam.stPoint.s32Y = p->top*screen_h/SCREEN_DEF_HEIGHT;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV,"LINE:%d,=======do OSD_Mask_update== ch = %d, %d,%d  i=%d  handle = %d==\n",__LINE__,ch,unParam.stPoint.s32X,unParam.stPoint.s32Y,i,cov_handle[ch][i]);
				s32Ret = HI_MPI_VPP_ControlRegion(cov_handle[ch][i],enCtrl,&unParam);
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,OSD_Mask_update faild 0x%x!!!\n",__LINE__,s32Ret);
					return HI_FAILURE;
				}
				
				enCtrl = REGION_SET_SIZE;
				unParam.stDimension.s32Height = p->height*screen_h/SCREEN_DEF_HEIGHT;
				unParam.stDimension.s32Width = p->width;//*704/720;
				s32Ret = HI_MPI_VPP_ControlRegion(cov_handle[ch][i],enCtrl,&unParam);
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,OSD_Mask_update faild 0x%x!!!\n",__LINE__,s32Ret);
					return HI_FAILURE;
				}
			}
		}
		p++;
	}
	return 0;
}
#endif

#if defined(SN9234H1)
//****************************************************
//遮盖区域控制显示、隐藏函数
//*****************************************************

int OSD_Mask_Ctl(unsigned char ch,unsigned char on)
{
	HI_S32 i=0,s32Ret=0,flag=0;
	REGION_CRTL_CODE_E enCtrl;
	REGION_CTRL_PARAM_U unParam;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV,"LINE:%d,=======do OSD_Mask_Ctl==  %d===\n",__LINE__,on);
	if(on)
	{
		enCtrl = REGION_SHOW;
		flag= 1;
	}
	else
	{
		enCtrl = REGION_HIDE;
		flag =0;
	}
	for(i=0;i<COVER_MAX_SIZE;i++)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"LINE:%d,=======do OSD_Mask_Ctl==  %d= %d==\n",__LINE__,Mask_Rect[ch][i].width,Mask_Rect[ch][i].height);
		if((Mask_Rect[ch][i].width != 0)&&(Mask_Rect[ch][i].height!=0))
		{
			
			s32Ret = HI_MPI_VPP_ControlRegion(cov_handle[ch][i],enCtrl,&unParam);
			if(s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,show faild 0x%x!!!\n",__LINE__,s32Ret);
				return HI_FAILURE;
			}
			cov_flag[ch][i] = flag;
		}else
		{
			cov_flag[ch][i] = 0;
		}
	}
	return 0;
}
//****************************************************
//在配置遮盖区域时的控制显示、隐藏函数
//*****************************************************

int OSD_Mask_disp_Ctl(unsigned char ch,unsigned char on)
{
	HI_S32 i=0,s32Ret=0;
	REGION_CRTL_CODE_E enCtrl;
	REGION_CTRL_PARAM_U unParam;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV,"LINE:%d,=======do OSD_Mask_disp_Ctl==  %d===\n",__LINE__,on);
	if(on)
	{
		for(i=0;i<COVER_MAX_SIZE;i++)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV,"LINE:%d,=======do OSD_Mask_disp_Ctl==  %d= %d==\n",__LINE__,Mask_Rect[ch][i].width,Mask_Rect[ch][i].height);
			if((Mask_Rect[ch][i].width != 0)&&(Mask_Rect[ch][i].height!=0))
			{
				if(cov_flag[ch][i])
				{
					enCtrl = REGION_SHOW;
				}
				else
				{
					enCtrl = REGION_HIDE;
				}
				s32Ret = HI_MPI_VPP_ControlRegion(cov_handle[ch][i],enCtrl,&unParam);
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH, MOD_PRV,"LINE:%d,HI_MPI_VPP_ControlRegion show faild 0x%x!!!\n",__LINE__,s32Ret);
					return HI_FAILURE;
				}
			}
			
		}
	}
	else
	{
		enCtrl = REGION_HIDE;
		for(i=0;i<COVER_MAX_SIZE;i++)
		{
			if((Mask_Rect[ch][i].width != 0)&&(Mask_Rect[ch][i].height!=0))
			{
				s32Ret = HI_MPI_VPP_ControlRegion(cov_handle[ch][i],enCtrl,&unParam);
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,show faild 0x%x!!!\n",__LINE__,s32Ret);
					return HI_FAILURE;
				}
			}
		}
	}
	return 0;
}
//****************************************************
//遮盖单通道初始化函数
//*****************************************************

int OSD_Mask_Ch_init(unsigned char ch)
{
	HI_S32 j=0;
	HI_S32 s32Ret;
	
	REGION_ATTR_S stRgnAttr;
	REGION_HANDLE handle;
	for(j=0;j<COVER_MAX_SIZE;j++)
	{
		stRgnAttr.enType = COVER_REGION;
		stRgnAttr.unAttr.stCover.bIsPublic = HI_FALSE;
		stRgnAttr.unAttr.stCover.u32Color  = 0;
		stRgnAttr.unAttr.stCover.u32Layer  = j+1;
		stRgnAttr.unAttr.stCover.stRect.s32X = 0;
		stRgnAttr.unAttr.stCover.stRect.s32Y = 0;
		stRgnAttr.unAttr.stCover.stRect.u32Height = 10;
		stRgnAttr.unAttr.stCover.stRect.u32Width  = 10;
		stRgnAttr.unAttr.stCover.ViChn = ch%4;
#if defined(SN6116HE)||defined(SN6116LE) || defined(SN6108HE)||defined(SN6108LE) || defined(SN8608D_LE) || defined(SN8608M_LE) || defined(SN8616D_LE) || defined(SN8616M_LE)|| defined(SN9234H1)
		if(ch< PRV_VI_CHN_NUM)
		{
			stRgnAttr.unAttr.stCover.ViDevId = PRV_656_DEV_1;
		}
		else
		{
			stRgnAttr.unAttr.stCover.ViDevId = PRV_656_DEV;
		}
#else
		stRgnAttr.unAttr.stCover.ViDevId = ch/4;
#endif

		/*create region*/
		s32Ret = HI_MPI_VPP_CreateRegion(&stRgnAttr, &handle);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,HI_MPI_VPP_CreateRegion err 0x%x\n",__LINE__,s32Ret);
			return HI_FAILURE;
		}
		cov_handle[ch][j] = handle;
		cov_flag[ch][j] = 0;
		Mask_Rect[ch][j].left = 0;
		Mask_Rect[ch][j].top = 0;
		Mask_Rect[ch][j].width= 0;
		Mask_Rect[ch][j].height= 0;
	}
	return 0;
}

#else
//****************************************************
//遮盖区域控制显示、隐藏函数
//*****************************************************

int OSD_Mask_Ctl(unsigned char ch,unsigned char on)
{
	HI_S32 i=0,s32Ret=0,flag=0;

	 MPP_CHN_S stChn;
    RGN_CHN_ATTR_S stChnAttr;

    stChn.enModId = HI_ID_GROUP;
    stChn.s32DevId = 0;
    stChn.s32ChnId = ch;
	for(i=0;i<COVER_MAX_SIZE;i++)
	{
		s32Ret = HI_MPI_RGN_GetDisplayAttr(cov_handle[ch][i], &stChn, &stChnAttr);
	    if(HI_SUCCESS != s32Ret)
	    {
	        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"HI_MPI_RGN_GetDisplayAttr (%d)) failed with %#x!\n",\
	               cov_handle[ch][i], s32Ret);
	        return HI_FAILURE;
	    }
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"LINE:%d,=======do OSD_Mask_Ctl==  %d===\n",__LINE__,on);
		if(on)
		{
			stChnAttr.bShow = HI_TRUE;
			flag= 1;
		}
		else
		{
			stChnAttr.bShow  = HI_FALSE;
			flag =0;
		}
		
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"LINE:%d,=======do OSD_Mask_Ctl==  %d= %d==\n",__LINE__,Mask_Rect[ch][i].width,Mask_Rect[ch][i].height);
		if((Mask_Rect[ch][i].width != 0)&&(Mask_Rect[ch][i].height!=0))
		{
			s32Ret = HI_MPI_RGN_SetDisplayAttr(cov_handle[ch][i],&stChn,&stChnAttr);
			if(s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,show faild 0x%x!!!\n",__LINE__,s32Ret);
				return HI_FAILURE;
			}
			cov_flag[ch][i] = flag;
		}else
		{
			cov_flag[ch][i] = 0;
		}
	}
	return 0;
}
//****************************************************
//在配置遮盖区域时的控制显示、隐藏函数
//*****************************************************

int OSD_Mask_disp_Ctl(unsigned char ch,unsigned char on)
{
	HI_S32 i=0;
	MPP_CHN_S stChn;
    RGN_CHN_ATTR_S stChnAttr;
    HI_S32 s32Ret;

    stChn.enModId = HI_ID_GROUP;
    stChn.s32DevId = 0;
    stChn.s32ChnId = ch;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV,"LINE:%d,=======do OSD_Mask_disp_Ctl==  %d===\n",__LINE__,on);
	for(i=0;i<COVER_MAX_SIZE;i++)
	{	
		s32Ret = HI_MPI_RGN_GetDisplayAttr(cov_handle[ch][i], &stChn, &stChnAttr);
	    if(HI_SUCCESS != s32Ret)
	    {
	        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"HI_MPI_RGN_GetDisplayAttr (%d)) failed with %#x!\n",\
	               cov_handle[ch][i], s32Ret);
	        return HI_FAILURE;
	    }
		if(on)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV,"LINE:%d,=======do OSD_Mask_disp_Ctl==  %d= %d==\n",__LINE__,Mask_Rect[ch][i].width,Mask_Rect[ch][i].height);
			if((Mask_Rect[ch][i].width != 0)&&(Mask_Rect[ch][i].height!=0))
			{
				if(cov_flag[ch][i])
				{
					stChnAttr.bShow = HI_TRUE;
				}
				else
				{
					stChnAttr.bShow = HI_FALSE;
				}
				s32Ret = HI_MPI_RGN_SetDisplayAttr(cov_handle[ch][i],&stChn,&stChnAttr);
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,show faild 0x%x!!!\n",__LINE__,s32Ret);
					return HI_FAILURE;
				}
			}

		}
		else
		{
			stChnAttr.bShow = HI_FALSE;
			if((Mask_Rect[ch][i].width != 0)&&(Mask_Rect[ch][i].height!=0))
			{
				s32Ret = HI_MPI_RGN_SetDisplayAttr(cov_handle[ch][i],&stChn,&stChnAttr);
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,show faild 0x%x!!!\n",__LINE__,s32Ret);
					return HI_FAILURE;
				}
			}
		}
	}
	return 0;
}
//****************************************************
//遮盖单通道初始化函数
//*****************************************************

int OSD_Mask_Ch_init(unsigned char ch)
{
	HI_S32 j=0;
	HI_S32 s32Ret;
	
	HI_S32 handle;
	RGN_ATTR_S stRgnAttr;
	MPP_CHN_S stChn;
    RGN_CHN_ATTR_S stCoverChnAttr;
	for(j=0;j<COVER_MAX_SIZE;j++)
	{
		handle = COVER_MAX_SIZE*ch + j+1;
		stRgnAttr.enType = COVER_RGN;
        stRgnAttr.unAttr.stOverlay.enPixelFmt = PIXEL_FORMAT_RGB_1555;
		stRgnAttr.unAttr.stOverlay.stSize.u32Width  = 10;
        stRgnAttr.unAttr.stOverlay.stSize.u32Height = 10;
		stRgnAttr.unAttr.stOverlay.u32BgColor = 0x00;

		/*create region*/
	//	printf("===============handle:%d\n",handle);
		 s32Ret = HI_MPI_RGN_Create(handle, &stRgnAttr);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,HI_MPI_RGN_Create err 0x%x\n",__LINE__,s32Ret);
			return HI_FAILURE;
		}
		stChn.enModId = HI_ID_VIU;
        stChn.s32DevId = 0;
        stChn.s32ChnId = ch;
        
        memset(&stCoverChnAttr,0,sizeof(stCoverChnAttr));
        stCoverChnAttr.bShow = HI_TRUE;
        stCoverChnAttr.enType = COVER_RGN;
        stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X = 0;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y = 0;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width = 160;
		stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height = 160;
		stCoverChnAttr.unChnAttr.stCoverChn.u32Color = 0xff;
		stCoverChnAttr.unChnAttr.stCoverChn.u32Layer = 0; 

        s32Ret = HI_MPI_RGN_AttachToChn(handle, &stChn, &stCoverChnAttr);
        if(HI_SUCCESS != s32Ret)
        {
            printf("HI_MPI_RGN_AttachToChn (%d) failed with %#x!\n",\
                   handle, s32Ret);
            return HI_FAILURE;
        }
		cov_handle[ch][j] = handle;
		cov_flag[ch][j] = 0;
		Mask_Rect[ch][j].left = 0;
		Mask_Rect[ch][j].top = 0;
		Mask_Rect[ch][j].width= 0;
		Mask_Rect[ch][j].height= 0;
	}
	return 0;
}
#endif
//****************************************************
//遮盖初始化函数
//*****************************************************

int OSD_Mask_init(PPRV_VO_SLAVE_STAT_S pslave)
{
	int i=0,ret=0;
	int w=0,h=0,sub_w=0,sub_h=0,np_flag=0;
	PRM_GENERAL_CFG_BASIC stGeneral;
	PRM_RCD_CFG_BASIC stRCD;
	PRM_Net_COMPRESSION_INFO stNetRcd;
	if(SN_RET_OK == GetParameter(PRM_ID_GENERAL_CFG_BASIC, NULL, &stGeneral, sizeof(PRM_GENERAL_CFG_BASIC),  1 , SUPER_USER_ID, NULL))
	{
		np_flag = (0 == stGeneral.CVBSOutputType) ? VIDEO_ENCODING_MODE_NTSC : VIDEO_ENCODING_MODE_PAL;
	}
	for(i=0;i<DEV_CHANNEL_NUM;i++)
	{
		if(SN_RET_OK == GetParameter(PRM_ID_RCD_CFG_BASIC, NULL, &stRCD, sizeof(PRM_RCD_CFG_BASIC),  i+1 , SUPER_USER_ID, NULL))
		{
			switch(stRCD.Resolution)
			{
				case _4CIF:
				{
					w= SCREEN_4CIF_WIDTH;
					h= (VIDEO_ENCODING_MODE_PAL == np_flag)?SCREEN_4CIF_HEIGHT: SCREEN_4CIF_N_HEIGHT;
				}
					break;
				case _DCIF:
				{
					w= SCREEN_D1_WIDTH;
					h= (VIDEO_ENCODING_MODE_PAL == np_flag)?SCREEN_4CIF_HEIGHT: SCREEN_4CIF_N_HEIGHT;
				}	
					break;
				case _2CIF:
				{
					w= SCREEN_4CIF_WIDTH;
					h= (VIDEO_ENCODING_MODE_PAL == np_flag)?SCREEN_CIF_HEIGHT: SCREEN_CIF_N_HEIGHT;
				}		
					break;
				case _CIF:
				{
					w= SCREEN_CIF_WIDTH;
					h= (VIDEO_ENCODING_MODE_PAL == np_flag)?SCREEN_CIF_HEIGHT: SCREEN_CIF_N_HEIGHT;
				}		
					break;
				case _QCIF:
				{
					w= SCREEN_QCIF_WIDTH;
					h= (VIDEO_ENCODING_MODE_PAL == np_flag)?SCREEN_QCIF_HEIGHT: SCREEN_QCIF_N_HEIGHT;
				}		
					break;
				default:
				{
					w = SCREEN_CIF_WIDTH;
					h = (VIDEO_ENCODING_MODE_PAL == np_flag)?SCREEN_CIF_HEIGHT: SCREEN_CIF_N_HEIGHT;
				}	
					break;
			}
		}
		else
		{
			w= SCREEN_CIF_WIDTH;
			h= (VIDEO_ENCODING_MODE_PAL == np_flag)?SCREEN_CIF_HEIGHT: SCREEN_CIF_N_HEIGHT;
		}
		if(SN_RET_OK == GetParameter(PRM_ID_NET_COMPRESSION_INFO, NULL, &stNetRcd, sizeof(PRM_Net_COMPRESSION_INFO),  i+1 , SUPER_USER_ID, NULL))
		{
			switch(stNetRcd.byResolution)
			{
				case _4CIF:
				{
					sub_w = SCREEN_4CIF_WIDTH;
					sub_h = (0 == np_flag)?SCREEN_4CIF_HEIGHT: SCREEN_4CIF_N_HEIGHT;
				}
					break;
				case _DCIF:
				{
					sub_w = SCREEN_D1_WIDTH;
					sub_h = (0 == np_flag)?SCREEN_4CIF_HEIGHT: SCREEN_4CIF_N_HEIGHT;
				}	
					break;
				case _2CIF:
				{
					sub_w = SCREEN_D1_WIDTH;
					sub_h = (0 == np_flag)?SCREEN_CIF_HEIGHT: SCREEN_CIF_N_HEIGHT;
				}		
					break;
				case _CIF:
				{
					sub_w = SCREEN_CIF_WIDTH;
					sub_h = (0 == np_flag)?SCREEN_CIF_HEIGHT: SCREEN_CIF_N_HEIGHT;
				}		
					break;
				case _QCIF:
				{
					sub_w = SCREEN_QCIF_WIDTH;
					sub_h = (0 == np_flag)?SCREEN_QCIF_HEIGHT: SCREEN_QCIF_N_HEIGHT;
				}		
					break;
				default:
				{
					sub_w = SCREEN_CIF_WIDTH;
					sub_h = (0 == np_flag)?SCREEN_CIF_HEIGHT: SCREEN_CIF_N_HEIGHT;
				}	
					break;
			}
		}
		else
		{
			sub_w = SCREEN_CIF_WIDTH;
			sub_h = (0 == np_flag)?SCREEN_CIF_HEIGHT: SCREEN_CIF_N_HEIGHT;
		}
		if(i<PRV_CHAN_NUM)
		{//主片遮盖初始化
		#if defined(Hi3535)
			//#warning ("TO DO OSD_Mask_Ch_init")
		#else
			ret = OSD_Mask_Ch_init(i);
		#endif
			g_rec_srceen_w[REC_MAINSTREAM][i] = w;		//主码流
			g_rec_srceen_h[REC_MAINSTREAM][i] = h;		//主码流
			//g_rec_srceen_w[REC_JPEG][i] = w;		//主码流
			//g_rec_srceen_h[REC_JPEG][i] = h;		//主码流
			g_rec_srceen_w[REC_SUBSTREAM][i] = SCREEN_4CIF_WIDTH;//sub_w;		//次码流
			g_rec_srceen_h[REC_SUBSTREAM][i] = (0 == np_flag)?SCREEN_4CIF_HEIGHT: SCREEN_4CIF_N_HEIGHT;//sub_h;		//次码流
		}
		else
		{//从片还没有修改，后面需要进行修改
			pslave->f_rec_srceen_w[REC_MAINSTREAM][i-PRV_CHAN_NUM] = w;
			pslave->f_rec_srceen_h[REC_MAINSTREAM][i-PRV_CHAN_NUM] = h;
			pslave->f_rec_srceen_w[REC_SUBSTREAM][i-PRV_CHAN_NUM] = SCREEN_4CIF_WIDTH;//sub_w;		//JPEG抓图，固定为cif
			pslave->f_rec_srceen_h[REC_SUBSTREAM][i-PRV_CHAN_NUM] = (0 == np_flag)?SCREEN_4CIF_HEIGHT: SCREEN_4CIF_N_HEIGHT;//sub_h;		//JPEG抓图，固定为cif
		}
	}	
	TRACE(SCI_TRACE_NORMAL,MOD_PRV,"LINE:%d,OSD_Mask_init suc 0x%x\n",__LINE__,ret);
	return ret;
}
//****************************************************
//遮盖单通道关闭函数
//*****************************************************

int OSD_Mask_Ch_Close(unsigned char ch)
{
	HI_S32 j=0;
	HI_S32 s32Ret;
	
	for(j=0;j<COVER_MAX_SIZE;j++)
	{
#if defined(SN9234H1)
		s32Ret = HI_MPI_VPP_DestroyRegion(cov_handle[ch][j]);
#else
		s32Ret = HI_MPI_RGN_Destroy(cov_handle[ch][j]);
#endif
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,HI_MPI_VPP_DestroyRegion err 0x%x\n",__LINE__,s32Ret);
			return HI_FAILURE;
		}
		cov_handle[ch][j] = 0;
	}
	return 0;
}
//****************************************************
//遮盖关闭函数
//*****************************************************

int OSD_Mask_Close(void)
{
	int i=0,ret=0;
	for(i=0;i<PRV_CHAN_NUM;i++)
	{
		ret = OSD_Mask_Ch_Close(i);
		if(ret == HI_FAILURE)
		{
			break;
		}
	}
	return ret;
}

//----------------------------------------------
//录像部分OSD字符显示接口函数
//注意录像部分OSD区域不能重叠
//
//
/*************************************************
Function: //OSD_GetType_Index
Description: //获取OSD图标类型对应图标层
Calls: 
Called By: //
Input: // // msg_req:通用配置消息
			rsp : 回复给GUI的消息
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
static int OSD_GetType_Index(unsigned char ch,unsigned char osd_type,unsigned char *ptype)
{
	switch(osd_type)
	{
		case OSD_ALARM_TYPE:	//视频丢失报警
			SN_MEMCPY(g_alarm_type[ch],sizeof(ALARM_BITS_PATH),ALARM_BITS_PATH,sizeof(ALARM_BITS_PATH),sizeof(ALARM_BITS_PATH));
			*ptype = OSD_ALARM_LAYER;	//返回报警图标层
			alarmtype[ch] = osd_type;
			break;
		case OSD_ALARM_MD_TYPE:	//移动侦测报警
			SN_MEMCPY(g_alarm_type[ch],sizeof(ALARM_MD_BITS_PATH),ALARM_MD_BITS_PATH,sizeof(ALARM_MD_BITS_PATH),sizeof(ALARM_MD_BITS_PATH));
			*ptype = OSD_ALARM_LAYER;	//返回报警图标层
			alarmtype[ch] = osd_type;
			break;
		case OSD_REC_ALARM_TYPE:	//报警录像
			SN_MEMCPY(g_rec_type[ch],sizeof(ALARM_REC_BITS_PATH),ALARM_REC_BITS_PATH,sizeof(ALARM_REC_BITS_PATH),sizeof(ALARM_REC_BITS_PATH));
			*ptype = OSD_REC_LAYER;	//返回报警图标层
			rectype[ch] = osd_type;
			break;
		case OSD_REC_MANUAL_TYPE:	//手动录像
			SN_MEMCPY(g_rec_type[ch],sizeof(MANUAL_REC_BITS_PATH),MANUAL_REC_BITS_PATH,sizeof(MANUAL_REC_BITS_PATH),sizeof(MANUAL_REC_BITS_PATH));
			*ptype = OSD_REC_LAYER;	//返回报警图标层
			rectype[ch] = osd_type;
			break;
		case OSD_REC_TYPE:			//定时录像
			SN_MEMCPY(g_rec_type[ch],sizeof(TIME_REC_BITS_PATH),TIME_REC_BITS_PATH,sizeof(TIME_REC_BITS_PATH),sizeof(TIME_REC_BITS_PATH));
			*ptype = OSD_REC_LAYER;	//返回录像图标层
			rectype[ch] = osd_type;
			break;
		case OSD_TIME_TYPE:		//时间
			*ptype = OSD_TIME_LAYER;	//返回时间图标层
			break;
		case OSD_NAME_TYPE:		//通道名称
			*ptype = OSD_NAME_LAYER;	//返回通道名称图标层
			break;
		case OSD_CLICKVISUAL_TYPE:
			SN_MEMCPY(g_clickvisual_type[ch],sizeof(CLICKVISUAL_BITS_PATH),CLICKVISUAL_BITS_PATH,sizeof(CLICKVISUAL_BITS_PATH),sizeof(CLICKVISUAL_BITS_PATH));
			*ptype = OSD_CLICKVISUAL_LAYER;	//返回报警图标层
			clickvisualtype[ch] = osd_type;
			break;
		default://错误类型
			return	HI_FAILURE;
	}
	return 0;
}

//****************************************************
//录像部分OSD图片转换函数，直接由BMP图片转换成对应格式
//*****************************************************
#if 0
static HI_S32 Set_BmpByFile(const char *filename, REGION_CTRL_PARAM_U *pParam)
{
	OSD_SURFACE_S Surface;
	OSD_BITMAPFILEHEADER bmpFileHeader;
	OSD_BITMAPINFO bmpInfo;
	
	if(GetBmpInfo(filename,&bmpFileHeader,&bmpInfo) < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,GetBmpInfo err!\n",__LINE__);
		return HI_FAILURE;
	}
	
	Surface.enColorFmt = OSD_COLOR_FMT_RGB1555;
	
	pParam->stBitmap.pData = SN_MALLOC(2*(bmpInfo.bmiHeader.biWidth)*(bmpInfo.bmiHeader.biHeight));
	
	if(NULL == pParam->stBitmap.pData)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,SN_MALLOC osd memroy err!\n",__LINE__);
		return HI_FAILURE;
	}
	
	CreateSurfaceByBitMap(filename,&Surface,(HI_U8*)(pParam->stBitmap.pData));
	
	pParam->stBitmap.u32Width = Surface.u16Width;
	pParam->stBitmap.u32Height = Surface.u16Height;
	pParam->stBitmap.enPixelFormat = PIXEL_FORMAT_RGB_1555;
	return HI_SUCCESS;
}
#endif
//****************************************************
//录像部分OSD图片转换函数，由内存数据转换成对应格式
//*****************************************************

static HI_S32 Set_BmpByMem(STRING_BMP_ST *pBmpData, REGION_CTRL_PARAM_U *pParam)
{
	if(pParam == NULL || pBmpData->pBmpData == NULL || pBmpData == NULL)
	{
		return HI_FAILURE;
	}
    /*if (pParam->stBitmap.pData && pParam->stBitmap.pData != pBmpData->pBmpData)
    {
        SN_FREE(pParam->stBitmap.pData);
    }*/
	pParam->stBitmap.pData = (HI_VOID *)pBmpData->pBmpData;
	pParam->stBitmap.u32Width = pBmpData->width;
	pParam->stBitmap.u32Height = pBmpData->height;
	pParam->stBitmap.enPixelFormat = PIXEL_FORMAT_RGB_1555;
	//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,u32Width = %d,u32Height = %d\n",__LINE__,pParam->stBitmap.u32Width, pParam->stBitmap.u32Height);
	return HI_SUCCESS;
}
//****************************************************
//录像部分OSD，命令控制函数
//*****************************************************
#if defined(SN9234H1)
static HI_S32 Set_Vpp_ControlBMP(REGION_HANDLE handle, REGION_CTRL_PARAM_U *pParam)
{
	REGION_CRTL_CODE_E enCtrl;
	HI_S32 s32Ret;
	
	if(pParam == NULL)
	{
		return HI_FAILURE;
	}
	enCtrl = REGION_SET_BITMAP;
	s32Ret = HI_MPI_VPP_ControlRegion(handle, enCtrl, pParam);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,HI_MPI_VPP_ControlRegion 0x%x!\n",__LINE__,s32Ret);

		return HI_FAILURE;
	}
	return HI_SUCCESS;
}
#endif
/*************************************************
Function: //OSD_Time_Str_icon_idx
Description: //查找当前时间数组中相同的图片信息
Calls: 
Called By: //
Input: // chidx :返回的通道ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int OSD_Time_Str_BmpData_idx(unsigned char rec_group,unsigned char ch,unsigned char *icon_idx)
{
	int idx=0;
	int w;
	
	if(ch < PRV_CHAN_NUM)
	{
		w = g_rec_srceen_w[rec_group][ch];
	}
	else
	{
		w = s_slaveVoStat.f_rec_srceen_w[rec_group][ch%PRV_CHAN_NUM];
	}
    
	switch(w)
	{
		case SCREEN_D1_WIDTH:
		case SCREEN_4CIF_WIDTH:
			idx = 0;
			break;
		case SCREEN_CIF_WIDTH:
			idx = 1;
			break;
		case SCREEN_QCIF_WIDTH:
			idx = 2;
			break;
		default:
			idx  = 1;
			break;
	}
	*icon_idx = idx;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############w=%d,h=%d,idx=%d################\n",g_rec_srceen_h[rec_group][ch],g_rec_srceen_w[rec_group][ch],idx);
	return 0;
}
#if 0
/*************************************************
Function: //OSD_Time_Str_BmpData_Width
Description: //查找当前字符的最大宽度、高度
Calls: 
Called By: //
Input: // pStr : 字符
		font:字体
		pWidth:返回的宽度
		pHeight:返回的高度
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int OSD_Time_Str_BmpData_Width(unsigned char *pStr ,int font,int *pWidth,int *pHeight)
{
	int w=0,h=0;
	switch(font)
	{
		case MMI_FONT_12:
		{
			if(pStr[0] & 0x80)
			{//如果为汉字
				w = HANZI_MAX_12_W;
			}
			else
			{
				w = ZIMU_MAX_12_W;
			}
			h = MAX_12_H;
		}
			break;
		case MMI_FONT_16:
		{
			if(pStr[0] & 0x80)
			{//如果为汉字
				w = HANZI_MAX_16_W;
			}
			else
			{
				w = ZIMU_MAX_16_W;
			}
			h = MAX_16_H;
		}
			break;	
		case MMI_FONT_20:
			break;	
		case MMI_FONT_24:
			break;	
		case MMI_FONT_28:
		{
			if(pStr[0] & 0x80)
			{//如果为汉字
				w = HANZI_MAX_28_W;
			}
			else
			{
				w = ZIMU_MAX_28_W;
			}
			h = MAX_28_H;
		}
			break;	
		case MMI_FONT_32:
		case MMI_FONT_36:
		case MMI_FONT_40:
			break;
		default:
		{
			if(pStr[0] & 0x80)
			{//如果为汉字
				w = HANZI_MAX_16_W;
			}
			else
			{
				w = ZIMU_MAX_16_W;
			}
			h = MAX_16_H;
		}
			break;
	}
	*pWidth = w;
	*pHeight = h;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############w=%d,h=%d,idx=%d################\n",g_rec_srceen_h[rec_group][ch],g_rec_srceen_w[rec_group][ch],idx);
	return 0;
}
#endif
//****************************************************
//OSD，从GUI获取图片函数
//*****************************************************
static int Get_TimeBmp_From_Gui(const unsigned char *pstr,STRING_BMP_ST *pstStrBmp, int font)
{
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "##########font = %d  ,s_as32FontH[font] = %d#############\n",font,s_as32FontH[font]);
	if (font<0 || font>=sizeof(s_as32FontH)/sizeof(s_as32FontH[0]))
	{
		printf(TEXT_COLOR_RED("invalid font: %d, use default font 0\n"), font);
		font = 0;//default font
	}

	SN_STRNCPY((char *)pstStrBmp->Str, MAX_BMP_STR_LEN, (char *)pstr, MAX_BMP_STR_LEN);
	pstStrBmp->Str[MAX_BMP_STR_LEN - 1] = 0; 
	pstStrBmp->nStrlen = SN_STRLEN(pstStrBmp->Str);
	pstStrBmp->x = OSD_X_OFFSET;
	pstStrBmp->y = OSD_Y_OFFSET;
	//pstStrBmp->width = MAX_STRBMP_WIDTH;
	pstStrBmp->width = pstStrBmp->nStrlen * s_as32FontH[font] + pstStrBmp->x * 2;
	pstStrBmp->width = (pstStrBmp->width > MAX_STRTIMEBMP_WIDTH)? MAX_STRTIMEBMP_WIDTH : pstStrBmp->width;
	
	pstStrBmp->height = s_as32FontH[font] + 2*pstStrBmp->y;//font_h;
	pstStrBmp->pixelbyte = PIXBYTE;
	pstStrBmp->DataSize = pstStrBmp->width	* pstStrBmp->height * pstStrBmp->pixelbyte;
	pstStrBmp->font= s_as32FontH[font];
	
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "##########pstStrBmp->font = %d  ,s_as32FontH[font] = %d ,pstStrBmp->Str =%s#############\n",pstStrBmp->font,s_as32FontH[font],pstStrBmp->Str);
	pstStrBmp->BGColor.Aval = 0x00;
	pstStrBmp->BGColor.Rval = 0x00;
	pstStrBmp->BGColor.Gval= 0x00;
	pstStrBmp->BGColor.Bval = 0x00;
	
	pstStrBmp->TxtColor.Aval = 0xff;
	pstStrBmp->TxtColor.Rval = 0xff;
	pstStrBmp->TxtColor.Gval= 0xff;
	pstStrBmp->TxtColor.Bval = 0xff;
	//pstStrBmp->pBmpData = pParam->stBitmap.pData;//SN_MALLOC(pstStrBmp->DataSize);	//初始化固定申请的内存，专门用来存放这些图片的	
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "##########pstStrBmp->pBmpData = %x #############\n",pstStrBmp->pBmpData );
	if(pstStrBmp->pBmpData == NULL)
	{
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "##########pstr = %s  ,s_as32FontH[font] = %d #############\n",pstr,s_as32FontH[font]);
		pstStrBmp->pBmpData = SN_MALLOC(pstStrBmp->DataSize);//初始化固定申请的内存，专门用来存放这些图片的
		if(pstStrBmp->pBmpData == NULL)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"SN_MALLOC fail!\n");
			return -1;
		}
	}
	if (g_guifd <= 0)
	{
		printf(TEXT_COLOR_PURPLE("%s: gui not ready!"), __FUNCTION__);
		SN_MEMSET(pstStrBmp->pBmpData,FB_BG_COLOR,pstStrBmp->DataSize);
		return 0;
	}
	else
	{
        int retry_cnt = 5;
        int ret = 0;
		
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "##########pstStrBmp->font = %d	,s_as32FontH[font] = %d ,pstStrBmp->Str =%s#############\n",pstStrBmp->font,s_as32FontH[font],pstStrBmp->Str);
        do
        {
            SN_MEMSET(pstStrBmp->pBmpData, 0, pstStrBmp->DataSize);
            ret =MMIGetStringBMP(pstStrBmp);
    		if(ret != STRINGBMP_ERR_NONE)
    		{
    			TRACE(SCI_TRACE_NORMAL,MOD_PRV, TEXT_COLOR_RED("Line:%d,MMIGetStringBMP failed,ret = %#x"), __LINE__, ret);
    			TRACE(SCI_TRACE_NORMAL,MOD_PRV,"FUNC:%s,Line:%d\n"
					"pstStrBmp = 0x%x\n"
                    "pstStrBmp->nStrlen = %d\n"
                	"pstStrBmp->Str = %s\n"
                	"pstStrBmp->pBmpData = 0x%x\n"
                	"pstStrBmp->DataSize = %d\n"
                	"pstStrBmp->x = %d\n"
                	"pstStrBmp->y = %d\n"
                	"pstStrBmp->width = %d\n"
                	"pstStrBmp->height = %d\n"
                	"pstStrBmp->pixelbyte = %d\n"
                	"pstStrBmp->font = %d\n", __FUNCTION__, __LINE__,
                	pstStrBmp,
                	pstStrBmp->nStrlen,
                	pstStrBmp->Str,
                	pstStrBmp->pBmpData,
                	pstStrBmp->DataSize,
                	pstStrBmp->x,
                	pstStrBmp->y,
                	pstStrBmp->width,
                	pstStrBmp->height,
                	pstStrBmp->pixelbyte,
                	pstStrBmp->font);
    			sleep(1);///return ret;
    		}
            else
            {
                break;
            }
        } while(--retry_cnt);
	}

#if 0	
	for(i=0;i<pstStrBmp->DataSize;i++)
	{
		tmp += pstStrBmp->pBmpData[i];
	}
	TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,pstStrBmp->nStrlen = %d@@@@@@@@@@@@ pstr = %s tmp = %d\n",__LINE__, pstStrBmp->nStrlen,pstr,tmp);
#endif
	
	
#if 1 /*2010-8-30 修正：将预览OSD字体加边框，将录像OSD字体也加边框并去除字体底色*/
	{
		int i,j,index;
		unsigned short color1=0xffff/*字色*/, color2=0x0000/*背景色*/, color3=0x8003/*边框色1*/;//, color4=0x8002/*边框色2*/;
		unsigned short *dat = (unsigned short *)pstStrBmp->pBmpData;
		if(NULL != dat)
		{
			/*字体加边*/
			for (i=0;i<pstStrBmp->height;i++)
			{
				for (j=0;j<pstStrBmp->width; j++)
				{
					index = i*pstStrBmp->width + j;
					if ((color1 | 0x8000) == (dat[index] | 0x8000))
					{
						dat[index] = color1;
						if (j > 0)
						{
							if (color2 == dat[index-1])
							{
								dat[index-1] = color3;
							}
						}
						if (i > 0)
						{
							if (color2 == dat[index-pstStrBmp->width])
							{
								dat[index-pstStrBmp->width] = color3;
							}
						}
						if (j < pstStrBmp->width-1)
						{
							if (color2 == dat[index+1])
							{
								dat[index+1] = color3;
							}
						}
						if (i < pstStrBmp->height-1)
						{
							if (color2 == dat[index+pstStrBmp->width])
							{
								dat[index+pstStrBmp->width] = color3;
							}
						}
					}
				}/*end for 2*/
			}/*end for 1*/
			/*字体再次加边，以消除闪烁（仅针对预览OSD进行这个操作）*/
#if 0
			if (GUI_FONT_REC != font)
			{
				for (i=0;i<pstStrBmp->height;i++)
				{
					for (j=0;j<pstStrBmp->width; j++)
					{
						index = i*pstStrBmp->width + j;
						if (color3 == dat[index])
						{
							if (j > 0)
							{
								if (color2 == dat[index-1])
								{
									dat[index-1] = color4;
								}
							}
							if (i > 0)
							{
								if (color2 == dat[index-pstStrBmp->width])
								{
									dat[index-pstStrBmp->width] = color4;
								}
							}
							if (j < pstStrBmp->width-1)
							{
								if (color2 == dat[index+1])
								{
									dat[index+1] = color4;
								}
							}
							if (i < pstStrBmp->height-1)
							{
								if (color2 == dat[index+pstStrBmp->width])
								{
									dat[index+pstStrBmp->width] = color4;
								}
							}
						}
					}/*end for 2*/
				}/*end for 1*/
			}/*end if (GUI_FONT_REC != font)*/
#endif
		}/*end if(NULL != dat)*/
	}
#endif
	
#if 0 /*2010-8-30 观察：从GUI获取的BMP数据内容*/
	static int cnt1 = 0;
	if (cnt1<100)
	{
		int i,j;
		FILE *pf = fopen("bmp1.dat", "a");
		unsigned short *dat = (unsigned short *)pstStrBmp->pBmpData;
		if(NULL!=pf)
		{
			fprintf(pf,"string: %s\n", pstStrBmp->Str);
			for (i=0;i<pstStrBmp->height;i++)
			{
				for (j=0;j<pstStrBmp->width; j++)
				{
					fprintf(pf, "%04x ", dat[i*pstStrBmp->width + j]);
				}
				fputc('\n', pf);
			}
			fputc('\n', pf);
			fclose(pf);
		}
		cnt1++;
	}
#endif

#if 1
	int max_valid_width = 0;
	{
		int i,j;
		unsigned short *pix = (unsigned short *)(pstStrBmp->pBmpData);
		for(i=0;i<pstStrBmp->height;i++)
		{
			for (j=max_valid_width; j<pstStrBmp->width; j++)
			{
				if (0 != pix[(i*pstStrBmp->width + j)])
				{
					max_valid_width = j;
				}
			}
		}
		if(max_valid_width == 0)
		{//如果为空格
			max_valid_width = 6;
		}
		//max_valid_width += (2*pstStrBmp->x);
		//max_valid_width += (32-(max_valid_width%32));
		max_valid_width = (pstStrBmp->width > max_valid_width)?max_valid_width:pstStrBmp->width;
		for(i=0;i<pstStrBmp->height;i++)
		{
			SN_MEMCPY((pstStrBmp->pBmpData + i*max_valid_width*pstStrBmp->pixelbyte), pstStrBmp->width*pstStrBmp->pixelbyte,
				(pstStrBmp->pBmpData + i*pstStrBmp->width*pstStrBmp->pixelbyte), pstStrBmp->width*pstStrBmp->pixelbyte,max_valid_width*pstStrBmp->pixelbyte);
		}
	}
	pstStrBmp->width = max_valid_width;
	//TRACE(SCI_TRACE_NORMAL,MOD_PRV,TEXT_COLOR_PURPLE("%s max_valid_width = %d,pstStrBmp->height=%d\n"), pstr, max_valid_width,pstStrBmp->height);
#endif
#if 0 /*2010-8-30 观察：从GUI获取的BMP数据内容*/
	static int cnt = 0;
	if (cnt<2000)
	{
		int i,j;
		FILE *pf = fopen("bmp1.dat", "a");
		unsigned short *dat = (unsigned short *)pstStrBmp->pBmpData;
		if(NULL!=pf)
		{
			fprintf(pf,"string: %s\n", pstStrBmp->Str);
			for (i=0;i<pstStrBmp->height;i++)
			{
				for (j=0;j<pstStrBmp->width; j++)
				{
					fprintf(pf, "%04x ", dat[i*pstStrBmp->width + j]);
				}
				fputc('\n', pf);
			}
			fputc('\n', pf);
			fclose(pf);
		}
		cnt++;
	}
#endif
	
#if 0
	{
		int i,j;
		unsigned short *pix = (unsigned short *)(pstStrBmp->pBmpData);
		for(i=0;i<pstStrBmp->height;i++)
		{
			for (j=0; j<pstStrBmp->width; j++)
			{
				if (0 == pix[(i*pstStrBmp->width + j)])
				{
					pix[(i*pstStrBmp->width + j)] = 0x8002;
				}
			}
		}
	}
#endif
	return 0;
}

//****************************************************
//OSD，从GUI获取图片函数
//*****************************************************
static int Get_Bmp_From_Gui2(const unsigned char *pstr,STRING_BMP_ST *pstStrBmp, int font)
{
	//int font_w=0,font_h=0;
	//unsigned int i=0,tmp=0;
	//STRING_BMP_ST stStrBmp;
	/*if(font == GUI_FONT_PRV)
	{
		font_w = GUI_FONT_PRV_WIDTH;
		font_h = GUI_FONT_PRV_HEIGHT;
	}
	else
	{
		font_w = GUI_FONT_REC_WIDTH;
		font_h = GUI_FONT_REC_HEIGHT;
	}*/
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "##########font = %d  ,s_as32FontH[font] = %d#############\n",font,s_as32FontH[font]);
	if (font<0 || font>=sizeof(s_as32FontH)/sizeof(s_as32FontH[0]))
	{
		printf(TEXT_COLOR_RED("invalid font: %d, use default font 0\n"), font);
		font = 0;//default font
	}

	SN_STRNCPY(pstStrBmp->Str, MAX_BMP_STR_LEN, (char *)pstr, MAX_BMP_STR_LEN);
	pstStrBmp->Str[MAX_BMP_STR_LEN - 1] = 0; 
	pstStrBmp->nStrlen = SN_STRLEN(pstStrBmp->Str);
	pstStrBmp->x = OSD_X_OFFSET;
	pstStrBmp->y = OSD_Y_OFFSET;
	pstStrBmp->width = MAX_STRBMP_WIDTH;
	//pstStrBmp->width = pstStrBmp->nStrlen * font_w + pstStrBmp->x * 2;
	//pstStrBmp->width = (pstStrBmp->width%2)+pstStrBmp->width;
	pstStrBmp->height = s_as32FontH[font] + 2*pstStrBmp->y;//font_h;
	pstStrBmp->pixelbyte = PIXBYTE;
	pstStrBmp->DataSize = pstStrBmp->width	* pstStrBmp->height * pstStrBmp->pixelbyte;
	pstStrBmp->font= s_as32FontH[font];
	
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "##########pstStrBmp->font = %d  ,s_as32FontH[font] = %d pstStrBmp->width =%d#############\n",pstStrBmp->font,s_as32FontH[font],pstStrBmp->width );
	pstStrBmp->BGColor.Aval = 0x00;
	pstStrBmp->BGColor.Rval = 0x00;
	pstStrBmp->BGColor.Gval= 0x00;
	pstStrBmp->BGColor.Bval = 0x00;
	
	pstStrBmp->TxtColor.Aval = 0xff;
	pstStrBmp->TxtColor.Rval = 0xff;
	pstStrBmp->TxtColor.Gval= 0xff;
	pstStrBmp->TxtColor.Bval = 0xff;
	//pstStrBmp->pBmpData = pParam->stBitmap.pData;//SN_MALLOC(pstStrBmp->DataSize);	//初始化固定申请的内存，专门用来存放这些图片的	
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "##########pstStrBmp->pBmpData = %x #############\n",pstStrBmp->pBmpData );
	if(pstStrBmp->pBmpData == NULL)
	{
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "##########pstr = %s  ,s_as32FontH[font] = %d #############\n",pstr,s_as32FontH[font]);
		pstStrBmp->pBmpData = SN_MALLOC(pstStrBmp->DataSize);//初始化固定申请的内存，专门用来存放这些图片的
		if(pstStrBmp->pBmpData == NULL)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"SN_MALLOC fail!\n");
			return -1;
		}
	}
	if (g_guifd <= 0)
	{
		printf(TEXT_COLOR_PURPLE("%s: gui not ready!"), __FUNCTION__);
		SN_MEMSET(pstStrBmp->pBmpData,FB_BG_COLOR,pstStrBmp->DataSize);
		return 0;
	}
	else
	{
        int retry_cnt = 5;
        int ret = 0;
        do
        {
            SN_MEMSET(pstStrBmp->pBmpData, 0, pstStrBmp->DataSize);
			if(pstStrBmp->Str[0] == '\0')
			{//如果为空字符串，直接退出
				TRACE(SCI_TRACE_NORMAL,MOD_PRV, "##########pstr = %d  ,s_as32FontH[font] = %d #############\n",pstStrBmp->Str[0],s_as32FontH[font]);
				pstStrBmp->width = 4;
				return 0;
			}
            ret =MMIGetStringBMP(pstStrBmp);
    		if(ret != STRINGBMP_ERR_NONE)
    		{
    			TRACE(SCI_TRACE_NORMAL,MOD_PRV, TEXT_COLOR_RED("Line:%d,MMIGetStringBMP failed,ret = %#x"), __LINE__, ret);
    			TRACE(SCI_TRACE_NORMAL,MOD_PRV,"FUNC:%s,Line:%d\n"
                    "pstStrBmp->nStrlen = %d\n"
                	"pstStrBmp->Str = %s\n"
                	"pstStrBmp->pBmpData = 0x%x\n"
                	"pstStrBmp->DataSize = %d\n"
                	"pstStrBmp->x = %d\n"
                	"pstStrBmp->y = %d\n"
                	"pstStrBmp->width = %d\n"
                	"pstStrBmp->height = %d\n"
                	"pstStrBmp->pixelbyte = %d\n"
                	"pstStrBmp->font = %d\n", __FUNCTION__, __LINE__,
                	pstStrBmp->nStrlen,
                	pstStrBmp->Str,
                	pstStrBmp->pBmpData,
                	pstStrBmp->DataSize,
                	pstStrBmp->x,
                	pstStrBmp->y,
                	pstStrBmp->width,
                	pstStrBmp->height,
                	pstStrBmp->pixelbyte,
                	pstStrBmp->font);
    			sleep(1);///return ret;
    		}
            else
            {
                break;
            }
        } while(--retry_cnt);
	}

#if 0	
	for(i=0;i<pstStrBmp->DataSize;i++)
	{
		tmp += pstStrBmp->pBmpData[i];
	}
	TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,pstStrBmp->nStrlen = %d@@@@@@@@@@@@ pstr = %s tmp = %d\n",__LINE__, pstStrBmp->nStrlen,pstr,tmp);
#endif
	
	
#if 1 /*2010-8-30 修正：将预览OSD字体加边框，将录像OSD字体也加边框并去除字体底色*/
	{
		int i,j,index;
		unsigned short color1=0xffff/*字色*/, color2=0x0000/*背景色*/, color3=0x8003/*边框色1*/;//, color4=0x8002/*边框色2*/;
		unsigned short *dat = (unsigned short *)pstStrBmp->pBmpData;
		if(NULL != dat)
		{
			/*字体加边*/
			for (i=0;i<pstStrBmp->height;i++)
			{
				for (j=0;j<pstStrBmp->width; j++)
				{
					index = i*pstStrBmp->width + j;
					if ((color1 | 0x8000) == (dat[index] | 0x8000))
					{
						dat[index] = color1;
						if (j > 0)
						{
							if (color2 == dat[index-1])
							{
								dat[index-1] = color3;
							}
						}
						if (i > 0)
						{
							if (color2 == dat[index-pstStrBmp->width])
							{
								dat[index-pstStrBmp->width] = color3;
							}
						}
						if (j < pstStrBmp->width-1)
						{
							if (color2 == dat[index+1])
							{
								dat[index+1] = color3;
							}
						}
						if (i < pstStrBmp->height-1)
						{
							if (color2 == dat[index+pstStrBmp->width])
							{
								dat[index+pstStrBmp->width] = color3;
							}
						}
					}
				}/*end for 2*/
			}/*end for 1*/
			/*字体再次加边，以消除闪烁（仅针对预览OSD进行这个操作）*/
#if 0
			if (GUI_FONT_REC != font)
			{
				for (i=0;i<pstStrBmp->height;i++)
				{
					for (j=0;j<pstStrBmp->width; j++)
					{
						index = i*pstStrBmp->width + j;
						if (color3 == dat[index])
						{
							if (j > 0)
							{
								if (color2 == dat[index-1])
								{
									dat[index-1] = color4;
								}
							}
							if (i > 0)
							{
								if (color2 == dat[index-pstStrBmp->width])
								{
									dat[index-pstStrBmp->width] = color4;
								}
							}
							if (j < pstStrBmp->width-1)
							{
								if (color2 == dat[index+1])
								{
									dat[index+1] = color4;
								}
							}
							if (i < pstStrBmp->height-1)
							{
								if (color2 == dat[index+pstStrBmp->width])
								{
									dat[index+pstStrBmp->width] = color4;
								}
							}
						}
					}/*end for 2*/
				}/*end for 1*/
			}/*end if (GUI_FONT_REC != font)*/
#endif
		}/*end if(NULL != dat)*/
	}
#endif
	
#if 0 /*2010-8-30 观察：从GUI获取的BMP数据内容*/
	static int cnt1 = 0;
	if (cnt1<100)
	{
		int i,j;
		FILE *pf = fopen("bmp1.dat", "a");
		unsigned short *dat = (unsigned short *)pstStrBmp->pBmpData;
		if(NULL!=pf)
		{
			fprintf(pf,"string: %s\n", pstStrBmp->Str);
			for (i=0;i<pstStrBmp->height;i++)
			{
				for (j=0;j<pstStrBmp->width; j++)
				{
					fprintf(pf, "%04x ", dat[i*pstStrBmp->width + j]);
				}
				fputc('\n', pf);
			}
			fputc('\n', pf);
			fclose(pf);
		}
		cnt1++;
	}
#endif

#if 1
	int max_valid_width = 0;
	{
		int i,j;
		unsigned short *pix = (unsigned short *)(pstStrBmp->pBmpData);
		for(i=0;i<pstStrBmp->height;i++)
		{
			for (j=max_valid_width; j<pstStrBmp->width; j++)
			{
				if (0 != pix[(i*pstStrBmp->width + j)])
				{
					max_valid_width = j;
				}
			}
		}
		if(max_valid_width == 0)
		{//如果为空格
			max_valid_width = 6;
		}
		//max_valid_width += (2*pstStrBmp->x);
		//max_valid_width += (32-(max_valid_width%32));
		max_valid_width = (pstStrBmp->width > max_valid_width)?max_valid_width:pstStrBmp->width;
		max_valid_width += max_valid_width%2;
		for(i=0;i<pstStrBmp->height;i++)
		{
			SN_MEMCPY((pstStrBmp->pBmpData + i*max_valid_width*pstStrBmp->pixelbyte), pstStrBmp->width*pstStrBmp->pixelbyte,
				(pstStrBmp->pBmpData + i*pstStrBmp->width*pstStrBmp->pixelbyte), pstStrBmp->width*pstStrBmp->pixelbyte,max_valid_width*pstStrBmp->pixelbyte);
		}
	}
	pstStrBmp->width = max_valid_width;
	//TRACE(SCI_TRACE_NORMAL,MOD_PRV,TEXT_COLOR_PURPLE("%s max_valid_width = %d,pstStrBmp->height=%d\n"), pstr, max_valid_width,pstStrBmp->height);
#endif
#if 0 /*2010-8-30 观察：从GUI获取的BMP数据内容*/
	static int cnt = 0;
	if (cnt<2000)
	{
		int i,j;
		FILE *pf = fopen("bmp1.dat", "a");
		unsigned short *dat = (unsigned short *)pstStrBmp->pBmpData;
		if(NULL!=pf)
		{
			fprintf(pf,"string: %s\n", pstStrBmp->Str);
			for (i=0;i<pstStrBmp->height;i++)
			{
				for (j=0;j<pstStrBmp->width; j++)
				{
					fprintf(pf, "%04x ", dat[i*pstStrBmp->width + j]);
				}
				fputc('\n', pf);
			}
			fputc('\n', pf);
			fclose(pf);
		}
		cnt++;
	}
#endif
	
#if 0
	{
		int i,j;
		unsigned short *pix = (unsigned short *)(pstStrBmp->pBmpData);
		for(i=0;i<pstStrBmp->height;i++)
		{
			for (j=0; j<pstStrBmp->width; j++)
			{
				if (0 == pix[(i*pstStrBmp->width + j)])
				{
					pix[(i*pstStrBmp->width + j)] = 0x8002;
				}
			}
		}
	}
#endif
	return 0;
}
static int Get_Bmp_From_Gui(const unsigned char *pstr,STRING_BMP_ST *pstStrBmp,int font)
{
	int ret;
	pthread_mutex_lock(&mutex_getbmp);
	ret = Get_Bmp_From_Gui2(pstr, pstStrBmp, font);
	pthread_mutex_unlock(&mutex_getbmp);
	return ret;
}
/************************************************************************/
/* 
{16, 20, 12, 16, 24, 28, 32, 36, 40};
  0    1    2    3    4   5    6    7   8   
  */
/************************************************************************/
static int OSD_GetVoChnFontSize(VO_DEV VoDev, VO_CHN VoChn)
{
	int font;
	int chn_height;
	RECT_S stRect;
	SIZE_S stVoDspSize, stVoImgSize;
	unsigned char vo_dev = VoDev;
	
#ifdef SECOND_DEV
	if(VoDev == AD)
	{
		vo_dev = SD;
	}
#endif	

	if(HI_SUCCESS != PRV_GetVoDevDispSize(vo_dev, &stVoDspSize)
	|| HI_SUCCESS != PRV_GetVoDevImgSize(vo_dev, &stVoImgSize))
	{
		return (font = 0);
	}

	if (VoChn < 0 || HI_SUCCESS != PRV_GetVoChnRect(vo_dev, VoChn, &stRect))
	{
		chn_height = stVoDspSize.u32Height;
	}
	else
	{
		chn_height = stRect.u32Height * stVoDspSize.u32Height / stVoImgSize.u32Height;
	}

	if (chn_height < 145) // 576/4 + 1 = 144 + 1 = 145
	{
		font = 2;
	} 
	else if (chn_height < 145+147) // (1024-144)/6 = 146.666666... = 147
	{
		font = 3;
	}
	else if (chn_height < 145+147*2)
	{
		font = 1;
	}
	else if (chn_height < 145+147*3)
	{
		font = 4;
	}
	else if (chn_height < 145+147*4)
	{
		font = 5;
	}
	else if (chn_height < 145+147*5)
	{
		font = 6;
	}
	else if (chn_height < 145+147*6)
	{
		font = 7;
	}
	else
	{
		font = 8;
	}

	//printf(TEXT_COLOR_PURPLE("dev:%d, chn:%d, font:%d\n"), VoDev, VoChn, font);
	return font;
}
/************************************************************************/
/* 
{16, 20, 12, 16, 24, 28, 32, 36, 40};
  0   1   2   3   4   5   6   7   8   
  */
/************************************************************************/
static int OSD_GetRecFontSize(unsigned char rec_group,unsigned char ch)
{
	int font;
	int w;
	
	if(ch < PRV_CHAN_NUM)
	{
		w = g_rec_srceen_w[rec_group][ch];
	}
	else
	{
		w = s_slaveVoStat.f_rec_srceen_w[rec_group][ch%PRV_CHAN_NUM];
	}

	switch(w)
	{
		case SCREEN_D1_WIDTH:
		case SCREEN_4CIF_WIDTH:
			font = 5;
			break;
		case SCREEN_CIF_WIDTH:
			font = 0;
			break;
		case SCREEN_QCIF_WIDTH:
			font = 2;
			break;
		default:
			font = 0;
			break;
	}
	//printf(TEXT_COLOR_PURPLE("dev:%d, chn:%d, font:%d\n"), VoDev, VoChn, font);
	return font;
}
/************************************************************************/
/* 
注码流，次码流，JPEG
  0                      1                    2 
  */
/************************************************************************/
static int OSD_GetRecGroupChn(unsigned char rec_group,unsigned char ch)
{
	int venc_ch=ch;
	//return venc_ch;
	switch(rec_group)
	{
		case REC_MAINSTREAM:
			venc_ch = ch *2;
			break;
		case REC_SUBSTREAM:
		/*	venc_ch = ch *2+1;
			break;
		case REC_JPEG:*/
			venc_ch = ch +REC_JPEG_START_CHN_ID;
			break;
		default:
			venc_ch = ch ;
			break;
	}
	//printf(TEXT_COLOR_PURPLE("############OSD_GetRecGroupChn  chn:%d, font:%d\n"), ch, font);
	return venc_ch;
}

//****************************************************
//OSD，获取字符串位图函数接口
//*****************************************************
static int OSD_Get_String(const unsigned char * str,STRING_BMP_ST *pBmpData,int font)
{
	HI_S32 s32Ret=0;
	//调用字符转BMP函数
	s32Ret = Get_Bmp_From_Gui(str,pBmpData,font);
	if(s32Ret != STRINGBMP_ERR_NONE)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Line:%d,Get_Bmp_From_Gui in ctl!\n", __LINE__);
		if(pBmpData->pBmpData)
		{
			SN_FREE(pBmpData->pBmpData);		
			pBmpData->pBmpData = NULL;
		}
		return s32Ret;
	}
	return s32Ret;
}
//****************************************************
//OSD，释放获取到的字符串位图内存接口
//*****************************************************

static int OSD_Free_String(STRING_BMP_ST *pBmpData)
{
	HI_S32 s32Ret=0;
	if(pBmpData->pBmpData)
	{
		SN_FREE(pBmpData->pBmpData);		
		pBmpData->pBmpData = NULL;
	}
	return s32Ret;
}
#if defined(SN9234H1)
static int Rec_OSD_Set_xy(REGION_HANDLE handle,int x,int y)
{
	HI_S32 s32Ret=0;
	REGION_CRTL_CODE_E enCtrl;
	REGION_CTRL_PARAM_U unParam;
	
	if(x%8)	x = (x/8)*8;
	if(y%2)	y = (y/2)*2;
	enCtrl = REGION_SET_POSTION;
	unParam.stPoint.s32X = x;
	unParam.stPoint.s32Y = y;
	s32Ret = HI_MPI_VPP_ControlRegion(handle, enCtrl, &unParam);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_MPI_VPP_ControlRegion failed,ret=0x%x!\n", __LINE__, s32Ret);
	}
	else
	{
		//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,Rec_OSD_Set_xy suc,x=%d,y=%d+++++++++++++++++++++++\n",__LINE__,x,y);
	}
	return s32Ret;
}
#else
static int Rec_OSD_Set_xy(RGN_HANDLE handle,int ch,int x,int y)
{
	HI_S32 s32Ret=0;
	MPP_CHN_S stChn;
    RGN_CHN_ATTR_S stChnAttr;

    stChn.enModId = HI_ID_GROUP;
    stChn.s32DevId = 0;
    stChn.s32ChnId = ch;
	
	if(x%8)	x = (x/8)*8;
	if(y%2)	y = (y/2)*2;
	s32Ret = HI_MPI_RGN_GetDisplayAttr(handle, &stChn, &stChnAttr);
    if(HI_SUCCESS != s32Ret)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"HI_MPI_RGN_GetDisplayAttr (%d)) failed with %#x!\n",\
               handle, s32Ret);
        return HI_FAILURE;
    }
	stChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = x;//*704/720;//起始位置为采集起始位置加上704下的x位置
	stChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = y;
	s32Ret = HI_MPI_RGN_SetDisplayAttr(handle,&stChn,&stChnAttr);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,OSD_Mask_update faild 0x%x!!!\n",__LINE__,s32Ret);
		return HI_FAILURE;
	}
	else
	{
		//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,Rec_OSD_Set_xy suc,x=%d,y=%d+++++++++++++++++++++++\n",__LINE__,x,y);
	}
	return s32Ret;
}
#endif
#if 1
static int Prv_OSD_Cmp_nameAtime(int *pn_x,int *pn_y,int n_w,int n_h,
                                          int t_x,int t_y,int t_w,int t_h,int curY)
{
    if ((*pn_y+n_h) > t_y && (t_y+t_h) > *pn_y)
    {
        if (*pn_x+n_w > t_x && t_x+t_w > *pn_x)
        {
        	if(t_y - n_h>curY)
            	*pn_y = t_y - n_h;
			else
				*pn_y = t_y + t_h;
        }
    }
    return 0;
}
/*************************************************
Function: //Rec_OSD_Cmp_nameAtime
Description: //录像部分OSD，时间和通道名称位置比较
Calls: 
Called By: //
Input: // ch :通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int Rec_OSD_Cmp_nameAtime(unsigned char rec_group,unsigned char ch,int *pn_x,int *pn_y,int n_w,int n_h,int *pt_x,int *pt_y,int t_w,int t_h)
{
	int name_x=0,name_y=0,name_w=0,name_h=0,time_x=0,time_y=0,time_w=0,time_h=0;
	
	//pthread_mutex_lock(&mutex_setbmp);
	name_x = (((*pn_x*g_rec_srceen_w[rec_group][ch])/SCREEN_DEF_WIDTH+7)/8)*8;
	name_y = (((*pn_y*g_rec_srceen_h[rec_group][ch])/SCREEN_DEF_HEIGHT+1)/2)*2;
	name_w = ((n_w+1)/2)*2;
	name_h = ((n_h*+1)/2)*2;
	time_x = (((*pt_x*g_rec_srceen_w[rec_group][ch])/SCREEN_DEF_WIDTH+7)/8)*8;
	time_y = (((*pt_y*g_rec_srceen_h[rec_group][ch])/SCREEN_DEF_HEIGHT+1)/2)*2;
	time_w = ((t_w+1)/2)*2;
	time_h = ((t_h*+1)/2)*2;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########111111111111111 rec_group=%d,ch=%d,name_x=%d,name_y=%d,name_w=%d,name_h=%d,time_x=%d,time_y=%d,time_w=%d,time_h=%d,g_rec_srceen_w[rec_group][ch]=%d,g_rec_srceen_h[rec_group][ch]=%d#############################\n",
	//	rec_group,ch,name_x,name_y,name_w,name_h,time_x,time_y,time_w,time_h,g_rec_srceen_w[rec_group][ch],g_rec_srceen_h[rec_group][ch]);
	//比较时间和通道名称大小
	if(((name_x >= time_x) &&(name_x < (time_x+time_w))) || (((name_x+name_w) > time_x)&&((name_x+name_w) <= (time_x+time_w))))
	{//通道名称与时间x范围有重叠
		if((name_y > time_y) && (name_y <= (time_y+time_h)))
		{//通道名称在时间的下面，y范围有重叠，
			if((time_y+time_h+2+name_h)>g_rec_srceen_h[rec_group][ch])
			{//移动后超过边界，那么向上移动
				time_y =  name_y - time_h-2;
			}
			else
			{
				name_y = time_y+time_h+2;
			}
		}
		else if(((name_y+name_h) >= time_y)&&((name_y+name_h) < (time_y+time_h)))
		{//通道名称在时间的上面，y范围有重叠，
			
			if((name_y+name_h+2+time_h)>g_rec_srceen_h[rec_group][ch])
			{//移动后超过边界，那么向下移动
				name_y = time_y-name_h-2;
			}
			else
			{
				time_y = name_y+name_h+2;
			}
		}
		else if(name_y == time_y)
		{
			if(name_x >= time_x)
			{//在时间后面
				if(((name_x - time_w-1)/8*8 ) <= 0)
				{//往前移动名称位置，后移时间位置都出错，那么上、下移
					if(((time_x + time_w +name_w+7)/8*8) > g_rec_srceen_w[rec_group][ch])
					{//超过边界
						if((time_y+time_h+2+name_h)>g_rec_srceen_h[rec_group][ch])
						{//移动后超过边界，那么向上移动
							time_y =  name_y - time_h-2;
						}
						else
						{
							name_y = time_y+time_h+2;
						}
					}
					else
					{
						name_x = (time_x + time_w+7)/8*8;
					}
				}
				else
				{
					time_x = (name_x - time_w -1)/8*8;
				}
			}
			else
			{//在时间前面
				if(((time_x - name_w -1)/8*8)  <= 0)
				{//往前移动名称位置，后移时间位置都出错，那么上、下移
					if(((name_x + name_w + time_w + 7)/8*8)  > g_rec_srceen_w[rec_group][ch])
					{//超过边界
						if((time_y+time_h+2+name_h)>g_rec_srceen_h[rec_group][ch])
						{//移动后超过边界，那么向上移动
							time_y =  name_y - time_h-2;
						}
						else
						{
							name_y = time_y+time_h+2;
						}
					}
					else
					{
						time_x  = (name_x + name_w +7)/8*8 ;
					}
				}
				else
				{
					name_x = (time_x - name_w -1)/8*8 ;
				}
			}
		}
	}
	//else
	{//如果当前的哦范围有超出边界的，统一下移或者上移
		if((name_x + name_w) > g_rec_srceen_w[rec_group][ch])
		{//如果名称超出边界，那么下移
			name_x = (g_rec_srceen_w[rec_group][ch] - name_w - 1)/8*8 ;
			if(name_x < 0)
			{
				name_x = 0;
			}
			if(name_x <= (time_x + time_w))
			{//如果前移后的通道名称的位置与通道名称有重叠如果通道名称在时间上方
				if(((name_y+name_h) >= time_y)&&((name_y+name_h) <= (time_y+time_h)))
				{//如果纵坐标有重叠,那么移动纵坐标
					if((time_y + time_h) > (g_rec_srceen_h[rec_group][ch]- time_h-2))
					{
						time_y = name_y + name_h + 2;
					}
					else
					{
						name_y = time_y - name_h -2;
					}
				}
				else if((name_y > time_y) && (name_y <= (time_y+time_h)))
				{
					if((name_y + name_h) > (g_rec_srceen_h[rec_group][ch]- name_h-2))
					{
						name_y = time_y + time_h+2;
					}
					else
					{
						time_y = name_y - time_h -2;
					}
				}
					
			}
		}
		if((time_x + time_w) > g_rec_srceen_w[rec_group][ch])
		{//如果时间超出边界
			time_x = (g_rec_srceen_w[rec_group][ch] - time_w - 1)/8*8  ;
			if(time_x < 0)
			{
				time_x = 0;
			}
			if(time_x <= (name_x + name_w))
			{//如果前移后的时间横位置与通道名称有重叠
				if(((name_y+name_h) >= time_y)&&((name_y+name_h) <= (time_y+time_h)))
				{//如果纵坐标有重叠,那么移动纵坐标，//如果通道名称在时间上方
				//需要重新计算
					if((time_y + time_h) < (g_rec_srceen_h[rec_group][ch] - time_h-2))
					{
						time_y = name_y + name_h + 2;
					}
					else
					{
						name_y = time_y - name_h -2;
					}	
				}
				else if((name_y > time_y) && (name_y <= (time_y+time_h)))
				{//如果通道名称在时间下方
					if((name_y + name_h) < (g_rec_srceen_h[rec_group][ch] - name_h-2))
					{
						name_y = time_y + time_h+2;
					}
					else
					{
						time_y = name_y - time_h -2;
					}
				}
			}
		}
		if((name_y + name_h) > g_rec_srceen_h[rec_group][ch])
		{//如果名称超出边界，那么上移
			name_y = g_rec_srceen_h[rec_group][ch] - name_h -2;
			if((name_y  <= (time_y+time_h)) && (name_y >= time_y))
			{//如果上移后的通道名称位置与时间有重叠
				if(((name_x >= time_x) &&(name_x <= (time_x+time_w))) || (((name_x+name_w) >= time_x)&&((name_x+name_w) <= (time_x+time_w))))
				{//如果横坐标方向上有重叠
					time_y = name_y - time_h-2;
				}
			}
			else if(((name_y+ name_h)  <= (time_y+time_h)) && ((name_y+ time_h) >= time_y))
			{
				if(((name_x >= time_x) &&(name_x <= (time_x+time_w))) || (((name_x+name_w) >= time_x)&&((name_x+name_w) <= (time_x+time_w))))
				{//如果横坐标方向上有重叠
					name_y = time_y - name_h-2;
				}
			}
				
		}
		if((time_y + time_h) > g_rec_srceen_h[rec_group][ch])
		{
			time_y = g_rec_srceen_h[rec_group][ch] - time_h-2;
			if((time_y  <= (name_y+name_h)) && (time_y >= name_y))
			{//如果上移后的通道名称位置与时间有重叠
				if(((name_x >= time_x) &&(name_x <= (time_x+time_w))) || (((name_x+name_w) >= time_x)&&((name_x+name_w) <= (time_x+time_w))))
				{//如果横坐标方向上有重叠
					name_y = time_y - name_h-2;
				}
			}
			else if(((time_y+ time_h)  <= (name_y+name_h)) && ((time_y+ time_h) >= name_y))
			{
				if(((name_x >= time_x) &&(name_x <= (time_x+time_w))) || (((name_x+name_w) >= time_x)&&((name_x+name_w) <= (time_x+time_w))))
				{//如果横坐标方向上有重叠
					time_y = name_y - time_h-2;
				}	
			}
		}
	}	
		
	*pn_x = name_x;
	*pn_y = name_y;
	*pt_x = time_x;
	*pt_y = time_y;
	//pthread_mutex_unlock(&mutex_setbmp);
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########222222222222222222 rec_group=%d,ch=%d,name_x=%d,name_y=%d,name_w=%d,name_h=%d,time_x=%d,time_y=%d,time_w=%d,time_h=%d,g_rec_srceen_w[rec_group][ch]=%d,g_rec_srceen_h[rec_group][ch]=%d#############################\n",
	//	rec_group,ch,name_x,name_y,name_w,name_h,time_x,time_y,time_w,time_h,g_rec_srceen_w[rec_group][ch],g_rec_srceen_h[rec_group][ch]);
	return 0;
}

/*************************************************
Function: //OSD_Cmp_nameAtime
Description: //录像部分OSD，时间和通道名称位置刷新
Calls: 
Called By: //
Input: // ch :通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int OSD_Cmp_nameAtime(int *pn_x,int *pn_y,int n_w,int n_h,int *pt_x,int *pt_y,int t_w,int t_h)
{
	int name_x=*pn_x,name_y=*pn_y,name_w=n_w,name_h=n_h,time_x=*pt_x,time_y=*pt_y,time_w=t_w,time_h=t_h;
	/*
	name_x = (((*pn_x*SCREEN_4CIF_WIDTH)/SCREEN_DEF_WIDTH+7)/8)*8;
	name_y = (((*pn_y*SCREEN_4CIF_HEIGHT)/SCREEN_DEF_HEIGHT+1)/2)*2;
	name_w = ((n_w+1)/2)*2;
	name_h = ((n_h*+1)/2)*2;
	time_x = (((*pt_x*SCREEN_4CIF_WIDTH)/SCREEN_DEF_WIDTH+7)/8)*8;
	time_y = (((*pt_y*SCREEN_4CIF_HEIGHT)/SCREEN_DEF_HEIGHT+1)/2)*2;
	time_w = ((t_w+1)/2)*2;
	time_h = ((t_h*+1)/2)*2;*/
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########111111111111111 name_x=%d,name_y=%d,name_w=%d,name_h=%d,time_x=%d,time_y=%d,time_w=%d,time_h=%d#############################\n",
//		name_x,name_y,name_w,name_h,time_x,time_y,time_w,time_h);
	//比较时间和通道名称大小
	if(((name_x >= time_x) &&(name_x <= (time_x+time_w))) || (((name_x+name_w) >= time_x)&&((name_x+name_w) <= (time_x+time_w))))
	{//通道名称与时间x范围有重叠
		if((name_y > time_y) && (name_y <= (time_y+time_h)))
		{//通道名称在时间的下面，y范围有重叠，
			if((time_y+time_h+2+name_h)>SCREEN_DEF_HEIGHT)
			{//移动后超过边界，那么向上移动
				time_y =  name_y - time_h-2;
			}
			else
			{
				name_y = time_y+time_h+2;
			}
		}
		else if(((name_y+name_h) >= time_y)&&((name_y+name_h) < (time_y+time_h)))
		{//通道名称在时间的上面，y范围有重叠，
			
			if((name_y+name_h+2+time_h)>SCREEN_DEF_HEIGHT)
			{//移动后超过边界，那么向下移动
				name_y = time_y-name_h-2;
			}
			else
			{
				time_y = name_y+name_h+2;
			}
		}
		else if(name_y == time_y)
		{
			if(name_x >= time_x)
			{//在时间后面
				if(((name_x - time_w -1)/8*8) <= 0)
				{//往前移动名称位置，后移时间位置都出错，那么上、下移
					if(((time_x + time_w +name_w+7)/8*8) > SCREEN_DEF_WIDTH)
					{//超过边界
						if((time_y+time_h+2+name_h)>SCREEN_DEF_HEIGHT)
						{//移动后超过边界，那么向上移动
							time_y =  name_y - time_h-2;
						}
						else
						{
							name_y = time_y+time_h+2;
						}
					}
					else
					{
						name_x = (time_x + time_w+7)/8*8;
					}
				}
				else
				{
					time_x = (name_x - time_w -1)/8*8;
				}
			}
			else
			{//在时间前面
				if(((time_x - name_w -1)/8*8) <= 0)
				{//往前移动名称位置，后移时间位置都出错，那么上、下移
					if(((name_x + name_w + time_w + 7)/8*8) > SCREEN_DEF_WIDTH)
					{//超过边界
						if((time_y+time_h+2+name_h)>SCREEN_DEF_HEIGHT)
						{//移动后超过边界，那么向上移动
							time_y =  name_y - time_h-2;
						}
						else
						{
							name_y = time_y+time_h+2;
						}
					}
					else
					{
						time_x  = (name_x + name_w +7)/8*8;
					}
				}
				else
				{
					name_x = (time_x - name_w -1)/8*8;
				}
			}
		}
	}
	//else
	{//如果当前的哦范围有超出边界的，统一下移或者上移
		if((name_x + name_w) > SCREEN_DEF_WIDTH)
		{//如果名称超出边界，那么下移
			name_x = (SCREEN_DEF_WIDTH - name_w - 1)/8*8;
			if(name_x < 0)
			{
				name_x = 0;
			}
			if(name_x <= (time_x + time_w))
			{//如果前移后的通道名称的位置与通道名称有重叠如果通道名称在时间上方
				if(((name_y+name_h) >= time_y)&&((name_y+name_h) <= (time_y+time_h)))
				{//如果纵坐标有重叠,那么移动纵坐标
					if((time_y + time_h) > (SCREEN_DEF_HEIGHT- time_h-2))
					{
						time_y = name_y + name_h + 2;
					}
					else
					{
						name_y = time_y - name_h -2;
					}
				}
				else if((name_y > time_y) && (name_y <= (time_y+time_h)))
				{
					if((name_y + name_h) > (SCREEN_DEF_HEIGHT - name_h-2))
					{
						name_y = time_y + time_h+2;
					}
					else
					{
						time_y = name_y - time_h -2;
					}
				}
					
			}
		}
		if((time_x + time_w) > SCREEN_DEF_WIDTH)
		{//如果时间超出边界
			time_x = (SCREEN_DEF_WIDTH- time_w - 1)/8*8;
			if(time_x < 0)
			{
				time_x = 0;
			}
			if(time_x <= (name_x + name_w))
			{//如果前移后的时间横位置与通道名称有重叠
				if(((name_y+name_h) >= time_y)&&((name_y+name_h) <= (time_y+time_h)))
				{//如果纵坐标有重叠,那么移动纵坐标，//如果通道名称在时间上方
				//需要重新计算
					if((time_y + time_h) < (SCREEN_DEF_HEIGHT - time_h-2))
					{
						time_y = name_y + name_h + 2;
					}
					else
					{
						name_y = time_y - name_h -2;
					}	
				}
				else if((name_y > time_y) && (name_y <= (time_y+time_h)))
				{//如果通道名称在时间下方
					if((name_y + name_h) < (SCREEN_DEF_HEIGHT - name_h-2))
					{
						name_y = time_y + time_h+2;
					}
					else
					{
						time_y = name_y - time_h -2;
					}
				}
			}
		}
		if((name_y + name_h) > SCREEN_DEF_HEIGHT)
		{//如果名称超出边界，那么上移
			name_y = SCREEN_DEF_HEIGHT - name_h -2;
			if((name_y  <= (time_y+time_h)) && (name_y >= time_y))
			{//如果上移后的通道名称位置与时间有重叠
				if(((name_x >= time_x) &&(name_x <= (time_x+time_w))) || (((name_x+name_w) >= time_x)&&((name_x+name_w) <= (time_x+time_w))))
				{//如果横坐标方向上有重叠
					time_y = name_y - time_h-2;
				}
			}
			else if(((name_y+ name_h)  <= (time_y+time_h)) && ((name_y+ time_h) >= time_y))
			{
				if(((name_x >= time_x) &&(name_x <= (time_x+time_w))) || (((name_x+name_w) >= time_x)&&((name_x+name_w) <= (time_x+time_w))))
				{//如果横坐标方向上有重叠
					name_y = time_y - name_h-2;
				}
			}
				
		}
		if((time_y + time_h) > SCREEN_DEF_HEIGHT)
		{
			time_y = SCREEN_DEF_HEIGHT - time_h-2;
			if((time_y  <= (name_y+name_h)) && (time_y >= name_y))
			{//如果上移后的通道名称位置与时间有重叠
				if(((name_x >= time_x) &&(name_x <= (time_x+time_w))) || (((name_x+name_w) >= time_x)&&((name_x+name_w) <= (time_x+time_w))))
				{//如果横坐标方向上有重叠
					name_y = time_y - name_h-2;
				}
			}
			else if(((time_y+ time_h)  <= (name_y+name_h)) && ((time_y+ time_h) >= name_y))
			{
				if(((name_x >= time_x) &&(name_x <= (time_x+time_w))) || (((name_x+name_w) >= time_x)&&((name_x+name_w) <= (time_x+time_w))))
				{//如果横坐标方向上有重叠
					time_y = name_y - time_h-2;
				}	
			}
		}
	}	
	*pn_x = name_x;
	*pn_y = name_y;
	*pt_x = time_x;
	*pt_y = time_y;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########222222222222222222 name_x=%d,name_y=%d,name_w=%d,name_h=%d,time_x=%d,time_y=%d,time_w=%d,time_h=%d#############################\n",
	//	name_x,name_y,name_w,name_h,time_x,time_y,time_w,time_h);
	return 0;
}
#else
//计算两个位置是否相交,相交返回1，不相交返回0
static int OSD_Focus_Cal(int x,int y,int w,int h,int x1,int y1，int w1,int h1)
{
	int mid_x=0,mid_y=0,mid_w=0,mid_h=0,mid_x1=0,mid_y1=0;//各个区域的中心位置x,y变量
	int tmp_x=x,tmp_y=y,tmp_w=w,tmp_h=h,tmp_x1=x1,tmp_y1=y1,tmp_w1=w1,tmp_h1=h1;

	mid_x = (tmp_x + tmp_w)/2;
	mid_y = (tmp_y + tmp_h)/2;
	mid_x1 = (tmp_x1 + tmp_w1)/2;
	mid_y1 = (tmp_y1 + tmp_h1)/2;
	mid_w = (tmp_w + tmp_w1)/2;
	mid_h = (tmp_h + tmp_h1)/2;
	
	//如果在x方向上 中心点间距离差的绝对值小于等于两个矩形宽的和的1/2，
	//而且在y方向上中心点间距离差的绝对值小于等于两个矩形高的和的1/2，矩形重叠
	if((ABS(mid_x - mid_x1) <=  mid_w) && (ABS(mid_y - mid_y1) <=  mid_h))
	{
		return 1;
	}
	return 0;
}
/*************************************************
Function: //OSD_Cmp_nameAtime
Description: //录像部分OSD，时间和通道名称位置刷新
Calls: 
Called By: //
Input: // ch :通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int OSD_Cmp_nameAtime(int *pn_x,int *pn_y,int n_w,int n_h,int *pne_x,int *pne_y,int ne_w,int ne_h,int *pt_x,int *pt_y,int t_w,int t_h)
{
	int ret=0;
	int name_x=*pn_x,name_y=*pn_y,name_w=n_w,name_h=n_h,time_x=*pt_x,time_y=*pt_y,time_w=t_w,time_h=t_h;
	int name_e_x=*pne_x,name_e_y=*pne_y,name_e_w=ne_w,name_e_h=ne_h;
	
	if(name_w == 0)
	{//如果通道名称为0，剩余2个区域
		if(name_e_w != 0 && time_w != 0)
		{//另外两个区域都不为0，那么才需要判断了
			ret = OSD_Focus_Cal(name_e_x,name_e_y,name_e_w,name_e_h,time_x,time_y,time_w,time_h);
			if(ret == 1)
			{//区域相交，
				goto Deal;
			}
		}
	}
	else if(name_e_w == 0)
	{//如果通道名称1为0，剩余2个区域
		if(name_w != 0 && time_w != 0)
		{//另外两个区域都不为0，
			ret = OSD_Focus_Cal(name_x,name_y,name_w,name_h,time_x,time_y,time_w,time_h);
			if(ret == 1)
			{//区域相交，
				goto Deal;
			}
		} 
	}
	else if(time_w == 0)
	{//如果时间为0，剩余2个区域
		if(name_e_w != 0 && name_w != 0)
		{//另外两个区域都不为0，那么才需要判断了
			ret = OSD_Focus_Cal(name_x,name_y,name_w,name_h,name_e_x,name_e_y,name_e_w,name_e_h);
			if(ret == 1)
			{//区域相交，
				goto Deal;
			}
		} 
	}
	else
	{//否则3个区域都存在
		ret = OSD_Focus_Cal(name_x,name_y,name_w,name_h,name_e_x,name_e_y,name_e_w,name_e_h);
		if(ret == 1)
		{//2区域相交，
			goto Deal;
		}
		ret = OSD_Focus_Cal(name_x,name_y,name_w,name_h,time_x,time_y,time_w,time_h);
		if(ret == 1)
		{//区域相交，
			goto Deal;
		}
		ret = OSD_Focus_Cal(name_e_x,name_e_y,name_e_w,name_e_h,time_x,time_y,time_w,time_h);
		if(ret == 1)
		{//区域相交，
			goto Deal;
		}
	}
	*pn_x = name_x;
	*pn_y = name_y;
	*pne_x = name_e_x;
	*pne_y = name_e_y;
	*pt_x = time_x;
	*pt_y = time_y;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########222222222222222222 name_x=%d,name_y=%d,name_w=%d,name_h=%d,time_x=%d,time_y=%d,time_w=%d,time_h=%d#############################\n",
	//	name_x,name_y,name_w,name_h,time_x,time_y,time_w,time_h);
	return 0;
Deal:
	{
		name_x = (((osd_def_pos.name_def_x*g_rec_srceen_w[rec_group][ch])/SCREEN_DEF_WIDTH+7)/8)*8;
		name_y = (((osd_def_pos.name_def_y*g_rec_srceen_h[rec_group][ch])/SCREEN_DEF_HEIGHT+1)/2)*2;
		//通道名称1位置
		name_e_x = (((osd_def_pos.osd_def_x*g_rec_srceen_w[rec_group][ch])/SCREEN_DEF_WIDTH+7)/8)*8;
		name_e_y = (((osd_def_pos.osd_def_y*g_rec_srceen_h[rec_group][ch])/SCREEN_DEF_HEIGHT+1)/2)*2;
		//时间位置
		time_x = (((osd_def_pos.time_def_x*g_rec_srceen_w[rec_group][ch])/SCREEN_DEF_WIDTH+7)/8)*8;
		time_y = (((osd_def_pos.time_def_y*g_rec_srceen_h[rec_group][ch])/SCREEN_DEF_HEIGHT+1)/2)*2;
		*pn_x = name_x;
		*pn_y = name_y;
		*pne_x = name_e_x;
		*pne_y = name_e_y;
		*pt_x = time_x;
		*pt_y = time_y;
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########222222222222222222 name_x=%d,name_y=%d,name_w=%d,name_h=%d,time_x=%d,time_y=%d,time_w=%d,time_h=%d#############################\n",
		//	name_x,name_y,name_w,name_h,time_x,time_y,time_w,time_h);
		return 0;
	}	
}

/*************************************************
Function: //OSD_Cmp_nameAtime
Description: //录像部分OSD，时间和通道名称位置刷新
Calls: 
Called By: //
Input: // ch :通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int Rec_OSD_Cmp_nameAtime(unsigned char rec_group,unsigned char ch,int *pn_x,int *pn_y,int n_w,int n_h,int *pne_x,int *pne_y,int ne_w,int ne_h,int *pt_x,int *pt_y,int t_w,int t_h)
{
	int ret=0;
	int name_x=*pn_x,name_y=*pn_y,name_w=n_w,name_h=n_h,time_x=*pt_x,time_y=*pt_y,time_w=t_w,time_h=t_h;
	int name_e_x=*pne_x,name_e_y=*pne_y,name_e_w=ne_w,name_e_h=ne_h;
		//通道名称位置
	name_x = (((*pn_x*g_rec_srceen_w[rec_group][ch])/SCREEN_DEF_WIDTH+7)/8)*8;
	name_y = (((*pn_y*g_rec_srceen_h[rec_group][ch])/SCREEN_DEF_HEIGHT+1)/2)*2;
	name_w = ((n_w+1)/2)*2;
	name_h = ((n_h*+1)/2)*2;
	//通道名称1位置
	name_e_x = (((*pne_x*g_rec_srceen_w[rec_group][ch])/SCREEN_DEF_WIDTH+7)/8)*8;
	name_e_y = (((*pne_y*g_rec_srceen_h[rec_group][ch])/SCREEN_DEF_HEIGHT+1)/2)*2;
	name_e_w = ((ne_w+1)/2)*2;
	name_e_h = ((ne_h*+1)/2)*2;
	//时间位置
	time_x = (((*pt_x*g_rec_srceen_w[rec_group][ch])/SCREEN_DEF_WIDTH+7)/8)*8;
	time_y = (((*pt_y*g_rec_srceen_h[rec_group][ch])/SCREEN_DEF_HEIGHT+1)/2)*2;
	time_w = ((t_w+1)/2)*2;
	time_h = ((t_h*+1)/2)*2;
	//比较3个区域是否有重叠
	OSD_Cmp_nameAtime(&name_x,&name_y,name_w,name_h,&name_e_x,&name_e_y,name_e_w,name_e_h,&time_x,&time_y,time_w,time_h);
	*pn_x = name_x;
	*pn_y = name_y;
	*pne_x = name_e_x;
	*pne_y = name_e_y;
	*pt_x = time_x;
	*pt_y = time_y;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########222222222222222222 name_x=%d,name_y=%d,name_w=%d,name_h=%d,time_x=%d,time_y=%d,time_w=%d,time_h=%d#############################\n",
	//	name_x,name_y,name_w,name_h,time_x,time_y,time_w,time_h);
	return 0;
}
#endif


/*************************************************
Function: //Rec_Slave_OsdStr_Create
Description: //创建从片录像部分通道OSD图片
Calls: 
Called By: //
Input: // p_Slave_info :从片OSD图片保存地址
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
int Prv_Rec_Slave_OsdStr_Create(unsigned char ch,PRV_VO_SLAVE_STAT_S *p_Slave_info)
{
//	HI_S32 s32Ret=0;
//	STRING_BMP_ST BmpData={0};

	if(ch < PRV_CHAN_NUM)
	{//如果主片通道号，那么返回失败
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Rec_Slave_OsdStr_Create Invalid Parameter: VoChn:%d!\n", ch);
		return HI_FAILURE;
	}
	//从片通道名称图片转换
	/*s32Ret = OSD_Get_String(OSD_Name_Buf[ch],&BmpData,OSD_GetRecFontSize(0,ch));
	if(s32Ret != STRINGBMP_ERR_NONE)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Get_String failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}*/
	return 0;
}
#if 1
//从片时间图片信息初始化接口
int Slave_Get_Time_InitInfo(Rec_Osd_Time_Info *pSlave_Time_Info,int Info_len)
{
	int i=0,cnt=Info_len;

	if(pSlave_Time_Info == NULL)
	{
		return -1;
	}
	if(Info_len > MAX_TIME_STR_LEN)
	{
		cnt = MAX_TIME_STR_LEN;
	}
	for(i=0;i<cnt;i++)
	{
		pSlave_Time_Info[i] = Rec_Osd_Str[i].osd_time;
	}

	return 0;	
}

#ifdef OPEN_REC_OSD	

//结束时，释放所有的图片内存
static int Rec_OSD_Bmp_Free(void)
{
	HI_S32 i=0,j=0;
	for(i=0;i<OSD_TIME_RES;i++)
	{
		for(j=0;j<MAX_TIME_STR_LEN;j++)
		{
			if(Rec_Osd_Str[j].pData[i] != NULL)
			{
				SN_FREE(Rec_Osd_Str[j].pData[i]);
			}
			Rec_Osd_Str[j].pData[i] = NULL;
		}
	}
	return  0;
}
#endif
//初始化图片所有字符位置
static int Rec_OSD_Bmp_Init(void)
{
	HI_S32 s32Ret=0,i=0,j=0,k=0,len=0;
	STRING_BMP_ST BmpData={0};
	int font[]={5,0,2,6,7,8};
	char Str[MAX_BMP_STR_LEN];
	if (MMI_GetLangID() == Chinese)
		SN_SPRINTF(Str,sizeof(Str),"0123456789 -/:星期一二三四五六日月年");
	else
		SN_SPRINTF(Str,sizeof(Str),"0123456789 -/:SundayMoTesWhrFit");
	len = strlen(Str);
	if(len > MAX_BMP_STR_LEN)
	{
		len = MAX_BMP_STR_LEN;
	}
	for(j=0,k=0;j<len && k<MAX_TIME_STR_LEN;j++,k++)
	{
		if(Str[j] & 0x80)
		{//为汉字
			Rec_Osd_Str[k].osd_time.T_str[0] = Str[j];
			Rec_Osd_Str[k].osd_time.T_str[1] = Str[j+1];
			Rec_Osd_Str[k].osd_time.T_str[2] = '\0';
			j++;
		}
		else
		{
			Rec_Osd_Str[k].osd_time.T_str[0] = Str[j];			
			Rec_Osd_Str[k].osd_time.T_str[1] = '\0';
		}
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################Rec_Osd_Str[k].T_str =%s,k=%d,j = %d,i=%d            1################1\n",Rec_Osd_Str[k].T_str,k,j,i);
	}
	Rec_OSD_Bmp_Lenth = k;
	for(i=0;i<OSD_TIME_RES;i++)
	{
		//获取3种分辨率下字符图片集
		for(j=0;j<Rec_OSD_Bmp_Lenth;j++)
		{
			Rec_Osd_Str[j].pData[i] = NULL;
			Rec_Osd_Str[j].osd_time.str_w_off[i] = 0;
			Rec_Osd_Str[j].osd_time.str_h_off[i] = 0;
			SN_MEMSET(&BmpData,0,sizeof(BmpData));
			//想GUI获取字符图片
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################Rec_Osd_Str[k].T_str =%s,j = %d,i=%d################1\n",Rec_Osd_Str[j].T_str,j,i);
			pthread_mutex_lock(&mutex_getbmp);
			s32Ret = Get_TimeBmp_From_Gui(Rec_Osd_Str[j].osd_time.T_str,&BmpData,font[i]);
			pthread_mutex_unlock(&mutex_getbmp);
			if(s32Ret != STRINGBMP_ERR_NONE)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Bmp_From_Gui failed,ret=0x%x!\n", __LINE__, s32Ret);
				return HI_FAILURE;
			}
			//保存字符图片
			Rec_Osd_Str[j].pData[i] = BmpData.pBmpData;
			Rec_Osd_Str[j].osd_time.str_w_off[i] = BmpData.width;
			Rec_Osd_Str[j].osd_time.str_h_off[i] = BmpData.height;
			//OSD_Free_String(&BmpData);
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################Rec_Osd_Str[k].str_w_off[i] = %d,   Rec_Osd_Str[k].str_g_off[i] = %d   ,Rec_Osd_Str[k].pData[i] =%x, ################1\n",
			//						Rec_Osd_Str[j].str_w_off[i],Rec_Osd_Str[j].str_h_off[i],Rec_Osd_Str[j].pData[i]);
		}
	}
	return HI_SUCCESS;
}
#if 0
//对比当前字符串
static int Rec_Osd_Cmp_str(unsigned char *pStr_Src,unsigned char *pStr_Dst,unsigned char *pNewStr,unsigned int str_len,unsigned char idx,unsigned int *pOffset)
{//查找当前的字符串中是否有不同的字符，返回第1个不同字符位置,并返回不同字符的后面所有字符
	unsigned int i=0,j=0,w_len=0;
	if(pStr_Src == NULL || pStr_Dst == NULL || pNewStr == NULL)
	{
		return -1;
	}
	SN_MEMCPY(pNewStr,str_len,&pStr_Dst[i],str_len,str_len);
	*pOffset = w_len;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################pNewStr =%s,   str_len = %d    ,i=%d    ,pOffset =%d###################2\n",pNewStr,str_len,i,*pOffset);
	return i;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################pStr_Src = %s,   pStr_Dst = %s   ,pNewStr =%s,   str_len = %d################1\n",pStr_Src,pStr_Dst,pNewStr,str_len);
	for(i=0;i<str_len;i++)
	{
		if(pStr_Dst[i] == '\0')
		{
			return i;
		}
		if(pStr_Dst[i] & 0x80)
		{//如果为汉字，那么需要对比2个字节
			if((pStr_Src[i] != pStr_Dst[i]) || (pStr_Src[i+1] != pStr_Dst[i+1]))
			{//查找到第1个不相同的字符
				SN_MEMCPY(pNewStr,str_len-i,&pStr_Dst[i],str_len-i,str_len-i);
				*pOffset = w_len;
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################pNewStr =%s,   str_len = %d    ,i=%d    ,pOffset =%d###################2\n",pNewStr,str_len,i,*pOffset);
				return i;
			}
			//计算当前的相同的字符串位移
			for(j=0;j<MAX_TIME_STR_LEN;j++)
			{
				if((pStr_Src[i] == Rec_Osd_Str[j].osd_time.T_str[0]) && (pStr_Src[i+1] == Rec_Osd_Str[j].osd_time.T_str[1]))
				{
					w_len = w_len + Rec_Osd_Str[j].osd_time.str_w_off[idx];
					break;
				}
			}
			i++;
		}
		else 
		{
			if(pStr_Src[i] != pStr_Dst[i])
			{//查找到第1个不相同的字符
				SN_MEMCPY(pNewStr,str_len-i,&pStr_Dst[i],str_len-i,str_len-i);
				*pOffset = w_len;
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################pNewStr =%s,   str_len = %d    ,i=%d    ,pOffset =%d##################2\n",pNewStr,str_len,i,*pOffset);
				return i;
			}
			//计算当前的相同的字符串位移
			for(j=0;j<MAX_TIME_STR_LEN;j++)
			{
				if(pStr_Src[i] == Rec_Osd_Str[j].osd_time.T_str[0])
				{
					w_len = w_len + Rec_Osd_Str[j].osd_time.str_w_off[idx];
					break;
				}
			}
		}
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################pNewStr =%s,   str_len = %d    ,i=%d    ,pOffset =%d###################2\n",pNewStr,str_len,i,*pOffset);
	}
	return 0;
}
#endif
//查找当前字符或者汉字的图片
static int Rec_Osd_Search_str(unsigned char *pStr,unsigned int str_len)
{//查找当前的字符串中是否有不同的字符，返回第1个不同字符位置,并返回不同字符的后面所有字符
	unsigned int i=0,j=0,ret=0;
	if(pStr == NULL)
	{
		return -1;
	}
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################pStr =%s,   str_len = %d  ##################3\n",pStr,str_len);
	for(i=0;i<str_len;i++)
	{
		for(j=0;j<MAX_TIME_STR_LEN;j++)
		{
			if(pStr[i] == '\0')
			{
				return j;
			}
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################Rec_Osd_Str =%s,   pStr[i] = %s  ,  i=%d      ,j=%d##################41\n",&Rec_Osd_Str[j].T_str[i],&pStr[i],i,j);
			if(pStr[i] & 0x80)
			{//如果为汉字，那么需要对比2个字节
				if((pStr[i] == Rec_Osd_Str[j].osd_time.T_str[0]) && (pStr[i+1] == Rec_Osd_Str[j].osd_time.T_str[1]))
				{//查找到第1个相同的字符
					ret = j;
					//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################Rec_Osd_Str =%s,   pStr[i] = %s  ,  i=%d      ,j=%d##################42\n",&Rec_Osd_Str[j].T_str[i],&pStr[i],i,j);
					return ret;
				}
			}
			else
			{//如果为字母或者数字
				if(pStr[i] == Rec_Osd_Str[j].osd_time.T_str[0])
				{//查找到第1个相同的字符
					ret = j;
					//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################Rec_Osd_Str =%s,   pStr[i] = %s  ,  i=%d      ,j=%d##################42\n",&Rec_Osd_Str[j].T_str[i],&pStr[i],i,j);
					return ret;
				}
			}
		}
	}
	return -1;
}
//计算当前字符串的宽度
static int Rec_Osd_Cal_strWidth(unsigned char idx,unsigned char *pStr,unsigned int str_len,unsigned int *pWidth)
{//查找当前的字符串中是否有不同的字符，返回第1个不同字符位置,并返回不同字符的后面所有字符
	unsigned int i=0,j=0,len=0;
	if(pStr == NULL || pWidth == NULL)
	{
		return -1;
	}
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################idx = %d,pStr =%s,   str_len = %d  ##################3\n",idx,pStr,str_len);
	for(i=0;i<str_len;i++)
	{
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################pStr[i] = %s,  i=%d ##################41\n",&pStr[i],i);
		if(pStr[i] == '\0')
		{
			*pWidth = len;
			return 0;
		}
		for(j=0;j<MAX_TIME_STR_LEN;j++)
		{
			if(pStr[i] & 0x80)
			{//如果为汉字，那么需要对比2个字节
				if((pStr[i] == Rec_Osd_Str[j].osd_time.T_str[0]) && (pStr[i+1] == Rec_Osd_Str[j].osd_time.T_str[1]))
				{//查找到第1个相同的字符
					len += Rec_Osd_Str[j].osd_time.str_w_off[idx];
					++i;
					//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################Rec_Osd_Str =%s,   pStr[i] = %s  ,  i=%d      ,j=%d  ,len=%d##################42\n",&Rec_Osd_Str[j].T_str[0],&pStr[i],i,j,len);
					break;
				}
			}
			else
			{//如果为字母或者数字
				if(pStr[i] == Rec_Osd_Str[j].osd_time.T_str[0])
				{//查找到第1个相同的字符
					len += Rec_Osd_Str[j].osd_time.str_w_off[idx];
					//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "####################Rec_Osd_Str =%s,   pStr[i] = %s  ,  i=%d      ,j=%d  ,len=%d###################42\n",&Rec_Osd_Str[j].T_str[0],&pStr[i],i,j,len);
					break;
				}
			}
		}
	}
	*pWidth = len;
	return 0;
}

/*************************************************
Function: //Pic_Joint
Description: //图片拼接
Calls: 
Called By: //
Input: // idx :录像通道组ID
		pStr:字符串
		pQStr:QCIF下字符串
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/	
static int Pic_Joint(unsigned char idx, char *pStr, char *pQStr)
{
	unsigned char i=0,j=0,k=0,num=1;
	HI_S32 diff_idx=0,str_idx=0,ret=0;
	unsigned int x_offset=0;
	char *pTime=pStr;//,*pSrc_Time=time_str_buf.Time_Str,Str_New[MAX_TIME_STR_LEN];
	unsigned int bmp_x=0,bmp_w=0, bmp_h=0,max_width=0,w_offset=0;
	
	switch(idx)
	{
		case 0:
			max_width = OSD_TIME_WIDTH;
			break;
		case 1:
			max_width = OSD_TIME_CIF_WIDTH;
			break;
		case 2:
		{
			max_width = OSD_TIME_QCIF_WIDTH;
			pTime = pQStr;
		}
			break;
		case 3:
		case 4:
		case 5:
			max_width = MAX_STRTIMEBMP_WIDTH;
			break;
		default:
			break;
	}
	//比较当前字符串与原来的字符串,返回的位置为数组中的位置加1	
	//diff_idx = Rec_Osd_Cmp_str(pSrc_Time,pTime,Str_New,MAX_TIME_STR_LEN,idx,&x_offset);
	/*if(diff_idx == -1)
	{//如果返回-1，说明字符串不正确
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"###########Rec_Osd_Cmp_str failed diff_idx = %d  4############################\n",diff_idx);
		return diff_idx;
	}*/
	if(time_str_buf.time_Bmp_param[idx].pBmpData == NULL)
	{
		return -1;
	}
	SN_MEMSET(time_str_buf.time_Bmp_param[idx].pBmpData,0,(MAX_STRTIMEBMP_WIDTH-1)*(MAX_STRBMP_HEIGHT-1)*PIXBYTE);
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "###########time_str_buf.time_Bmp_param[idx].width = %d,time_str_buf.time_Bmp_param[idx].heigth=%d i=%d,j=%d4############################\n",time_str_buf.time_Bmp_param[idx].width,time_str_buf.time_Bmp_param[idx].height,i,j);
	//计算当前字符串的最大宽度
	ret = Rec_Osd_Cal_strWidth(idx,(unsigned char*)pTime,MAX_TIME_STR_LEN,&w_offset);
	if(ret == -1)
	{
		w_offset = MAX_STRTIMEBMP_WIDTH;
	}
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "###########Pic_Joint   :w_offset = %d############################\n",w_offset);
	OSD_PRV_Time_Width[idx] = w_offset;//预览时间宽度
	if(max_width > w_offset)
	{
		w_offset = max_width;
	}
	w_offset += (2-(w_offset%2));
	
	if(MAX_STRTIMEBMP_WIDTH < w_offset)
	{//如果当前宽度大于最大的BMP宽度。那么赋值为最大宽度
		w_offset = MAX_STRTIMEBMP_WIDTH;
	}
	//拼图
	for(i=diff_idx,j=0;i<MAX_TIME_STR_LEN;i++,j++)
	{
		unsigned short *pdat = NULL,*pData=NULL;//(unsigned short *)pstStrBmp->pBmpData;
		if(pTime[j] == '\0')
		{
			break;
		}
		if(pTime[j] & 0x80)
		{//如果为汉字
			num=2;
			//当前的字符或者汉字在图片集中的位置
			str_idx = Rec_Osd_Search_str((unsigned char *)(&pTime[j]),num);
			if(str_idx == -1)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"###########Rec_Osd_Cmp_str failed str_idx = %d  41############################\n",str_idx);
				return str_idx;
			}
			j++;
			
		}
		else
		{
			num=1;
			//当前的字符或者汉字在图片集中的位置
			str_idx = Rec_Osd_Search_str((unsigned char *)(&pTime[j]),num);
			if(str_idx == -1)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"###########Rec_Osd_Cmp_str failed str_idx = %d  42############################\n",str_idx);
				return str_idx;
			}
		}
		bmp_w = Rec_Osd_Str[str_idx].osd_time.str_w_off[idx];
		bmp_h = Rec_Osd_Str[str_idx].osd_time.str_h_off[idx];
		pdat = (unsigned short *)Rec_Osd_Str[str_idx].pData[idx];
		pData = (unsigned short *)time_str_buf.time_Bmp_param[idx].pBmpData;
		//bmp_x = MAX_STRTIMEBMP_WIDTH;
		for(k=0;k<bmp_h;k++)
		{
			bmp_x = w_offset*k+x_offset;
			SN_MEMCPY((unsigned char *)&pData[bmp_x],bmp_w*PIXBYTE,(unsigned char *)&pdat[bmp_w*k],bmp_w*PIXBYTE,bmp_w*PIXBYTE);	
		}
		x_offset += bmp_w;
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "###########bmp_x = %d, x_offset= %d,,bmp_w = %d,bmp_h = %d,idx=%d############################\n",bmp_x,x_offset,bmp_w,bmp_h,idx);
	}
	time_str_buf.time_Bmp_param[idx].height= Rec_Osd_Str[0].osd_time.str_h_off[idx];
	time_str_buf.time_Bmp_param[idx].width = w_offset;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "###########bmp_x = %d, x_offset= %d,,bmp_w = %d,idx=%d############################\n",bmp_x,x_offset,bmp_w,idx);
#if 0 /*2010-8-30 观察：从GUI获取的BMP数据内容*/
	static int fp_cnt = 0;
	if (fp_cnt<100)
	{
		int ii=0,ij=0;
		FILE *pf = fopen("bmp4.dat", "a");
		unsigned short *dat = (unsigned short *)time_str_buf.time_Bmp_param[idx].pBmpData;
		if(NULL!=pf)
		{
			fprintf(pf,"string: %s\n", Str_New);
			for (ii=0;ii<time_str_buf.time_Bmp_param[idx].height;ii++)
			{
				for (ij=0;ij<time_str_buf.time_Bmp_param[idx].width; ij++)
				{
					fprintf(pf, "%04x ", dat[ii*MAX_STRTIMEBMP_WIDTH + ij]);
				}
				fputc('\n', pf);
			}
			fputc('\n', pf);
			fclose(pf);
		}
		fp_cnt++;
	}
#endif
	return 0;
}

static int	Rec_Osd_TimeInfo_Init(unsigned char time_type)
{
	int i=0,s32Ret=0;
	char time_buf[MAX_BMP_STR_LEN],qtime_buf[MAX_BMP_STR_LEN];
	struct tm newtime;
	time_t ltime;
	//时间
	//时间图片加载
	time(&ltime);
	localtime_r(&ltime, &newtime);
	switch(time_type)
	{
		case 0:
			if (MMI_GetLangID() == Chinese)
				SN_SPRINTF(time_buf,sizeof(time_buf), "%04d年%02d月%02d日 %s %02d:%02d:%02d",newtime.tm_year + 1900,newtime.tm_mon + 1,newtime.tm_mday,weekday[newtime.tm_wday],newtime.tm_hour,newtime.tm_min,newtime.tm_sec);
			else
				SN_SPRINTF(time_buf,sizeof(time_buf), "%04d-%02d-%02d  %s %02d:%02d:%02d",newtime.tm_year + 1900,newtime.tm_mon + 1,newtime.tm_mday,weekday_en[newtime.tm_wday],newtime.tm_hour,newtime.tm_min,newtime.tm_sec);
			SN_SPRINTF(qtime_buf,sizeof(qtime_buf), "%04d-%02d-%02d %02d:%02d:%02d",newtime.tm_year + 1900,newtime.tm_mon + 1,newtime.tm_mday,newtime.tm_hour,newtime.tm_min,newtime.tm_sec);				
			break;
		case 1:
			if (MMI_GetLangID() == Chinese)
				SN_SPRINTF(time_buf,sizeof(time_buf), "%02d月%02d日%04d年 %s %02d:%02d:%02d",newtime.tm_mon + 1,newtime.tm_mday,newtime.tm_year + 1900,weekday[newtime.tm_wday],newtime.tm_hour,newtime.tm_min,newtime.tm_sec);
			else
				SN_SPRINTF(time_buf,sizeof(time_buf), "%02d-%02d-%04d  %s %02d:%02d:%02d",newtime.tm_mon + 1,newtime.tm_mday,newtime.tm_year + 1900,weekday_en[newtime.tm_wday],newtime.tm_hour,newtime.tm_min,newtime.tm_sec);
			SN_SPRINTF(qtime_buf,sizeof(qtime_buf), "%02d-%02d-%04d %02d:%02d:%02d",newtime.tm_mon + 1,newtime.tm_mday,newtime.tm_year + 1900,newtime.tm_hour,newtime.tm_min,newtime.tm_sec);				
			break;
		case 2:
			if (MMI_GetLangID() == Chinese)
				SN_SPRINTF(time_buf,sizeof(time_buf), "%02d日%02d月%04d年 %s %02d:%02d:%02d",newtime.tm_mday,newtime.tm_mon + 1,newtime.tm_year + 1900,weekday[newtime.tm_wday],newtime.tm_hour,newtime.tm_min,newtime.tm_sec);
			else
				SN_SPRINTF(time_buf,sizeof(time_buf), "%02d-%02d-%04d  %s %02d:%02d:%02d",newtime.tm_mday,newtime.tm_mon + 1,newtime.tm_year + 1900,weekday_en[newtime.tm_wday],newtime.tm_hour,newtime.tm_min,newtime.tm_sec);
			SN_SPRINTF(qtime_buf,sizeof(qtime_buf), "%02d-%02d-%04d %02d:%02d:%02d",newtime.tm_mday,newtime.tm_mon + 1,newtime.tm_year + 1900,newtime.tm_hour,newtime.tm_min,newtime.tm_sec);				
			break;	
	}
	s32Ret = Rec_OSD_Bmp_Init();
	if(s32Ret == HI_FAILURE)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_OSD_Bmp_Init failed,ret=0x%x!\n", __LINE__, s32Ret);
		return HI_FAILURE;
	}
    else
    {
#if defined (SN8616D_LE) || defined(SN8616M_LE)|| defined(SN9234H1)
        PRV_Send_RecOsdTime2Slave();
#endif
    }
	for(i=0;i<OSD_TIME_RES;i++)
	{
		s32Ret = Pic_Joint(i,time_buf,qtime_buf);
		if(s32Ret == HI_FAILURE)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Pic_Joint failed,ret=0x%x!\n", __LINE__, s32Ret);
			return HI_FAILURE;
		}
	}
	SN_MEMCPY(time_str_buf.Time_Str,MAX_BMP_STR_LEN,time_buf,MAX_BMP_STR_LEN,MAX_BMP_STR_LEN);
	SN_MEMCPY(time_str_buf.qTime_Str,MAX_BMP_STR_LEN,qtime_buf,MAX_BMP_STR_LEN,MAX_BMP_STR_LEN);
	return 0;
}
#endif

/*************************************************
Function: //Rec_Create_region
Description: //录像部分OSD，创建区域接口函数
Calls: 
Called By: //
Input: // ch :通道号
		type:区域类型
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int Rec_Create_region(unsigned char rec_group,unsigned char ch,unsigned char type,int x,int y,int w,int h,unsigned char u32BgAlpha,unsigned char u32fgAlpha,unsigned int u32BgColor)
{
#if defined(SN9234H1)
	HI_S32 s32Ret=0;
	REGION_ATTR_S stRgnAttr;	
	unsigned char  venc_ch=OSD_GetRecGroupChn(rec_group,ch);

	stRgnAttr.enType = OVERLAY_REGION;
	stRgnAttr.unAttr.stOverlay.bPublic = HI_FALSE;
	stRgnAttr.unAttr.stOverlay.enPixelFmt = PIXEL_FORMAT_RGB_1555;
	stRgnAttr.unAttr.stOverlay.stRect.s32X= x;
	stRgnAttr.unAttr.stOverlay.stRect.s32Y= y;
	stRgnAttr.unAttr.stOverlay.stRect.u32Width = ((w+1)/2)*2;
	stRgnAttr.unAttr.stOverlay.stRect.u32Height = ((h+1)/2)*2;
	stRgnAttr.unAttr.stOverlay.u32BgAlpha = u32BgAlpha;
	stRgnAttr.unAttr.stOverlay.u32FgAlpha = u32fgAlpha;
	stRgnAttr.unAttr.stOverlay.u32BgColor = u32BgColor;
	stRgnAttr.unAttr.stOverlay.VeGroup = venc_ch;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "LINE:%d, rec_group = %d,ch name:  s_x=%d,s_y=%d,x=%d,y=%d,w=%d,h=%d  ,type=%d,g_rec_srceen_w[rec_group][ch]=%d,g_rec_srceen_w[rec_group][ch]=%d\n",
	//		__LINE__,rec_group,x,y,stRgnAttr.unAttr.stOverlay.stRect.s32X,stRgnAttr.unAttr.stOverlay.stRect.s32Y,stRgnAttr.unAttr.stOverlay.stRect.u32Width,stRgnAttr.unAttr.stOverlay.stRect.u32Height,type,g_rec_srceen_w[rec_group][ch],g_rec_srceen_h[rec_group][ch]);
	s32Ret = HI_MPI_VPP_CreateRegion(&stRgnAttr, &rec_osd_handle[venc_ch][type]);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,HI_MPI_VPP_CreateRegion err 0x%x! ch =%d: %s\n",__LINE__,s32Ret,ch, PRV_GetErrMsg(s32Ret));

		return HI_FAILURE;
	}
#else
	HI_S32 s32Ret=0;
	RGN_ATTR_S stRgnAttr;	
	//int i = 0;
	RGN_HANDLE handle = 0;
    MPP_CHN_S stChn;
	RGN_CHN_ATTR_S stChnAttr;
	//unsigned char venc_ch  = 0;
	unsigned char  venc_ch=OSD_GetRecGroupChn(rec_group,ch);
	handle = COVER_MAX_SIZE*DEV_CHANNEL_NUM + venc_ch*type+type+1;
	/*for(i=0;i<3;i++)
	{
		if(rec_osd_handle[ch*3 + i][type] > COVER_MAX_SIZE*DEV_CHANNEL_NUM)
		{
			continue;
		}
		else if(rec_osd_handle[ch*3 + i][type] == 0)
		{
			venc_ch = COVER_MAX_SIZE*DEV_CHANNEL_NUM+ch*3 + i;
			break;
		}
	}
	if(i>=3)
	{
		printf("Rec_Create_region no handle ch:%d\n",ch);
		return -1;
	}*/
	//unsigned char venc_ch = COVER_MAX_SIZE*DEV_CHANNEL_NUM + type*DEV_CHANNEL_NUM + ch;
	stRgnAttr.enType = OVERLAY_RGN;
    stRgnAttr.unAttr.stOverlay.enPixelFmt = PIXEL_FORMAT_RGB_1555;
	stRgnAttr.unAttr.stOverlay.stSize.u32Width  = ((w+1)/2)*2;
    stRgnAttr.unAttr.stOverlay.stSize.u32Height = ((h+1)/2)*2;
	stRgnAttr.unAttr.stOverlay.u32BgColor = 0x00;

	/*create region*/
	printf("Rec_Create_region venc_ch:%d",handle);
	 s32Ret = HI_MPI_RGN_Create(handle, &stRgnAttr);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,HI_MPI_RGN_Create err 0x%x\n",__LINE__,s32Ret);
		return HI_FAILURE;
	}
	rec_osd_handle[venc_ch][type] = handle;
	stChn.enModId = HI_ID_GROUP;
    stChn.s32DevId = 0;
    stChn.s32ChnId = 0;
    
    memset(&stChnAttr,0,sizeof(stChnAttr));
    stChnAttr.bShow = HI_TRUE;
    stChnAttr.enType = OVERLAY_RGN;
    stChnAttr.unChnAttr.stOverlayChn.stPoint.s32X =x;
    stChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y =y;
    stChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = u32BgAlpha;
    stChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = u32fgAlpha;
    stChnAttr.unChnAttr.stOverlayChn.u32Layer = 0;

    stChnAttr.unChnAttr.stOverlayChn.stQpInfo.bAbsQp = HI_FALSE;
    stChnAttr.unChnAttr.stOverlayChn.stQpInfo.s32Qp  = 0;
	printf("#########x=%d,y=%d,u32BgAlpha=%d,u32fgAlpha=%d,venc_ch=%d\n",x,y,u32BgAlpha,u32fgAlpha,ch);
    s32Ret = HI_MPI_RGN_AttachToChn(handle, &stChn, &stChnAttr);
    if(HI_SUCCESS != s32Ret)
    {
        printf("HI_MPI_RGN_AttachToChn (%d) failed with %#x!\n",\
               handle, s32Ret);
        return HI_FAILURE;
    }
#endif
	return s32Ret;
}

/*************************************************
Function: //Rec_SetOSD_Bmp
Description: //录像部分OSD，字符串设置函数
Calls: 
Called By: //
Input: // ch :通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
int Rec_SetOSD_Bmp(unsigned char rec_group,unsigned char ch,unsigned char type,REGION_CTRL_PARAM_U *pParam)
{
	HI_S32 s32Ret=0;	
	//unsigned char venc_ch  = 0;
	unsigned char  venc_ch=OSD_GetRecGroupChn(rec_group,ch);
#if defined(SN9234H1)
	s32Ret = Set_Vpp_ControlBMP(rec_osd_handle[venc_ch][type],pParam);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Set_Vpp_ControlBMP failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}

#else
	s32Ret = HI_MPI_RGN_SetBitMap(rec_osd_handle[venc_ch][type],&(pParam->stBitmap));
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,HI_MPI_VPP_ControlRegion 0x%x!\n",__LINE__,s32Ret);

		return HI_FAILURE;
	}
#endif
	return s32Ret;
}

/*************************************************
Function: //Rec_OSD_Ctl
Description: //录像部分OSD显示、隐藏控制函数
Calls: 
Called By: //
Input: // ch :通道号
		rec_group: 通道编码组id
		on: 1--显示、0---隐藏
		type: 控制图标类型
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
int Rec_OSD_Ctl(unsigned char rec_group, unsigned char ch, unsigned char on, unsigned char type)
{//OSD显示或者关闭,1:显示；0:关闭
	//type->  0:表示报警图标;  1:表示录像图标 2:表示时间;  3:表示通道名称; 

#if defined(SN9234H1)
	HI_S32 s32Ret=0,i=0;
	REGION_CRTL_CODE_E enCtrl;
	REGION_CTRL_PARAM_U unParam;
	unsigned char  venc_ch=ch,group_mum=0;

	//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,OSD_Ctl  ch = %d,on= %d  type = %d\n", __LINE__,ch,on,type);
	if ((type != OSD_NAME_LAYER) && (type != OSD_TIME_LAYER))
	{
		return HI_SUCCESS;
	}
	if(on)
		enCtrl = REGION_SHOW;
	else
		enCtrl = REGION_HIDE;
	if(type > REGION_NUM) 
		return HI_FAILURE;
	if(rec_group==REC_CTRL_ALL)
	{
		group_mum = REC_OSD_GROUP;
		i=0;
	}
	else
	{
		group_mum = rec_group+1;
		i = rec_group;
	}
	for(;i<group_mum;i++)
	{
		venc_ch = OSD_GetRecGroupChn(i,ch);
		s32Ret = HI_MPI_VPP_ControlRegion(rec_osd_handle[venc_ch][type], enCtrl, &unParam);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_MPI_VPP_ControlRegion failed,rec_osd_handle[venc_ch][type] = %d,ret=0x%x!\n", __LINE__, rec_osd_handle[venc_ch][type],s32Ret);
		}
	}
#else
	HI_S32 s32Ret=0,i=0;
	 MPP_CHN_S stChn;
    RGN_CHN_ATTR_S stChnAttr;

   
	unsigned char  venc_ch = ch,group_mum = 0;

	//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,OSD_Ctl  ch = %d,on= %d  type = %d\n", __LINE__,ch,on,type);
	if ((type != OSD_NAME_LAYER) && (type != OSD_TIME_LAYER))
	{
		return HI_SUCCESS;
	}
	
	if(type > REGION_NUM) 
		return HI_FAILURE;
	if(rec_group == REC_CTRL_ALL)
	{
		group_mum = REC_OSD_GROUP;
		i=0;
	}
	else
	{
		group_mum = rec_group+1;
		i = rec_group;
	}
	for(; i < group_mum; i++)
	{
		venc_ch = OSD_GetRecGroupChn(i, ch);
		stChn.enModId = HI_ID_GROUP;
	    stChn.s32DevId = 0;
	    stChn.s32ChnId = ch;
		s32Ret = HI_MPI_RGN_GetDisplayAttr(rec_osd_handle[venc_ch][type], &stChn, &stChnAttr);
	    if(HI_SUCCESS != s32Ret)
	    {
	        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"HI_MPI_RGN_GetDisplayAttr (%d)) failed with %#x!\n",\
	               rec_osd_handle[venc_ch][type], s32Ret);
	        return HI_FAILURE;
	    }
		if(on)
			stChnAttr.bShow = HI_TRUE;
		else
			stChnAttr.bShow = HI_FALSE;
		s32Ret = HI_MPI_RGN_SetDisplayAttr(rec_osd_handle[venc_ch][type], &stChn, &stChnAttr);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,show faild 0x%x!!!\n",__LINE__,s32Ret);
			return HI_FAILURE;
		}
	}
#endif	
	return s32Ret;
}
//****************************************************
//录像部分OSD，时间位置设置函数
//*****************************************************
#if 0
static int Rec_SetOSD_Time_xy(unsigned char rec_group,unsigned char ch,int x,int y)
{//配置时间位置
	HI_S32 s32Ret=0;
	int temp_x = x,temp_y=y;
	unsigned char  venc_ch=OSD_GetRecGroupChn(rec_group,ch);

	//if(g_rec_srceen_w[rec_group][ch] == SCREEN_QCIF_WIDTH)
	//{//如果当前通道录像分辨率为QCIF模式，那么时间位置不需要改变
		//temp_x = OSD_REC_TIME_X;
		//temp_y = OSD_REC_TIME_Y;
		//temp_x = (((temp_x *g_rec_srceen_w[rec_group][ch])/SCREEN_DEF_WIDTH+7)/8)*8;
		//temp_y = (((temp_y *g_rec_srceen_h[rec_group][ch])/SCREEN_DEF_HEIGHT+1)/2)*2;
	//}
	
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########temp_x = %d,temp_y=%d,g_rec_srceen_w[rec_group][ch] =%d,h=%d############\n",temp_x,temp_y,g_rec_srceen_w[rec_group][ch],g_rec_srceen_h[rec_group][ch]);
#if 1 /*2010-9-2 修正：录像时间OSD超出画面边界*/
		temp_x = (temp_x+time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Width > g_rec_srceen_w[rec_group][ch]) ? (g_rec_srceen_w[rec_group][ch]-time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Width) : temp_x;
		temp_y = (temp_y+time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Height > g_rec_srceen_h[rec_group][ch]) ? (g_rec_srceen_h[rec_group][ch]-time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Height) : temp_y;
#endif

	s32Ret = Rec_OSD_Set_xy(rec_osd_handle[venc_ch][OSD_TIME_LAYER],temp_x,temp_y);
	return s32Ret;
}
#endif
//****************************************************
//录像部分OSD，设置通道名称位置
//*****************************************************

int Rec_SetOSD_CH_xy(unsigned char rec_group,unsigned char ch,int x,int y)
{//配置通道位置
	HI_S32 s32Ret=0;
	int temp_x = x,temp_y=y;
	unsigned char  venc_ch= OSD_GetRecGroupChn(rec_group,ch);
/*	
	if(g_rec_srceen_w[rec_group][ch] == SCREEN_QCIF_WIDTH)
	{//如果当前通道录像分辨率为QCIF模式，那么通道名称位置不需要改变
		temp_x = OSD_NAME_X;
		temp_y = OSD_NAME_Y;
		temp_x = (((temp_x *g_rec_srceen_w[rec_group][ch])/SCREEN_DEF_WIDTH+7)/8)*8;
		temp_y = (((temp_y *g_rec_srceen_h[rec_group][ch])/SCREEN_DEF_HEIGHT+1)/2)*2;
	}
*/
#if 1 /*2010-9-2 修正：录像通道名称OSD超出画面边界*/
	temp_x = (temp_x+name_icon_param[rec_group][ch].stBitmap.u32Width > g_rec_srceen_w[rec_group][ch]) ? (g_rec_srceen_w[rec_group][ch]-name_icon_param[rec_group][ch].stBitmap.u32Width) : temp_x;
	temp_y = (temp_y+name_icon_param[rec_group][ch].stBitmap.u32Height > g_rec_srceen_h[rec_group][ch]) ? (g_rec_srceen_h[rec_group][ch]-name_icon_param[rec_group][ch].stBitmap.u32Height) : temp_y;
#endif
#if defined(SN9234H1)
	s32Ret = Rec_OSD_Set_xy(rec_osd_handle[venc_ch][OSD_NAME_LAYER],temp_x,temp_y);
#else
	s32Ret = Rec_OSD_Set_xy(rec_osd_handle[venc_ch][OSD_NAME_LAYER],ch,temp_x,temp_y);
#endif
	return s32Ret;
}
#if 0
/*************************************************
Function: //Rec_SetOSD_xy
Description: //录像部分OSD，通道名称\时间位置设置
Calls: 
Called By: //
Input: // ch :通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int Rec_SetOSD_xy(unsigned char rec_group,unsigned char ch,int n_x,int n_y,int t_x,int t_y)
{
	int s32Ret=0,venc_ch=0;
	TDE2_RECT_S stTmpRect,tmpRect;	
	int name_x=n_x,name_y=n_y,time_x=t_x,time_y=t_y;
	unsigned char vo_dev = s_VoDevCtrlDflt;
	if(g_rec_srceen_w[rec_group][ch] == SCREEN_QCIF_WIDTH)
	{//如果当前通道录像分辨率为QCIF模式，那么通道名称不需要改变 
		return 0;
	}
#ifdef SECOND_DEV	
	if(s_VoDevCtrlDflt == s_VoSecondDev)
	{
		vo_dev = AD;
	}
#endif		
	stTmpRect=OSD_Rect[vo_dev][ch][OSD_TIME_LAYER];
	tmpRect = OSD_Rect[vo_dev][ch][OSD_NAME_LAYER];
	
	Rec_OSD_Cmp_nameAtime(rec_group,ch,&name_x,&name_y,name_icon_param[rec_group][ch].stBitmap.u32Width,name_icon_param[rec_group][ch].stBitmap.u32Height,&time_x,&time_y,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Width,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Height);
	venc_ch = OSD_GetRecGroupChn(rec_group,ch);
	if(HI_MPI_VPP_DestroyRegion(rec_osd_handle[venc_ch][OSD_TIME_LAYER]) == HI_FAILURE)	//
	{
		return HI_FAILURE;
	}
	s32Ret = Rec_SetOSD_CH_xy(rec_group,ch,name_x,name_y);
	if(0>s32Ret)
	{
		OSD_Rect[vo_dev][ch][OSD_NAME_LAYER] = tmpRect;
		name_x = (((OSD_Rect[vo_dev][ch][OSD_NAME_LAYER].s32Xpos *g_rec_srceen_w[rec_group][ch])/SCREEN_DEF_WIDTH+7)/8)*8;
		name_y = (((OSD_Rect[vo_dev][ch][OSD_NAME_LAYER].s32Ypos *g_rec_srceen_h[rec_group][ch])/SCREEN_DEF_HEIGHT+1)/2)*2;
		Rec_SetOSD_CH_xy(rec_group,ch,name_x,name_y);
		return s32Ret;
	}
	s32Ret = Rec_Create_region(rec_group,ch,OSD_TIME_LAYER, time_x,time_y,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Width,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Height,OSD_BG_ALPHAL,OSD_FG_ALPHAL,OSD_BG_COLOR);
	if (HI_SUCCESS!=s32Ret)
	{
		OSD_Rect[vo_dev][ch][OSD_TIME_LAYER] = stTmpRect;
		time_x = (((OSD_Rect[vo_dev][ch][OSD_TIME_LAYER].s32Xpos *g_rec_srceen_w[rec_group][ch])/SCREEN_DEF_WIDTH+7)/8)*8;
		time_y = (((OSD_Rect[vo_dev][ch][OSD_TIME_LAYER].s32Ypos *g_rec_srceen_h[rec_group][ch])/SCREEN_DEF_HEIGHT+1)/2)*2;
		Rec_Create_region(rec_group,ch,OSD_TIME_LAYER, time_x,time_y,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Width,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Height,OSD_BG_ALPHAL,OSD_FG_ALPHAL,OSD_BG_COLOR);
		return s32Ret;
	}
	s32Ret =  Rec_SetOSD_Bmp(rec_group,ch,OSD_TIME_LAYER,&time_str_buf.time_icon_param[rec_group][ch]);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d, Rec_SetOSD_Bmp faild 0x%x!\n",__LINE__,s32Ret);
		//OSD_Free_String(&BmpData);
		return s32Ret;
	}
	//OSD_Free_String(&BmpData);
	if(!(OSD_off_flag[vo_dev][ch] & OSD_TIME_OFF))
	{
		s32Ret = Rec_OSD_Ctl(rec_group,ch,1,OSD_TIME_LAYER);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_OSD_Ctl failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
	}
	return 0;
}
#endif
/*************************************************
Function: //Rec_ResetOSD_BmpInfo
Description: //录像部分OSD，通道名称字符串信息形成函数
Calls: 
Called By: //
Input: // ch :通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int Rec_ResetOSD_BmpInfo(unsigned char rec_group,unsigned char ch)
{
	HI_S32 s32Ret=0;
	unsigned char idx=0,*pName=OSD_Name_Buf[ch],name_str[4];
	STRING_BMP_ST BmpData={0};
	int name_x=0,name_y=0,time_x=0,time_y=0;
#if defined(SN9234H1)
	REGION_CTRL_PARAM_U unParam,unParam_t;
#else
	RGN_ATTR_S unParam,unParam_t;
#endif
	unsigned char venc_ch=OSD_GetRecGroupChn(rec_group,ch);
	unsigned char vo_dev=s_VoDevCtrlDflt;
	
#ifdef SECOND_DEV	
		if(s_VoDevCtrlDflt == s_VoSecondDev)
		{
			vo_dev = AD;
		}
#endif	
	name_x=OSD_Rect[vo_dev][ch][OSD_NAME_LAYER].s32Xpos;
	name_y=OSD_Rect[vo_dev][ch][OSD_NAME_LAYER].s32Ypos;
	time_x=OSD_Rect[vo_dev][ch][OSD_TIME_LAYER].s32Xpos;
	time_y=OSD_Rect[vo_dev][ch][OSD_TIME_LAYER].s32Ypos;

	if(g_rec_srceen_w[rec_group][ch] == SCREEN_QCIF_WIDTH)
	{//如果当前通道录像分辨率为QCIF模式，那么通道名称不需要改变		
		SN_SPRINTF((char*)name_str,sizeof(name_str),"%d",ch+1);
		pName = name_str;
		//name_x = OSD_NAME_X;
		//name_y = OSD_NAME_Y;
		//time_x = OSD_REC_TIME_X;
		//time_y = OSD_REC_TIME_Y;
	}
	//更新时间	
	OSD_Time_Str_BmpData_idx(rec_group,ch,&idx);
	
	//获取字符图片
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "##########Rec_ResetOSD_BmpInfo pstr = %s  #############\n",pName);
	s32Ret = Get_Bmp_From_Gui(pName,&BmpData,OSD_GetRecFontSize(rec_group,ch));
	if(s32Ret != STRINGBMP_ERR_NONE)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Bmp_From_Gui failed,ret=0x%x!\n", __LINE__, s32Ret);
		return HI_FAILURE;
	}
	//需要对新的XY进行重新计算	
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############3333 name_x =%d,name_y = %d,time_x = %d,time_y = %d################\n",name_x,name_y,time_x,time_y);
	Rec_OSD_Cmp_nameAtime(rec_group,ch,&name_x,&name_y,BmpData.width,BmpData.height,&time_x,&time_y,time_str_buf.time_Bmp_param[idx].width,time_str_buf.time_Bmp_param[idx].height);
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########rec_group=%d,ch=%d,name_x=%d,name_y=%d,name_w=%d,name_h=%d,time_x=%d,time_y=%d,time_w=%d,time_h=%d,g_rec_srceen_w[rec_group][ch]=%d,g_rec_srceen_h[rec_group][ch]=%d#############################\n",
	//			rec_group,ch,name_x,name_y,BmpData.width,BmpData.height,time_x,time_y,
	//			time_str_buf.time_Bmp_param[idx].width,time_str_buf.time_Bmp_param[idx].height,g_rec_srceen_w[rec_group][ch],g_rec_srceen_h[rec_group][ch]);

#if defined(SN9234H1)
	//记录当前的名称区域信息
	HI_MPI_VPP_ControlRegion(rec_osd_handle[venc_ch][OSD_NAME_LAYER], REGION_GET_SIGNLE_ATTR, &unParam);
	HI_MPI_VPP_ControlRegion(rec_osd_handle[venc_ch][OSD_TIME_LAYER], REGION_GET_SIGNLE_ATTR, &unParam_t);
	
	//删除名称的OSD区域
	s32Ret = HI_MPI_VPP_DestroyRegion(rec_osd_handle[venc_ch][OSD_NAME_LAYER]);
	s32Ret = HI_MPI_VPP_DestroyRegion(rec_osd_handle[venc_ch][OSD_TIME_LAYER]);

#else
	//记录当前的名称区域信息
	HI_MPI_RGN_GetAttr(rec_osd_handle[venc_ch][OSD_NAME_LAYER], &unParam);
	HI_MPI_RGN_GetAttr(rec_osd_handle[venc_ch][OSD_TIME_LAYER], &unParam_t);
	
	//删除名称的OSD区域
	s32Ret = HI_MPI_RGN_Destroy(rec_osd_handle[venc_ch][OSD_NAME_LAYER]);
	s32Ret = HI_MPI_RGN_Destroy(rec_osd_handle[venc_ch][OSD_TIME_LAYER]);
#endif	
	//更新字符
	s32Ret = Set_BmpByMem(&time_str_buf.time_Bmp_param[idx],&time_str_buf.time_icon_param[rec_group][ch]);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Set_BmpByMem failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
	//重新创建时间OSD区域
	s32Ret = Rec_Create_region(rec_group,ch,OSD_TIME_LAYER,time_x,time_y,time_str_buf.time_Bmp_param[idx].width,time_str_buf.time_Bmp_param[idx].height,OSD_BG_ALPHAL,OSD_FG_ALPHAL,OSD_BG_COLOR);
	if(s32Ret ==HI_FAILURE )
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_Create_region ,h=%d w = %d,group=%d,venc_ch = %d,ch=%d\n", __LINE__, g_rec_srceen_h[rec_group][ch],g_rec_srceen_w[rec_group][ch],rec_group,venc_ch,ch);
#if defined(SN9234H1)
		HI_MPI_VPP_CreateRegion(&unParam_t.stRegionAttr,&rec_osd_handle[venc_ch][OSD_TIME_LAYER]);
#else
		HI_MPI_RGN_Create(rec_osd_handle[venc_ch][OSD_TIME_LAYER],&unParam_t);
#endif
		return s32Ret;
	}
	s32Ret =  Rec_SetOSD_Bmp(rec_group,ch,OSD_TIME_LAYER,&time_str_buf.time_icon_param[rec_group][ch]);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d, Rec_SetOSD_Bmp faild 0x%x!\n",__LINE__,s32Ret);
		return s32Ret;
	}
	if(!(OSD_off_flag[vo_dev][ch] & OSD_TIME_OFF))
	{
		TRACE(SCI_TRACE_NORMAL,MOD_PRV,"###2222222pParam->stBitmap.u32Width = %d,pBmpData->width=%d,pParam->stBitmap.u32Height=%d,pBmpData->height=%d##################\n",time_str_buf.time_Bmp_param[idx].width,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Width,time_str_buf.time_Bmp_param[idx].height,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Height);
		s32Ret = Rec_OSD_Ctl(rec_group,ch,1,OSD_TIME_LAYER);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_OSD_Ctl failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
	}
	//更新字符
	s32Ret = Set_BmpByMem(&BmpData,&name_icon_param[rec_group][ch]);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Set_BmpByMem failed,ret=0x%x!\n", __LINE__, s32Ret);
		OSD_Free_String(&BmpData);
		return s32Ret;
	}
	//重新创建名称OSD区域
	s32Ret = Rec_Create_region(rec_group,ch,OSD_NAME_LAYER,name_x,name_y,BmpData.width,BmpData.height,OSD_BG_ALPHAL,OSD_FG_ALPHAL,OSD_BG_COLOR);
	if(s32Ret ==HI_FAILURE )
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_Create_region ,h=%d w = %d,group=%d,venc_ch = %d,ch=%d\n", __LINE__, g_rec_srceen_h[rec_group][ch],g_rec_srceen_w[rec_group][ch],rec_group,venc_ch,ch);
#if defined(SN9234H1)
		HI_MPI_VPP_CreateRegion(&unParam.stRegionAttr,&rec_osd_handle[venc_ch][OSD_NAME_LAYER]);
#else
		HI_MPI_RGN_Create(rec_osd_handle[venc_ch][OSD_NAME_LAYER],&unParam);
#endif
		OSD_Free_String(&BmpData);
		return s32Ret;
	}
	s32Ret =  Rec_SetOSD_Bmp(rec_group,ch,OSD_NAME_LAYER,&name_icon_param[rec_group][ch]);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d, Rec_SetOSD_Bmp faild 0x%x!\n",__LINE__,s32Ret);
		return s32Ret;
	}
	OSD_Free_String(&BmpData);
	if(!(OSD_off_flag[vo_dev][ch] & OSD_NAME_OFF))
	{
		TRACE(SCI_TRACE_NORMAL,MOD_PRV,"###OSD_NAME :pParam->stBitmap.u32Width = %d,pBmpData->width=%d,pParam->stBitmap.u32Height=%d,pBmpData->height=%d##################\n",name_icon_param[rec_group][ch].stBitmap.u32Width,BmpData.width,name_icon_param[rec_group][ch].stBitmap.u32Height,BmpData.height);
		s32Ret = Rec_OSD_Ctl(rec_group,ch,1,OSD_NAME_LAYER);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_OSD_Ctl failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
	}
	return HI_SUCCESS;
}
#if 0
/*************************************************
Function: //Rec_GetOSD_Name_BmpInfo
Description: //录像部分OSD，通道名称字符串信息形成函数
Calls: 
Called By: //
Input: // ch :通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int Rec_GetOSD_Name_BmpInfo(unsigned char rec_group,unsigned char ch,unsigned char *pstr,STRING_BMP_ST *pBmpData)
{
	HI_S32 s32Ret=0;
	int name_x=0,name_y=0,time_x=0,time_y=0;
	unsigned char vo_dev=s_VoDevCtrlDflt;
#ifdef SECOND_DEV	
		if(s_VoDevCtrlDflt == s_VoSecondDev)
		{
			vo_dev = AD;
		}
#endif	
	name_x=OSD_Rect[vo_dev][ch][OSD_NAME_LAYER].s32Xpos;
	name_y=OSD_Rect[vo_dev][ch][OSD_NAME_LAYER].s32Ypos;
	time_x=OSD_Rect[vo_dev][ch][OSD_TIME_LAYER].s32Xpos;
	time_y=OSD_Rect[vo_dev][ch][OSD_TIME_LAYER].s32Ypos;
	
	//获取字符图片
	s32Ret = Get_Bmp_From_Gui(pstr,pBmpData,OSD_GetRecFontSize(rec_group,ch));
	if(s32Ret != STRINGBMP_ERR_NONE)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Bmp_From_Gui failed,ret=0x%x!\n", __LINE__, s32Ret);
		return HI_FAILURE;
	}
	
	//需要对新的XY进行重新计算
	Rec_OSD_Cmp_nameAtime(rec_group,ch,&name_x,&name_y,pBmpData->width,pBmpData->height,&time_x,&time_y,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Width,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Height);
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########rec_group=%d,ch=%d,name_x=%d,name_y=%d,name_w=%d,name_h=%d,time_x=%d,time_y=%d,time_w=%d,time_h=%d,g_rec_srceen_w[rec_group][ch]=%d,g_rec_srceen_h[rec_group][ch]=%d#############################\n",
	//			rec_group,ch,name_x,name_y,pBmpData->width,pBmpData->height,time_x,time_y,
	//			time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Width,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Height,g_rec_srceen_w[rec_group][ch],g_rec_srceen_h[rec_group][ch]);
	//检查是否需要重新配置区域
	if((name_icon_param[rec_group][ch].stBitmap.u32Width != pBmpData->width) || (name_icon_param[rec_group][ch].stBitmap.u32Height != pBmpData->height))
	{//区域变化，那么需要重建区域
		REGION_CTRL_PARAM_U unParam,unParam_t;
		unsigned char venc_ch=OSD_GetRecGroupChn(rec_group,ch);
		
		//记录当前的名称区域信息
		HI_MPI_VPP_ControlRegion(rec_osd_handle[venc_ch][OSD_NAME_LAYER], REGION_GET_SIGNLE_ATTR, &unParam);
		HI_MPI_VPP_ControlRegion(rec_osd_handle[venc_ch][OSD_TIME_LAYER], REGION_GET_SIGNLE_ATTR, &unParam_t);
		//删除名称的OSD区域
		s32Ret = HI_MPI_VPP_DestroyRegion(rec_osd_handle[venc_ch][OSD_NAME_LAYER]);
		//重新设置时间位置
		Rec_SetOSD_Time_xy(rec_group,ch,time_x,time_y);
		//重新创建名称OSD区域
		s32Ret = Rec_Create_region(rec_group,ch,OSD_NAME_LAYER,name_x,name_y,pBmpData->width,pBmpData->height,OSD_BG_ALPHAL,OSD_FG_ALPHAL,OSD_BG_COLOR);
		if(s32Ret ==HI_FAILURE )
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_Create_region ,h=%d w = %d,group=%d,venc_ch = %d,ch=%d\n", __LINE__, g_rec_srceen_h[rec_group][ch],g_rec_srceen_w[rec_group][ch],rec_group,venc_ch,ch);
			HI_MPI_VPP_CreateRegion(&unParam.stRegionAttr,&rec_osd_handle[venc_ch][OSD_NAME_LAYER]);
			return s32Ret;
		}
	}
	if(!(OSD_off_flag[vo_dev][ch] & OSD_NAME_OFF))
	{
		//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"###pParam->stBitmap.u32Width = %d,pBmpData->width=%d,pParam->stBitmap.u32Height=%d,pBmpData->height=%d##################\n",name_icon_param[rec_group][ch].stBitmap.u32Width,pBmpData->width,name_icon_param[rec_group][ch].stBitmap.u32Height,pBmpData->height);
		s32Ret = Rec_OSD_Ctl(rec_group,ch,1,OSD_NAME_LAYER);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_OSD_Ctl failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
	}
	//更新字符
	s32Ret = Set_BmpByMem(pBmpData,&name_icon_param[rec_group][ch]);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Set_BmpByMem failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
	return HI_SUCCESS;
}
#endif
#if 0
/*************************************************
Function: //Rec_GetOSD_Time_BmpInfo
Description: //录像部分OSD，时间字符串图片形成函数
Calls: 
Called By: //
Input: // ch :通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
int Rec_GetOSD_Time_BmpInfo(unsigned char *pstr,unsigned char *pQstr)
{
	HI_S32 s32Ret=0,i=0;//,j=0,len=0;
	//STRING_BMP_ST BmpData;
	int font[]={5,0, 2};
	unsigned char *pTime=pstr;//,strrmp[3];
	//
	for(i=0;i<OSD_TIME_RES;i++)
	{
		pTime=pstr;
		if(i == 2)
		{//最后一个是QCIF的图片，所以要转换字符
			 pTime=pQstr;
			 //TRACE(SCI_TRACE_NORMAL, MOD_PRV, "#############Rec_GetOSD_Time_BmpInfo pTime=%s###############\n",pTime);
		}
		//获取D1大小字符图片
		s32Ret = Get_Bmp_From_Gui(pTime,&time_str_buf.time_Bmp_param[i],font[i]);
		if(s32Ret != STRINGBMP_ERR_NONE)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Bmp_From_Gui failed,ret=0x%x!\n", __LINE__, s32Ret);
			return HI_FAILURE;
		}
	}
	//while(1);
	return HI_SUCCESS;
}
#endif
static void PRV_OSD_RECT_CHANGE_NOTIFY(int chn)
{
    Set_chn_osd_Rsp rsp = {{0},0};
    rsp.chn = chn+1; /*  编号从1-PRV_CHAN_NUM  */
    SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_VAM, 0, 0, 
        MSG_ID_PRV_SET_CHN_OSD_RSP, &rsp, sizeof(rsp));
}

/*************************************************
Function: //Rec_SetOSD_Time_Icon
Description: //录像部分OSD，时间字符串信息形成函数
Calls: 
Called By: //
Input: // ch :通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
int Rec_SetOSD_Time_Icon(unsigned char rec_group,unsigned char ch)
{
	HI_S32 s32Ret=0, osd_rect_change = 0;
	unsigned char idx=0;
	int name_x=0,name_y=0,time_x=0,time_y=0;
	unsigned char vo_dev=s_VoDevCtrlDflt;
	//	
#ifdef SECOND_DEV	
		if(s_VoDevCtrlDflt == s_VoSecondDev)
		{
			vo_dev = AD;
		}
#endif	
	name_x=OSD_Rect[vo_dev][ch][OSD_NAME_LAYER].s32Xpos;
	name_y=OSD_Rect[vo_dev][ch][OSD_NAME_LAYER].s32Ypos;
	time_x=OSD_Rect[vo_dev][ch][OSD_TIME_LAYER].s32Xpos;
	time_y=OSD_Rect[vo_dev][ch][OSD_TIME_LAYER].s32Ypos;
	
	if(time_str_buf.Change_flag[rec_group][ch])
	{//如果此标志位为1，表示时间或者通道名称的区域有变化，那么重新设置这两个区域
		Rec_ResetOSD_BmpInfo(rec_group,ch);
        osd_rect_change = 1;
	}
	else
	{
		/*if(g_rec_srceen_w[rec_group][ch] == SCREEN_QCIF_WIDTH)
		{//如果当前通道录像分辨率为QCIF模式，那么通道名称不需要改变	
			name_x = OSD_NAME_X;
			name_y = OSD_NAME_Y;
			time_x = OSD_REC_TIME_X;
			time_y = OSD_REC_TIME_Y;
		}*/
		//更新时间	
		OSD_Time_Str_BmpData_idx(rec_group,ch,&idx);
		
		//判断是否需要重新配置区域
		if((time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Width != time_str_buf.time_Bmp_param[idx].width) || (time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Height != time_str_buf.time_Bmp_param[idx].height))
		{//区域变化，那么需要重建区域
#if defined(SN9234H1)
			REGION_CTRL_PARAM_U unParam;
#else
			RGN_ATTR_S unParam;
#endif
			unsigned char venc_ch=OSD_GetRecGroupChn(rec_group,ch);

            osd_rect_change = 1;
			//需要对新的XY进行重新计算
			Rec_OSD_Cmp_nameAtime(rec_group,ch,&name_x,&name_y,name_icon_param[rec_group][ch].stBitmap.u32Width,name_icon_param[rec_group][ch].stBitmap.u32Height,&time_x,&time_y,time_str_buf.time_Bmp_param[idx].width,time_str_buf.time_Bmp_param[idx].height);
		
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########rec_group=%d,ch=%d,name_x=%d,name_y=%d,name_w=%d,name_h=%d,time_x=%d,time_y=%d,time_w=%d,time_h=%d,g_rec_srceen_w[rec_group][ch]=%d,g_rec_srceen_h[rec_group][ch]=%d#############################\n",
			//	rec_group,ch,name_x,name_y,name_icon_param[rec_group][ch].stBitmap.u32Width,name_icon_param[rec_group][ch].stBitmap.u32Height,
			//	time_x,time_y,time_str_buf.time_Bmp_param[idx].width,time_str_buf.time_Bmp_param[idx].height,g_rec_srceen_w[rec_group][ch],g_rec_srceen_h[rec_group][ch]);
#if defined(SN9234H1)
			//记录当前的名称区域信息
			HI_MPI_VPP_ControlRegion(rec_osd_handle[venc_ch][OSD_TIME_LAYER], REGION_GET_SIGNLE_ATTR, &unParam);
			//删除名称的OSD区域
			s32Ret = HI_MPI_VPP_DestroyRegion(rec_osd_handle[venc_ch][OSD_TIME_LAYER]);
#else
			//记录当前的名称区域信息
			HI_MPI_RGN_GetAttr(rec_osd_handle[venc_ch][OSD_TIME_LAYER],&unParam);
			//删除名称的OSD区域
			s32Ret = HI_MPI_RGN_Destroy(rec_osd_handle[venc_ch][OSD_TIME_LAYER]);
#endif			
			//重新设置时间位置
			Rec_SetOSD_CH_xy(rec_group,ch,name_x,name_y);
			
			//重新创建名称OSD区域
			s32Ret = Rec_Create_region(rec_group,ch,OSD_TIME_LAYER,time_x,time_y,time_str_buf.time_Bmp_param[idx].width,time_str_buf.time_Bmp_param[idx].height,OSD_BG_ALPHAL,OSD_FG_ALPHAL,OSD_BG_COLOR);
			if(s32Ret ==HI_FAILURE )
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_Create_region ,h=%d w = %d,group=%d,venc_ch = %d,ch=%d\n", __LINE__, g_rec_srceen_h[rec_group][ch],g_rec_srceen_w[rec_group][ch],rec_group,venc_ch,ch);
#if defined(SN9234H1)
				HI_MPI_VPP_CreateRegion(&unParam.stRegionAttr,&rec_osd_handle[venc_ch][OSD_TIME_LAYER]);
#else
				HI_MPI_RGN_Create(rec_osd_handle[venc_ch][OSD_TIME_LAYER],&unParam);
#endif
				return s32Ret;
			}			
			if(!(OSD_off_flag[vo_dev][ch] & OSD_TIME_OFF))
			{
				//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,Rec_SetOSD_Time_Icon Rec_Create_region ,ch =%d,idx = %d,rec_group = %d,I_W=%d,b_w=%d,I_H =%d,b_h=%d\n", 
				//		__LINE__,ch,idx,rec_group,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Width ,time_str_buf.time_Bmp_param[idx].width,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Height ,time_str_buf.time_Bmp_param[idx].height);
				s32Ret = Rec_OSD_Ctl(rec_group,ch,1,OSD_TIME_LAYER);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_OSD_Ctl failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
			}
			//更新字符
			s32Ret = Set_BmpByMem(&time_str_buf.time_Bmp_param[idx],&time_str_buf.time_icon_param[rec_group][ch]);
			if(s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Set_BmpByMem failed,ret=0x%x!\n", __LINE__, s32Ret);
				return s32Ret;
			}
		}
	}
    
    if (osd_rect_change && REC_MAINSTREAM == rec_group)
    {
        PRV_OSD_RECT_CHANGE_NOTIFY(ch);
    }
	return HI_SUCCESS;
}

//****************************************************
//录像部分OSD，设置通道名称
//*****************************************************
#if 0
int Rec_SetOSD_CH(unsigned char rec_group,unsigned char ch,unsigned char *str)
{//通道名称
	HI_S32 s32Ret=0;
	STRING_BMP_ST BmpData={0};
	unsigned char *pName=str;
	unsigned char venc_ch=ch;
	//调用字符转BMP函数
		
	if(g_rec_srceen_w[rec_group][ch] == SCREEN_QCIF_WIDTH)
	{//如果当前通道录像分辨率为QCIF模式，那么通道名称不需要改变
		return 0;
	}
	//获取通道名称图片
	s32Ret = Rec_GetOSD_Name_BmpInfo(rec_group,ch,pName,&BmpData);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Set_BmpByMem failed,ret=0x%x!\n", __LINE__, s32Ret);
		OSD_Free_String(&BmpData);
		return s32Ret;
	}
	//设置通道名称图片到录像OSD上
	venc_ch = OSD_GetRecGroupChn(rec_group,ch);
	TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,Rec_SetOSD_CH SUC,pName = %s,h=%d w = %d,group=%d,venc_ch = %d,ch=%d\n", __LINE__, pName,g_rec_srceen_h[rec_group][ch],g_rec_srceen_w[rec_group][ch],rec_group,venc_ch,ch);
	s32Ret = Set_Vpp_ControlBMP(rec_osd_handle[venc_ch][OSD_NAME_LAYER],&name_icon_param[rec_group][ch]);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d, Rec_SetOSD_CH faild 0x%x!\n", __LINE__,s32Ret);
		OSD_Free_String(&BmpData);
		return s32Ret;
	}
	OSD_Free_String(&BmpData);
	return s32Ret;
}
#endif
//****************************************************
//录像部分OSD，单通道初始化函数
//*****************************************************

int Rec_OSD_Init_Ch(unsigned char ch)
{
	HI_S32 s32Ret;
	unsigned char rec_group=0;
	STRING_BMP_ST BmpData = {0};
	int x_name=0;
	int y_name=0;
	int x_time=0;
	int y_time=0;
	//int w_name=0;
	unsigned char vo_dev=s_VoDevCtrlDflt;
	unsigned char name_buf[CHANNEL_NAME_LEN],*pName = OSD_Name_Buf[ch];
	unsigned char idx = 0;
	
    
#ifdef SECOND_DEV	
	if(s_VoDevCtrlDflt == s_VoSecondDev)
	{
		vo_dev = AD;
	}
#endif	
	
	for(rec_group = 0;rec_group<REC_OSD_GROUP;rec_group++)
	{
        int w;
        
        if(ch < PRV_CHAN_NUM)
        {
            w = g_rec_srceen_w[rec_group][ch];
        }
        else
        {
            w = s_slaveVoStat.f_rec_srceen_w[rec_group][ch%PRV_CHAN_NUM];
        }
		x_name=OSD_Rect[vo_dev][ch][OSD_NAME_LAYER].s32Xpos;
		y_name=OSD_Rect[vo_dev][ch][OSD_NAME_LAYER].s32Ypos;
		x_time=OSD_Rect[vo_dev][ch][OSD_TIME_LAYER].s32Xpos;
		y_time=OSD_Rect[vo_dev][ch][OSD_TIME_LAYER].s32Ypos;
		pName = OSD_Name_Buf[ch];
		if(w == SCREEN_QCIF_WIDTH)
		{//如果当前通道录像分辨率为QCIF模式，那么通道名称固定，时间缩小，位置固定
			//通道名称
			SN_SPRINTF((char*)name_buf,sizeof(name_buf),"%d",ch+1);
			pName = name_buf;
			//x_name = OSD_NAME_X;
			//y_name = OSD_NAME_Y;
			//x_time = OSD_REC_TIME_X;
			//y_time = OSD_REC_TIME_Y;
		}
		//通道名称
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@@@@@@@@@@@@Rec_OSD_Init_Ch   : rec_group =%d,ch=%d,  pName = %s\n",rec_group,ch,pName);
        bzero(&BmpData, sizeof(BmpData));
        s32Ret = Get_Bmp_From_Gui(pName,&BmpData,OSD_GetRecFontSize(rec_group,ch));
		if(s32Ret != STRINGBMP_ERR_NONE)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Bmp_From_Gui failed,ret=0x%x!\n", __LINE__, s32Ret);
			OSD_Free_String(&BmpData);
			return HI_FAILURE;
		}
		//更新字符
		if (ch < PRV_CHAN_NUM)
		{
			s32Ret = Set_BmpByMem(&BmpData,&name_icon_param[rec_group][ch]);
		}
		else
		{
            if(name_icon_param_slave[rec_group][ch%PRV_CHAN_NUM].stBitmap.pData)
            {
                SN_FREE(name_icon_param_slave[rec_group][ch%PRV_CHAN_NUM].stBitmap.pData);
            }
			s32Ret = Set_BmpByMem(&BmpData,&name_icon_param_slave[rec_group][ch%PRV_CHAN_NUM]);
		}
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Set_BmpByMem failed,ret=0x%x!\n", __LINE__, s32Ret);
			OSD_Free_String(&BmpData);
			return s32Ret;
		}
        if (ch>=PRV_CHAN_NUM)
        {
            continue;//不能FREE BmpData，要到之后有更新的时候再FREE p_name_icon_param对应的BMP。
        }
		//更新时间		
		OSD_Time_Str_BmpData_idx(rec_group,ch,&idx);
		//更新字符
		s32Ret = Set_BmpByMem(&time_str_buf.time_Bmp_param[idx],&time_str_buf.time_icon_param[rec_group][ch]);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Set_BmpByMem failed,ret=0x%x!\n", __LINE__, s32Ret);
			OSD_Free_String(&BmpData);
			return s32Ret;
		}
		//重新计算时间和通道名称的位置，防止位置重叠或者超出范围
		Rec_OSD_Cmp_nameAtime(rec_group,ch,&x_name,&y_name,BmpData.width,BmpData.height,&x_time,&y_time,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Width,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Height);
		
		s32Ret = Rec_Create_region(rec_group,ch,OSD_NAME_LAYER,x_name,y_name,name_icon_param[rec_group][ch].stBitmap.u32Width,name_icon_param[rec_group][ch].stBitmap.u32Height,OSD_BG_ALPHAL,OSD_FG_ALPHAL,OSD_BG_COLOR);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_Create_region failed,ret=0x%x!\n", __LINE__, s32Ret);
			OSD_Free_String(&BmpData);
			return s32Ret;
		}
		s32Ret = Rec_SetOSD_Bmp(rec_group,ch,OSD_NAME_LAYER,&name_icon_param[rec_group][ch]);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d, Rec_SetOSD_Bmp faild 0x%x!\n",__LINE__,s32Ret);
			OSD_Free_String(&BmpData);
			return s32Ret;
		}
		OSD_Free_String(&BmpData);
		//时间
		s32Ret = Rec_Create_region(rec_group,ch,OSD_TIME_LAYER,x_time,y_time,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Width,time_str_buf.time_icon_param[rec_group][ch].stBitmap.u32Height,OSD_BG_ALPHAL,OSD_FG_ALPHAL,OSD_BG_COLOR);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,Rec_Create_region err 0x%x! ch =%d\n",__LINE__,s32Ret,ch);
			//OSD_Free_String(&BmpData);
			return HI_FAILURE;
		}
		s32Ret =  Rec_SetOSD_Bmp(rec_group,ch,OSD_TIME_LAYER,&time_str_buf.time_icon_param[rec_group][ch]);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d, Rec_SetOSD_Bmp faild 0x%x!\n",__LINE__,s32Ret);
			//OSD_Free_String(&BmpData);
			return s32Ret;
		}
		//OSD_Free_String(&BmpData);
	}
    if (ch < PRV_CHAN_NUM)
    {
    	if(!(OSD_off_flag[vo_dev][ch] & OSD_TIME_OFF))
    	{
    		s32Ret = Rec_OSD_Ctl(REC_CTRL_ALL,ch,1,OSD_TIME_LAYER);
    		if(s32Ret < 0)
    		{
    			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_OSD_Ctl failed,ret=0x%x!\n", __LINE__, s32Ret);
    			return s32Ret;
    		}
    	}

    	if(!(OSD_off_flag[vo_dev][ch] & OSD_NAME_OFF))
    	{
    		s32Ret = Rec_OSD_Ctl(REC_CTRL_ALL,ch,1,OSD_NAME_LAYER);
    		if(s32Ret < 0)
    		{
    			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_OSD_Ctl failed,ret=0x%x!\n", __LINE__, s32Ret);
    			return s32Ret;
    		}
    	}
    }
	//Rec_SetOSD_Time_xy(rec_group,ch,x_time,y_time);
	//Rec_SetOSD_CH_xy(rec_group,ch,x_name,y_name);
	return 0; 
}
//****************************************************
//录像部分OSD，单通道关闭函数
//*****************************************************

int Rec_OSD_Close_ch(unsigned char ch)
{
	HI_S32 i=0,j=0;
	HI_S32 s32Ret=0;	
	unsigned char  venc_ch=ch;
	
	for(j=0;j<REC_OSD_GROUP;j++)
	{
		venc_ch = OSD_GetRecGroupChn(j,ch);
		for(i=0;i<REGION_NUM;i++)
		{
#if defined(SN9234H1)
			s32Ret = HI_MPI_VPP_DestroyRegion(rec_osd_handle[venc_ch][i]);
#else
			s32Ret = HI_MPI_RGN_Destroy(rec_osd_handle[venc_ch][i]);
#endif
			if(s32Ret == HI_FAILURE)
			{
				continue;
			}
			rec_osd_handle[venc_ch][i] =0;
		}
	}		
	return s32Ret;
}
//****************************************************
//录像部分OSD，初始化函数接口
//*****************************************************

int Rec_OSD_Init(void)
{
	HI_S32 s32Ret=0,i=0;
/*	
	//报警图片转换
	SN_MEMSET(&alarm_icon_param, 0, sizeof(REGION_CTRL_PARAM_U));
	s32Ret = Set_BmpByFile(ALARM_ICON_PATH,&alarm_icon_param);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,Set_BmpByFile err 0x%x!\n",__LINE__,s32Ret);
		return HI_FAILURE;
	}
	//录像图片转换
	SN_MEMSET(&rec_icon_param, 0, sizeof(REGION_CTRL_PARAM_U));
	s32Ret = Set_BmpByFile(REC_ICON_PATH, &rec_icon_param);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"LINE:%d,Set_BmpByFile err 0x%x!\n",__LINE__,s32Ret);
		return HI_FAILURE;
	}
	//获取时间字符图片
	s32Ret = Rec_GetOSD_Time_BmpInfo(time_buf,qtime_buf);
	if(s32Ret != 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_GetOSD_Time_BmpInfo failed,ret=0x%x!\n", __LINE__, s32Ret);
		return HI_FAILURE;
	}	
	SN_MEMCPY(time_str_buf.Time_Str,MAX_BMP_STR_LEN,time_buf,MAX_BMP_STR_LEN,MAX_BMP_STR_LEN);
*/
	for(i=0;i<PRV_CHAN_NUM*PRV_CHIP_NUM;i++)
	{
		s32Ret = Rec_OSD_Init_Ch(i);
		if(s32Ret == HI_FAILURE)
		{
            TRACE(SCI_TRACE_HIGH,MOD_PRV,"Rec_OSD_Init_Ch: %d fail!", i);
			break;
		}
        else if (i >= PRV_CHAN_NUM)
        {
            PRV_Send_RecOsdName2Slave(i%PRV_CHAN_NUM);
        }
	}
	TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,Rec_OSD_Init suc,ret=0x%x!\n", __LINE__, s32Ret);
	return s32Ret;
}
//****************************************************
//录像部分OSD，关闭函数接口
//*****************************************************
#ifdef OPEN_REC_OSD	
static int Rec_OSD_Close(void)
{
	int i=0,ret=0;//,rec_group=0;
/*	for(rec_group=0;rec_group<REC_OSD_GROUP;rec_group++)
	{
		for(i=0;i<g_Max_Vo_Num;i++)
		{
			if(name_icon_param[rec_group][i].stBitmap.pData != NULL)
			{
				SN_FREE(name_icon_param[rec_group][i].stBitmap.pData);
				name_icon_param[rec_group][i].stBitmap.pData = NULL;
			}
		}
	}*/
	for(i=0;i<OSD_TIME_RES;i++)
	{
		if(time_str_buf.time_Bmp_param[i].pBmpData != NULL)
		{
			SN_FREE(time_str_buf.time_Bmp_param[i].pBmpData);
		}
		time_str_buf.time_Bmp_param[i].pBmpData = NULL;
	}
/*	if(rec_icon_param.stBitmap.pData != NULL)
	{
		SN_FREE(rec_icon_param.stBitmap.pData);
		rec_icon_param.stBitmap.pData = NULL;
	}
	if(alarm_icon_param.stBitmap.pData != NULL)
	{
		SN_FREE(alarm_icon_param.stBitmap.pData);
		alarm_icon_param.stBitmap.pData = NULL;
	}*/
	Rec_OSD_Bmp_Free();
	for(i=0;i<PRV_CHAN_NUM;i++)
	{
		ret = Rec_OSD_Close_ch(i);
		if(ret == HI_FAILURE)
		{
			break;
		}
	}
	return ret;
}
#endif
//----------------------------------------------
//预览部分OSD字符显示接口函数
//
//
static int Prv_OSD_Ctl_Off(unsigned char devid,unsigned char ch,unsigned char type,TDE2_RECT_S *pRect,unsigned char flag);
static int Prv_Disp_Pic(unsigned char devid,unsigned char ch,TDE_HANDLE tde_s32Handle);
static int PRV_Osd_Chn_reflesh(unsigned char devid,unsigned char ch);

//****************************************************
//预览部分OSD，图片转换函数，内存数据转换接口
//*****************************************************

static HI_S32 TDE_CreateSurface(STRING_BMP_ST *pBmpData,TDE2_SURFACE_S *pstSurface, HI_U8 *pu8Virt)
{
	HI_U32 colorfmt,stride;
	//int i=0;
	unsigned  int size=0;
	if((NULL == pBmpData) || (NULL == pstSurface) || (NULL == pBmpData->pBmpData))
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"%s, LINE %d, NULL ptr!\n", __FUNCTION__, __LINE__);
		return -1;
	}
#if defined(SN9234H1)
	colorfmt = 5;
#else
	colorfmt = TDE2_COLOR_FMT_ARGB1555;
#endif
	stride = pBmpData->width*(pBmpData->pixelbyte);
	pstSurface->enColorFmt = colorfmt;
	pstSurface->u32Width = pBmpData->width;
	pstSurface->u32Height = pBmpData->height;
	pstSurface->u32Stride = stride;
	pstSurface->u8Alpha0 = 55;
	pstSurface->u8Alpha1 = 255;
	pstSurface->bAlphaMax255 = HI_TRUE;
#if defined(Hi3531)||defined(Hi3535)	
	pstSurface->bAlphaExt1555 = HI_TRUE;
#endif
	size = stride*(pBmpData->height);
	//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,pstSurface->enColorFmt = %d,u32Width = %d,u32Height = %d,u32Stride = %d\n",
	//		__LINE__, pstSurface->enColorFmt,pstSurface->u32Width,pstSurface->u32Height,pstSurface->u32Stride);
#if !defined(USE_UI_OSD)
	SN_MEMCPY(pu8Virt,size, pBmpData->pBmpData,size, size);
#endif
	/*for(i=0;i<size;i=i+2)
	{
	pu8Virt[i] = pu8Virt[i] | 0x03;
}*/
	return 0;
}
//****************************************************
//预览部分OSD，图片转换函数，BMP图片直接转换接口
//*****************************************************

static HI_S32 TDE_CreateSurfaceByFile(const unsigned char *pszFileName, TDE2_SURFACE_S *pstSurface, HI_U8 *pu8Virt)
{
	FILE *fp;
#if defined(Hi3531)||defined(Hi3535)
#if !defined(USE_UI_OSD)
	unsigned short* color= NULL;
	int i = 0;
#endif
#endif	
	HI_U32 colorfmt, w, h, stride;
	
	if((NULL == pszFileName) || (NULL == pstSurface))
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"%s, LINE %d, NULL ptr!\n", __FUNCTION__, __LINE__);
		return -1;
	}
	
	fp = fopen((char*)pszFileName, "rb");
	if(NULL == fp)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"error when open pszFileName %s, line:%d\n", pszFileName, __LINE__);
		return -1;
	}
	
	fread(&colorfmt, 1, 4, fp);
	fread(&w, 1, 4, fp);
	fread(&h, 1, 4, fp);
	fread(&stride, 1, 4, fp);
#if defined(Hi3531)||defined(Hi3535)	
	colorfmt = TDE2_COLOR_FMT_ARGB1555;
#endif	
	 pstSurface->enColorFmt = colorfmt;
    pstSurface->u32Width = w;
    pstSurface->u32Height = h;
    pstSurface->u32Stride = stride;
    pstSurface->u8Alpha0 = 0x00;
    pstSurface->u8Alpha1 = 0xff;
    pstSurface->bAlphaMax255 = HI_TRUE;
#if defined(Hi3531)||defined(Hi3535)
    pstSurface->bAlphaExt1555 = HI_TRUE;
#endif	
#if !defined(USE_UI_OSD)
	fread(pu8Virt, 1, stride*h, fp);
	//TRACE(SCI_TRACE_HIGH,MOD_PRV,"pstSurface->enColorFmt = %d,u32Width = %d,u32Height = %d,u32Stride = %d&&&&&&&&&&&&&&&\n",
	//			pstSurface->enColorFmt,pstSurface->u32Width,pstSurface->u32Height,pstSurface->u32Stride);
#if defined(Hi3531)||defined(Hi3535)
	color = (unsigned short*)pu8Virt;
	for(i=0;i<stride*h/2;i++)
	{
		if((*color & 0x7FFF) == 0)
			*color = 0x00;
		color++;
	}
#endif	
#endif
	fclose(fp);
	
	return 0;
}
//****************************************************
//预览部分OSD，获取当前预览模式下的通道号
//*****************************************************

static int Get_Cur_idx(unsigned char devid,unsigned char ch)
{
	int i=0;
	unsigned char vo_dev = devid;
#ifdef SECOND_DEV
	if(devid == AD)
	{
		vo_dev = SD;
	}
#endif	
	//TRACE(SCI_TRACE_HIGH,MOD_PRV,"##############Get_Cur_idx vo_dev=%d,ch = %d##################\n",vo_dev,ch);
	for(i=0;i<preview_cur_param[vo_dev].ch_num ;i++)
	{
		if(preview_cur_param[vo_dev].ch_order[i] == ch)
		{
			return i;
		}
	}
	return -1;
}
//OSD 预览模式枚举
//enum OSD_PreviewMode_enum{Single_Scene=1,Two_Scene=2,Four_Scene=4,Six_Scene=6,Eight_Scene=8,Nine_Scene=9,Sixteen_Scene=16};

//****************************************************
//预览部分OSD，根据当前预览模式缩放osd大小
//*****************************************************

static int Get_Cur_Rect(unsigned char devid,unsigned char ch,TDE2_RECT_S *pDstRect,TDE2_RECT_S *pSrcRect,unsigned char type)
{
#if defined(SN9234H1)
	if(devid == SPOT_VO_DEV || devid == AD)
		return -1;
#else
	if(devid > DHD0)
		return -1;
#endif	
	HI_S32 s32indx=0;
	int w=0,h=0,ratio=0,ratio_x=0;
	unsigned char vo_dev = devid;
	//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,Get_Cur_Rect,devid = %d,ch_num=%d, prv_mode = %d ,ch = %d\n", __LINE__,devid,preview_cur_param[devid].ch_num, preview_cur_param[devid].prv_mode,ch);
	if(pDstRect == NULL || pSrcRect ==NULL)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Get_Cur_Rect error ,point NULL\n");
		return -1;
	}
#if 1 /*2010-11-4 启用了通道隐藏功用！*/
	if(ch>=DEV_CHANNEL_NUM|| devid>=PRV_VO_DEV_NUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ch=%d, dev=%d\n",ch,devid);
		return OSD_NOT_IN_MODE;
	}
#endif
#ifdef SECOND_DEV
		if(devid == AD)
		{
			vo_dev = SD;
		}
#endif	
	if(type != OSD_TIME_LAYER)
	{
		s32indx = Get_Cur_idx(devid,ch);
		if(s32indx < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_idx failed,ret=0x%x!\n", __LINE__, s32indx);
			pDstRect->s32Xpos = 0;
			pDstRect->s32Ypos= 0;
			pDstRect->u32Width= 0;
			pDstRect->u32Height = 0;
			return OSD_NOT_IN_MODE;
		}
	}
	switch(type)
	{
		case OSD_REC_LAYER:
		case OSD_ALARM_LAYER:	
		case OSD_CLICKVISUAL_LAYER:
			ratio = 1;//GUI_DIS_RATIO;
			ratio_x = 0;
			break;	
		case OSD_NAME_LAYER:
		case OSD_TIME_LAYER:
			ratio = GUI_DIS_RATIO;
			ratio_x = 0;
			break;	
		default:
			ratio = GUI_DIS_RATIO;
			ratio_x = 0;
			break;
	}
	//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"!!!!!Get_Cur_Rect   devid = %d,ch = %d,s32indx = %d,s_astVoDevStatDflt[devid].enPreviewMode = %d,pSrcRect->s32Xpos = %d,pSrcRect->s32Ypos=%d,pSrcRect->u32Width=%d,pSrcRect->u32Height=%d\n",
	//	devid,ch,s32indx,preview_cur_param[devid].prv_mode,pSrcRect->s32Xpos,pSrcRect->s32Ypos,pSrcRect->u32Width,pSrcRect->u32Height);
	switch(preview_cur_param[vo_dev].prv_mode)
	{
		case SingleScene:
			{
				w = preview_cur_param[vo_dev].w;
				h = preview_cur_param[vo_dev].h;
				pDstRect->s32Xpos = pSrcRect->s32Xpos*w/ SCREEN_DEF_WIDTH;
				pDstRect->s32Ypos= pSrcRect->s32Ypos*h/SCREEN_DEF_HEIGHT;
				pDstRect->u32Width= (pSrcRect->u32Width*w)/ SCREEN_DEF_WIDTH;
				pDstRect->u32Height = (pSrcRect->u32Height*h)/SCREEN_DEF_HEIGHT;
			}
			break;
#if defined(SN9234H1)
		case TwoScene:
			{
				s32indx = s32indx%2;
				w = preview_cur_param[vo_dev].w/2;
				h = preview_cur_param[vo_dev].h/2;
				pDstRect->s32Xpos = (pSrcRect->s32Xpos*w/ SCREEN_DEF_WIDTH) + s32indx*w;
				pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT) + h/2;
				pDstRect->u32Width= (pSrcRect->u32Width*w)/SCREEN_DEF_WIDTH;
				pDstRect->u32Height = (pSrcRect->u32Height*h)/SCREEN_DEF_HEIGHT;
			}
			break;
#endif
        case ThreeScene: 
			{
				s32indx = s32indx%3;
				w = preview_cur_param[vo_dev].w/3;
				h = preview_cur_param[vo_dev].h/3;
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@!!!!!SixteenScene   w = %d,h = %d  s32indx = %d\n",w,h,s32indx);
				pDstRect->s32Xpos = ((pSrcRect->s32Xpos + ratio_x*pSrcRect->u32Width*(ratio-1))*w/ SCREEN_DEF_WIDTH) +(s32indx%3)*w;
				pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT) +(s32indx/3)*h;
				pDstRect->u32Width= (pSrcRect->u32Width*w*ratio)/ SCREEN_DEF_WIDTH;
				pDstRect->u32Height = (pSrcRect->u32Height*h*ratio)/ SCREEN_DEF_HEIGHT;
			}	
			break;
		case FourScene:
		case LinkFourScene:
			{
				s32indx = s32indx%4;
				w = preview_cur_param[vo_dev].w/2;
				h = preview_cur_param[vo_dev].h/2;
				pDstRect->s32Xpos = (pSrcRect->s32Xpos*w/ SCREEN_DEF_WIDTH) +(s32indx%2)*w;
				pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT) +(s32indx/2)*h;
				pDstRect->u32Width= (pSrcRect->u32Width*w)/ SCREEN_DEF_WIDTH;
				pDstRect->u32Height = (pSrcRect->u32Height*h)/ SCREEN_DEF_HEIGHT;			
			}
			break;
#if defined(SN9234H1)
		case SixScene:
			{
				s32indx = s32indx%6;
				w = preview_cur_param[vo_dev].w/3;
				h = preview_cur_param[vo_dev].h/3;
				if(s32indx == 0)
				{
					pDstRect->s32Xpos = pSrcRect->s32Xpos*2*w/ SCREEN_DEF_WIDTH;
					pDstRect->s32Ypos= pSrcRect->s32Ypos*2*h/ SCREEN_DEF_HEIGHT;
					pDstRect->u32Width= (pSrcRect->u32Width*2*w)/ SCREEN_DEF_WIDTH;
					pDstRect->u32Height = (pSrcRect->u32Height*2*h)/ SCREEN_DEF_HEIGHT;
				}
				else if(s32indx <3)
				{
					
					pDstRect->s32Xpos =((pSrcRect->s32Xpos + ratio_x*pSrcRect->u32Width*(ratio-1))*w/ SCREEN_DEF_WIDTH) +2*w;
					pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT) +(s32indx-1)*h;
					pDstRect->u32Width= (pSrcRect->u32Width*w*ratio)/ SCREEN_DEF_WIDTH;
					pDstRect->u32Height = (pSrcRect->u32Height*h*ratio)/ SCREEN_DEF_HEIGHT;
				}
				else
				{
					pDstRect->s32Xpos =(pSrcRect->s32Xpos + ratio_x*pSrcRect->u32Width*(ratio-1))*w/ SCREEN_DEF_WIDTH +(s32indx%3)*w;
					pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT) + 2*h;
					pDstRect->u32Width= (pSrcRect->u32Width*w*ratio)/ SCREEN_DEF_WIDTH;
					pDstRect->u32Height = (pSrcRect->u32Height*h*ratio)/ SCREEN_DEF_HEIGHT;				
				}
			}	
			break;
		case EightScene:
			{
				s32indx = s32indx%8;
				w = preview_cur_param[vo_dev].w/4;
				h = preview_cur_param[vo_dev].h/4;
				if(s32indx == 0)
				{
					pDstRect->s32Xpos = pSrcRect->s32Xpos*3*w/ SCREEN_DEF_WIDTH;
					pDstRect->s32Ypos= pSrcRect->s32Ypos*3*h/ SCREEN_DEF_HEIGHT;
					pDstRect->u32Width= (pSrcRect->u32Width*w*3)/ SCREEN_DEF_WIDTH;
					pDstRect->u32Height = (pSrcRect->u32Height*h*3)/ SCREEN_DEF_HEIGHT;
				}else if(s32indx <4)
				{
					
					pDstRect->s32Xpos = ((pSrcRect->s32Xpos + ratio_x*pSrcRect->u32Width*(ratio-1))*w/ SCREEN_DEF_WIDTH) +w*3;
					pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT)+(s32indx-1)*h;
					pDstRect->u32Width= (pSrcRect->u32Width*w*ratio)/ SCREEN_DEF_WIDTH;
					pDstRect->u32Height = (pSrcRect->u32Height*h*ratio)/ SCREEN_DEF_HEIGHT;			
				}
				else 
				{
					pDstRect->s32Xpos = ((pSrcRect->s32Xpos + ratio_x*pSrcRect->u32Width*(ratio-1))*w/ SCREEN_DEF_WIDTH) +(s32indx%4)*w;
					pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT)+ h*3;
					pDstRect->u32Width= (pSrcRect->u32Width*w*ratio)/ SCREEN_DEF_WIDTH;
					pDstRect->u32Height = (pSrcRect->u32Height*h*ratio)/ SCREEN_DEF_HEIGHT;			
				}
			}		
			break;
#endif
        case FiveScene: 
			{
				s32indx = s32indx%5;
				w = preview_cur_param[vo_dev].w/3;
				h = preview_cur_param[vo_dev].h/3;
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@!!!!!SixteenScene   w = %d,h = %d  s32indx = %d\n",w,h,s32indx);
				if(s32indx<2)
				{
                    pDstRect->s32Xpos = ((pSrcRect->s32Xpos + ratio_x*pSrcRect->u32Width*(ratio-1))*w/ SCREEN_DEF_WIDTH) +(s32indx%3)*w;
				    pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT) +(s32indx/3)*h;
				    pDstRect->u32Width= (pSrcRect->u32Width*w*ratio)/ SCREEN_DEF_WIDTH;
				    pDstRect->u32Height = (pSrcRect->u32Height*h*ratio)/ SCREEN_DEF_HEIGHT;
				}
				else
				{
				    s32indx=s32indx-2;
                    pDstRect->s32Xpos = ((pSrcRect->s32Xpos + ratio_x*pSrcRect->u32Width*(ratio-1))*w/ SCREEN_DEF_WIDTH) +(s32indx%1+2)*w;
				    pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT) +(s32indx/1)*h;
				    pDstRect->u32Width= (pSrcRect->u32Width*w*ratio)/ SCREEN_DEF_WIDTH;
				    pDstRect->u32Height = (pSrcRect->u32Height*h*ratio)/ SCREEN_DEF_HEIGHT;
				}
			}	
			break;
		case SevenScene: 
			{
				s32indx = s32indx%7;
				w = preview_cur_param[vo_dev].w/3;
				h = preview_cur_param[vo_dev].h/3;
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@!!!!!SixteenScene   w = %d,h = %d  s32indx = %d\n",w,h,s32indx);
				if(s32indx==0)
				{   
				    
                    pDstRect->s32Xpos = ((pSrcRect->s32Xpos + ratio_x*pSrcRect->u32Width*(ratio-1))*w/ SCREEN_DEF_WIDTH) +(s32indx%3)*w;
				    pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT) +(s32indx/3)*h;
				    pDstRect->u32Width= (pSrcRect->u32Width*w*ratio)/ SCREEN_DEF_WIDTH;
				    pDstRect->u32Height = (pSrcRect->u32Height*h*ratio)/ SCREEN_DEF_HEIGHT;
				}
				else
				{
				    s32indx=s32indx-1;
				    pDstRect->s32Xpos = ((pSrcRect->s32Xpos + ratio_x*pSrcRect->u32Width*(ratio-1))*w/ SCREEN_DEF_WIDTH) +(s32indx%2+1)*w;
				    pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT) +(s32indx/2)*h;
				    pDstRect->u32Width= (pSrcRect->u32Width*w*ratio)/ SCREEN_DEF_WIDTH;
				    pDstRect->u32Height = (pSrcRect->u32Height*h*ratio)/ SCREEN_DEF_HEIGHT;
				}
				
			}	
			break;
		case NineScene: 
		case LinkNineScene:
			{
				s32indx = s32indx%9;
				w = preview_cur_param[vo_dev].w/3;
				h = preview_cur_param[vo_dev].h/3;
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@!!!!!SixteenScene   w = %d,h = %d  s32indx = %d\n",w,h,s32indx);
				pDstRect->s32Xpos = ((pSrcRect->s32Xpos + ratio_x*pSrcRect->u32Width*(ratio-1))*w/ SCREEN_DEF_WIDTH) +(s32indx%3)*w;
				pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT) +(s32indx/3)*h;
				pDstRect->u32Width= (pSrcRect->u32Width*w*ratio)/ SCREEN_DEF_WIDTH;
				pDstRect->u32Height = (pSrcRect->u32Height*h*ratio)/ SCREEN_DEF_HEIGHT;
			}	
			break;
		case SixteenScene:
			{
				s32indx = s32indx%16;
				w = preview_cur_param[vo_dev].w/4;
				h = preview_cur_param[vo_dev].h/4;
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@!!!!!SixteenScene   w = %d,h = %d  s32indx = %d\n",w,h,s32indx);
				pDstRect->s32Xpos = ((pSrcRect->s32Xpos + ratio_x*pSrcRect->u32Width*(ratio-1))*w/ SCREEN_DEF_WIDTH) +(s32indx%4)*w;
				pDstRect->s32Ypos= (pSrcRect->s32Ypos*h/ SCREEN_DEF_HEIGHT) +(s32indx/4)*h;
				pDstRect->u32Width= (pSrcRect->u32Width*w*ratio)/ SCREEN_DEF_WIDTH;
				pDstRect->u32Height = (pSrcRect->u32Height*h*ratio)/ SCREEN_DEF_HEIGHT;
			}		
			break;
		default:
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Get_Cur_Rect error ,not surport screen\n");
			return -1;
	}
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@!!!!!Get_Cur_Rect   devid = %d,ch = %d,pDstRect->s32Xpos = %d,pDstRect->s32Ypos=%d,pDstRect->u32Width=%d,pDstRect->u32Height=%d\n",
	//	devid,ch,pDstRect->s32Xpos,pDstRect->s32Ypos,pDstRect->u32Width,pDstRect->u32Height);
	
#if 1 /*2010-8-31 修正：预览OSD超出画面边界*/
	{
		TDE2_RECT_S stTmpRect;
		//if(type == OSD_NAME_LAYER)
		{
#if defined(SN9234H1)
			if(preview_cur_param[vo_dev].prv_mode == EightScene)
			{//如果8画面
				if(s32indx == 0)
				{//第1个画面
					stTmpRect.u32Height = (pSrcRect->u32Height>(h*3))?(h*3):pSrcRect->u32Height;	//使用原来的宽高，不进行缩放
					stTmpRect.u32Width = (pSrcRect->u32Width>(w*3))?(w*3):pSrcRect->u32Width;
				}
				else
				{
					stTmpRect.u32Height = (pSrcRect->u32Height>h)?h:pSrcRect->u32Height;	//使用原来的宽高，不进行缩放
					stTmpRect.u32Width = (pSrcRect->u32Width>w)?w:pSrcRect->u32Width;
				}
			}
			else if(preview_cur_param[vo_dev].prv_mode == SixScene)
			{//如果6画面
				if(s32indx == 0)
				{//第1个画面
					stTmpRect.u32Height = (pSrcRect->u32Height>(h*2))?(h*2):pSrcRect->u32Height;	//使用原来的宽高，不进行缩放
					stTmpRect.u32Width = (pSrcRect->u32Width>(w*2))?(w*2):pSrcRect->u32Width;
				}
				else
				{//其他画面
					stTmpRect.u32Height = (pSrcRect->u32Height>h)?h:pSrcRect->u32Height;	//使用原来的宽高，不进行缩放
					stTmpRect.u32Width = (pSrcRect->u32Width>w)?w:pSrcRect->u32Width;
				}
			}
			else
			{
				stTmpRect.u32Height = (pSrcRect->u32Height>h)?h:pSrcRect->u32Height;	//使用原来的宽高，不进行缩放
				stTmpRect.u32Width = (pSrcRect->u32Width>w)?w:pSrcRect->u32Width;
			}
#else
			stTmpRect.u32Height = (pSrcRect->u32Height>h)?h:pSrcRect->u32Height;	//使用原来的宽高，不进行缩放
			stTmpRect.u32Width = (pSrcRect->u32Width>w)?w:pSrcRect->u32Width;
#endif			
		}
		/*else
		{
			stTmpRect.u32Height = (pDstRect->u32Height>h)?h:pDstRect->u32Height;
			stTmpRect.u32Width = (pDstRect->u32Width>w)?w:pDstRect->u32Width;
		}*/
		if(type == OSD_ALARM_LAYER)
		{
			pDstRect->s32Xpos -= pSrcRect->u32Width - pDstRect->u32Width;
		}
		stTmpRect.s32Xpos = pDstRect->s32Xpos%w;
		stTmpRect.s32Ypos = pDstRect->s32Ypos%h;
		stTmpRect.s32Xpos = (stTmpRect.s32Xpos+stTmpRect.u32Width+2 > w)?(w-stTmpRect.u32Width-2):stTmpRect.s32Xpos;
		stTmpRect.s32Ypos = (stTmpRect.s32Ypos+stTmpRect.u32Height+2 > h)?(h-stTmpRect.u32Height-2):stTmpRect.s32Ypos;
		stTmpRect.s32Xpos += (pDstRect->s32Xpos/w)*w;
		stTmpRect.s32Ypos += (pDstRect->s32Ypos/h)*h;
		if(stTmpRect.s32Xpos < 0)
		{
			stTmpRect.s32Xpos = 0;
		}
		if(stTmpRect.s32Ypos < 0)
		{
			stTmpRect.s32Ypos  = 0;
		}
		if(preview_cur_param[vo_dev].prv_mode >= NineScene && type == OSD_CLICKVISUAL_LAYER)
		{
			stTmpRect.s32Xpos -=25;
		}
		*pDstRect = stTmpRect;
	}
	//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"@!!!!!Get_Cur_Rect   devid = %d,ch = %d,type =%d,pDstRect->s32Xpos = %d,pDstRect->s32Ypos=%d,pDstRect->u32Width=%d,pDstRect->u32Height=%d ,w=%d,h=%d\n",
	//	devid,ch,type,pDstRect->s32Xpos,pDstRect->s32Ypos,pDstRect->u32Width,pDstRect->u32Height,w,h);

#endif
	
	return 0;
}


static int Get_Cur_Ypos(unsigned char devid,unsigned char ch,int *ypos)
{
#if defined(SN9234H1)
	if(devid == SPOT_VO_DEV || devid == AD)
		return -1;
#else
	if(devid > DHD0)
		return -1;
#endif	
	HI_S32 s32indx=0;
	int w=0,h=0;
	unsigned char vo_dev = devid;
	//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,Get_Cur_Rect,devid = %d,ch_num=%d, prv_mode = %d ,ch = %d\n", __LINE__,devid,preview_cur_param[devid].ch_num, preview_cur_param[devid].prv_mode,ch);
	
#if 1 /*2010-11-4 启用了通道隐藏功用！*/
	if(ch>=DEV_CHANNEL_NUM|| devid>=PRV_VO_DEV_NUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ch=%d, dev=%d\n",ch,devid);
		return OSD_NOT_IN_MODE;
	}
#endif
#ifdef SECOND_DEV
		if(devid == AD)
		{
			vo_dev = SD;
		}
#endif	

		s32indx = Get_Cur_idx(devid,ch);
		if(s32indx < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_idx failed,ret=0x%x!\n", __LINE__, s32indx);
			*ypos =0;
			return OSD_NOT_IN_MODE;
		}
	//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"!!!!!Get_Cur_Rect   devid = %d,ch = %d,s32indx = %d,s_astVoDevStatDflt[devid].enPreviewMode = %d,pSrcRect->s32Xpos = %d,pSrcRect->s32Ypos=%d,pSrcRect->u32Width=%d,pSrcRect->u32Height=%d\n",
	//	devid,ch,s32indx,preview_cur_param[devid].prv_mode,pSrcRect->s32Xpos,pSrcRect->s32Ypos,pSrcRect->u32Width,pSrcRect->u32Height);
	switch(preview_cur_param[vo_dev].prv_mode)
	{
		case SingleScene:
			{
				*ypos =0;
			}
			break;
		case ThreeScene:
			{
				s32indx = s32indx%3;
				w = preview_cur_param[vo_dev].w/3;
				h = preview_cur_param[vo_dev].h/3;
				*ypos = (h/ SCREEN_DEF_HEIGHT) +(s32indx/3)*h;
			}
			break;
		case FourScene:
		case LinkFourScene:
			{
				s32indx = s32indx%4;
				w = preview_cur_param[vo_dev].w/2;
				h = preview_cur_param[vo_dev].h/2;
				*ypos = (h/ SCREEN_DEF_HEIGHT) +(s32indx/2)*h;
			}
			break;
		case FiveScene:
			{
				s32indx = s32indx%5;
				w = preview_cur_param[vo_dev].w/3;
				h = preview_cur_param[vo_dev].h/3;
                if(s32indx<2)
				{  
					*ypos = (h/ SCREEN_DEF_HEIGHT) +(s32indx/3)*h;
				}
				else
				{ 
				    s32indx=s32indx-2;
				    *ypos = (h/ SCREEN_DEF_HEIGHT) +(s32indx/1)*h;
				}
			}
			break;
		case SevenScene:
			{
				s32indx = s32indx%7;
				w = preview_cur_param[vo_dev].w/3;
				h = preview_cur_param[vo_dev].h/3;
                if(s32indx==0)
				{  
					*ypos = (h/ SCREEN_DEF_HEIGHT) +(s32indx/3)*h;
				}
				else
				{ 
				   s32indx=s32indx-1;
				    *ypos = (h/ SCREEN_DEF_HEIGHT) +(s32indx/2)*h;
				}
				
			}
			break;
		case NineScene: 
		case LinkNineScene:
			{
				s32indx = s32indx%9;
				w = preview_cur_param[vo_dev].w/3;
				h = preview_cur_param[vo_dev].h/3;
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@!!!!!SixteenScene   w = %d,h = %d  s32indx = %d\n",w,h,s32indx);
				*ypos = (h/ SCREEN_DEF_HEIGHT) +(s32indx/3)*h;
			}	
			break;
		case SixteenScene:
			{
				s32indx = s32indx%16;
				w = preview_cur_param[vo_dev].w/4;
				h = preview_cur_param[vo_dev].h/4;
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@!!!!!SixteenScene   w = %d,h = %d  s32indx = %d\n",w,h,s32indx);
				*ypos = (h/ SCREEN_DEF_HEIGHT) +(s32indx/4)*h;
			}		
			break;
		default:
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Get_Cur_Rect error ,not surport screen\n");
			return -1;
	}
	return 0;
}

//****************************************************
//预览部分OSD，打开TDE任务
//*****************************************************

int Prv_Open_Task(TDE_HANDLE *ptde_s32Handle)
{
	TDE_HANDLE tde_s32Handle=-1;
	
	//打开TDE任务
	// 1. start job 
	tde_s32Handle = HI_TDE2_BeginJob();
	if(HI_ERR_TDE_INVALID_HANDLE == tde_s32Handle)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_BeginJob failed,ret=0x%x!\n", __LINE__, tde_s32Handle);
		return -1;
	}
	if(HI_ERR_TDE_DEV_NOT_OPEN == tde_s32Handle)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_BeginJob failed,ret=0x%x!\n", __LINE__, tde_s32Handle);
		return tde_s32Handle;

	}
	*ptde_s32Handle = tde_s32Handle;
	return 0;
}
//****************************************************
//预览部分OSD，关闭TDE任务
//*****************************************************

static int Prv_Close_Task(TDE_HANDLE *ptde_s32Handle)
{
	HI_S32 s32Ret=-1;
	// 5. submit job
	if(!ptde_s32Handle) return s32Ret;
	s32Ret = HI_TDE2_EndJob(*ptde_s32Handle, HI_FALSE, HI_TRUE, 10);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_EndJob failed,ret=0x%x!\n", __LINE__, s32Ret);
		HI_TDE2_CancelJob(*ptde_s32Handle);
		return s32Ret;
	}
	return s32Ret;
}
////////////////////FB//////////////////////////////////////////
//****************************************************
//预览部分OSD，获取GUI图层的FD，进行抗闪准备
//*****************************************************
int Get_Fb_param(int fd,unsigned char *pmmap)
{
	int w=0,h=0;
	struct fb_fix_screeninfo stFixInfo;
	struct fb_var_screeninfo stVarInfo;
	PRV_RECT_S stDspRect;
	const HI_U32 u32GuiFbOffsetH = 600;//this max value is vram4_size/720/2 - 576*2 ==> 236!
	
	printf(TEXT_COLOR_RED("s_VoDevCtrlDflt=%d,return..., fd=%d\n"),s_VoDevCtrlDflt, fd);	
	CHECK(PRV_GetVoDspRect(s_VoDevCtrlDflt, &stDspRect));
	CHECK(PRV_SetFbStartXY(fd, stDspRect.s32X, stDspRect.s32Y));
	
	if (ioctl(fd, FBIOGET_VSCREENINFO, &stVarInfo) < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"process frame buffer device error\n");
		return -1;
	}
	w = stVarInfo.xres;
	h = stVarInfo.yres;
	if (w!=stDspRect.u32Width || h!=stDspRect.u32Height)
	{
		printf(TEXT_COLOR_RED("w=%d,h=%d, W=%d,H=%d, return...\n"),w,h,stDspRect.u32Width, stDspRect.u32Height);
		return 0;
	}
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, TEXT_COLOR_PURPLE("Get_Fb_param: w=%d, h=%d\n"), w, h);
#if defined(SN9234H1)
	if(s_VoDevCtrlDflt != HD)
#else
	if(s_VoDevCtrlDflt != DHD0)
#endif		
	{
		stVarInfo.yres_virtual = 2 * stVarInfo.yres + u32GuiFbOffsetH;
	}
	else
	{
		stVarInfo.yres_virtual = 2 * stVarInfo.yres;
	}
	if (ioctl(fd, FBIOPUT_VSCREENINFO, &stVarInfo) < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"FBIOPUT_VSCREENINFO:::::process frame buffer device error\n");
		//return -1;
	}	
	if (ioctl(fd, FBIOGET_FSCREENINFO, &stFixInfo) < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"FBIOGET_FSCREENINFO:::::process frame buffer device error\n");
		return -1;
	}

	/* 3. create surface */
	g_fb_stScreen.enColorFmt = TDE2_COLOR_FMT_ARGB1555;
	g_fb_stScreen.u32PhyAddr = stFixInfo.smem_start;
	g_fb_stScreen.u32Width = w;
	g_fb_stScreen.u32Height = h;
	g_fb_stScreen.u32Stride = stFixInfo.line_length;
	g_fb_stScreen.bAlphaMax255 = HI_TRUE;
	g_fb_stScreen.bAlphaExt1555 = HI_TRUE;
	g_fb_stScreen.u8Alpha0 = 1;
	g_fb_stScreen.u8Alpha1 = 255;

#if defined(SN9234H1)
	if(s_VoDevCtrlDflt != HD)
#else
	if(s_VoDevCtrlDflt != DHD0)
#endif
	{	
		g_fb_stScreen1 = g_fb_stScreen;
		g_fb_stScreen1.u32PhyAddr = g_fb_stScreen.u32PhyAddr + g_fb_stScreen.u32Stride * (g_fb_stScreen.u32Height + u32GuiFbOffsetH);
		
		stVarInfo.yoffset = stVarInfo.yres + u32GuiFbOffsetH;
		if (ioctl(fd, FBIOPAN_DISPLAY, &stVarInfo) < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,TEXT_COLOR_RED("PAN_DISPLAY: process frame buffer device error: fd=%d!\n"), fd);
			return -1;
		}
	}
	
	g_fb_phyaddr = stFixInfo.smem_start;
	g_fb_mmap = pmmap;
	g_guifd = fd;

	PRV_CalcGuiResUnit();
	//只有单个GUI显示时，此变量都赋值为1
	s_width_unit = 1;
	s_height_unit = 1;
	//Set_FB_Flicker(0,0,g_fb_stScreen.u32Width,g_fb_stScreen.u32Height);
	return 0;
}

int Get_Fb_param_exit(void)
{
	Set_FB_Flicker(0,0,preview_cur_param[s_VoDevCtrlDflt].w,preview_cur_param[s_VoDevCtrlDflt].h);
	printf(TEXT_COLOR_YELLOW("close gui........\n"));
	g_guifd = -1;
	return 0;
}
//****************************************************
//预览部分OSD，抗闪区域刷新接口
//*****************************************************
int Set_FB_Flicker(int x,int y,int w,int h)
{
#if defined(Hi3531)||defined(Hi3535)	
	return 0;
#endif
	HI_S32 s32Ret;
	TDE_HANDLE tde_s32Handle;
	TDE2_RECT_S stSrcRect;
	TDE2_RECT_S stDstRect;
	
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Set_FB_Flicker	START s_VoDevCtrlDflt = %d,g_fb_flag = %d,s_height_unit = %d,s_width_unit=%d,g_guifd=%d\n",s_VoDevCtrlDflt,g_fb_flag,s_height_unit,s_width_unit,g_guifd);
#if defined(SN9234H1)
	if(s_VoDevCtrlDflt == HD)
#else
	if(s_VoDevCtrlDflt == DHD0)
#endif		
	{
		return 0;
	}
	if (!g_fb_flag || s_height_unit == 0 || s_width_unit == 0 || g_guifd <= 0)
	{
		return 0;
	}
	
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Set_FB_Flicker aD START1111\n");

	pthread_mutex_lock(&mutex);
	
	x -= 2;
	y -= 2;
	w += 4;
	h += 4;
	x=(x<0)?0:x;
	y=(y<0)?0:y;

	w += (x % s_width_unit);
	h += (y % s_height_unit);

	x = (x / s_width_unit) * s_width_unit;
	y = (y / s_height_unit) * s_height_unit;

	w = (w+s_width_unit-1) / s_width_unit * s_width_unit;
	h = (h+s_height_unit-1) / s_height_unit * s_height_unit;

	/*
	x = 0;
	y = 0; 
	w = g_fb_stScreen.u32Width;
	h = g_fb_stScreen.u32Height;*/
	stSrcRect.s32Xpos = x;
	stSrcRect.s32Ypos = y;
	stSrcRect.u32Height = h;
	stSrcRect.u32Width = w;
/*	
	stDstRect.s32Xpos = x*g_fb_stScreen2.u32Width/g_fb_stScreen.u32Width;
	stDstRect.s32Ypos = y*g_fb_stScreen2.u32Height/g_fb_stScreen.u32Height;
	stDstRect.u32Height = h*g_fb_stScreen2.u32Height/g_fb_stScreen.u32Height;
	stDstRect.u32Width = w*g_fb_stScreen2.u32Width/g_fb_stScreen.u32Width;
*/	
	//打开任务
	s32Ret = Prv_Open_Task(&tde_s32Handle);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Open_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
		pthread_mutex_unlock(&mutex);
		return s32Ret;
	}
	
	//双屏拷贝
	if (g1fd>0)
	{
		s32Ret = HI_TDE2_QuickResize(tde_s32Handle,&g_fb_stScreen,&stSrcRect,&g_fb_stScreen2,&stDstRect);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_QuickResize failed,ret=0x%x!\n", __LINE__, s32Ret);
			HI_TDE2_CancelJob(tde_s32Handle);
			pthread_mutex_unlock(&mutex);
			return s32Ret;
		}
	}
	else
	{
		//printf(TEXT_COLOR_RED("G1 not open! fd=%d\n"), g1fd);
	}
	
	//抗闪烁拷贝
	if(OSD_off_flag[0][0]  & (OSD_FB_OFF|OSD_FB_FLICKER_OFF))
	{
		s32Ret = HI_TDE2_QuickCopy(tde_s32Handle,&g_fb_stScreen,&stSrcRect,&g_fb_stScreen1,&stSrcRect);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_QuickCopy failed,ret=0x%x!\n", __LINE__, s32Ret);
			HI_TDE2_CancelJob(tde_s32Handle);
			pthread_mutex_unlock(&mutex);
			return s32Ret;
		}
	}
	else
	{
		s32Ret = HI_TDE2_QuickDeflicker(tde_s32Handle,&g_fb_stScreen,&stSrcRect,&g_fb_stScreen1,&stSrcRect);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_QuickDeflicker failed,ret=0x%x!\n", __LINE__, s32Ret);
			HI_TDE2_CancelJob(tde_s32Handle);
			pthread_mutex_unlock(&mutex);
			return s32Ret;
		}
	}
	
	//关闭任务
	s32Ret = Prv_Close_Task(&tde_s32Handle);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
		pthread_mutex_unlock(&mutex);
		return s32Ret;
	}

	pthread_mutex_unlock(&mutex);
	
	return 0;
}

//****************************************************
//预览部分OSD，设置抗闪的标志位
//*****************************************************

int Prv_Set_Flicker(unsigned char on)
{
	TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Set_Flicker on = %d!\n", __LINE__, on);
	if(on)
	{
		OSD_off_flag[0][0]  &= ~OSD_FB_FLICKER_OFF;
	}
	else
	{
		OSD_off_flag[0][0]  |= OSD_FB_FLICKER_OFF;
	}
	Set_FB_Flicker(0,0,preview_cur_param[s_VoDevCtrlDflt].w,preview_cur_param[s_VoDevCtrlDflt].h);
#if defined(SN9234H1)
	Prv_Disp_OSD(AD);
#endif
#if defined(SN2116LE) || defined(SN2116LS) || defined(SN2116LP) || defined(SN2116HE) || defined(SN2116HS) || defined(SN2116HP)
	Prv_Disp_OSD(SD);
#endif
	return 0;
}

int OSD_Update_GroupName()
{
	int i = 0,s32Ret = 0;
	PRM_LINKAGE_GROUP_CFG group_name;
	unsigned char groupname_buf[CHANNEL_NAME_LEN];
	for(i=0;i<LINKAGE_MAX_GROUPNUM;i++)
	{
		SN_MEMSET(groupname_buf,0,sizeof(groupname_buf));
		SN_MEMSET(OSD_GroupName_Buf[i],0,sizeof(OSD_GroupName_Buf[i]));
		s32Ret = GetParameter(PRM_ID_LINKAGE_GROUP_CFG,NULL,&group_name,sizeof(PRM_LINKAGE_GROUP_CFG),i+1,SUPER_USER_ID,NULL);
		if(s32Ret != PARAM_OK)
		{
			SN_SPRINTF((char*)groupname_buf,sizeof(groupname_buf),"分组 %d",i+1);
			SN_STRNCPY((char*)OSD_GroupName_Buf[i],CHANNEL_NAME_LEN,(char*)groupname_buf,CHANNEL_NAME_LEN);
		}
		else
		{
			SN_STRNCPY((char*)OSD_GroupName_Buf[i],CHANNEL_NAME_LEN,(char *)group_name.GroupName,CHANNEL_NAME_LEN);
		}
	//	printf("i:%d,OSD_GroupName_Buf:%s\n",i,OSD_GroupName_Buf[i]);
	}
	return 0;
}

#if defined(Hi3531)||defined(Hi3535)
int Prv_FB_Flll_Test(int x,int y,int w,int h, UINT32 color)
{
	HI_S32 s32Ret;
	TDE_HANDLE tde_s32Handle = 0;
	TDE2_RECT_S stSrcRect;
	//TDE2_RECT_S stDstRect;

	printf("FB_Flll_Test   s_height_unit = %d,s_width_unit=%d,g_guifd=%d\n",s_VoDevCtrlDflt,g_fb_flag,s_height_unit,s_width_unit,g_guifd);

	if (s_height_unit == 0 || s_width_unit == 0 || g_guifd <= 0)
	{
		return 0;
	}

	//pthread_mutex_lock(&mutex);

	#if 0
	x -= 2;
	y -= 2;
	w += 4;
	h += 4;
	x=(x<0)?0:x;
	y=(y<0)?0:y;

	w += (x % s_width_unit);
	h += (y % s_height_unit);

	x = (x / s_width_unit) * s_width_unit;
	y = (y / s_height_unit) * s_height_unit;

	w = (w+s_width_unit-1) / s_width_unit * s_width_unit;
	h = (h+s_height_unit-1) / s_height_unit * s_height_unit;
	#endif
	/*
	x = 0;
	y = 0; 
	w = g_fb_stScreen.u32Width;
	h = g_fb_stScreen.u32Height;*/
	stSrcRect.s32Xpos = x;
	stSrcRect.s32Ypos = y;
	stSrcRect.u32Height = h;
	stSrcRect.u32Width = w;

	//打开任务
	s32Ret = Prv_Open_Task(&tde_s32Handle);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Open_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
		//pthread_mutex_unlock(&mutex);
		return s32Ret;
	}

	s32Ret = HI_TDE2_QuickFill(tde_s32Handle, &g_fb_stScreen, &stSrcRect, color);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_QuickDeflicker failed,ret=0x%x!\n", __LINE__, s32Ret);
		HI_TDE2_CancelJob(tde_s32Handle);
		//pthread_mutex_unlock(&mutex);
		return s32Ret;
	}

	//关闭任务
	s32Ret = Prv_Close_Task(&tde_s32Handle);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
		//pthread_mutex_unlock(&mutex);
		return s32Ret;
	}

	//pthread_mutex_unlock(&mutex);
	
	return 0;
}
#endif
//隐藏图标
static int Prv_TimeOsd_clear(unsigned char devid)
{
#if defined(USE_UI_OSD)			
	MMI_DestroyOsdTime();	
	return 0;
#else
	TDE2_RECT_S stDstRect;	
	unsigned char *p = fb_mmap[devid];
	unsigned char vo_dev = devid;
	if(!p)
		return -1;
	if (Prv_fd[devid] <= 0)
	{
		printf(TEXT_COLOR_YELLOW("devid=%d not opened!\n"), devid);
		return -1;
	}
			
	stDstRect.s32Xpos= (OSD_PRV_Time_Rect[devid].s32Xpos *preview_cur_param[vo_dev].w)/SCREEN_DEF_WIDTH;
	stDstRect.s32Ypos= (OSD_PRV_Time_Rect[devid].s32Ypos *preview_cur_param[vo_dev].h)/SCREEN_DEF_HEIGHT;
	stDstRect.u32Width= g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//g_stImgSur[devid][OSD_TIME_LAYER].u32Width;
	stDstRect.u32Height= g_stImgSur[devid][OSD_TIME_LAYER].u32Height;//g_stImgSur[devid][OSD_TIME_LAYER].u32Height;//(OSD_PRV_Time_Rect.u32Height*h)/SCREEN_DEF_HEIGHT;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "stDstRect.s32Xpos: %d, stDstRect.s32Ypos: %d, stDstRect.u32Width: %d, stDstRect.u32Height: %d", 
									stDstRect.s32Xpos, stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height);

	//printf(TEXT_COLOR_PURPLE("HI_TDE2_QuickFill type = %d stsrcrect: %d,%d,%d,%d,     stDstRect: %d, %d, %d, %d\n"), type,OSD_Rect[devid][ch][type].s32Xpos, OSD_Rect[devid][ch][type].s32Ypos, OSD_Rect[devid][ch][type].u32Width, OSD_Rect[devid][ch][type].u32Height,stDstRect.s32Xpos, stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height);
		
	TDE_HANDLE s32Handle = HI_TDE2_BeginJob();
	if (HI_SUCCESS == HI_TDE2_QuickFill(s32Handle, &g_stScreen[devid], &stDstRect, FB_BG_COLOR))
	{
		CHECK_RET(HI_TDE2_EndJob(s32Handle, HI_FALSE, HI_TRUE, 10));
	}
	else
	{
		printf(TEXT_COLOR_PURPLE("HI_TDE2_QuickFill fail! stDstRect: %d, %d, %d, %d\n"), stDstRect.s32Xpos, stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height);
		CHECK_RET(HI_TDE2_CancelJob(s32Handle));
	}
	
	return 0;
#endif
}

void set_TimeOsd_xy( )
{
	PRM_TIMEOSD_CFG timeosd_info;
	if (PARAM_OK != GetParameter(PRM_ID_TIMEOSD_CFG, NULL, &timeosd_info, sizeof(timeosd_info), 0, SUPER_USER_ID, NULL))
	{
		timeosd_info.startx = OSD_TIME_X;
		timeosd_info.starty = OSD_TIME_Y;
		timeosd_info.size = 0;
	}
#if defined(SN9234H1)
	//时间位置
	Prv_TimeOsd_clear(HD);
	OSD_PRV_Time_Rect[HD].s32Xpos = timeosd_info.startx;
	OSD_PRV_Time_Rect[AD].s32Xpos = timeosd_info.startx;
	OSD_PRV_Time_Rect[SD].s32Xpos = timeosd_info.startx;
	OSD_PRV_Time_Rect[HD].s32Ypos = timeosd_info.starty;
	OSD_PRV_Time_Rect[AD].s32Ypos = timeosd_info.starty;
	OSD_PRV_Time_Rect[SD].s32Ypos = timeosd_info.starty;	
	Prv_TimeOsd_clear(HD);
#else
	//时间位置
	Prv_TimeOsd_clear(DHD0);
	OSD_PRV_Time_Rect[DHD0].s32Xpos = timeosd_info.startx;
	OSD_PRV_Time_Rect[DSD0].s32Xpos = timeosd_info.startx;
	OSD_PRV_Time_Rect[DHD0].s32Ypos = timeosd_info.starty;
	OSD_PRV_Time_Rect[DSD0].s32Ypos = timeosd_info.starty;	
	Prv_TimeOsd_clear(DHD0);
#endif
	TimeOsd_size = timeosd_info.size;
	printf("startx:%d,starty:%d,size:%d\n",timeosd_info.startx,timeosd_info.starty,timeosd_info.size);
	return;
}
///////////////////////////////////////////////////////////////////////////////////////////
//****************************************************
//预览部分OSD，初始化当前OSD的所有变量
//*****************************************************

int get_OSD_param_init(PPRV_VO_SLAVE_STAT_S pSlave_state)
{//初始化参数
	HI_S32 s32Ret=-1,i=0;
	PRM_OSD_CFG_CHAN osd_xy;
	PRM_CHAN_CFG_BASIC_CHAN osd_name;
	PRM_TIMEOSD_CFG timeosd_info;
	char str_buf[CHANNEL_NAME_LEN];
	
	for(i = 0; i < DEV_CHANNEL_NUM; i++)
	{
#if defined(SN9234H1)
		OSD_off_flag[HD][i] |= OSD_ALARM_OFF|OSD_REC_OFF|OSD_CLICKVISUAL_OFF;
		OSD_off_flag[AD][i] |= OSD_ALARM_OFF|OSD_REC_OFF|OSD_CLICKVISUAL_OFF;
		OSD_off_flag[SD][i] |= OSD_ALARM_OFF|OSD_REC_OFF|OSD_CLICKVISUAL_OFF;
#else
		OSD_off_flag[DHD0][i] |= OSD_ALARM_OFF|OSD_REC_OFF|OSD_CLICKVISUAL_OFF;
		OSD_off_flag[DSD0][i] |= OSD_ALARM_OFF|OSD_REC_OFF|OSD_CLICKVISUAL_OFF;
#endif
		SN_MEMCPY(g_rec_type[i],sizeof(TIME_REC_BITS_PATH),TIME_REC_BITS_PATH,sizeof(TIME_REC_BITS_PATH),sizeof(TIME_REC_BITS_PATH));//默认录像类型为定时录像类型
		rectype[i] = OSD_REC_TYPE;
		SN_MEMCPY(g_alarm_type[i],sizeof(ALARM_BITS_PATH),ALARM_BITS_PATH,sizeof(ALARM_BITS_PATH),sizeof(ALARM_BITS_PATH));//默认报警类型为视频丢失报警
		alarmtype[i] = OSD_ALARM_TYPE;
		SN_MEMCPY(g_clickvisual_type[i],sizeof(CLICKVISUAL_BITS_PATH),CLICKVISUAL_BITS_PATH,sizeof(CLICKVISUAL_BITS_PATH),sizeof(CLICKVISUAL_BITS_PATH));//默认报警类型为视频丢失报警
		clickvisualtype[i] = OSD_CLICKVISUAL_TYPE;
		if(i < PRV_CHAN_NUM)
		{
			
			//OSD配置
			//位置
			s32Ret = GetParameter(PRM_ID_OSD_CFG_CHAN,NULL,&osd_xy,sizeof(PRM_OSD_CFG_CHAN),i+1,SUPER_USER_ID,NULL);
			if(s32Ret != PARAM_OK)
			{
#if defined(SN9234H1)
				OSD_Rect[HD][i][OSD_NAME_LAYER].s32Xpos = OSD_NAME_X;
				OSD_Rect[AD][i][OSD_NAME_LAYER].s32Xpos = OSD_NAME_X;
				OSD_Rect[SD][i][OSD_NAME_LAYER].s32Xpos = OSD_NAME_X;
				OSD_Rect[HD][i][OSD_NAME_LAYER].s32Ypos = OSD_NAME_Y;
				OSD_Rect[AD][i][OSD_NAME_LAYER].s32Ypos = OSD_NAME_Y;
				OSD_Rect[SD][i][OSD_NAME_LAYER].s32Ypos = OSD_NAME_Y;
				//时间位置
				OSD_Rect[HD][i][OSD_TIME_LAYER].s32Xpos = OSD_TIME_X;
				OSD_Rect[AD][i][OSD_TIME_LAYER].s32Xpos = OSD_TIME_X;
				OSD_Rect[SD][i][OSD_TIME_LAYER].s32Xpos = OSD_TIME_X;
				OSD_Rect[HD][i][OSD_TIME_LAYER].s32Ypos = OSD_TIME_Y;
				OSD_Rect[AD][i][OSD_TIME_LAYER].s32Ypos = OSD_TIME_Y;
				OSD_Rect[SD][i][OSD_TIME_LAYER].s32Ypos = OSD_TIME_Y;
#else
				OSD_Rect[DHD0][i][OSD_NAME_LAYER].s32Xpos = OSD_NAME_X;
				OSD_Rect[DSD0][i][OSD_NAME_LAYER].s32Xpos = OSD_NAME_X;
				OSD_Rect[DHD0][i][OSD_NAME_LAYER].s32Ypos = OSD_NAME_Y;
				OSD_Rect[DSD0][i][OSD_NAME_LAYER].s32Ypos = OSD_NAME_Y;
				//时间位置
				OSD_Rect[DHD0][i][OSD_TIME_LAYER].s32Xpos = OSD_TIME_X;
				OSD_Rect[DSD0][i][OSD_TIME_LAYER].s32Xpos = OSD_TIME_X;
				OSD_Rect[DHD0][i][OSD_TIME_LAYER].s32Ypos = OSD_TIME_Y;
				OSD_Rect[DSD0][i][OSD_TIME_LAYER].s32Ypos = OSD_TIME_Y;
#endif				
			}
			else
			{
				//首先对X,Y进行范围检测
				if(osd_xy.ChannelNamePosition_x >= SCREEN_DEF_WIDTH)
				{
					osd_xy.ChannelNamePosition_x = SCREEN_DEF_WIDTH-1;
				}
				if(osd_xy.ChannelTimePosition_x >= SCREEN_DEF_WIDTH)
				{
					osd_xy.ChannelTimePosition_x = SCREEN_DEF_WIDTH-1;
				}
				if(osd_xy.ChannelNamePosition_y >= SCREEN_DEF_HEIGHT)
				{
					osd_xy.ChannelNamePosition_y = SCREEN_DEF_HEIGHT-1;
				}
				if(osd_xy.ChannelTimePosition_y >= SCREEN_DEF_HEIGHT)
				{
					osd_xy.ChannelTimePosition_y = SCREEN_DEF_HEIGHT-1;
				}
#if defined(SN9234H1)
				OSD_Rect[HD][i][OSD_NAME_LAYER].s32Xpos = osd_xy.ChannelNamePosition_x;
				OSD_Rect[AD][i][OSD_NAME_LAYER].s32Xpos = osd_xy.ChannelNamePosition_x;
				OSD_Rect[SD][i][OSD_NAME_LAYER].s32Xpos = osd_xy.ChannelNamePosition_x;
				OSD_Rect[HD][i][OSD_NAME_LAYER].s32Ypos = osd_xy.ChannelNamePosition_y;
				OSD_Rect[AD][i][OSD_NAME_LAYER].s32Ypos = osd_xy.ChannelNamePosition_y;
				OSD_Rect[SD][i][OSD_NAME_LAYER].s32Ypos = osd_xy.ChannelNamePosition_y;
				//时间位置
				OSD_Rect[HD][i][OSD_TIME_LAYER].s32Xpos = osd_xy.ChannelTimePosition_x;
				OSD_Rect[AD][i][OSD_TIME_LAYER].s32Xpos = osd_xy.ChannelTimePosition_x;
				OSD_Rect[SD][i][OSD_TIME_LAYER].s32Xpos = osd_xy.ChannelTimePosition_x;
				OSD_Rect[HD][i][OSD_TIME_LAYER].s32Ypos = osd_xy.ChannelTimePosition_y;
				OSD_Rect[AD][i][OSD_TIME_LAYER].s32Ypos = osd_xy.ChannelTimePosition_y;
				OSD_Rect[SD][i][OSD_TIME_LAYER].s32Ypos = osd_xy.ChannelTimePosition_y;
#else
				OSD_Rect[DHD0][i][OSD_NAME_LAYER].s32Xpos = osd_xy.ChannelNamePosition_x;
				OSD_Rect[DSD0][i][OSD_NAME_LAYER].s32Xpos = osd_xy.ChannelNamePosition_x;
				OSD_Rect[DHD0][i][OSD_NAME_LAYER].s32Ypos = osd_xy.ChannelNamePosition_y;
				OSD_Rect[DSD0][i][OSD_NAME_LAYER].s32Ypos = osd_xy.ChannelNamePosition_y;
				//时间位置
				OSD_Rect[DHD0][i][OSD_TIME_LAYER].s32Xpos = osd_xy.ChannelTimePosition_x;
				OSD_Rect[DSD0][i][OSD_TIME_LAYER].s32Xpos = osd_xy.ChannelTimePosition_x;
				OSD_Rect[DHD0][i][OSD_TIME_LAYER].s32Ypos = osd_xy.ChannelTimePosition_y;
				OSD_Rect[DSD0][i][OSD_TIME_LAYER].s32Ypos = osd_xy.ChannelTimePosition_y;
#endif				
			}
#if defined(SN9234H1)
			OSD_Jpeg_Rect[HD][i][OSD_TIME_LAYER] = OSD_Rect[HD][i][OSD_TIME_LAYER];
			OSD_Jpeg_Rect[AD][i][OSD_TIME_LAYER] = OSD_Rect[AD][i][OSD_TIME_LAYER];
			OSD_Jpeg_Rect[SD][i][OSD_TIME_LAYER] = OSD_Rect[AD][i][OSD_TIME_LAYER];
			OSD_Jpeg_Rect[HD][i][OSD_NAME_LAYER] = OSD_Rect[HD][i][OSD_NAME_LAYER];
			OSD_Jpeg_Rect[AD][i][OSD_NAME_LAYER] = OSD_Rect[AD][i][OSD_NAME_LAYER];
			OSD_Jpeg_Rect[SD][i][OSD_NAME_LAYER] = OSD_Rect[AD][i][OSD_NAME_LAYER];
#else			
			OSD_Jpeg_Rect[DHD0][i][OSD_TIME_LAYER] = OSD_Rect[DHD0][i][OSD_TIME_LAYER];
			OSD_Jpeg_Rect[DSD0][i][OSD_TIME_LAYER] = OSD_Rect[DSD0][i][OSD_TIME_LAYER];
			OSD_Jpeg_Rect[DHD0][i][OSD_NAME_LAYER] = OSD_Rect[DHD0][i][OSD_NAME_LAYER];
			OSD_Jpeg_Rect[DSD0][i][OSD_NAME_LAYER] = OSD_Rect[DSD0][i][OSD_NAME_LAYER];
#endif
			//通道
			s32Ret = GetParameter(PRM_ID_CHAN_CFG_BASIC,NULL,&osd_name,sizeof(PRM_CHAN_CFG_BASIC_CHAN),i+1,SUPER_USER_ID,NULL);
			if(s32Ret != PARAM_OK)
			{
				SN_SPRINTF(str_buf,sizeof(str_buf),"通道 %d",i+1);
				SN_STRNCPY((char *)OSD_Name_Buf[i],CHANNEL_NAME_LEN,str_buf,CHANNEL_NAME_LEN);
			}
			else
			{
				if(osd_name.ChanNameDsp)	
				{
#if defined(SN9234H1)
					OSD_off_flag[HD][i] &= ~OSD_NAME_OFF;
					OSD_off_flag[AD][i] &= ~OSD_NAME_OFF;
					OSD_off_flag[SD][i] &= ~OSD_NAME_OFF;
#else
					OSD_off_flag[DHD0][i] &= ~OSD_NAME_OFF;
					OSD_off_flag[DSD0][i] &= ~OSD_NAME_OFF;
#endif
				}
				else
				{
#if defined(SN9234H1)
					OSD_off_flag[HD][i] |= OSD_NAME_OFF;
					OSD_off_flag[AD][i] |= OSD_NAME_OFF;
					OSD_off_flag[SD][i] |= OSD_NAME_OFF;
#else
					OSD_off_flag[DHD0][i] |= OSD_NAME_OFF;
					OSD_off_flag[DSD0][i] |= OSD_NAME_OFF;
#endif					
				}
				
				//通道名称
				SN_STRNCPY((char *)OSD_Name_Buf[i],CHANNEL_NAME_LEN, osd_name.ChannelName,CHANNEL_NAME_LEN);

				if(osd_name.ChanDataDsp)
				{
#if defined(SN9234H1)
					OSD_off_flag[HD][i] &= ~OSD_TIME_OFF;
					OSD_off_flag[AD][i] &= ~OSD_TIME_OFF;
					OSD_off_flag[SD][i] &= ~OSD_TIME_OFF;
#else					
					OSD_off_flag[DHD0][i] &= ~OSD_TIME_OFF;
					OSD_off_flag[DSD0][i] &= ~OSD_TIME_OFF;
#endif					
				}else
				{
#if defined(SN9234H1)
					OSD_off_flag[HD][i] |= OSD_TIME_OFF;
					OSD_off_flag[AD][i] |= OSD_TIME_OFF;
					OSD_off_flag[SD][i] |= OSD_TIME_OFF;
#else					
					OSD_off_flag[DHD0][i] |= OSD_TIME_OFF;
					OSD_off_flag[DSD0][i] |= OSD_TIME_OFF;
#endif					
				}
			}
		}
		else
		{//从片OSD信息
			//OSD配置
			s32Ret = GetParameter(PRM_ID_OSD_CFG_CHAN,NULL,&osd_xy,sizeof(PRM_OSD_CFG_CHAN),i+1,SUPER_USER_ID,NULL);
			if(s32Ret != PARAM_OK)
			{
#if defined(SN9234H1)
				//从片OSD位置赋值
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_NAME_LAYER].x = OSD_Rect[HD][i][OSD_NAME_LAYER].s32Xpos = OSD_NAME_X;
				OSD_Rect[AD][i][OSD_NAME_LAYER].s32Xpos = OSD_NAME_X;
				OSD_Rect[SD][i][OSD_NAME_LAYER].s32Xpos = OSD_NAME_X;
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_NAME_LAYER].y = OSD_Rect[HD][i][OSD_NAME_LAYER].s32Ypos = OSD_NAME_Y;
				OSD_Rect[AD][i][OSD_NAME_LAYER].s32Ypos = OSD_NAME_Y;
				OSD_Rect[SD][i][OSD_NAME_LAYER].s32Ypos = OSD_NAME_Y;
				//时间位置
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_TIME_LAYER].x = OSD_Rect[HD][i][OSD_TIME_LAYER].s32Xpos = OSD_TIME_X;
				OSD_Rect[AD][i][OSD_TIME_LAYER].s32Xpos = OSD_TIME_X;
				OSD_Rect[SD][i][OSD_TIME_LAYER].s32Xpos = OSD_TIME_X;
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_TIME_LAYER].y = OSD_Rect[HD][i][OSD_TIME_LAYER].s32Ypos = OSD_TIME_Y;
				OSD_Rect[AD][i][OSD_TIME_LAYER].s32Ypos = OSD_TIME_Y;
				OSD_Rect[SD][i][OSD_TIME_LAYER].s32Ypos = OSD_TIME_Y;
#else
				//从片OSD位置赋值
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_NAME_LAYER].x = OSD_Rect[DHD0][i][OSD_NAME_LAYER].s32Xpos = OSD_NAME_X;
				OSD_Rect[DSD0][i][OSD_NAME_LAYER].s32Xpos = OSD_NAME_X;
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_NAME_LAYER].y = OSD_Rect[DHD0][i][OSD_NAME_LAYER].s32Ypos = OSD_NAME_Y;
				OSD_Rect[DSD0][i][OSD_NAME_LAYER].s32Ypos = OSD_NAME_Y;
				//时间位置
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_TIME_LAYER].x = OSD_Rect[DHD0][i][OSD_TIME_LAYER].s32Xpos = OSD_TIME_X;
				OSD_Rect[DSD0][i][OSD_TIME_LAYER].s32Xpos = OSD_TIME_X;
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_TIME_LAYER].y = OSD_Rect[DHD0][i][OSD_TIME_LAYER].s32Ypos = OSD_TIME_Y;
				OSD_Rect[DSD0][i][OSD_TIME_LAYER].s32Ypos = OSD_TIME_Y;
#endif				
			}
			else
			{
				//首先对X,Y进行范围检测
				if(osd_xy.ChannelNamePosition_x >= SCREEN_DEF_WIDTH)
				{
					osd_xy.ChannelNamePosition_x = SCREEN_DEF_WIDTH-1;
				}
				if(osd_xy.ChannelTimePosition_x >= SCREEN_DEF_WIDTH)
				{
					osd_xy.ChannelTimePosition_x = SCREEN_DEF_WIDTH-1;
				}
				if(osd_xy.ChannelNamePosition_y >= SCREEN_DEF_HEIGHT)
				{
					osd_xy.ChannelNamePosition_y = SCREEN_DEF_HEIGHT-1;
				}
				if(osd_xy.ChannelTimePosition_y >= SCREEN_DEF_HEIGHT)
				{
					osd_xy.ChannelTimePosition_y = SCREEN_DEF_HEIGHT-1;
				}
#if defined(SN9234H1)
				//从片OSD位置赋值
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_NAME_LAYER].x = OSD_Rect[HD][i][OSD_NAME_LAYER].s32Xpos = osd_xy.ChannelNamePosition_x;
				OSD_Rect[AD][i][OSD_NAME_LAYER].s32Xpos = osd_xy.ChannelNamePosition_x;
				OSD_Rect[SD][i][OSD_NAME_LAYER].s32Xpos = osd_xy.ChannelNamePosition_x;
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_NAME_LAYER].y = OSD_Rect[HD][i][OSD_NAME_LAYER].s32Ypos = osd_xy.ChannelNamePosition_y;
				OSD_Rect[AD][i][OSD_NAME_LAYER].s32Ypos = osd_xy.ChannelNamePosition_y;
				OSD_Rect[SD][i][OSD_NAME_LAYER].s32Ypos = osd_xy.ChannelNamePosition_y;
				//时间位置
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_TIME_LAYER].x = OSD_Rect[HD][i][OSD_TIME_LAYER].s32Xpos = osd_xy.ChannelTimePosition_x;
				OSD_Rect[AD][i][OSD_TIME_LAYER].s32Xpos = osd_xy.ChannelTimePosition_x;
				OSD_Rect[SD][i][OSD_TIME_LAYER].s32Xpos = osd_xy.ChannelTimePosition_x;
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_TIME_LAYER].y = OSD_Rect[HD][i][OSD_TIME_LAYER].s32Ypos = osd_xy.ChannelTimePosition_y;
				OSD_Rect[AD][i][OSD_TIME_LAYER].s32Ypos = osd_xy.ChannelTimePosition_y;
				OSD_Rect[SD][i][OSD_TIME_LAYER].s32Ypos = osd_xy.ChannelTimePosition_y;
#else
				//从片OSD位置赋值
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_NAME_LAYER].x = OSD_Rect[DHD0][i][OSD_NAME_LAYER].s32Xpos = osd_xy.ChannelNamePosition_x;
				OSD_Rect[DSD0][i][OSD_NAME_LAYER].s32Xpos = osd_xy.ChannelNamePosition_x;
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_NAME_LAYER].y = OSD_Rect[DHD0][i][OSD_NAME_LAYER].s32Ypos = osd_xy.ChannelNamePosition_y;
				OSD_Rect[DSD0][i][OSD_NAME_LAYER].s32Ypos = osd_xy.ChannelNamePosition_y;
				//时间位置
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_TIME_LAYER].x = OSD_Rect[DHD0][i][OSD_TIME_LAYER].s32Xpos = osd_xy.ChannelTimePosition_x;
				OSD_Rect[DSD0][i][OSD_TIME_LAYER].s32Xpos = osd_xy.ChannelTimePosition_x;
				pSlave_state->slave_OSD_Rect[i-PRV_CHAN_NUM][OSD_TIME_LAYER].y = OSD_Rect[DHD0][i][OSD_TIME_LAYER].s32Ypos = osd_xy.ChannelTimePosition_y;
				OSD_Rect[DSD0][i][OSD_TIME_LAYER].s32Ypos = osd_xy.ChannelTimePosition_y;
#endif				
			}
			
			s32Ret = GetParameter(PRM_ID_CHAN_CFG_BASIC,NULL,&osd_name,sizeof(PRM_CHAN_CFG_BASIC_CHAN),i+1,SUPER_USER_ID,NULL);
			if(s32Ret != PARAM_OK)
			{
				SN_SPRINTF(str_buf,sizeof(str_buf),"通道 %d",i+1);
				SN_STRNCPY((char *)OSD_Name_Buf[i],CHANNEL_NAME_LEN,str_buf,CHANNEL_NAME_LEN);
			}
			else
			{
#if defined(SN9234H1)
				if(osd_name.ChanNameDsp)	
				{
					OSD_off_flag[HD][i] &= ~OSD_NAME_OFF;
					OSD_off_flag[AD][i] &= ~OSD_NAME_OFF;
					OSD_off_flag[SD][i] &= ~OSD_NAME_OFF;
				}
				else
				{
					OSD_off_flag[HD][i] |= OSD_NAME_OFF;
					OSD_off_flag[AD][i] |= OSD_NAME_OFF;
					OSD_off_flag[SD][i] |= OSD_NAME_OFF;
				}
				
				//通道名称
				SN_STRNCPY(OSD_Name_Buf[i],CHANNEL_NAME_LEN,(unsigned char *)osd_name.ChannelName,CHANNEL_NAME_LEN);
				if(osd_name.ChanDataDsp)
				{
					OSD_off_flag[HD][i] &= ~OSD_TIME_OFF;
					OSD_off_flag[AD][i] &= ~OSD_TIME_OFF;
					OSD_off_flag[SD][i] &= ~OSD_TIME_OFF;
				}else
				{
					OSD_off_flag[HD][i] |= OSD_TIME_OFF;
					OSD_off_flag[AD][i] |= OSD_TIME_OFF;
					OSD_off_flag[SD][i] |= OSD_TIME_OFF;
				}
				//从片OSD标志位赋值
				pSlave_state->slave_OSD_off_flag[i-PRV_CHAN_NUM] = OSD_off_flag[HD][i];

#else
				if(osd_name.ChanNameDsp)	
				{
					OSD_off_flag[DHD0][i] &= ~OSD_NAME_OFF;
					OSD_off_flag[DSD0][i] &= ~OSD_NAME_OFF;
				}
				else
				{
					OSD_off_flag[DHD0][i] |= OSD_NAME_OFF;
					OSD_off_flag[DSD0][i] |= OSD_NAME_OFF;
				}
				
				//通道名称
				SN_STRNCPY((char *)OSD_Name_Buf[i],CHANNEL_NAME_LEN,osd_name.ChannelName,CHANNEL_NAME_LEN);
				if(osd_name.ChanDataDsp)
				{
					OSD_off_flag[DHD0][i] &= ~OSD_TIME_OFF;
					OSD_off_flag[DSD0][i] &= ~OSD_TIME_OFF;
				}else
				{
					OSD_off_flag[DHD0][i] |= OSD_TIME_OFF;
					OSD_off_flag[DSD0][i] |= OSD_TIME_OFF;
				}
				//从片OSD标志位赋值
				pSlave_state->slave_OSD_off_flag[i-PRV_CHAN_NUM] = OSD_off_flag[DHD0][i];
#endif
			}
			//从片通道名称图片转换
			//s32Ret = OSD_Get_String(OSD_Name_Buf[i],&pSlave_state->slave_BmpData[i-PRV_CHAN_NUM],OSD_GetRecFontSize(0,i));
			//if(s32Ret != STRINGBMP_ERR_NONE)
			//{
			//	TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Get_String failed,ret=0x%x!\n", __LINE__, s32Ret);
			//}
		}
		
		PRM_PREVIEW_CFG_EX preview_info;
		if (PARAM_OK != GetParameter(PRM_ID_PREVIEW_CFG_EX, NULL, &preview_info, sizeof(preview_info), 0, SUPER_USER_ID, NULL))
		{
			RET_FAILURE("get_OSD_param_init---get parameter PRM_PREVIEW_CFG_EX fail!");
		}
		//预览时间OSD显示
		if(preview_info.reserve[0])
		{
			OSD_off_flag[0][0] &= ~OSD_TIME_OFF;
			
		}
		else
		{
			OSD_off_flag[0][0] |= OSD_TIME_OFF;

		}
		
        if (i < PRV_CHAN_NUM)
		{
			unsigned char  idx=0;
			int str_len = SN_STRLEN((char *)OSD_Name_Buf[i]),str_w=0,font=0;
			int OSD_width[OSD_TIME_RES]={OSD_TIME_WIDTH,OSD_TIME_CIF_WIDTH,OSD_TIME_QCIF_WIDTH,MAX_STRTIMEBMP_WIDTH,MAX_STRTIMEBMP_WIDTH,MAX_STRTIMEBMP_WIDTH};
			int OSD_heigth[OSD_TIME_RES]={OSD_TIME_HEIGTH,OSD_TIME_CIF_HEIGTH,OSD_TIME_QCIF_HEIGTH,MAX_STRBMP_HEIGHT,MAX_STRBMP_HEIGHT,MAX_STRBMP_HEIGHT};
			//时间默认宽度
			OSD_Time_Str_BmpData_idx(REC_MAINSTREAM,i,&idx);
			time_str_buf.time_icon_param[REC_MAINSTREAM][i].stBitmap.u32Width = (OSD_width[idx]+30)*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][i];
			time_str_buf.time_icon_param[REC_MAINSTREAM][i].stBitmap.u32Height = OSD_heigth[idx]*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][i];
			//通道名称默认宽度
			font = OSD_GetRecFontSize(REC_MAINSTREAM,i);
			if (font<0 || font>=sizeof(s_as32FontH)/sizeof(s_as32FontH[0]))
			{
				printf(TEXT_COLOR_RED("invalid font: %d, use default font 0\n"), font);
				font = 0;//default font
			}
			str_w = s_as32FontH[font]/2;
			name_icon_param[REC_MAINSTREAM][i].stBitmap.u32Width = str_len*str_w*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][i];
			name_icon_param[REC_MAINSTREAM][i].stBitmap.u32Height = OSD_heigth[idx]*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][i];
		}
#if defined(SN9234H1)
		//报警图标位置
		OSD_Rect[HD][i][OSD_CLICKVISUAL_LAYER].s32Xpos = ICON_X - ICON_WIDTH - 8;
		OSD_Rect[AD][i][OSD_CLICKVISUAL_LAYER].s32Xpos = ICON_X - ICON_WIDTH - 8;
		OSD_Rect[SD][i][OSD_CLICKVISUAL_LAYER].s32Xpos = ICON_X - ICON_WIDTH - 8;
		OSD_Rect[HD][i][OSD_CLICKVISUAL_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[AD][i][OSD_CLICKVISUAL_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[SD][i][OSD_CLICKVISUAL_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[HD][i][OSD_CLICKVISUAL_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[AD][i][OSD_CLICKVISUAL_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[SD][i][OSD_CLICKVISUAL_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[HD][i][OSD_CLICKVISUAL_LAYER].u32Width = ICON_HEIGTH;
		OSD_Rect[AD][i][OSD_CLICKVISUAL_LAYER].u32Width = ICON_HEIGTH;
		OSD_Rect[SD][i][OSD_CLICKVISUAL_LAYER].u32Width = ICON_HEIGTH;
		OSD_Rect[HD][i][OSD_ALARM_LAYER].s32Xpos = ICON_X;
		OSD_Rect[AD][i][OSD_ALARM_LAYER].s32Xpos = ICON_X;
		OSD_Rect[SD][i][OSD_ALARM_LAYER].s32Xpos = ICON_X;
		OSD_Rect[HD][i][OSD_ALARM_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[AD][i][OSD_ALARM_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[SD][i][OSD_ALARM_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[HD][i][OSD_ALARM_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[AD][i][OSD_ALARM_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[SD][i][OSD_ALARM_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[HD][i][OSD_ALARM_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[AD][i][OSD_ALARM_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[SD][i][OSD_ALARM_LAYER].u32Height = ICON_HEIGTH;
		//录像图标位置
		OSD_Rect[HD][i][OSD_REC_LAYER].s32Xpos = OSD_Rect[HD][i][OSD_ALARM_LAYER].s32Xpos + ICON_WIDTH+8;;
		OSD_Rect[AD][i][OSD_REC_LAYER].s32Xpos = OSD_Rect[AD][i][OSD_ALARM_LAYER].s32Xpos + ICON_WIDTH+8;;
		OSD_Rect[SD][i][OSD_REC_LAYER].s32Xpos = OSD_Rect[SD][i][OSD_ALARM_LAYER].s32Xpos + ICON_WIDTH+8;;
		OSD_Rect[HD][i][OSD_REC_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[AD][i][OSD_REC_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[SD][i][OSD_REC_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[HD][i][OSD_REC_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[AD][i][OSD_REC_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[SD][i][OSD_REC_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[HD][i][OSD_REC_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[AD][i][OSD_REC_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[SD][i][OSD_REC_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[HD][i][OSD_TIME_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[AD][i][OSD_TIME_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[SD][i][OSD_TIME_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[HD][i][OSD_TIME_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[AD][i][OSD_TIME_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[SD][i][OSD_TIME_LAYER].u32Height = ICON_HEIGTH;

		OSD_Rect[HD][i][OSD_NAME_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[AD][i][OSD_NAME_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[SD][i][OSD_NAME_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[HD][i][OSD_NAME_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[AD][i][OSD_NAME_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[SD][i][OSD_NAME_LAYER].u32Height = ICON_HEIGTH;

#else
		//报警图标位置
		OSD_Rect[DHD0][i][OSD_CLICKVISUAL_LAYER].s32Xpos = ICON_X - ICON_WIDTH - 8;
		OSD_Rect[DSD0][i][OSD_CLICKVISUAL_LAYER].s32Xpos = ICON_X - ICON_WIDTH - 8;
		OSD_Rect[DHD0][i][OSD_CLICKVISUAL_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[DSD0][i][OSD_CLICKVISUAL_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[DHD0][i][OSD_CLICKVISUAL_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[DSD0][i][OSD_CLICKVISUAL_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[DHD0][i][OSD_CLICKVISUAL_LAYER].u32Width = ICON_HEIGTH;
		OSD_Rect[DSD0][i][OSD_CLICKVISUAL_LAYER].u32Width = ICON_HEIGTH;
		
		OSD_Rect[DHD0][i][OSD_ALARM_LAYER].s32Xpos = ICON_X;
		OSD_Rect[DSD0][i][OSD_ALARM_LAYER].s32Xpos = ICON_X;
		OSD_Rect[DHD0][i][OSD_ALARM_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[DSD0][i][OSD_ALARM_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[DHD0][i][OSD_ALARM_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[DSD0][i][OSD_ALARM_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[DHD0][i][OSD_ALARM_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[DSD0][i][OSD_ALARM_LAYER].u32Height = ICON_HEIGTH;
		//录像图标位置
		OSD_Rect[DHD0][i][OSD_REC_LAYER].s32Xpos = OSD_Rect[DHD0][i][OSD_ALARM_LAYER].s32Xpos + ICON_WIDTH+8;;
		OSD_Rect[DSD0][i][OSD_REC_LAYER].s32Xpos = OSD_Rect[DSD0][i][OSD_ALARM_LAYER].s32Xpos + ICON_WIDTH+8;;
		OSD_Rect[DHD0][i][OSD_REC_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[DSD0][i][OSD_REC_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[DHD0][i][OSD_REC_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[DSD0][i][OSD_REC_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[DHD0][i][OSD_REC_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[DSD0][i][OSD_REC_LAYER].u32Height = ICON_HEIGTH;
		
		OSD_Rect[DHD0][i][OSD_TIME_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[DSD0][i][OSD_TIME_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[DHD0][i][OSD_TIME_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[DSD0][i][OSD_TIME_LAYER].u32Height = ICON_HEIGTH;

		OSD_Rect[DHD0][i][OSD_NAME_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[DSD0][i][OSD_NAME_LAYER].u32Width = ICON_WIDTH;
		OSD_Rect[DHD0][i][OSD_NAME_LAYER].u32Height = ICON_HEIGTH;
		OSD_Rect[DSD0][i][OSD_NAME_LAYER].u32Height = ICON_HEIGTH;
#endif		
	}
	OSD_Update_GroupName();
	if (PARAM_OK != GetParameter(PRM_ID_TIMEOSD_CFG, NULL, &timeosd_info, sizeof(timeosd_info), 0, SUPER_USER_ID, NULL))
	{
		timeosd_info.startx = OSD_TIME_X;
		timeosd_info.starty = OSD_TIME_Y;
		timeosd_info.size = 0;
	}
#if defined(SN9234H1)
	//时间位置
	OSD_PRV_Time_Rect[HD].s32Xpos = timeosd_info.startx;
	OSD_PRV_Time_Rect[AD].s32Xpos = timeosd_info.startx;
	OSD_PRV_Time_Rect[SD].s32Xpos = timeosd_info.startx;
	OSD_PRV_Time_Rect[HD].s32Ypos = timeosd_info.starty;
	OSD_PRV_Time_Rect[AD].s32Ypos = timeosd_info.starty;
	OSD_PRV_Time_Rect[SD].s32Ypos = timeosd_info.starty;
#else
	//时间位置
	OSD_PRV_Time_Rect[DHD0].s32Xpos = timeosd_info.startx;
	OSD_PRV_Time_Rect[DSD0].s32Xpos = timeosd_info.startx;
	OSD_PRV_Time_Rect[DHD0].s32Ypos = timeosd_info.starty;
	OSD_PRV_Time_Rect[DSD0].s32Ypos = timeosd_info.starty;
#endif
	TimeOsd_size = timeosd_info.size;
	//初始化时间信息结构体
	//SN_MEMSET((unsigned char *)&time_str_buf,0,sizeof(Time_Rec_Info));
	for(i=0;i<OSD_TIME_RES;i++)
	{
		if(time_str_buf.time_Bmp_param[i].pBmpData != NULL)
		{
			SN_FREE(time_str_buf.time_Bmp_param[i].pBmpData);
		}
		time_str_buf.time_Bmp_param[i].width = MAX_STRTIMEBMP_WIDTH;
		time_str_buf.time_Bmp_param[i].height = MAX_STRBMP_HEIGHT;
		time_str_buf.time_Bmp_param[i].pixelbyte = PIXBYTE;
		time_str_buf.time_Bmp_param[i].pBmpData = SN_MALLOC(MAX_STRTIMEBMP_WIDTH*MAX_STRBMP_HEIGHT*PIXBYTE);
		if(time_str_buf.time_Bmp_param[i].pBmpData != NULL)
		{
			SN_MEMSET(time_str_buf.time_Bmp_param[i].pBmpData,0,MAX_STRTIMEBMP_WIDTH*MAX_STRBMP_HEIGHT*PIXBYTE);
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "###########time_str_buf.time_Bmp_param[idx].width = %d,time_str_buf.time_Bmp_param[idx].heigth=%d############################\n",time_str_buf.time_Bmp_param[i].width,time_str_buf.time_Bmp_param[i].height);
		}
		
	}
	//获取当前默认的两个通道名称位置
	s32Ret = GetParameter(PRM_ID_OSD_CFG_CHAN_FAC,NULL,&osd_xy,sizeof(PRM_OSD_CFG_CHAN),1,SUPER_USER_ID,NULL);
	if(s32Ret != PARAM_OK)
	{
		osd_def_pos.name_def_x = OSD_NAME_DEF_X;
		osd_def_pos.name_def_y = OSD_NAME_DEF_Y;
		osd_def_pos.osd_def_x = OSD_NAME_DEF_X;
		osd_def_pos.osd_def_y = OSD_OTHERNAME_DEF_Y;
		osd_def_pos.time_def_x = OSD_TIME_DEF_X;
		osd_def_pos.time_def_y = OSD_TIME_DEF_Y;
	}
	else
	{
		osd_def_pos.name_def_x = osd_xy.ChannelNamePosition_x;
		osd_def_pos.name_def_y = osd_xy.ChannelNamePosition_y;
#if 0		
		osd_def_pos.osd_def_x = osd_xy.ChannelName2Position_x;
		osd_def_pos.osd_def_y = osd_xy.ChannelName2Position_y;
#endif		
		osd_def_pos.time_def_x = osd_xy.ChannelTimePosition_x;
		osd_def_pos.time_def_y = osd_xy.ChannelTimePosition_y;
	}
	//SN_MEMSET(OSD_Time_Buf,0,MAX_BMP_STR_LEN);
	TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,get_OSD_param_init suc!\n", __LINE__);
	return s32Ret;
}
//****************************************************
//预览部分OSD，设备初始化函数
//*****************************************************

/*************************************************
Function: //Prv_OSD_Fb_Init
Description: //  预览OSD，图形层初始化程序
Calls: 
Called By: //
Input: //devid:设备号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int Prv_OSD_Fb_Init(unsigned char devid)
{
#if defined(USE_UI_OSD)
	g_stScreen[devid].enColorFmt = TDE2_COLOR_FMT_ARGB1555;
	g_stScreen[devid].u32Width = preview_cur_param[devid].w;
	g_stScreen[devid].u32Height = preview_cur_param[devid].h;

	return 0;
#else

	char devname[20];
	HIFB_COLORKEY_S keytmp;
	struct fb_fix_screeninfo stFixInfo;
	struct fb_var_screeninfo stVarInfo;
	struct fb_bitfield stR32 = {10, 5, 0};
	struct fb_bitfield stG32 = {5, 5, 0};
	struct fb_bitfield stB32 = {0, 5, 0};
	struct fb_bitfield stA32 = {15, 1, 0};
	HI_U32 u32Size;
	int w=0,h=0;
	unsigned char vo_dev = devid;
#if defined(Hi3531)||defined(Hi3535)	
	HIFB_ALPHA_S stAlpha = {0};
#endif
#if defined(SN9234H1)	
#ifdef SECOND_DEV
	if(devid == AD)
	{
		vo_dev = SD;
	}
#endif		
#endif
	printf("Prv_OSD_Fb_Init %d\n",devid);
	if(devid)
		SN_SPRINTF(devname,sizeof(devname),"/dev/fb%d",devid+1);
	else
		SN_SPRINTF(devname,sizeof(devname),"/dev/fb%d",devid);
	Prv_fd[devid] = -1;
	Prv_fd[devid] = open(devname, O_RDWR);	
	if (Prv_fd[devid] == -1)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"open frame buffer device error\n");
		return -1;
	}

#if defined(Hi3531)||defined(Hi3535)	
	HI_BOOL g_bCompress = HI_FALSE;
	if (ioctl(Prv_fd[devid], FBIOPUT_COMPRESSION_HIFB, &g_bCompress) < 0)
	{
		 printf("Put FBIOPUT_COMPRESSION_HIFB info failed!\n");
		 close(Prv_fd[devid]);
		Prv_fd[devid] =-1;
		return -1;
	}
	stAlpha.bAlphaChannel = HI_FALSE;
    stAlpha.bAlphaEnable = HI_TRUE;
	stAlpha.u8Alpha0 = 0;
	stAlpha.u8Alpha1 = 0xff;
    if (ioctl(Prv_fd[devid], FBIOPUT_ALPHA_HIFB, &stAlpha) < 0)
    {
   	    printf("Put alpha info failed!\n");
		 close(Prv_fd[devid]);
		Prv_fd[devid] =-1;
		return -1;
    }
#endif

	w = preview_cur_param[vo_dev].w;
	h = preview_cur_param[vo_dev].h;

	stVarInfo.xres_virtual		= w;
	stVarInfo.yres_virtual		= h*2;
	stVarInfo.xres			= w;
	stVarInfo.yres			= h;
	stVarInfo.activate			= FB_ACTIVATE_NOW;
	stVarInfo.bits_per_pixel	= 16;
	stVarInfo.xoffset = 0;
	stVarInfo.yoffset = 0;
	stVarInfo.red	= stR32;
	stVarInfo.green = stG32;
	stVarInfo.blue	= stB32;
	stVarInfo.transp = stA32;
	if (ioctl(Prv_fd[devid], FBIOPUT_VSCREENINFO, &stVarInfo) < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"process frame buffer device error 1\n");
		close(Prv_fd[devid]);
		Prv_fd[devid] =-1;
		return -1;
	}
	
	if (ioctl(Prv_fd[devid], FBIOGET_FSCREENINFO, &stFixInfo) < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"process frame buffer device error 2\n");
		close(Prv_fd[devid]);
		Prv_fd[devid] =-1;
		return -1;
	}
#if defined(SN9234H1)
	keytmp.bKeyEnable = 1;
	keytmp.u32Key = FB_BG_COLORKEY;
	keytmp.bMaskEnable = 0;
	if (ioctl(Prv_fd[devid], FBIOPUT_COLORKEY_HIFB, &keytmp) < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"process frame buffer device error\n");
		close(Prv_fd[devid]);
		Prv_fd[devid] =-1;
		return -1;
	}
	
	if (ioctl(Prv_fd[devid], FBIOGET_COLORKEY_HIFB, &keytmp) < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"process frame buffer device error\n");
		close(Prv_fd[devid]);
		Prv_fd[devid] =-1;
		return -1;
	}
#endif	
	u32Size 	= stFixInfo.smem_len;
	fb_memlen[devid] = stFixInfo.smem_len;
	fb_phyaddr[devid]  = stFixInfo.smem_start;
	fb_mmap[devid]= mmap(NULL, u32Size, PROT_READ|PROT_WRITE, MAP_SHARED, Prv_fd[devid], 0);
	if (MAP_FAILED == fb_mmap[devid])
	{
		fprintf(stderr, TEXT_COLOR_RED("mmap devid=%d fail: %s\n"), devid, strerror(errno));
		close(Prv_fd[devid]);
		Prv_fd[devid] = -1;
		return -1;
	}
	/* 3. create surface */
	g_stScreen[devid].enColorFmt = TDE2_COLOR_FMT_ARGB1555;
	g_stScreen[devid].u32PhyAddr = fb_phyaddr[devid];
	//g_stScreen[devid].u32PhyAddr = stFixInfo.smem_start;
	g_stScreen[devid].u32Width = w;
	g_stScreen[devid].u32Height = h;
	g_stScreen[devid].u32Stride = stFixInfo.line_length;
	g_stScreen[devid].bAlphaMax255 = HI_TRUE;		
	TRACE(SCI_TRACE_NORMAL, MOD_PRV,"++keytmp.bKeyEnable = %d,keytmp.u32Key=%x\n",
		keytmp.bKeyEnable,keytmp.u32Key);
	
/*	
	bShow = HI_TRUE;
    if (ioctl(Prv_fd[devid], FBIOPUT_SHOW_HIFB, &bShow) < 0)
    {
        printf ("Couldn't show fb\n");
        return -1;
    }*/
	return 0;
#endif
}

static int Prv_OSD_Init(unsigned char devid)
{
#if defined(USE_UI_OSD)
	HI_S32 s32Ret=0,flag=0;
	unsigned char ch=0,idx=0;
	TDE2_OPT_S stOpt = {0};
	int i=0,w=0,h=0;//,ch_num=0,ch_idx=0;
	TDE2_RECT_S stSrcRect;
	TDE2_RECT_S stDstRect, stDstRect_t;
	STRING_BMP_ST BmpData={0},*pBmp;
	TDE_HANDLE tde_s32Handle=-1;
	int curY = 0;
	unsigned char vo_dev=devid;
	stOpt.enOutAlphaFrom = ENOUTALPHAFORM;
	stOpt.enColorKeyMode = ENCOLORKEYMODE;
	stOpt.unColorKeyValue.struCkARGB.stAlpha.bCompIgnore = ARGB_IGNORE;
	stOpt.bResize = HI_TRUE;
	
	if (devid>=PRV_VO_DEV_NUM)
	{
		RET_FAILURE("bad parameter: VoDev!!!");
	}
	if(HI_FAILURE == Prv_OSD_Fb_Init(devid))
	{
		return HI_FAILURE;
	}

	w = preview_cur_param[vo_dev].w;
	h = preview_cur_param[vo_dev].h;

	//Get_Bmp_From_Gui(&g_stImgSur[0],fb_mmap[devid] + ((HI_U32)g_stImgSur[0].u32PhyAddr - fb_phyaddr[devid]));
	//默认报警图标
	//g_stImgSur[devid][OSD_ALARM_LAYER].u32PhyAddr = g_stScreen[devid].u32PhyAddr + g_stScreen[devid].u32Stride * g_stScreen[devid].u32Height;
	s32Ret = TDE_CreateSurfaceByFile(g_alarm_type[ch],&g_stImgSur[devid][OSD_ALARM_LAYER],NULL);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
		//return s32Ret;
	}
	//默认录像图标
	//g_stImgSur[devid][OSD_REC_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_ALARM_LAYER].u32PhyAddr + g_stImgSur[devid][OSD_ALARM_LAYER].u32Stride * g_stImgSur[devid][OSD_ALARM_LAYER].u32Height;
	s32Ret = TDE_CreateSurfaceByFile(g_rec_type[ch],&g_stImgSur[devid][OSD_REC_LAYER],NULL);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
		//return s32Ret;
	}
	s32Ret = TDE_CreateSurfaceByFile(g_clickvisual_type[ch],&g_stImgSur[devid][OSD_CLICKVISUAL_LAYER],NULL);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
		//return s32Ret;
	}
	//g_stImgSur[devid][OSD_OUTCAP_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_REC_LAYER].u32PhyAddr + g_stImgSur[devid][OSD_REC_LAYER].u32Stride * g_stImgSur[devid][OSD_REC_LAYER].u32Height;
	
	//默认时间图标
	if(devid==DHD0)
	{//vga画面使用D1分辨率下的时间
		if(TimeOsd_size>=0&&TimeOsd_size<OSD_TIME_RES)
		{
			pBmp = &time_str_buf.time_Bmp_param[TimeOsd_size];
		}
		else
		{
			pBmp = &time_str_buf.time_Bmp_param[0];
		}
	}
	else
	{
		pBmp = &time_str_buf.time_Bmp_param[1];
	}
	//OSD_Free_String(&BmpData);
	//SN_STRNCPY(OSD_Time_Buf,MAX_BMP_STR_LEN,str_buf,MAX_BMP_STR_LEN);
	
	//默认通道名称
	s32Ret = OSD_Get_String(OSD_Name_Buf[0],&BmpData,OSD_GetVoChnFontSize(devid, 0));
	if(s32Ret != STRINGBMP_ERR_NONE)
	{
		OSD_Free_String(&BmpData);
		return -1;
	}
	
	//初始化所有通道位置信息
	for(i=0;i<DEV_CHANNEL_NUM;i++)
	{	
		OSD_Rect[devid][i][OSD_CLICKVISUAL_LAYER].s32Xpos = ((ICON_X-9-g_stImgSur[devid][OSD_ALARM_LAYER].u32Width)/8)*8;
		OSD_Rect[devid][i][OSD_CLICKVISUAL_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[devid][i][OSD_CLICKVISUAL_LAYER].u32Width= g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32Width;
		OSD_Rect[devid][i][OSD_CLICKVISUAL_LAYER].u32Height= g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32Height;
		
		OSD_Rect[devid][i][OSD_ALARM_LAYER].u32Width= g_stImgSur[devid][OSD_ALARM_LAYER].u32Width;
		OSD_Rect[devid][i][OSD_ALARM_LAYER].u32Height= g_stImgSur[devid][OSD_ALARM_LAYER].u32Height;


		OSD_Rect[devid][i][OSD_REC_LAYER].s32Xpos = ((ICON_X+7+g_stImgSur[devid][OSD_ALARM_LAYER].u32Width)/8)*8;
		OSD_Rect[devid][i][OSD_REC_LAYER].s32Ypos = ICON_Y;
		OSD_Rect[devid][i][OSD_REC_LAYER].u32Width= g_stImgSur[devid][OSD_REC_LAYER].u32Width;
		OSD_Rect[devid][i][OSD_REC_LAYER].u32Height= g_stImgSur[devid][OSD_REC_LAYER].u32Height;


		OSD_Rect[devid][i][OSD_TIME_LAYER].u32Width= pBmp->width;
		OSD_Rect[devid][i][OSD_TIME_LAYER].u32Height= pBmp->height;


		OSD_Rect[devid][i][OSD_NAME_LAYER].u32Width= BmpData.width;
		OSD_Rect[devid][i][OSD_NAME_LAYER].u32Height= BmpData.height;

	}
	OSD_Free_String(&BmpData);
	//开始根据当前画面打上预览OSD图标
	//打开任务
	
/*2010-10-25*/
	//clear screen 
	stDstRect.s32Xpos= 0;
	stDstRect.s32Ypos= 0;
	stDstRect.u32Width= g_stScreen[devid].u32Width;
	stDstRect.u32Height= g_stScreen[devid].u32Height;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "#####devid=%d,x =%d,y=%d,w=%d,h=%d,tde_s32Handle=%d##################\n",devid,stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height,tde_s32Handle);
	for(i=0;i<DEV_CHANNEL_NUM;i++)
	{
		MMI_DestroyOsd(i,OSD_ALARM_LAYER);
		MMI_DestroyOsd(i,OSD_REC_LAYER);
		MMI_DestroyOsd(i,OSD_NAME_LAYER);
		MMI_DestroyOsd(i,OSD_CLICKVISUAL_LAYER);
	}
	MMI_DestroyOsdTime();

	//显示时间
	// 3. calculate new pisition
	//g_stImgSur[devid][OSD_TIME_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_TIME_LAYER-1].u32PhyAddr + g_stImgSur[devid][OSD_TIME_LAYER-1].u32Stride * g_stImgSur[devid][OSD_TIME_LAYER-1].u32Height;
	s32Ret = TDE_CreateSurface(pBmp,&g_stImgSur[devid][OSD_TIME_LAYER],NULL);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurface failed,ret=0x%x!\n", __LINE__, s32Ret);
		return -1;
	}
	OSD_PRV_Time_Rect[devid].u32Width= g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//
	OSD_PRV_Time_Rect[devid].u32Height= g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
	
	stSrcRect.s32Xpos = 0;
	stSrcRect.s32Ypos = 0;
	stSrcRect.u32Width = g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//
	stSrcRect.u32Height = g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
	
	stDstRect.s32Xpos= (OSD_PRV_Time_Rect[devid].s32Xpos *w)/SCREEN_DEF_WIDTH;
	stDstRect.s32Ypos= (OSD_PRV_Time_Rect[devid].s32Ypos *h)/SCREEN_DEF_HEIGHT;
	stDstRect.u32Width= stSrcRect.u32Width;//OSD_PRV_Time_Width[devid];//g_stImgSur[devid][OSD_TIME_LAYER].u32Width;
	stDstRect.u32Height= stSrcRect.u32Height;//g_stImgSur[devid][OSD_TIME_LAYER].u32Height;//(OSD_PRV_Time_Rect.u32Height*h)/SCREEN_DEF_HEIGHT;
	stDstRect_t = stDstRect;
	//4. bitblt image to screen 
//	printf("11111111111devid=%d,stSrcRect.u32Width=%d,stSrcRect.u32Height=%d,stDstRect.s32Xpos=%d,stDstRect.s32Ypos=%d,g_stImgSur[devid][OSD_TIME_LAYER]=%d,g_stScreen[devid]=%d\n",devid,stSrcRect.u32Width,stSrcRect.u32Height,stDstRect.s32Xpos,stDstRect.s32Yposg_stImgSur[devid][OSD_TIME_LAYER],g_stScreen[devid]);
	
	
	//关闭任务
	
	for(idx =0;idx<preview_cur_param[vo_dev].ch_num;idx++)
	{	
		ch = preview_cur_param[vo_dev].ch_order[idx];
		if (ch>=DEV_CHANNEL_NUM)
		{
			continue;
		}
		//打开任务
		
		//清除标志位
		flag = 0;

//step1: name icon
		do
		{	//显示通道名称
			if(!(OSD_off_flag[devid][ch] & OSD_NAME_OFF))
			{
				//g_stImgSur[devid][OSD_NAME_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_TIME_LAYER].u32PhyAddr + g_stImgSur[devid][OSD_TIME_LAYER].u32Stride * g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
				s32Ret = OSD_Get_String(OSD_Name_Buf[ch],&BmpData,OSD_GetVoChnFontSize(devid, ch));
				if(s32Ret != STRINGBMP_ERR_NONE)
				{
					OSD_Free_String(&BmpData);
					break;
				}
				s32Ret = TDE_CreateSurface(&BmpData,&g_stImgSur[devid][OSD_NAME_LAYER],NULL);
				if(s32Ret != HI_SUCCESS)
				{
					OSD_Free_String(&BmpData);
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurface failed,ret=0x%x!\n", __LINE__, s32Ret);
					break;
				}
				OSD_Free_String(&BmpData);
				//3. calculate new pisition 
				stSrcRect.s32Xpos = 0;
				stSrcRect.s32Ypos = 0;
				stSrcRect.u32Width = g_stImgSur[devid][OSD_NAME_LAYER].u32Width;
				stSrcRect.u32Height = g_stImgSur[devid][OSD_NAME_LAYER].u32Height;
			
				OSD_Rect[devid][ch][OSD_NAME_LAYER].u32Width= g_stImgSur[devid][OSD_NAME_LAYER].u32Width;
				OSD_Rect[devid][ch][OSD_NAME_LAYER].u32Height= g_stImgSur[devid][OSD_NAME_LAYER].u32Height;
				
				s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][OSD_NAME_LAYER],OSD_NAME_LAYER);
				if(s32Ret == OSD_NOT_IN_MODE)
				{//如果不在当前预览模式下
					break;
				}
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
					break;
				}
				//4. bitblt image to screen
				stDstRect.u32Height = stSrcRect.u32Height;
				stDstRect.u32Width = stSrcRect.u32Width;
				curY = 0;
				Get_Cur_Ypos(devid,ch,&curY);
				Prv_OSD_Cmp_nameAtime(&stDstRect.s32Xpos, &stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height,
					stDstRect_t.s32Xpos, stDstRect_t.s32Ypos, stDstRect_t.u32Width, stDstRect_t.u32Height,curY);
				if(OSD_Name_Type[ch] == 0)
				{
					MMI_CreateOsdName(ch,stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height,(char*)OSD_Name_Buf[ch],s_as32FontH[OSD_GetVoChnFontSize(devid, ch)]);
				}
				else if(OSD_Name_Type[ch] > 0 && OSD_Name_Type[ch] <= LINKAGE_MAX_GROUPNUM)
				{
					int groupNo = OSD_Name_Type[ch]-1;
					MMI_CreateOsdName(ch,stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height,(char*)OSD_GroupName_Buf[groupNo],s_as32FontH[OSD_GetVoChnFontSize(devid, ch)]);
				}
				flag = 1;
			}
		}while(0);
//step2: alarm icon
		do
		{	//重新刷新报警图标
			if(!(OSD_off_flag[devid][ch] & OSD_ALARM_OFF))
			{
				s32Ret = TDE_CreateSurfaceByFile(g_alarm_type[ch],&g_stImgSur[devid][OSD_ALARM_LAYER],NULL);
				if(s32Ret >= 0)
				{
					
					stSrcRect.s32Xpos = 0;
					stSrcRect.s32Ypos = 0;
					stSrcRect.u32Width = g_stImgSur[devid][OSD_ALARM_LAYER].u32Width;
					stSrcRect.u32Height = g_stImgSur[devid][OSD_ALARM_LAYER].u32Height;
					
					OSD_Rect[devid][ch][OSD_ALARM_LAYER].u32Width= g_stImgSur[devid][OSD_ALARM_LAYER].u32Width;
					OSD_Rect[devid][ch][OSD_ALARM_LAYER].u32Height= g_stImgSur[devid][OSD_ALARM_LAYER].u32Height;
					
					s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][OSD_ALARM_LAYER],OSD_ALARM_LAYER);
					if(s32Ret == OSD_NOT_IN_MODE)
					{//如果不在当前预览模式下
						//HI_TDE2_CancelJob(tde_s32Handle);
						break;
					}
					if(s32Ret < 0)
					{
						TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
						//HI_TDE2_CancelJob(tde_s32Handle);
						break;
					}
					
					/* 4. bitblt image to screen */
					
					MMI_CreateOsdBmp(ch,OSD_ALARM_LAYER,alarmtype[ch],stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height);
					
					flag = 1;
				}
				else
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
					//return s32Ret;
				}
			}
		}while(0);
//step3: rec icon
		do
		{	//显示录像图标
			if(!(OSD_off_flag[devid][ch] & OSD_REC_OFF))
			{
				//录像图标
				s32Ret = TDE_CreateSurfaceByFile(g_rec_type[ch],&g_stImgSur[devid][OSD_REC_LAYER],NULL);
				//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,Prv_OSD_Init ,g_rec_type[ch]=%s!\n", __LINE__, g_rec_type[ch]);
				if(s32Ret >= 0)
				{
					stSrcRect.s32Xpos = 0;
					stSrcRect.s32Ypos = 0;
					stSrcRect.u32Width = g_stImgSur[devid][OSD_REC_LAYER].u32Width;
					stSrcRect.u32Height = g_stImgSur[devid][OSD_REC_LAYER].u32Height;
					
					OSD_Rect[devid][ch][OSD_REC_LAYER].u32Width= g_stImgSur[devid][OSD_REC_LAYER].u32Width;
					OSD_Rect[devid][ch][OSD_REC_LAYER].u32Height= g_stImgSur[devid][OSD_REC_LAYER].u32Height;
					
					s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][OSD_REC_LAYER],OSD_REC_LAYER);
					if(s32Ret == OSD_NOT_IN_MODE)
					{//如果不在当前预览模式下
						//HI_TDE2_CancelJob(tde_s32Handle);
						break;
					}
					if(s32Ret < 0)
					{
						TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
						//HI_TDE2_CancelJob(tde_s32Handle);
						break;
					}
					/* 4. bitblt image to screen */
					
					MMI_CreateOsdBmp(ch,OSD_REC_LAYER,rectype[ch],stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height);
					
					flag = 1;
				}
				else
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
					//return s32Ret;
				}
			}
		}while(0);
		do
		{	//重新刷新一点即视图标
			if(!(OSD_off_flag[devid][ch] & OSD_CLICKVISUAL_OFF))
			{
				s32Ret = TDE_CreateSurfaceByFile(g_clickvisual_type[ch],&g_stImgSur[devid][OSD_CLICKVISUAL_LAYER],NULL);
				if(s32Ret >= 0)
				{
					
					stSrcRect.s32Xpos = 0;
					stSrcRect.s32Ypos = 0;
					stSrcRect.u32Width = g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32Width;
					stSrcRect.u32Height = g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32Height;
					
					OSD_Rect[devid][ch][OSD_CLICKVISUAL_LAYER].u32Width= g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32Width;
					OSD_Rect[devid][ch][OSD_CLICKVISUAL_LAYER].u32Height= g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32Height;
					
					s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][OSD_CLICKVISUAL_LAYER],OSD_CLICKVISUAL_LAYER);
					if(s32Ret == OSD_NOT_IN_MODE)
					{//如果不在当前预览模式下
						//HI_TDE2_CancelJob(tde_s32Handle);
						break;
					}
					if(s32Ret < 0)
					{
						TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
						//HI_TDE2_CancelJob(tde_s32Handle);
						break;
					}
					
					/* 4. bitblt image to screen */
					
					MMI_CreateOsdBmp(ch,OSD_CLICKVISUAL_LAYER,clickvisualtype[ch],stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height);
					
					flag = 1;
				}
				else
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
					//return s32Ret;
				}
			}
		}while(0);
	}
	
	return 0;
#else
	HI_S32 s32Ret=0,flag=0;
	unsigned char ch=0,idx=0;
	TDE2_OPT_S stOpt = {0};
	int i=0,w=0,h=0;//,ch_num=0,ch_idx=0;
	TDE2_RECT_S stSrcRect;
	TDE2_RECT_S stDstRect, stDstRect_t;
	STRING_BMP_ST BmpData={0},*pBmp;
	TDE_HANDLE tde_s32Handle=-1;
	unsigned char vo_dev=devid;
	int curY = 0;
	stOpt.enOutAlphaFrom = ENOUTALPHAFORM;
	stOpt.enColorKeyMode = ENCOLORKEYMODE;
	stOpt.unColorKeyValue.struCkARGB.stAlpha.bCompIgnore = ARGB_IGNORE;
	stOpt.bResize = HI_TRUE;

#if defined(SN9234H1)
	if(devid == SPOT_VO_DEV || devid == AD)
#else
	if(devid > 0)
#endif		
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}


	if (devid>=PRV_VO_DEV_NUM)
	{
		RET_FAILURE("bad parameter: VoDev!!!");
	}
	//for(devid=0;devid<VO_MAX_DEV_NUM;devid++)
	{
		if(HI_FAILURE == Prv_OSD_Fb_Init(devid))
		{
			return HI_FAILURE;
		}
#ifdef SECOND_DEV

#if defined(SN9234H1)
		if(devid == AD)
		{
			vo_dev = SD;
		}
#elif defined(Hi3531)
		if(devid == DSD0)
		{
			vo_dev = DSD1;
		}
#endif		
#endif	
		w = preview_cur_param[vo_dev].w;
		h = preview_cur_param[vo_dev].h;
		//Get_Bmp_From_Gui(&g_stImgSur[0],fb_mmap[devid] + ((HI_U32)g_stImgSur[0].u32PhyAddr - fb_phyaddr[devid]));
		//默认报警图标
		g_stImgSur[devid][OSD_ALARM_LAYER].u32PhyAddr = g_stScreen[devid].u32PhyAddr + g_stScreen[devid].u32Stride * g_stScreen[devid].u32Height;
		s32Ret = TDE_CreateSurfaceByFile(g_alarm_type[ch],&g_stImgSur[devid][OSD_ALARM_LAYER],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][OSD_ALARM_LAYER].u32PhyAddr - fb_phyaddr[devid]));
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
			//return s32Ret;
		}
		//默认录像图标
		g_stImgSur[devid][OSD_REC_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_ALARM_LAYER].u32PhyAddr + g_stImgSur[devid][OSD_ALARM_LAYER].u32Stride * g_stImgSur[devid][OSD_ALARM_LAYER].u32Height;
		s32Ret = TDE_CreateSurfaceByFile(g_rec_type[ch],&g_stImgSur[devid][OSD_REC_LAYER],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][OSD_REC_LAYER].u32PhyAddr - fb_phyaddr[devid]));
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
			//return s32Ret;
		}
		g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_REC_LAYER].u32PhyAddr + g_stImgSur[devid][OSD_REC_LAYER].u32Stride * g_stImgSur[devid][OSD_REC_LAYER].u32Height;
		s32Ret = TDE_CreateSurfaceByFile(g_clickvisual_type[ch],&g_stImgSur[devid][OSD_CLICKVISUAL_LAYER],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32PhyAddr - fb_phyaddr[devid]));
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
			//return s32Ret;
		}
#if defined(SN9234H1)
		//默认时间图标
		if(devid==HD)
#else
		//默认时间图标
		if(devid==DHD0)
#endif			
		{//vga画面使用D1分辨率下的时间
			if(TimeOsd_size>=0&&TimeOsd_size<OSD_TIME_RES)
			{
				pBmp = &time_str_buf.time_Bmp_param[TimeOsd_size];
			}
			else
			{
				pBmp = &time_str_buf.time_Bmp_param[0];
			}
		}
		else
		{
			pBmp = &time_str_buf.time_Bmp_param[1];
		}
		//OSD_Free_String(&BmpData);
		//SN_STRNCPY(OSD_Time_Buf,MAX_BMP_STR_LEN,str_buf,MAX_BMP_STR_LEN);
		
		//默认通道名称
		s32Ret = OSD_Get_String(OSD_Name_Buf[0],&BmpData,OSD_GetVoChnFontSize(devid, 0));
		if(s32Ret != STRINGBMP_ERR_NONE)
		{
			OSD_Free_String(&BmpData);
			return -1;
		}
		
		//初始化所有通道位置信息
		for(i=0;i<DEV_CHANNEL_NUM;i++)
		{
			OSD_Rect[devid][i][OSD_CLICKVISUAL_LAYER].s32Xpos = ((ICON_X-9-g_stImgSur[devid][OSD_ALARM_LAYER].u32Width)/8)*8;
			OSD_Rect[devid][i][OSD_CLICKVISUAL_LAYER].s32Ypos = ICON_Y;
			OSD_Rect[devid][i][OSD_CLICKVISUAL_LAYER].u32Width= g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32Width;
			OSD_Rect[devid][i][OSD_CLICKVISUAL_LAYER].u32Height= g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32Height;
#if 1
			OSD_Rect[devid][i][OSD_ALARM_LAYER].u32Width= g_stImgSur[devid][OSD_ALARM_LAYER].u32Width;
			OSD_Rect[devid][i][OSD_ALARM_LAYER].u32Height= g_stImgSur[devid][OSD_ALARM_LAYER].u32Height;
#endif
#if 1
			OSD_Rect[devid][i][OSD_REC_LAYER].s32Xpos = ((ICON_X+7+g_stImgSur[devid][OSD_ALARM_LAYER].u32Width)/8)*8;
			OSD_Rect[devid][i][OSD_REC_LAYER].s32Ypos = ICON_Y;
			OSD_Rect[devid][i][OSD_REC_LAYER].u32Width= g_stImgSur[devid][OSD_REC_LAYER].u32Width;
			OSD_Rect[devid][i][OSD_REC_LAYER].u32Height= g_stImgSur[devid][OSD_REC_LAYER].u32Height;
#endif
#if 1
			OSD_Rect[devid][i][OSD_TIME_LAYER].u32Width= pBmp->width;
			OSD_Rect[devid][i][OSD_TIME_LAYER].u32Height= pBmp->height;
#endif
#if 1
			OSD_Rect[devid][i][OSD_NAME_LAYER].u32Width= BmpData.width;
			OSD_Rect[devid][i][OSD_NAME_LAYER].u32Height= BmpData.height;
#endif
		}
		OSD_Free_String(&BmpData);
		//开始根据当前画面打上预览OSD图标
		//打开任务
		s32Ret = Prv_Open_Task(&tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Open_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
#if 1 /*2010-10-25*/
		//clear screen 
		stDstRect.s32Xpos= 0;
		stDstRect.s32Ypos= 0;
		stDstRect.u32Width= g_stScreen[devid].u32Width;
		stDstRect.u32Height= g_stScreen[devid].u32Height;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "#####devid=%d,x =%d,y=%d,w=%d,h=%d,tde_s32Handle=%d##################\n",devid,stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height,tde_s32Handle);
		s32Ret = HI_TDE2_QuickFill(tde_s32Handle, &g_stScreen[devid], &stDstRect, FB_BG_COLOR);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_QuickFill failed,ret=0x%x!\n", __LINE__, s32Ret);
			HI_TDE2_CancelJob(tde_s32Handle);
			return s32Ret;
		}
#endif	
		//显示时间
		// 3. calculate new pisition
		g_stImgSur[devid][OSD_TIME_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_TIME_LAYER-1].u32PhyAddr + g_stImgSur[devid][OSD_TIME_LAYER-1].u32Stride * g_stImgSur[devid][OSD_TIME_LAYER-1].u32Height;
		s32Ret = TDE_CreateSurface(pBmp,&g_stImgSur[devid][OSD_TIME_LAYER],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][OSD_TIME_LAYER].u32PhyAddr - fb_phyaddr[devid]));
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurface failed,ret=0x%x!\n", __LINE__, s32Ret);
			HI_TDE2_CancelJob(tde_s32Handle);
			return -1;
		}
		OSD_PRV_Time_Rect[devid].u32Width= g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//
		OSD_PRV_Time_Rect[devid].u32Height= g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
		
		stSrcRect.s32Xpos = 0;
		stSrcRect.s32Ypos = 0;
		stSrcRect.u32Width = g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//
		stSrcRect.u32Height = g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
		
		stDstRect.s32Xpos= (OSD_PRV_Time_Rect[devid].s32Xpos *w)/SCREEN_DEF_WIDTH;
		stDstRect.s32Ypos= (OSD_PRV_Time_Rect[devid].s32Ypos *h)/SCREEN_DEF_HEIGHT;
		stDstRect.u32Width= stSrcRect.u32Width;//OSD_PRV_Time_Width[devid];//g_stImgSur[devid][OSD_TIME_LAYER].u32Width;
		stDstRect.u32Height= stSrcRect.u32Height;//g_stImgSur[devid][OSD_TIME_LAYER].u32Height;//(OSD_PRV_Time_Rect.u32Height*h)/SCREEN_DEF_HEIGHT;
		stDstRect_t = stDstRect;
		//时间OSD关闭时，不更新时间
		if(!(OSD_off_flag[0][0] & OSD_TIME_OFF))				
		{
#if defined(SN9234H1)
			//4. bitblt image to screen
			if((devid == HD) || (OSD_off_flag[0][0] & OSD_FB_FLICKER_OFF))
#else
			//4. bitblt image to screen 
			if((devid == DHD0) || (OSD_off_flag[0][0] & OSD_FB_FLICKER_OFF))
#endif				
			{
				s32Ret = HI_TDE2_QuickCopy(tde_s32Handle, &g_stImgSur[devid][OSD_TIME_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect);
			}
			else
			{
				s32Ret = HI_TDE2_QuickDeflicker(tde_s32Handle, &g_stImgSur[devid][OSD_TIME_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect);
			}
			if(s32Ret < 0)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_QuickDeflicker failed,ret=0x%x!\n", __LINE__, s32Ret);
				HI_TDE2_CancelJob(tde_s32Handle);
				return s32Ret;
			}
		}
		//关闭任务
		s32Ret = Prv_Close_Task(&tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}		
		for(idx =0;idx<preview_cur_param[vo_dev].ch_num;idx++)
		{	
			ch = preview_cur_param[vo_dev].ch_order[idx];
			if (ch >= DEV_CHANNEL_NUM)
			{
				continue;
			}
			
			//打开任务
			s32Ret = Prv_Open_Task(&tde_s32Handle);
			if(s32Ret < 0)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Open_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
				return s32Ret;
			}
			//清除标志位
			flag = 0;

//step1: name icon
            do
            {   //显示通道名称
                if(!(OSD_off_flag[devid][ch] & OSD_NAME_OFF))
                {
                    g_stImgSur[devid][OSD_NAME_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_TIME_LAYER].u32PhyAddr + g_stImgSur[devid][OSD_TIME_LAYER].u32Stride * g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
					s32Ret = OSD_Get_String(OSD_Name_Buf[ch],&BmpData,OSD_GetVoChnFontSize(devid, ch));
                    if(s32Ret != STRINGBMP_ERR_NONE)
                    {
                        OSD_Free_String(&BmpData);
                        HI_TDE2_CancelJob(tde_s32Handle);
                        break;
                    }
                    s32Ret = TDE_CreateSurface(&BmpData,&g_stImgSur[devid][OSD_NAME_LAYER],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][OSD_NAME_LAYER].u32PhyAddr - fb_phyaddr[devid]));
                    if(s32Ret != HI_SUCCESS)
                    {
                        OSD_Free_String(&BmpData);
                        
                        TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurface failed,ret=0x%x!\n", __LINE__, s32Ret);
                        HI_TDE2_CancelJob(tde_s32Handle);
                        break;
                    }
                    OSD_Free_String(&BmpData);
                    //3. calculate new pisition 
                    stSrcRect.s32Xpos = 0;
                    stSrcRect.s32Ypos = 0;
                    stSrcRect.u32Width = g_stImgSur[devid][OSD_NAME_LAYER].u32Width;
                    stSrcRect.u32Height = g_stImgSur[devid][OSD_NAME_LAYER].u32Height;
                
                    OSD_Rect[devid][ch][OSD_NAME_LAYER].u32Width = g_stImgSur[devid][OSD_NAME_LAYER].u32Width;
                    OSD_Rect[devid][ch][OSD_NAME_LAYER].u32Height = g_stImgSur[devid][OSD_NAME_LAYER].u32Height;
                    
                    s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][OSD_NAME_LAYER],OSD_NAME_LAYER);
                    if(s32Ret == OSD_NOT_IN_MODE)
                    {//如果不在当前预览模式下
                        HI_TDE2_CancelJob(tde_s32Handle);
                        break;
                    }
                    if(s32Ret < 0)
                    {
                        TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
                        HI_TDE2_CancelJob(tde_s32Handle);
                        break;
                    }
                    //4. bitblt image to screen
                    stDstRect.u32Height = stSrcRect.u32Height;
                    stDstRect.u32Width = stSrcRect.u32Width;
					curY = 0;
					Get_Cur_Ypos(devid,ch,&curY);
                    Prv_OSD_Cmp_nameAtime(&stDstRect.s32Xpos, &stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height,
                        stDstRect_t.s32Xpos, stDstRect_t.s32Ypos, stDstRect_t.u32Width, stDstRect_t.u32Height,curY);
#if defined(SN9234H1)
                    if((devid == HD) || (OSD_off_flag[0][0] & OSD_FB_FLICKER_OFF))
#else
                    if((devid == DHD0) || (OSD_off_flag[0][0] & OSD_FB_FLICKER_OFF))
#endif
                    {
                        s32Ret = HI_TDE2_QuickCopy(tde_s32Handle, &g_stImgSur[devid][OSD_NAME_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect);
                    }
                    else
                    {
                        s32Ret = HI_TDE2_QuickDeflicker(tde_s32Handle, &g_stImgSur[devid][OSD_NAME_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect);
                    }
                    if(s32Ret < 0)
                    {
                        TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_Bitblit failed,ret=0x%x, ch: %d!\n", __LINE__, s32Ret, ch);
                        HI_TDE2_CancelJob(tde_s32Handle);
                        break;
                    }
                    flag = 1;
                }
            }while(0);
//step2: alarm icon
            do
            {   //重新刷新报警图标
                if(!(OSD_off_flag[devid][ch] & OSD_ALARM_OFF))
                {
                    g_stImgSur[devid][OSD_ALARM_LAYER].u32PhyAddr = g_stScreen[devid].u32PhyAddr + g_stScreen[devid].u32Stride * g_stScreen[devid].u32Height;
                    s32Ret = TDE_CreateSurfaceByFile(g_alarm_type[ch],&g_stImgSur[devid][OSD_ALARM_LAYER],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][OSD_ALARM_LAYER].u32PhyAddr - fb_phyaddr[devid]));
                    if(s32Ret >= 0)
                    {
                        
                        stSrcRect.s32Xpos = 0;
                        stSrcRect.s32Ypos = 0;
                        stSrcRect.u32Width = g_stImgSur[devid][OSD_ALARM_LAYER].u32Width;
                        stSrcRect.u32Height = g_stImgSur[devid][OSD_ALARM_LAYER].u32Height;
                        
                        OSD_Rect[devid][ch][OSD_ALARM_LAYER].u32Width= g_stImgSur[devid][OSD_ALARM_LAYER].u32Width;
                        OSD_Rect[devid][ch][OSD_ALARM_LAYER].u32Height= g_stImgSur[devid][OSD_ALARM_LAYER].u32Height;
                        
                        s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][OSD_ALARM_LAYER],OSD_ALARM_LAYER);
                        if(s32Ret == OSD_NOT_IN_MODE)
                        {//如果不在当前预览模式下
                            //HI_TDE2_CancelJob(tde_s32Handle);
                            break;
                        }
                        if(s32Ret < 0)
                        {
                            TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
                            //HI_TDE2_CancelJob(tde_s32Handle);
                            break;
                        }
                        
                        /* 4. bitblt image to screen */
                        s32Ret = HI_TDE2_Bitblit(tde_s32Handle, &g_stScreen[devid], &stDstRect, &g_stImgSur[devid][OSD_ALARM_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect, &stOpt);
                        //s32Ret = HI_TDE2_QuickCopy(tde_s32Handle, &g_stImgSur[devid][OSD_ALARM_LAYER], &stSrcRect[OSD_ALARM_LAYER], &g_stScreen[devid], &stDstRect);
                        if(s32Ret < 0)
                        {
                            TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_Bitblit failed,ret=0x%x!\n", __LINE__, s32Ret);
                            //HI_TDE2_CancelJob(tde_s32Handle);
                            break;
                        }
                        flag = 1;
                    }
                    else
                    {
                        TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
                        //return s32Ret;
                    }
                }
            }while(0);
//step3: rec icon
            do
            {   //显示录像图标
                if(!(OSD_off_flag[devid][ch] & OSD_REC_OFF))
                {
                    //录像图标
                    g_stImgSur[devid][OSD_REC_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_ALARM_LAYER].u32PhyAddr + g_stImgSur[devid][OSD_ALARM_LAYER].u32Stride * g_stImgSur[devid][OSD_ALARM_LAYER].u32Height;
                    s32Ret = TDE_CreateSurfaceByFile(g_rec_type[ch],&g_stImgSur[devid][OSD_REC_LAYER],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][OSD_REC_LAYER].u32PhyAddr - fb_phyaddr[devid]));
                    //TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,Prv_OSD_Init ,g_rec_type[ch]=%s!\n", __LINE__, g_rec_type[ch]);
                    if(s32Ret >= 0)
                    {
                        stSrcRect.s32Xpos = 0;
                        stSrcRect.s32Ypos = 0;
                        stSrcRect.u32Width = g_stImgSur[devid][OSD_REC_LAYER].u32Width;
                        stSrcRect.u32Height = g_stImgSur[devid][OSD_REC_LAYER].u32Height;
                        
                        OSD_Rect[devid][ch][OSD_REC_LAYER].u32Width= g_stImgSur[devid][OSD_REC_LAYER].u32Width;
                        OSD_Rect[devid][ch][OSD_REC_LAYER].u32Height= g_stImgSur[devid][OSD_REC_LAYER].u32Height;
                        
                        s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][OSD_REC_LAYER],OSD_REC_LAYER);
                        if(s32Ret == OSD_NOT_IN_MODE)
                        {//如果不在当前预览模式下
                            //HI_TDE2_CancelJob(tde_s32Handle);
                            break;
                        }
                        if(s32Ret < 0)
                        {
                            TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
                            //HI_TDE2_CancelJob(tde_s32Handle);
                            break;
                        }
                        /* 4. bitblt image to screen */
                        s32Ret = HI_TDE2_Bitblit(tde_s32Handle, &g_stScreen[devid], &stDstRect, &g_stImgSur[devid][OSD_REC_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect, &stOpt);
                        if(s32Ret < 0)
                        {
                            TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_Bitblit failed,ret=0x%x!\n", __LINE__, s32Ret);
                            //HI_TDE2_CancelJob(tde_s32Handle);
                            break;
                        }
                        flag = 1;
                    }
                    else
                    {
                        TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
                        //return s32Ret;
                    }
                }
            }while(0);
			do
            {   //重新刷新一点即视图标
                if(!(OSD_off_flag[devid][ch] & OSD_CLICKVISUAL_OFF))
                {
                    g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_REC_LAYER].u32PhyAddr + g_stImgSur[devid][OSD_REC_LAYER].u32Stride * g_stImgSur[devid][OSD_REC_LAYER].u32Height;
                    s32Ret = TDE_CreateSurfaceByFile(g_clickvisual_type[ch],&g_stImgSur[devid][OSD_CLICKVISUAL_LAYER],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32PhyAddr - fb_phyaddr[devid]));
                    if(s32Ret >= 0)
                    {
                        
                        stSrcRect.s32Xpos = 0;
                        stSrcRect.s32Ypos = 0;
                        stSrcRect.u32Width = g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32Width;
                        stSrcRect.u32Height = g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32Height;
                        
                        OSD_Rect[devid][ch][OSD_CLICKVISUAL_LAYER].u32Width= g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32Width;
                        OSD_Rect[devid][ch][OSD_CLICKVISUAL_LAYER].u32Height= g_stImgSur[devid][OSD_CLICKVISUAL_LAYER].u32Height;
                        
                        s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][OSD_CLICKVISUAL_LAYER],OSD_CLICKVISUAL_LAYER);
                        if(s32Ret == OSD_NOT_IN_MODE)
                        {//如果不在当前预览模式下
                            //HI_TDE2_CancelJob(tde_s32Handle);
                            break;
                        }
                        if(s32Ret < 0)
                        {
                            TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
                            //HI_TDE2_CancelJob(tde_s32Handle);
                            break;
                        }
                        
                        /* 4. bitblt image to screen */
                        s32Ret = HI_TDE2_Bitblit(tde_s32Handle, &g_stScreen[devid], &stDstRect, &g_stImgSur[devid][OSD_CLICKVISUAL_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect, &stOpt);
                        //s32Ret = HI_TDE2_QuickCopy(tde_s32Handle, &g_stImgSur[devid][OSD_ALARM_LAYER], &stSrcRect[OSD_ALARM_LAYER], &g_stScreen[devid], &stDstRect);
                        if(s32Ret < 0)
                        {
                            TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_Bitblit failed,ret=0x%x!\n", __LINE__, s32Ret);
                            //HI_TDE2_CancelJob(tde_s32Handle);
                            break;
                        }
                        flag = 1;
                    }
                    else
                    {
                        TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
                        //return s32Ret;
                    }
                }
            }while(0);

			if(flag != 1)
			{
				HI_TDE2_CancelJob(tde_s32Handle);
			}
			else
			{
				//关闭任务
				s32Ret = Prv_Close_Task(&tde_s32Handle);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
					//return s32Ret;
					continue;
				}
			}
		}
		
	}
#if 1 /*2010-9-1 修正：CVBS口画面被挡掉一部分边界*/
	{
		PRV_RECT_S stDspRect;
		CHECK(PRV_GetVoDspRect(vo_dev, &stDspRect));
		CHECK(PRV_SetFbStartXY(Prv_fd[devid], stDspRect.s32X, stDspRect.s32Y));
	}
#endif
	
	return s32Ret;
#endif
}

/****************************************************
预览部分OSD，时间位图格式转换函数
*****************************************************/
static int OSD_Create_Tde_Surface(unsigned char devid,STRING_BMP_ST *pBmpData)
{
#if defined(USE_UI_OSD)
	return 0;
#else

	HI_S32 s32Ret=0;
	
	g_stImgSur[devid][OSD_TIME_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_TIME_LAYER-1].u32PhyAddr + g_stImgSur[devid][OSD_TIME_LAYER-1].u32Stride * g_stImgSur[devid][OSD_TIME_LAYER-1].u32Height;
	s32Ret = TDE_CreateSurface(pBmpData,&g_stImgSur[devid][OSD_TIME_LAYER],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][OSD_TIME_LAYER].u32PhyAddr - fb_phyaddr[devid]));
	if(s32Ret != HI_SUCCESS)
	{
		/*if(pBmpData->pBmpData)
		{
			SN_FREE(pBmpData->pBmpData);		
			pBmpData->pBmpData = NULL;
		}*/
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Line:%d,OSD_Create_Tde_Surface in ctl!\n", __LINE__);
		return -1;
	}
	return  s32Ret;
#endif
}

/****************************************************
预览部分OSD，时间设置接口函数
*****************************************************/
static int Prv_OSD_Set_Time(unsigned char devid,unsigned char ch,TDE_HANDLE tde_s32Handle)
{
#if defined(USE_UI_OSD)
	HI_S32 s32Ret=0;
	TDE2_RECT_S stSrcRect;
	TDE2_RECT_S stDstRect;
	unsigned char vo_dev=devid;
	int w=0,h=0;

	if(devid > DHD0)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (devid>=PRV_VO_DEV_NUM)
	{
		RET_FAILURE("bad parameter: VoDev!!!");
	}
#ifdef SECOND_DEV
#if defined(Hi3531)
		if(devid == DSD0)
		{
			vo_dev = DSD1;
		}
#endif
#endif	
	if(OSD_off_flag[devid][ch] & (OSD_FB_OFF))
	{	
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Line:%d,Prv_OSD_Set_Time in ctl! dev:%d, ch:%d\n", __LINE__, devid, ch);
		return OSD_NOT_IN_MODE;  //
	}

	stSrcRect.s32Xpos = 0;
	stSrcRect.s32Ypos = 0;
	stSrcRect.u32Width = g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//
	stSrcRect.u32Height = g_stImgSur[devid][OSD_TIME_LAYER].u32Height;

	/* 3. calculate new pisition */
	w = preview_cur_param[vo_dev].w;
	h = preview_cur_param[vo_dev].h;

	stDstRect.s32Xpos= (OSD_PRV_Time_Rect[devid].s32Xpos *w)/SCREEN_DEF_WIDTH;
	stDstRect.s32Ypos= (OSD_PRV_Time_Rect[devid].s32Ypos *h)/SCREEN_DEF_HEIGHT;
	stDstRect.u32Width= (OSD_PRV_Time_Rect[devid].u32Width*w)/SCREEN_DEF_WIDTH;
	stDstRect.u32Height= (OSD_PRV_Time_Rect[devid].u32Height*h)/SCREEN_DEF_HEIGHT;
	
	//加上色块填充接口，清除旧的时间
	//stDstRect.u32Width= (stSrcRect.u32Width*w)/SCREEN_DEF_WIDTH;//GUI_FONT_PRV_WIDTH
	

	/* 4. bitblt image to screen */
	//stDstRect.u32Width=  stSrcRect.u32Width;//OSD_PRV_Time_Width[devid];//g_stImgSur[devid][OSD_TIME_LAYER].u32Width;
	//stDstRect.u32Height= stSrcRect.u32Height;//g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
	OSD_PRV_Time_Rect[devid].u32Width = g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//
	OSD_PRV_Time_Rect[devid].u32Height = g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
	//stDstRect.u32Width= (stSrcRect.u32Width*w)/SCREEN_DEF_WIDTH;
	
	MMI_CreateOsdTime(stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height,(char*)time_str_buf.Time_Str,TimeOsd_size);
	
	return s32Ret;
#else

	HI_S32 s32Ret=0;
	TDE2_RECT_S stSrcRect;
	TDE2_RECT_S stDstRect;
	TDE2_OPT_S stOpt = {0};
	unsigned char vo_dev=devid;
	int w=0,h=0;
#if defined(SN9234H1)
	if(devid == SPOT_VO_DEV || devid == AD)
#else
	if(devid > DHD0)
#endif		
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}

	if (devid>=PRV_VO_DEV_NUM)
	{
		RET_FAILURE("bad parameter: VoDev!!!");
	}
#ifdef SECOND_DEV
#if defined(SN9234H1)
	if(devid == AD)
	{
		vo_dev = SD;
	}
#elif defined(Hi3531)
	if(devid == DSD0)
	{
		vo_dev = DSD1;
	}
#endif		
#endif	
	if(OSD_off_flag[devid][ch] & (OSD_FB_OFF))
	{	
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Line:%d,Prv_OSD_Set_Time in ctl! dev:%d, ch:%d\n", __LINE__, devid, ch);
		return OSD_NOT_IN_MODE;  //
	}

	if(Prv_fd[devid]<0)
	{	
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Line:%d,Prv_OSD_Set_Time the correspondence fb has not been opened, Prv_fd[%d]=%d!\n", __LINE__, devid, Prv_fd[devid]);
		return OSD_NOT_IN_MODE;  //
	}

	ch = 0;
	stOpt.enOutAlphaFrom = ENOUTALPHAFORM;
	stOpt.enColorKeyMode = ENCOLORKEYMODE;
	stOpt.unColorKeyValue.struCkARGB.stAlpha.bCompIgnore = ARGB_IGNORE;
	stOpt.bResize = HI_TRUE;
	stOpt.u8GlobalAlpha = 128;
	
	stSrcRect.s32Xpos = 0;
	stSrcRect.s32Ypos = 0;
	stSrcRect.u32Width = g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//
	stSrcRect.u32Height = g_stImgSur[devid][OSD_TIME_LAYER].u32Height;

	/* 3. calculate new pisition */
	w = preview_cur_param[vo_dev].w;
	h = preview_cur_param[vo_dev].h;

	stDstRect.s32Xpos= (OSD_PRV_Time_Rect[devid].s32Xpos *w)/SCREEN_DEF_WIDTH;
	stDstRect.s32Ypos= (OSD_PRV_Time_Rect[devid].s32Ypos *h)/SCREEN_DEF_HEIGHT;
	stDstRect.u32Width=  OSD_PRV_Time_Rect[devid].u32Width;//(OSD_PRV_Time_Rect[devid].u32Width*w)/SCREEN_DEF_WIDTH;
	stDstRect.u32Height= OSD_PRV_Time_Rect[devid].u32Height;//(OSD_PRV_Time_Rect[devid].u32Height*h)/SCREEN_DEF_HEIGHT;
	
	//加上色块填充接口，清除旧的时间
	//stDstRect.u32Width= (stSrcRect.u32Width*w)/SCREEN_DEF_WIDTH;//GUI_FONT_PRV_WIDTH
	s32Ret = HI_TDE2_QuickFill(tde_s32Handle, &g_stScreen[devid], &stDstRect, FB_BG_COLORKEY);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_QuickFill failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}

	/* 4. bitblt image to screen */
	stDstRect.u32Width=  stSrcRect.u32Width;//OSD_PRV_Time_Width[devid];//g_stImgSur[devid][OSD_TIME_LAYER].u32Width;
	stDstRect.u32Height= stSrcRect.u32Height;//g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
	OSD_PRV_Time_Rect[devid].u32Width = g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//
	OSD_PRV_Time_Rect[devid].u32Height = g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
	//stDstRect.u32Width= (stSrcRect.u32Width*w)/SCREEN_DEF_WIDTH;
#if 0
#if defined(SN9234H1)
	/*是否缩放时间显示 1-YES,0-NO*/
	if((devid == HD) || (OSD_off_flag[0][0] & OSD_FB_FLICKER_OFF))
#else
	/*是否缩放时间显示 1-YES,0-NO*/
	if((devid == DHD0) || (OSD_off_flag[0][0] & OSD_FB_FLICKER_OFF))
#endif		
	{
		stOpt.bDeflicker = HI_FALSE;
	}
	else
	{//如果设置了电视去抖功能，那么需要加上抗闪标志
		stOpt.bDeflicker = HI_TRUE;
	}
	s32Ret = HI_TDE2_Bitblit(tde_s32Handle, &g_stScreen[devid], &stDstRect, &g_stImgSur[devid][OSD_TIME_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect, &stOpt);
#else
	//stDstRect.u32Width= stSrcRect.u32Width;
	//stDstRect.u32Height= stSrcRect.u32Height;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "###########Prv_OSD_Set_Time    devid = %d  ,ch = %d  ,stSrcRect.s32Xpos= %d,stSrcRect.s32Ypos=%d,stSrcRect.u32Width=%d,stSrcRect.u32Height=%d,stDstRect.s32Xpos =%d ,stDstRect.s32Ypos=%d,stDstRect.u32Width=%d,stDstRect.u32Height=%d4############################\n",
	//			devid,ch,stSrcRect.s32Xpos,stSrcRect.s32Ypos,stSrcRect.u32Width,stSrcRect.u32Height,stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height);
#if defined(SN9234H1)
	if((devid == HD) || (OSD_off_flag[0][0] & OSD_FB_FLICKER_OFF))
#else
	if((devid == DHD0) || (OSD_off_flag[0][0] & OSD_FB_FLICKER_OFF))
#endif		
	{
		s32Ret = HI_TDE2_QuickCopy(tde_s32Handle, &g_stImgSur[devid][OSD_TIME_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect);
	}
	else
	{
		s32Ret = HI_TDE2_QuickDeflicker(tde_s32Handle, &g_stImgSur[devid][OSD_TIME_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect);
	}
#endif
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Time failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
	return s32Ret;
#endif
}

/****************************************************
预览部分OSD，时间位置设置接口函数
*****************************************************/
int Prv_OSD_Set_Time_xy(unsigned char ch,int x,int y)
{//配置通道位置
	HI_S32 s32Ret=0;
	int i=0;
	//STRING_BMP_ST BmpData={0};
	STRING_BMP_ST *pBmp;
	TDE_HANDLE tde_s32Handle=-1;
	
	ch = 0;
/*
	s32Ret = OSD_Get_String(OSD_Time_Buf,&BmpData,GUI_FONT_PRV);
	if(s32Ret != STRINGBMP_ERR_NONE)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Get_Time_String failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
*/
	for(i=0;i<VO_MAX_DEV_NUM;i++)
	{
#if defined(SN9234H1)
		if(i == SPOT_VO_DEV || i == AD)
		{
			continue;
		}
#else
		if(i > DHD0)
		{
			RET_FAILURE("Not Support Dev: SD!!");
		}
#endif	
#if !defined(USE_UI_OSD)
		if (Prv_fd[i] <= 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Prv_fd[%d] = %d\n", i, Prv_fd[i]);
			continue;
		}
#endif		
		Prv_OSD_Ctl_Off(i,ch,OSD_TIME_LAYER,NULL,1);
		OSD_PRV_Time_Rect[i].s32Xpos = x;
		OSD_PRV_Time_Rect[i].s32Ypos = y;
	}
	for(i=0;i<VO_MAX_DEV_NUM;i++)
	{
#if defined(SN9234H1)
		if(i == SPOT_VO_DEV || i == AD)
#else
		if(i > DHD0)
#endif			
		{
			continue;
		}
#if !defined(USE_UI_OSD)		
		if (Prv_fd[i] <= 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Prv_fd[%d] = %d\n", i, Prv_fd[i]);
			continue;
		}
#endif		
		if(Get_Cur_idx(i,ch)  < 0)
		{//不在当前模式下
			continue;
		}
		/*
		s32Ret = OSD_Get_String(OSD_Time_Buf,&BmpData,OSD_GetVoChnFontSize(i, -1));
		if(s32Ret != STRINGBMP_ERR_NONE)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Get_Time_String failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}*/
#if defined(SN9234H1)
		if(i==HD)
#else
		if(i==DHD0)
#endif			
		{//vga画面使用D1分辨率下的时间
			if(TimeOsd_size>=0&&TimeOsd_size<OSD_TIME_RES)
			{
				pBmp = &time_str_buf.time_Bmp_param[TimeOsd_size];
			}
			else
			{
				pBmp = &time_str_buf.time_Bmp_param[0];
			}
		}
		else
		{
			pBmp = &time_str_buf.time_Bmp_param[1];
		}
		//打开任务
		s32Ret = Prv_Open_Task(&tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Open_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
			//OSD_Free_String(&BmpData);
			return s32Ret;
		}
		s32Ret = OSD_Create_Tde_Surface(i,pBmp);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Create_Tde_Surface failed,ret=0x%x!\n", __LINE__, s32Ret);
			//OSD_Free_String(&BmpData);
			HI_TDE2_CancelJob(tde_s32Handle);
			return s32Ret;
		}
		s32Ret = Prv_OSD_Set_Time(i,ch,tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
			//OSD_Free_String(&BmpData);
			HI_TDE2_CancelJob(tde_s32Handle);
			return s32Ret;
		}
		if(s32Ret == OSD_NOT_IN_MODE)
		{
			HI_TDE2_CancelJob(tde_s32Handle);
			//OSD_Free_String(&BmpData);
			continue;
		}
		//关闭任务
		s32Ret = Prv_Close_Task(&tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
			//OSD_Free_String(&BmpData);
			return s32Ret;
		}
		//OSD_Free_String(&BmpData);
	}
	return s32Ret;
}

/****************************************************
预览部分OSD，通道名称设置接口函数
*****************************************************/
static int Prv_OSD_Set_Ch(unsigned char devid,unsigned char ch,STRING_BMP_ST *pBmpData,TDE_HANDLE tde_s32Handle)
{
#if defined(USE_UI_OSD)
	HI_S32 s32Ret=0;
	int curY = 0;
	//STRING_BMP_ST BmpData;
	TDE2_RECT_S stSrcRect;
	TDE2_RECT_S stDstRect, stDstRect_t;
	
	if(OSD_off_flag[devid][ch] & (OSD_NAME_OFF|OSD_FB_OFF))
	{	
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Line:%d,Prv_OSD_Set_Ch in ctl!\n", __LINE__);
		return OSD_NOT_IN_MODE;  //
	}
	
	if(Get_Cur_idx(devid,ch)  < 0)
	{//不在当前模式下
		return OSD_NOT_IN_MODE;
	}
	//
	//g_stImgSur[devid][OSD_NAME_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_NAME_LAYER-1].u32PhyAddr + g_stImgSur[devid][OSD_NAME_LAYER-1].u32Stride * g_stImgSur[devid][OSD_NAME_LAYER-1].u32Height;
	s32Ret = TDE_CreateSurface(pBmpData,&g_stImgSur[devid][OSD_NAME_LAYER],NULL);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurface failed,ret=0x%x!\n", __LINE__, s32Ret);
		return -1;
	}
	stSrcRect.s32Xpos = 0;
	stSrcRect.s32Ypos = 0;
	stSrcRect.u32Width = g_stImgSur[devid][OSD_NAME_LAYER].u32Width;
	stSrcRect.u32Height = g_stImgSur[devid][OSD_NAME_LAYER].u32Height;
	
	OSD_Rect[devid][ch][OSD_NAME_LAYER].u32Width = g_stImgSur[devid][OSD_NAME_LAYER].u32Width;
	OSD_Rect[devid][ch][OSD_NAME_LAYER].u32Height = g_stImgSur[devid][OSD_NAME_LAYER].u32Height;
	
	/* 3. calculate new pisition */
	s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][OSD_NAME_LAYER],OSD_NAME_LAYER);
	if(s32Ret == OSD_NOT_IN_MODE)
	{//如果不在当前预览模式下
		return OSD_NOT_IN_MODE;
	}
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
	/* 4. bitblt image to screen */

	//stDstRect.u32Height = stSrcRect.u32Height;
	//stDstRect.u32Width = stSrcRect.u32Width;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@!!!!!Get_Cur_Rect	ch_name devid = %d,ch = %d,pDstRect->s32Xpos = %d,pDstRect->s32Ypos=%d,pDstRect->u32Width=%d,pDstRect->u32Height=%d\n",
	//		devid,ch,stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height);
	stDstRect_t.s32Xpos= (OSD_PRV_Time_Rect[devid].s32Xpos *preview_cur_param[devid].w)/SCREEN_DEF_WIDTH;
	stDstRect_t.s32Ypos= (OSD_PRV_Time_Rect[devid].s32Ypos *preview_cur_param[devid].h)/SCREEN_DEF_HEIGHT;
	stDstRect_t.u32Width=  OSD_PRV_Time_Rect[devid].u32Width;
	stDstRect_t.u32Height= OSD_PRV_Time_Rect[devid].u32Height;
	Get_Cur_Ypos(devid,ch,&curY);
	Prv_OSD_Cmp_nameAtime(&stDstRect.s32Xpos, &stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height,
		stDstRect_t.s32Xpos, stDstRect_t.s32Ypos, stDstRect_t.u32Width, stDstRect_t.u32Height,curY);
	
	if(OSD_Name_Type[ch] ==0)
	{
		MMI_CreateOsdName(ch,stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height,(char*)OSD_Name_Buf[ch],s_as32FontH[OSD_GetVoChnFontSize(devid, ch)]);
	}
	else if(OSD_Name_Type[ch] > 0 && OSD_Name_Type[ch] <= LINKAGE_MAX_GROUPNUM)
	{
		int groupNo = OSD_Name_Type[ch]-1;
		MMI_CreateOsdName(ch,stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height,(char*)OSD_GroupName_Buf[groupNo],s_as32FontH[OSD_GetVoChnFontSize(devid, ch)]);
	}
	s32Ret = Prv_Disp_Pic(devid,ch,tde_s32Handle);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Disp_Pic failed,ret=0x%x! \n", __LINE__, s32Ret);
		return s32Ret;
	}
	//strcpy(OSD_Name_Buf[ch],str);
	return s32Ret;
#else

	HI_S32 s32Ret=0;
	int curY = 0;
	//STRING_BMP_ST BmpData;
	TDE2_OPT_S stOpt = {0};
	TDE2_RECT_S stSrcRect;
	TDE2_RECT_S stDstRect, stDstRect_t;
	
	if(OSD_off_flag[devid][ch] & (OSD_NAME_OFF|OSD_FB_OFF))
	{	
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Line:%d,Prv_OSD_Set_Ch in ctl!\n", __LINE__);
		return OSD_NOT_IN_MODE;  //
	}
	if(Prv_fd[devid]<0)
	{	
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Line:%d,Prv_OSD_Set_Ch the correspondence fb has not been opened, Prv_fd[%d]=%d!\n", __LINE__, devid, Prv_fd[devid]);
		return OSD_NOT_IN_MODE;  //
	}
	if(Get_Cur_idx(devid,ch)  < 0)
	{//不在当前模式下
		return OSD_NOT_IN_MODE;
	}
	//
	stOpt.enOutAlphaFrom = ENOUTALPHAFORM;
	stOpt.enColorKeyMode = ENCOLORKEYMODE;
	stOpt.unColorKeyValue.struCkARGB.stAlpha.bCompIgnore = ARGB_IGNORE;
	stOpt.bResize = HI_TRUE;
	g_stImgSur[devid][OSD_NAME_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_NAME_LAYER-1].u32PhyAddr + g_stImgSur[devid][OSD_NAME_LAYER-1].u32Stride * g_stImgSur[devid][OSD_NAME_LAYER-1].u32Height;
	s32Ret = TDE_CreateSurface(pBmpData,&g_stImgSur[devid][OSD_NAME_LAYER],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][OSD_NAME_LAYER].u32PhyAddr - fb_phyaddr[devid]));
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurface failed,ret=0x%x!\n", __LINE__, s32Ret);
		return -1;
	}
	stSrcRect.s32Xpos = 0;
	stSrcRect.s32Ypos = 0;
	stSrcRect.u32Width = g_stImgSur[devid][OSD_NAME_LAYER].u32Width;
	stSrcRect.u32Height = g_stImgSur[devid][OSD_NAME_LAYER].u32Height;
	
	OSD_Rect[devid][ch][OSD_NAME_LAYER].u32Width = g_stImgSur[devid][OSD_NAME_LAYER].u32Width;
	OSD_Rect[devid][ch][OSD_NAME_LAYER].u32Height = g_stImgSur[devid][OSD_NAME_LAYER].u32Height;
	
	/* 3. calculate new pisition */
	s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][OSD_NAME_LAYER],OSD_NAME_LAYER);
	if(s32Ret == OSD_NOT_IN_MODE)
	{//如果不在当前预览模式下
		return OSD_NOT_IN_MODE;
	}
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
	/* 4. bitblt image to screen */
#if 0
	s32Ret = HI_TDE2_Bitblit(tde_s32Handle, &g_stScreen[devid], &stDstRect, &g_stImgSur[devid][OSD_NAME_LAYER+ch], &stSrcRect, &g_stScreen[devid], &stDstRect, &stOpt);
#else
	//stDstRect.u32Height = stSrcRect.u32Height;
	//stDstRect.u32Width = stSrcRect.u32Width;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@!!!!!Get_Cur_Rect	ch_name devid = %d,ch = %d,pDstRect->s32Xpos = %d,pDstRect->s32Ypos=%d,pDstRect->u32Width=%d,pDstRect->u32Height=%d\n",
	//		devid,ch,stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height);
    stDstRect_t.s32Xpos= (OSD_PRV_Time_Rect[devid].s32Xpos *preview_cur_param[devid].w)/SCREEN_DEF_WIDTH;
    stDstRect_t.s32Ypos= (OSD_PRV_Time_Rect[devid].s32Ypos *preview_cur_param[devid].h)/SCREEN_DEF_HEIGHT;
    stDstRect_t.u32Width=  OSD_PRV_Time_Rect[devid].u32Width;
    stDstRect_t.u32Height= OSD_PRV_Time_Rect[devid].u32Height;

	Get_Cur_Ypos(devid,ch,&curY);
    Prv_OSD_Cmp_nameAtime(&stDstRect.s32Xpos, &stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height,
        stDstRect_t.s32Xpos, stDstRect_t.s32Ypos, stDstRect_t.u32Width, stDstRect_t.u32Height,curY);
	
#if defined(SN9234H1)
	if((devid == HD) || (OSD_off_flag[0][0] & OSD_FB_FLICKER_OFF))
#else
	if((devid == DHD0) || (OSD_off_flag[0][0] & OSD_FB_FLICKER_OFF))
#endif		
	{
		s32Ret = HI_TDE2_QuickCopy(tde_s32Handle, &g_stImgSur[devid][OSD_NAME_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect);
	}
	else
	{
		s32Ret = HI_TDE2_QuickDeflicker(tde_s32Handle, &g_stImgSur[devid][OSD_NAME_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect);
	}
#endif
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Ch failed,ret=0x%x!\n", __LINE__, s32Ret);
		//return s32Ret;
	}
	s32Ret = Prv_Disp_Pic(devid,ch,tde_s32Handle);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Disp_Pic failed,ret=0x%x! \n", __LINE__, s32Ret);
		return s32Ret;
	}
	//strcpy(OSD_Name_Buf[ch],str);
	return s32Ret;
#endif
}

/****************************************************
预览部分OSD，通道名称位置设置接口函数
*****************************************************/
static int Prv_OSD_Set_CH_xy(unsigned char ch,int x,int y)
{//配置通道位置
	HI_S32 s32Ret=0;
	int i=0;
	//TDE_HANDLE tde_s32Handle=-1;
	//STRING_BMP_ST BmpData={0};
	TDE2_RECT_S old_rect[VO_MAX_DEV_NUM];
	
	TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,Prv_OSD_Set_CH_xy  x   %d,y  %d \n", __LINE__,x,y);
	if((x == OSD_Rect[0][ch][OSD_NAME_LAYER].s32Xpos) && ( y == OSD_Rect[0][ch][OSD_NAME_LAYER].s32Ypos))
	{//x,y相同，那么不进行修改
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_CH_xy the same x ,y \n", __LINE__);
		return 0;
	}
	for(i=0;i<VO_MAX_DEV_NUM;i++)
	{
#if defined(SN9234H1)
		if(i == SPOT_VO_DEV  || i == AD)
#else
		if(i>DHD0)
#endif			
		{
			continue;
		}
		old_rect[i] = OSD_Rect[i][ch][OSD_NAME_LAYER];
		OSD_Rect[i][ch][OSD_NAME_LAYER].s32Xpos = x;
		OSD_Rect[i][ch][OSD_NAME_LAYER].s32Ypos = y;
	}
	for(i=0;i<VO_MAX_DEV_NUM;i++)
	{
#if defined(SN9234H1)
		if(i == SPOT_VO_DEV  || i == AD)
#else		
		if(i > DHD0)
#endif			
		{
			continue;
		}
#if !defined(USE_UI_OSD)
		if (Prv_fd[i] <= 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Prv_fd[%d] = %d\n", i, Prv_fd[i]);
			continue;
		}
#endif		
		if(OSD_off_flag[i][ch] & (OSD_PB_OFF|OSD_NAME_OFF|OSD_FB_OFF))
		{	
			TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Line:%d,Prv_OSD_Set_CH_xy in ctl!\n", __LINE__);
			if(i == VO_MAX_DEV_NUM-1)
			{
				return 0;
			}
			continue;
		}
		Prv_OSD_Ctl_Off(i,ch,OSD_NAME_LAYER,&old_rect[i],0);
	}
/*
	s32Ret = OSD_Get_String(OSD_Name_Buf[ch],&BmpData,GUI_FONT_PRV);
	if(s32Ret != STRINGBMP_ERR_NONE)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Get_String failed,ret=0x%x!\n", __LINE__, s32Ret);
		HI_TDE2_CancelJob(tde_s32Handle);
		return s32Ret;
	}
*/
	for(i=0;i<VO_MAX_DEV_NUM;i++)
	{
#if defined(SN9234H1)
		if(i == SPOT_VO_DEV  || i == AD)
#else
		if(i > DHD0)
#endif
		{
			continue;
		}
		s32Ret = PRV_Osd_Chn_reflesh(i,ch);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
	}
	return 0;
}
//隐藏图标
static int Prv_OSD_Ctl_Off(unsigned char devid, unsigned char ch, unsigned char type, TDE2_RECT_S *pRect, unsigned char flag)
{
#if defined(USE_UI_OSD)
	HI_S32 i = 0;;
	TDE2_RECT_S stDstRect; 
	unsigned char vo_dev = devid;
		
	if(type == OSD_CLEAR_LAYER)
	{
		//name
		for(i=0;i<DEV_CHANNEL_NUM;i++)
		{
			MMI_DestroyOsd(i,OSD_ALARM_LAYER);
			MMI_DestroyOsd(i,OSD_REC_LAYER);
			MMI_DestroyOsd(i,OSD_NAME_LAYER);
			MMI_DestroyOsd(i,OSD_CLICKVISUAL_LAYER);
		}
		MMI_DestroyOsdTime();
	}
	else
	{	
		if(type != OSD_TIME_LAYER)
		{
			//MMI_DestroyOsd(ch,OSD_ALARM_LAYER);
			//MMI_DestroyOsd(ch,OSD_REC_LAYER);
			//MMI_DestroyOsd(ch,OSD_NAME_LAYER);
			//MMI_DestroyOsd(ch,OSD_OUTCAP_LAYER);
			MMI_DestroyOsd(ch,type);
		}	
		else
		{			
			stDstRect.s32Xpos= (OSD_PRV_Time_Rect[devid].s32Xpos *preview_cur_param[vo_dev].w)/SCREEN_DEF_WIDTH;
			stDstRect.s32Ypos= (OSD_PRV_Time_Rect[devid].s32Ypos *preview_cur_param[vo_dev].h)/SCREEN_DEF_HEIGHT;
			stDstRect.u32Width= g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//g_stImgSur[devid][OSD_TIME_LAYER].u32Width;
			stDstRect.u32Height= g_stImgSur[devid][OSD_TIME_LAYER].u32Height;//g_stImgSur[devid][OSD_TIME_LAYER].u32Height;//(OSD_PRV_Time_Rect.u32Height*h)/SCREEN_DEF_HEIGHT;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "stDstRect.s32Xpos: %d, stDstRect.s32Ypos: %d, stDstRect.u32Width: %d, stDstRect.u32Height: %d", 
											stDstRect.s32Xpos, stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height);
			MMI_DestroyOsdTime();
		}
	}
	//printf(TEXT_COLOR_PURPLE("HI_TDE2_QuickFill type = %d stsrcrect: %d,%d,%d,%d, 	stDstRect: %d, %d, %d, %d\n"), type,OSD_Rect[devid][ch][type].s32Xpos, OSD_Rect[devid][ch][type].s32Ypos, OSD_Rect[devid][ch][type].u32Width, OSD_Rect[devid][ch][type].u32Height,stDstRect.s32Xpos, stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height);

	if(flag)
	{
		switch(type)
		{
			case OSD_TIME_LAYER :
				OSD_off_flag[devid][ch] |= OSD_TIME_OFF;
				break;	
			case OSD_NAME_LAYER :
				OSD_off_flag[devid][ch] |= OSD_NAME_OFF;
				break;	
			case OSD_ALARM_LAYER :
				OSD_off_flag[devid][ch] |= OSD_ALARM_OFF;
				break;	
			case OSD_REC_LAYER :
				OSD_off_flag[devid][ch] |= OSD_REC_OFF;
				break;
			case OSD_CLICKVISUAL_LAYER:
				OSD_off_flag[devid][ch] |= OSD_CLICKVISUAL_OFF;
				break;
			default:
				break;
		}
	}
	return 0;
#else

	HI_S32 s32Ret = 0;
	int curY = 0;
	TDE2_RECT_S stDstRect, stDstRect_t;	
	unsigned char *p = fb_mmap[devid];
	unsigned char vo_dev = devid;
	if(!p)
		return -1;
#ifdef SECOND_DEV
#if defined(SN9234H1)
	if(devid == AD)
	{
		vo_dev = SD;
	}
#elif defined(Hi3531)
	if(devid == DSD0)
	{
		vo_dev = DSD1;
	}
#endif	
#endif		
	if (Prv_fd[devid] <= 0)
	{
		printf(TEXT_COLOR_YELLOW("devid=%d not opened!\n"), devid);
		return -1;
	}
	
	if(type == OSD_CLEAR_LAYER)
	{
		stDstRect.s32Xpos = 0;
		stDstRect.s32Ypos = 0;
		stDstRect.u32Height = preview_cur_param[vo_dev].h;
		stDstRect.u32Width = preview_cur_param[vo_dev].w;
	}
	else
	{
	/*	if(type == OSD_TIME_LAYER)
		{
			goto out;
		}
		else*/
		if(type != OSD_TIME_LAYER)
		{
			s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,pRect,type);
			if(s32Ret == OSD_NOT_IN_MODE)
			{//如果不在当前预览模式下
				goto out;
			}
			if(s32Ret < 0)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
				return s32Ret;
			}
			if(type == OSD_NAME_LAYER)
			{
				stDstRect_t.s32Xpos= (OSD_PRV_Time_Rect[devid].s32Xpos *preview_cur_param[devid].w)/SCREEN_DEF_WIDTH;
	    		stDstRect_t.s32Ypos= (OSD_PRV_Time_Rect[devid].s32Ypos *preview_cur_param[devid].h)/SCREEN_DEF_HEIGHT;
	    		stDstRect_t.u32Width=  OSD_PRV_Time_Rect[devid].u32Width;
	    		stDstRect_t.u32Height= OSD_PRV_Time_Rect[devid].u32Height;
				Get_Cur_Ypos(devid,ch,&curY);
	    		Prv_OSD_Cmp_nameAtime(&stDstRect.s32Xpos, &stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height,\
	        		stDstRect_t.s32Xpos, stDstRect_t.s32Ypos, stDstRect_t.u32Width, stDstRect_t.u32Height,curY);
			}
		#if 0	
			if(type == OSD_NAME_LAYER)/*通道名称OSD不使用缩放后，刷新区域有所变动，使用此宽高。预览时间类似，报警和录像图标则与原先保持不变*/
			{
				stDstRect.u32Width = OSD_Rect[devid][ch][type].u32Width;// g_stImgSur[devid][OSD_NAME_LAYER].u32Width;
				stDstRect.u32Height = OSD_Rect[devid][ch][type].u32Height; //g_stImgSur[devid][OSD_NAME_LAYER].u32Height;
			}
		#endif	
		}	
		else
		{			
			stDstRect.s32Xpos= (OSD_PRV_Time_Rect[devid].s32Xpos *preview_cur_param[vo_dev].w)/SCREEN_DEF_WIDTH;
			stDstRect.s32Ypos= (OSD_PRV_Time_Rect[devid].s32Ypos *preview_cur_param[vo_dev].h)/SCREEN_DEF_HEIGHT;
			stDstRect.u32Width= g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//g_stImgSur[devid][OSD_TIME_LAYER].u32Width;
			stDstRect.u32Height= g_stImgSur[devid][OSD_TIME_LAYER].u32Height;//g_stImgSur[devid][OSD_TIME_LAYER].u32Height;//(OSD_PRV_Time_Rect.u32Height*h)/SCREEN_DEF_HEIGHT;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "stDstRect.s32Xpos: %d, stDstRect.s32Ypos: %d, stDstRect.u32Width: %d, stDstRect.u32Height: %d", 
											stDstRect.s32Xpos, stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height);

		}
	}
	//printf(TEXT_COLOR_PURPLE("HI_TDE2_QuickFill type = %d stsrcrect: %d,%d,%d,%d,     stDstRect: %d, %d, %d, %d\n"), type,OSD_Rect[devid][ch][type].s32Xpos, OSD_Rect[devid][ch][type].s32Ypos, OSD_Rect[devid][ch][type].u32Width, OSD_Rect[devid][ch][type].u32Height,stDstRect.s32Xpos, stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height);
		
	TDE_HANDLE s32Handle = HI_TDE2_BeginJob();
	if (HI_SUCCESS == HI_TDE2_QuickFill(s32Handle, &g_stScreen[devid], &stDstRect, FB_BG_COLOR))
	{
		CHECK_RET(HI_TDE2_EndJob(s32Handle, HI_FALSE, HI_TRUE, 10));
	}
	else
	{
		printf(TEXT_COLOR_PURPLE("HI_TDE2_QuickFill fail! stDstRect: %d, %d, %d, %d\n"), stDstRect.s32Xpos, stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height);
		CHECK_RET(HI_TDE2_CancelJob(s32Handle));
	}
	
out:
	if(flag)
	{
		switch(type)
		{
			case OSD_TIME_LAYER :
				OSD_off_flag[devid][ch] |= OSD_TIME_OFF;
				break;	
			case OSD_NAME_LAYER :
				OSD_off_flag[devid][ch] |= OSD_NAME_OFF;
				break;	
			case OSD_ALARM_LAYER :
				OSD_off_flag[devid][ch] |= OSD_ALARM_OFF;
				break;	
			case OSD_REC_LAYER :
				OSD_off_flag[devid][ch] |= OSD_REC_OFF;
				break;
			case OSD_CLICKVISUAL_LAYER:
				OSD_off_flag[devid][ch] |= OSD_CLICKVISUAL_OFF;
				break;
			default:
				break;
		}
	}
	return 0;
#endif
}
//显示图标
static int Prv_OSD_Ctl_On(unsigned char devid,unsigned char ch,unsigned char type,TDE_HANDLE tde_s32Handle)
{
#if defined(USE_UI_OSD)
	HI_S32 s32Ret=0;
	TDE2_OPT_S stOpt = {0};
	TDE2_RECT_S stSrcRect;
	TDE2_RECT_S stDstRect;
	STRING_BMP_ST *pBmp;
	int w = 0, h = 0;
	w = preview_cur_param[devid].w;
	h = preview_cur_param[devid].h;
	
	if(type != OSD_TIME_LAYER)
	{
		if(OSD_off_flag[devid][ch] & (OSD_PB_OFF|OSD_FB_OFF))
		{	
			s32Ret = OSD_NOT_IN_MODE;
			goto out;
		}
		if(Get_Cur_idx(devid,ch) < 0)
		{//不在当前模式下
			s32Ret = OSD_NOT_IN_MODE;
			goto out;
		}
	}
	
	stOpt.enOutAlphaFrom = ENOUTALPHAFORM;
	stOpt.enColorKeyMode = ENCOLORKEYMODE;
	stOpt.unColorKeyValue.struCkARGB.stAlpha.bCompIgnore = ARGB_IGNORE;
	stOpt.bResize = HI_TRUE;

	switch(type)
	{
		case OSD_ALARM_LAYER:
			{
				s32Ret = TDE_CreateSurfaceByFile(g_alarm_type[ch],&g_stImgSur[devid][type],NULL);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
				stSrcRect.s32Xpos = 0;
				stSrcRect.s32Ypos = 0;
				stSrcRect.u32Width = g_stImgSur[devid][type].u32Width;
				stSrcRect.u32Height = g_stImgSur[devid][type].u32Height;
				OSD_Rect[devid][ch][type].u32Width = g_stImgSur[devid][type].u32Width;
				OSD_Rect[devid][ch][type].u32Height = g_stImgSur[devid][type].u32Height;
				//3. calculate new pisition 
				s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][type],type);
				if(s32Ret == OSD_NOT_IN_MODE)
				{//如果不在当前预览模式下
					goto out;
				}
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
				MMI_DestroyOsd(ch,OSD_ALARM_LAYER);
				MMI_CreateOsdBmp(ch,OSD_ALARM_LAYER,alarmtype[ch],stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height);
			}
			break;
		case OSD_REC_LAYER:
			{
				s32Ret = TDE_CreateSurfaceByFile(g_rec_type[ch],&g_stImgSur[devid][type],NULL);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
				stSrcRect.s32Xpos = 0;
				stSrcRect.s32Ypos = 0;
				stSrcRect.u32Width = g_stImgSur[devid][type].u32Width;
				stSrcRect.u32Height = g_stImgSur[devid][type].u32Height;
				OSD_Rect[devid][ch][type].u32Width = g_stImgSur[devid][type].u32Width;
				OSD_Rect[devid][ch][type].u32Height = g_stImgSur[devid][type].u32Height;
				//3. calculate new pisition 
				s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][type],type);
				if(s32Ret == OSD_NOT_IN_MODE)
				{//如果不在当前预览模式下
					goto out;
				}
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
				MMI_DestroyOsd(ch,OSD_REC_LAYER);
				/* 4. bitblt image to screen */
				MMI_CreateOsdBmp(ch,OSD_REC_LAYER,rectype[ch],stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height);
			}
			break;
		case OSD_TIME_LAYER:
			{
			/*
			STRING_BMP_ST BmpData;
			
			 s32Ret = OSD_Get_String(OSD_Time_Buf,&BmpData,GUI_FONT_PRV);
			 if(s32Ret != STRINGBMP_ERR_NONE)
			 {
			 TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d,OSD_Get_Time_String faild 0x%x!\n",__LINE__,s32Ret);
			 return s32Ret;
			 }
			 s32Ret = OSD_Create_Tde_Surface(devid,&BmpData);
			 if(s32Ret != STRINGBMP_ERR_NONE)
			 {
			 TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d,OSD_Create_Tde_Surface faild 0x%x!\n", __LINE__, s32Ret);
			 OSD_Free_String(&BmpData);
			 return s32Ret;
			 }
			 OSD_Free_String(&BmpData); 
			 s32Ret = Prv_OSD_Set_Time(devid,ch,tde_s32Handle);
			 if(s32Ret < 0)
			 {
			 TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Time failed,ret=0x%x!\n", __LINE__, s32Ret);
			 return s32Ret;
			}
				s32Ret = OSD_NOT_IN_MODE;
				goto out
			*/;
				//显示时间
				// 3. calculate new pisition
			//默认时间图标
				if(devid==DHD0)
				{//vga画面使用D1分辨率下的时间
					if(TimeOsd_size>=0&&TimeOsd_size<OSD_TIME_RES)
					{
						pBmp = &time_str_buf.time_Bmp_param[TimeOsd_size];
					}
					else
					{
						pBmp = &time_str_buf.time_Bmp_param[0];
					}
						
				}
				else
				{
					pBmp = &time_str_buf.time_Bmp_param[1];
				}
				s32Ret = TDE_CreateSurface(pBmp,&g_stImgSur[devid][OSD_TIME_LAYER],NULL);
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurface failed,ret=0x%x!\n", __LINE__, s32Ret);
					return -1;
				}
				OSD_PRV_Time_Rect[devid].u32Width= g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//
				OSD_PRV_Time_Rect[devid].u32Height= g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
				
				stSrcRect.s32Xpos = 0;
				stSrcRect.s32Ypos = 0;
				stSrcRect.u32Width = g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//
				stSrcRect.u32Height = g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
				
				stDstRect.s32Xpos= (OSD_PRV_Time_Rect[devid].s32Xpos *w)/SCREEN_DEF_WIDTH;
				stDstRect.s32Ypos= (OSD_PRV_Time_Rect[devid].s32Ypos *h)/SCREEN_DEF_HEIGHT;
				stDstRect.u32Width= (stSrcRect.u32Width *w)/SCREEN_DEF_WIDTH;//OSD_PRV_Time_Width[devid];//g_stImgSur[devid][OSD_TIME_LAYER].u32Width;
				stDstRect.u32Height= (stSrcRect.u32Height*h)/SCREEN_DEF_HEIGHT;//g_stImgSur[devid][OSD_TIME_LAYER].u32Height;//(OSD_PRV_Time_Rect.u32Height*h)/SCREEN_DEF_HEIGHT;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "stDstRect.s32Xpos: %d, stDstRect.s32Ypos: %d, stDstRect.u32Width: %d, stDstRect.u32Height: %d", 
												stDstRect.s32Xpos, stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height);
				//4. bitblt image to screen 				
				MMI_CreateOsdTime(stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height,(char*)time_str_buf.Time_Str,TimeOsd_size);
			}
			break;
		case OSD_NAME_LAYER:
			{
				STRING_BMP_ST BmpData={0};
				OSD_off_flag[devid][ch] &= ~OSD_NAME_OFF; //首先修改状态，因为在设置字符串的时候会使用到这个标志位
				if(OSD_Name_Type[ch] == 0)
				{
					s32Ret = OSD_Get_String(OSD_Name_Buf[ch],&BmpData,/*GUI_FONT_PRV*/OSD_GetVoChnFontSize(devid, ch));
				}
				else if(OSD_Name_Type[ch] > 0 && OSD_Name_Type[ch] <= LINKAGE_MAX_GROUPNUM)
				{
					int groupNo = OSD_Name_Type[ch]-1;
					s32Ret = OSD_Get_String(OSD_GroupName_Buf[groupNo],&BmpData,OSD_GetVoChnFontSize(devid, ch));
				}
				else
				{
					break;
				}
				if(s32Ret != STRINGBMP_ERR_NONE)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Get_String failed,ret=0x%x!\n", __LINE__, s32Ret);
					OSD_off_flag[devid][ch] |= OSD_NAME_OFF; //如果失败需要回复状态
					return s32Ret;
				}
				s32Ret = Prv_OSD_Set_Ch(devid,ch,&BmpData,tde_s32Handle);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Ch failed,ret=0x%x!\n", __LINE__, s32Ret);
					OSD_Free_String(&BmpData);
					OSD_off_flag[devid][ch] |= OSD_NAME_OFF;
					return s32Ret;
				}
				OSD_Free_String(&BmpData);
			}	
			break;
		case OSD_CLICKVISUAL_LAYER:
			{
				s32Ret = TDE_CreateSurfaceByFile(g_clickvisual_type[ch],&g_stImgSur[devid][type],NULL);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
				stSrcRect.s32Xpos = 0;
				stSrcRect.s32Ypos = 0;
				stSrcRect.u32Width = g_stImgSur[devid][type].u32Width;
				stSrcRect.u32Height = g_stImgSur[devid][type].u32Height;
				OSD_Rect[devid][ch][type].u32Width = g_stImgSur[devid][type].u32Width;
				OSD_Rect[devid][ch][type].u32Height = g_stImgSur[devid][type].u32Height;
				//3. calculate new pisition 
				s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][type],type);
				if(s32Ret == OSD_NOT_IN_MODE)
				{//如果不在当前预览模式下
					goto out;
				}
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
				MMI_DestroyOsd(ch,OSD_CLICKVISUAL_LAYER);
				MMI_CreateOsdBmp(ch,OSD_CLICKVISUAL_LAYER,clickvisualtype[ch],stDstRect.s32Xpos,stDstRect.s32Ypos,stDstRect.u32Width,stDstRect.u32Height);
				
			}
			break;
		default:
			TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,Prv_OSD_Ctl_On not surport layer\n", __LINE__);
			return -1;
	}
out:	
	switch(type)
	{
		case OSD_ALARM_LAYER:
			OSD_off_flag[devid][ch] &= ~OSD_ALARM_OFF;
			break;
		case OSD_REC_LAYER:
			OSD_off_flag[devid][ch] &= ~OSD_REC_OFF;
			break;
		case OSD_TIME_LAYER:
			OSD_off_flag[devid][ch] &= ~OSD_TIME_OFF;
			break;
		case OSD_NAME_LAYER:
			OSD_off_flag[devid][ch] &= ~OSD_NAME_OFF;
			break;
		case OSD_CLICKVISUAL_LAYER:
			OSD_off_flag[devid][ch] &= ~OSD_CLICKVISUAL_OFF;
			break;
	}	
	return s32Ret;	
#else
	HI_S32 s32Ret=0;
	TDE2_OPT_S stOpt = {0};
	TDE2_RECT_S stSrcRect;
	TDE2_RECT_S stDstRect;
	STRING_BMP_ST *pBmp;
	int w = 0, h = 0;
	w = preview_cur_param[devid].w;
	h = preview_cur_param[devid].h;
	if (Prv_fd[devid] <= 0)
	{
		printf(TEXT_COLOR_YELLOW("devid=%d not opened!\n"), devid);
		return -1;
	}
	if(type != OSD_TIME_LAYER)
	{
		if(OSD_off_flag[devid][ch] & (OSD_PB_OFF|OSD_FB_OFF))
		{	
			s32Ret = OSD_NOT_IN_MODE;
			goto out;
		}
		if(Get_Cur_idx(devid,ch) < 0)
		{//不在当前模式下
			s32Ret = OSD_NOT_IN_MODE;
			goto out;
		}
	}
	stOpt.enOutAlphaFrom = ENOUTALPHAFORM;
	stOpt.enColorKeyMode = ENCOLORKEYMODE;
	stOpt.unColorKeyValue.struCkARGB.stAlpha.bCompIgnore = ARGB_IGNORE;
	stOpt.bResize = HI_TRUE;

	switch(type)
	{
		case OSD_ALARM_LAYER:
			{
				g_stImgSur[devid][type].u32PhyAddr = g_stScreen[devid].u32PhyAddr + g_stScreen[devid].u32Stride * g_stScreen[devid].u32Height;
				s32Ret = TDE_CreateSurfaceByFile(g_alarm_type[ch],&g_stImgSur[devid][type],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][type].u32PhyAddr - fb_phyaddr[devid]));
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
				stSrcRect.s32Xpos = 0;
				stSrcRect.s32Ypos = 0;
				stSrcRect.u32Width = g_stImgSur[devid][type].u32Width;
				stSrcRect.u32Height = g_stImgSur[devid][type].u32Height;
				OSD_Rect[devid][ch][type].u32Width = g_stImgSur[devid][type].u32Width;
				OSD_Rect[devid][ch][type].u32Height = g_stImgSur[devid][type].u32Height;
				//3. calculate new pisition 
				s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][type],type);
				if(s32Ret == OSD_NOT_IN_MODE)
				{//如果不在当前预览模式下
					goto out;
				}
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
				s32Ret = HI_TDE2_QuickFill(tde_s32Handle, &g_stScreen[devid], &stDstRect, FB_BG_COLOR);
				/* 4. bitblt image to screen */
				s32Ret = HI_TDE2_Bitblit(tde_s32Handle, &g_stScreen[devid], &stDstRect, &g_stImgSur[devid][type], &stSrcRect, &g_stScreen[devid], &stDstRect, &stOpt);
				//s32Ret = HI_TDE2_QuickCopy(tde_s32Handle, &g_stImgSur[devid][type], &stSrcRect, &g_stScreen[devid], &stDstRect);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_Bitblit failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
			}
			break;
		case OSD_REC_LAYER:
			{
				g_stImgSur[devid][type].u32PhyAddr = g_stImgSur[devid][type-1].u32PhyAddr + g_stImgSur[devid][type-1].u32Stride * g_stImgSur[devid][type-1].u32Height;
				s32Ret = TDE_CreateSurfaceByFile(g_rec_type[ch],&g_stImgSur[devid][type],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][type].u32PhyAddr - fb_phyaddr[devid]));
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
				stSrcRect.s32Xpos = 0;
				stSrcRect.s32Ypos = 0;
				stSrcRect.u32Width = g_stImgSur[devid][type].u32Width;
				stSrcRect.u32Height = g_stImgSur[devid][type].u32Height;
				OSD_Rect[devid][ch][type].u32Width = g_stImgSur[devid][type].u32Width;
				OSD_Rect[devid][ch][type].u32Height = g_stImgSur[devid][type].u32Height;
				//3. calculate new pisition 
				s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][type],type);
				if(s32Ret == OSD_NOT_IN_MODE)
				{//如果不在当前预览模式下
					goto out;
				}
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
				/* 4. bitblt image to screen */
				s32Ret = HI_TDE2_QuickFill(tde_s32Handle, &g_stScreen[devid], &stDstRect, FB_BG_COLOR);
				s32Ret = HI_TDE2_Bitblit(tde_s32Handle, &g_stScreen[devid], &stDstRect, &g_stImgSur[devid][type], &stSrcRect, &g_stScreen[devid], &stDstRect, &stOpt);
				//s32Ret = HI_TDE2_QuickCopy(tde_s32Handle, &g_stImgSur[devid][type], &stSrcRect, &g_stScreen[devid], &stDstRect);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_Bitblit failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
			}
			break;
		case OSD_TIME_LAYER:
			{
			/*
			STRING_BMP_ST BmpData;
			
			 s32Ret = OSD_Get_String(OSD_Time_Buf,&BmpData,GUI_FONT_PRV);
			 if(s32Ret != STRINGBMP_ERR_NONE)
			 {
			 TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d,OSD_Get_Time_String faild 0x%x!\n",__LINE__,s32Ret);
			 return s32Ret;
			 }
			 s32Ret = OSD_Create_Tde_Surface(devid,&BmpData);
			 if(s32Ret != STRINGBMP_ERR_NONE)
			 {
			 TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d,OSD_Create_Tde_Surface faild 0x%x!\n", __LINE__, s32Ret);
			 OSD_Free_String(&BmpData);
			 return s32Ret;
			 }
			 OSD_Free_String(&BmpData);	
			 s32Ret = Prv_OSD_Set_Time(devid,ch,tde_s32Handle);
			 if(s32Ret < 0)
			 {
			 TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Time failed,ret=0x%x!\n", __LINE__, s32Ret);
			 return s32Ret;
			}
				s32Ret = OSD_NOT_IN_MODE;
				goto out
			*/;
				//显示时间
				// 3. calculate new pisition
#if defined(SN9234H1)
				//默认时间图标
				if(devid==HD)
#else
				//默认时间图标
				if(devid==DHD0)
#endif					
				{//vga画面使用D1分辨率下的时间
					if(TimeOsd_size>=0&&TimeOsd_size<OSD_TIME_RES)
					{
						pBmp = &time_str_buf.time_Bmp_param[TimeOsd_size];
					}
					else
					{
						pBmp = &time_str_buf.time_Bmp_param[0];
					}
				}
				else
				{
					pBmp = &time_str_buf.time_Bmp_param[1];
				}
				g_stImgSur[devid][OSD_TIME_LAYER].u32PhyAddr = g_stImgSur[devid][OSD_TIME_LAYER-1].u32PhyAddr + g_stImgSur[devid][OSD_TIME_LAYER-1].u32Stride * g_stImgSur[devid][OSD_TIME_LAYER-1].u32Height;
				s32Ret = TDE_CreateSurface(pBmp,&g_stImgSur[devid][OSD_TIME_LAYER],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][OSD_TIME_LAYER].u32PhyAddr - fb_phyaddr[devid]));
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurface failed,ret=0x%x!\n", __LINE__, s32Ret);
					HI_TDE2_CancelJob(tde_s32Handle);
					return -1;
				}
				OSD_PRV_Time_Rect[devid].u32Width= g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//
				OSD_PRV_Time_Rect[devid].u32Height= g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
				
				stSrcRect.s32Xpos = 0;
				stSrcRect.s32Ypos = 0;
				stSrcRect.u32Width = g_stImgSur[devid][OSD_TIME_LAYER].u32Width;//OSD_PRV_Time_Width[devid];//
				stSrcRect.u32Height = g_stImgSur[devid][OSD_TIME_LAYER].u32Height;
				
				stDstRect.s32Xpos= (OSD_PRV_Time_Rect[devid].s32Xpos *w)/SCREEN_DEF_WIDTH;
				stDstRect.s32Ypos= (OSD_PRV_Time_Rect[devid].s32Ypos *h)/SCREEN_DEF_HEIGHT;
				stDstRect.u32Width= stSrcRect.u32Width;//OSD_PRV_Time_Width[devid];//g_stImgSur[devid][OSD_TIME_LAYER].u32Width;
				stDstRect.u32Height= stSrcRect.u32Height;//g_stImgSur[devid][OSD_TIME_LAYER].u32Height;//(OSD_PRV_Time_Rect.u32Height*h)/SCREEN_DEF_HEIGHT;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "stDstRect.s32Xpos: %d, stDstRect.s32Ypos: %d, stDstRect.u32Width: %d, stDstRect.u32Height: %d", 
												stDstRect.s32Xpos, stDstRect.s32Ypos, stDstRect.u32Width, stDstRect.u32Height);
#if defined(SN9234H1)
				//4. bitblt image to screen 
				if((devid == HD) || (OSD_off_flag[0][0] & OSD_FB_FLICKER_OFF))
#else
				//4. bitblt image to screen 
				if((devid == DHD0) || (OSD_off_flag[0][0] & OSD_FB_FLICKER_OFF))
#endif					
				{
					s32Ret = HI_TDE2_QuickCopy(tde_s32Handle, &g_stImgSur[devid][OSD_TIME_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect);
				}
				else
				{
					s32Ret = HI_TDE2_QuickDeflicker(tde_s32Handle, &g_stImgSur[devid][OSD_TIME_LAYER], &stSrcRect, &g_stScreen[devid], &stDstRect);
				}
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_QuickDeflicker failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
			}
			break;
		case OSD_NAME_LAYER:
			{
				STRING_BMP_ST BmpData={0};
				OSD_off_flag[devid][ch] &= ~OSD_NAME_OFF; //首先修改状态，因为在设置字符串的时候会使用到这个标志位
				if(OSD_Name_Type[ch] == 0)
				{
					s32Ret = OSD_Get_String(OSD_Name_Buf[ch],&BmpData,/*GUI_FONT_PRV*/OSD_GetVoChnFontSize(devid, ch));
				}
				else if(OSD_Name_Type[ch] > 0 && OSD_Name_Type[ch] <= LINKAGE_MAX_GROUPNUM)
				{
					int groupNo = OSD_Name_Type[ch]-1;
					s32Ret = OSD_Get_String(OSD_GroupName_Buf[groupNo],&BmpData,OSD_GetVoChnFontSize(devid, ch));
				}
				else
				{
					break;
				}
				if(s32Ret != STRINGBMP_ERR_NONE)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Get_String failed,ret=0x%x!\n", __LINE__, s32Ret);
					OSD_off_flag[devid][ch] |= OSD_NAME_OFF; //如果失败需要回复状态
					return s32Ret;
				}
				s32Ret = Prv_OSD_Set_Ch(devid,ch,&BmpData,tde_s32Handle);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Ch failed,ret=0x%x!\n", __LINE__, s32Ret);
					OSD_Free_String(&BmpData);
					OSD_off_flag[devid][ch] |= OSD_NAME_OFF;
					return s32Ret;
				}
				OSD_Free_String(&BmpData);
			}	
			break;
		case OSD_CLICKVISUAL_LAYER:
			{
				g_stImgSur[devid][type].u32PhyAddr = g_stImgSur[devid][OSD_REC_LAYER].u32PhyAddr + g_stImgSur[devid][OSD_REC_LAYER].u32Stride * g_stImgSur[devid][OSD_REC_LAYER].u32Height;
				s32Ret = TDE_CreateSurfaceByFile(g_clickvisual_type[ch],&g_stImgSur[devid][type],fb_mmap[devid] + ((HI_U32)g_stImgSur[devid][type].u32PhyAddr - fb_phyaddr[devid]));
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,TDE_CreateSurfaceByFile failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
				stSrcRect.s32Xpos = 0;
				stSrcRect.s32Ypos = 0;
				stSrcRect.u32Width = g_stImgSur[devid][type].u32Width;
				stSrcRect.u32Height = g_stImgSur[devid][type].u32Height;
				OSD_Rect[devid][ch][type].u32Width = g_stImgSur[devid][type].u32Width;
				OSD_Rect[devid][ch][type].u32Height = g_stImgSur[devid][type].u32Height;
				//3. calculate new pisition 
				s32Ret = Get_Cur_Rect(devid,ch,&stDstRect,&OSD_Rect[devid][ch][type],type);
				if(s32Ret == OSD_NOT_IN_MODE)
				{//如果不在当前预览模式下
					goto out;
				}
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Get_Cur_Rect failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
				s32Ret = HI_TDE2_QuickFill(tde_s32Handle, &g_stScreen[devid], &stDstRect, FB_BG_COLOR);
				/* 4. bitblt image to screen */
				s32Ret = HI_TDE2_Bitblit(tde_s32Handle, &g_stScreen[devid], &stDstRect, &g_stImgSur[devid][type], &stSrcRect, &g_stScreen[devid], &stDstRect, &stOpt);
				//s32Ret = HI_TDE2_QuickCopy(tde_s32Handle, &g_stImgSur[devid][type], &stSrcRect, &g_stScreen[devid], &stDstRect);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_Bitblit failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
			}
			break;
		default:
			TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,Prv_OSD_Ctl_On not surport layer\n", __LINE__);
			return -1;
	}
out:	
	switch(type)
	{
		case OSD_ALARM_LAYER:
			OSD_off_flag[devid][ch] &= ~OSD_ALARM_OFF;
			break;
		case OSD_REC_LAYER:
			OSD_off_flag[devid][ch] &= ~OSD_REC_OFF;
			break;
		case OSD_TIME_LAYER:
			OSD_off_flag[devid][ch] &= ~OSD_TIME_OFF;
			break;
		case OSD_NAME_LAYER:
			OSD_off_flag[devid][ch] &= ~OSD_NAME_OFF;
			break;
		case OSD_CLICKVISUAL_LAYER:
			OSD_off_flag[devid][ch] &= ~OSD_CLICKVISUAL_OFF;
			break;
	}	
	return s32Ret;
#endif
}
//****************************************************
//预览部分OSD，时间刷新接口函数
//*****************************************************

int Prv_Disp_Time(char * str)
{
	HI_S32 s32Ret=0;
	//STRING_BMP_ST BmpData={0};
	STRING_BMP_ST *pBmp;
	TDE_HANDLE tde_s32Handle=-1;
	int ch=0;//,ch_num=0,ch_idx=0;
	int i=0,flag=0;
	
	if(!g_fb_flag)
	{
		return 0;
	}
	
	//时间OSD关闭时，不更新时间
	if(OSD_off_flag[0][0] & OSD_TIME_OFF)
		return HI_SUCCESS;
					
	s32Ret = Prv_Open_Task(&tde_s32Handle);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Open_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
	for(i = 0;i < VO_MAX_DEV_NUM; i++)
	{
#if defined(SN9234H1)
		if(i == SPOT_VO_DEV  || i == AD)
#else
		if(i > DHD0)
#endif			
		{
			continue;
		}
#if !defined(USE_UI_OSD)
		if (Prv_fd[i] <= 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Prv_fd[%d] = %d\n", i, Prv_fd[i]);
			continue;
		}
#endif		
		//s32Ret = OSD_Get_String(str,&BmpData,OSD_GetVoChnFontSize(i, -1)/*GUI_FONT_PRV*/);
		/*if(s32Ret != STRINGBMP_ERR_NONE)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d, OSD_Set_Time OSD_Get_String faild 0x%x!\n",__LINE__,s32Ret);
			HI_TDE2_CancelJob(tde_s32Handle);
			return s32Ret;
		}*/
#if defined(SN9234H1)
		if(i==HD)
#else
		if(i==DHD0)
#endif			
		{//vga画面使用D1分辨率下的时间
			if(TimeOsd_size>=0&&TimeOsd_size<OSD_TIME_RES)
			{
				pBmp = &time_str_buf.time_Bmp_param[TimeOsd_size];
			}
			else
			{
				pBmp = &time_str_buf.time_Bmp_param[0];
			}
		}
		else
		{
			pBmp = &time_str_buf.time_Bmp_param[1];
		}
		s32Ret = OSD_Create_Tde_Surface(i,pBmp);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Create_Tde_Surface failed,ret=0x%x! i=%d\n", __LINE__, s32Ret,i);
			//OSD_Free_String(&BmpData);	
			HI_TDE2_CancelJob(tde_s32Handle);
			return s32Ret;
		}
		//for(ch =ch_idx;ch<(ch_idx+ch_num);ch++)
		{
			s32Ret = Prv_OSD_Set_Time(i,ch,tde_s32Handle);
			if(s32Ret < 0)
			{
				TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d,Prv_OSD_Set_Time faild 0x%x!\n", __LINE__,s32Ret);
				HI_TDE2_CancelJob(tde_s32Handle);
				return s32Ret;
			}
			if(s32Ret != OSD_NOT_IN_MODE)
				flag |=1;
		}
		//OSD_Free_String(&BmpData);	
	}
	//关闭任务
	if(!flag)
	{
		HI_TDE2_CancelJob(tde_s32Handle);
		return 0;
	}
	s32Ret = Prv_Close_Task(&tde_s32Handle);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
	return s32Ret;
}
//刷新单通道的录像、报警图标
static int Prv_Disp_Pic(unsigned char devid,unsigned char ch,TDE_HANDLE tde_s32Handle)
{
	HI_S32 s32Ret=0;
	
	if(!(OSD_off_flag[devid][ch] & OSD_ALARM_OFF))
	{
		//重新刷新报警图标
		s32Ret = Prv_OSD_Ctl_On(devid,ch,OSD_ALARM_LAYER,tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Ctl_On failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
	}
	if(!(OSD_off_flag[devid][ch] & OSD_REC_OFF))
	{
		//重新刷新录像图标
		s32Ret = Prv_OSD_Ctl_On(devid,ch,OSD_REC_LAYER,tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Ctl_On failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
	}
	if(!(OSD_off_flag[devid][ch] & OSD_CLICKVISUAL_OFF))
	{
		//重新刷新报警图标
		s32Ret = Prv_OSD_Ctl_On(devid,ch,OSD_CLICKVISUAL_LAYER,tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Ctl_On failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
	}
	return 0;
}

int PRV_GetClickVisual_IconState(int chn)
{
	if(chn < 0 || chn >= DEV_CHANNEL_NUM)
	{
		return 0;
	}
	if(!(OSD_off_flag[DHD0][chn] & OSD_CLICKVISUAL_OFF))
	{
		return 1;	
	}
	else
	{
		return 0;
	}
}

static int PRV_Osd_Chn_reflesh(unsigned char devid,unsigned char ch)
{
	HI_S32 s32Ret=0,flag=0;
	TDE_HANDLE tde_s32Handle=-1;
	STRING_BMP_ST BmpData={0};
#if !defined(USE_UI_OSD)	
	if (Prv_fd[devid] <= 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Prv_fd[%d] = %d\n",devid, Prv_fd[devid]);
		return -1;
	}
#endif
	//打开任务
	s32Ret = Prv_Open_Task(&tde_s32Handle);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Open_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
	//获取当前字符串图片信息
	if(OSD_Name_Type[ch] == 0)
	{
		s32Ret = OSD_Get_String(OSD_Name_Buf[ch],&BmpData,OSD_GetVoChnFontSize(devid, ch)/*GUI_FONT_PRV*/);
	}
	else if(OSD_Name_Type[ch] > 0 && OSD_Name_Type[ch] <= LINKAGE_MAX_GROUPNUM)
	{
		int groupNo = OSD_Name_Type[ch]-1;
		s32Ret = OSD_Get_String(OSD_GroupName_Buf[groupNo],&BmpData,OSD_GetVoChnFontSize(devid, ch));
	}
	else
	{
		HI_TDE2_CancelJob(tde_s32Handle);
		return 0;
	} 
	if(s32Ret != STRINGBMP_ERR_NONE)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Get_String failed,ret=0x%x!\n", __LINE__, s32Ret);
		HI_TDE2_CancelJob(tde_s32Handle);
		return s32Ret;
	}
	//显示通道名称OSD
	s32Ret = Prv_OSD_Set_Ch(devid,ch,&BmpData,tde_s32Handle);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Ch failed,ret=0x%x!\n", __LINE__, s32Ret);
		OSD_Free_String(&BmpData);
		HI_TDE2_CancelJob(tde_s32Handle);
		return s32Ret;
	}
	if(s32Ret != OSD_NOT_IN_MODE)
		flag |=1;
	OSD_Free_String(&BmpData);
	//关闭任务
	if(!flag)
	{
		HI_TDE2_CancelJob(tde_s32Handle);
		//SN_STRNCPY(OSD_Name_Buf[ch],CHANNEL_NAME_LEN,str,CHANNEL_NAME_LEN);
		return 0;
	}
	s32Ret = Prv_Close_Task(&tde_s32Handle);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}	
	return 0;
}
//----------------------------------------------
//对外接口
//
//
//****************************************************
//FB的显示、隐藏接口函数
//*****************************************************

int Prv_OSD_Show(unsigned char devid,unsigned char on)
{
#if defined(USE_UI_OSD)
	MMI_OsdShowCtrl(on);
	return 0;
#else
	HI_S32 s32Ret=0;
	if(Prv_fd[devid] == -1)
		return -1;
	s32Ret = ioctl (Prv_fd[devid],FBIOPUT_SHOW_HIFB,&on);
	return s32Ret;
#endif
}

int Prv_Disp_OSD(unsigned char devid)
{
#if defined(USE_UI_OSD)
	HI_S32 s32Ret=0;
	STRING_BMP_ST BmpData={0},*pBmp;
	TDE_HANDLE tde_s32Handle=-1;
	int ch=0,idx=0;//,w=0,h=0;
	int flag=0;
	unsigned char vo_dev = devid;
	
	if(!g_fb_flag)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Disp_OSD failed: dev:%d\n", __LINE__, devid);
		return OSD_INIT_ERR;
	}
	
	if (devid>=PRV_VO_DEV_NUM)
	{
		RET_FAILURE("bad parameter: VoDev!!!");
	}

	//刷掉所有的TDE
	s32Ret = Prv_OSD_Ctl_Off(devid,0,OSD_CLEAR_LAYER,NULL,OSD_CLEAR);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Ctl_Off failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
	if(!(OSD_off_flag[0][0] & OSD_TIME_OFF))				
	{
		
		if(devid==DHD0)
		{//vga画面使用D1分辨率下的时间
			if(TimeOsd_size>=0&&TimeOsd_size<OSD_TIME_RES)
			{
				pBmp = &time_str_buf.time_Bmp_param[TimeOsd_size];
			}
			else
			{
				pBmp = &time_str_buf.time_Bmp_param[0];
			}
		}
		else
		{
			pBmp = &time_str_buf.time_Bmp_param[1];
		}
		s32Ret = OSD_Create_Tde_Surface(devid,pBmp);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Create_Tde_Surface failed,ret=0x%x!\n", __LINE__, s32Ret);
			//OSD_Free_String(&BmpData);	
			return s32Ret;
		}
		{
			s32Ret = Prv_OSD_Set_Time(devid,ch,tde_s32Handle);
			if(s32Ret < 0)
			{
				TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d,Prv_OSD_Set_Time faild 0x%x!\n", __LINE__,s32Ret);
				//OSD_Free_String(&BmpData);	
				return s32Ret;
			}
		}
		//OSD_Free_String(&BmpData);	
		//关闭任务
		
	}
	for(idx =0;idx<preview_cur_param[vo_dev].ch_num;idx++)
	{
		ch = preview_cur_param[vo_dev].ch_order[idx];
		if (ch>=DEV_CHANNEL_NUM || ch<0)
		{
			continue;
		}
		//打开任务
		
		//清除标志位
		flag = 0;
		if(!(OSD_off_flag[devid][ch] & OSD_NAME_OFF))
		{
			//重新刷新通道名称
			if(OSD_Name_Type[ch] == 0)
			{
			s32Ret = OSD_Get_String(OSD_Name_Buf[ch],&BmpData,OSD_GetVoChnFontSize(devid, ch)/*GUI_FONT_PRV*/);
			if(s32Ret != STRINGBMP_ERR_NONE)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Get_Time_String failed,ret=0x%x!\n", __LINE__, s32Ret);
				return s32Ret;
			}
			s32Ret = Prv_OSD_Set_Ch(devid,ch,&BmpData,tde_s32Handle);
			if(s32Ret < 0)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Ch failed,ret=0x%x!\n", __LINE__, s32Ret);
				OSD_Free_String(&BmpData);	
				return s32Ret;
			}
			OSD_Free_String(&BmpData);
			if(s32Ret != OSD_NOT_IN_MODE)
				flag |=1;
		}
		else if(OSD_Name_Type[ch] > 0 && OSD_Name_Type[ch] <= LINKAGE_MAX_GROUPNUM)
		{
			int groupNo = OSD_Name_Type[ch]-1;
			s32Ret = OSD_Get_String(OSD_GroupName_Buf[groupNo],&BmpData,OSD_GetVoChnFontSize(devid, ch));
			if(s32Ret != STRINGBMP_ERR_NONE)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Get_Time_String failed,ret=0x%x!\n", __LINE__, s32Ret);
				return s32Ret;
			}
			s32Ret = Prv_OSD_Set_Ch(devid,ch,&BmpData,tde_s32Handle);
			if(s32Ret < 0)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Ch failed,ret=0x%x!\n", __LINE__, s32Ret);
				OSD_Free_String(&BmpData);	
				return s32Ret;
			}
			OSD_Free_String(&BmpData);
			if(s32Ret != OSD_NOT_IN_MODE)
				flag |=1;
		}
			
		}
		s32Ret = Prv_Disp_Pic(devid,ch,tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Ch failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
		if(s32Ret != OSD_NOT_IN_MODE)
			flag |=1;
		//关闭任务
		
	}
	return s32Ret;
#else

	HI_S32 s32Ret=0;
	STRING_BMP_ST BmpData={0},*pBmp;
	TDE_HANDLE tde_s32Handle=-1;
	int ch=0,idx=0;//,w=0,h=0;
	int flag=0;
	unsigned char vo_dev = devid;
	
	//TDE2_RECT_S stSrcRect;
	//TDE2_RECT_S stDstRect;
	
#ifdef 	OPEN_PRV_OSD	
	if(!g_fb_flag)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Disp_OSD failed: dev:%d\n", __LINE__, devid);
		return OSD_INIT_ERR;
	}
	
	if (Prv_fd[devid] <= 0)
	{
		printf(TEXT_COLOR_YELLOW("devid=%d not opened!\n"), devid);
		return -1;
	}
#if defined(SN9234H1)
	if(devid == SPOT_VO_DEV  || devid == AD)
#else
	if(devid > DHD0)
#endif		
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (devid >= PRV_VO_DEV_NUM)
	{
		RET_FAILURE("bad parameter: VoDev!!!");
	}
#ifdef SECOND_DEV
#if defined(SN9234H1)
	if(devid == AD)
	{
		vo_dev = SD;
	}
#elif defined(Hi3531)
	if(devid == DSD0)
	{
		vo_dev = DSD1;
	}
#endif
#endif		
	//刷掉所有的TDE
	s32Ret = Prv_OSD_Ctl_Off(devid, 0, OSD_CLEAR_LAYER, NULL, OSD_CLEAR);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Ctl_Off failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
	
	
	//重新刷新时间
	/*stSrcRect.s32Xpos = 0;
	stSrcRect.s32Ypos = 0;
	stSrcRect.u32Width = OSD_Rect[ch][OSD_TIME_LAYER].u32Width;
	stSrcRect.u32Height = OSD_Rect[ch][OSD_TIME_LAYER].u32Height;
	w = preview_cur_param[devid].w;
	h = preview_cur_param[devid].h;
	stDstRect.s32Xpos= (OSD_PRV_Time_Rect.s32Xpos *w)/SCREEN_DEF_WIDTH;
	stDstRect.s32Ypos= (OSD_PRV_Time_Rect.s32Ypos *h)/SCREEN_DEF_HEIGHT;
	stDstRect.u32Width= (OSD_PRV_Time_Rect.u32Width*w)/SCREEN_DEF_WIDTH;
	stDstRect.u32Height= (OSD_PRV_Time_Rect.u32Height*h)/SCREEN_DEF_HEIGHT;
	
	 s32Ret = HI_TDE2_QuickResize(tde_s32Handle,&g_stImgSur[devid][OSD_TIME_LAYER],&stSrcRect,&g_stScreen[devid],&stDstRect);
	 if(s32Ret != STRINGBMP_ERR_NONE)
	 {
	 TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,HI_TDE2_QuickResize failed,ret=0x%x!\n", __LINE__, s32Ret);
	 HI_TDE2_CancelJob(tde_s32Handle);
	 return s32Ret;
}*/

	//s32Ret = OSD_Get_String(OSD_Time_Buf,&BmpData,OSD_GetVoChnFontSize(devid, -1)/*GUI_FONT_PRV*/);
	/*if(s32Ret != STRINGBMP_ERR_NONE)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d, OSD_Set_Time OSD_Get_String faild 0x%x!\n",__LINE__,s32Ret);
		HI_TDE2_CancelJob(tde_s32Handle);
		return s32Ret;
	}*/
	//时间OSD关闭时，不更新时间
	if(!(OSD_off_flag[0][0] & OSD_TIME_OFF))				
	{
		//打开任务
		s32Ret = Prv_Open_Task(&tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Open_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
#if defined(SN9234H1)
		if(devid==HD)
#else
		if(devid==DHD0)
#endif			
		{//vga画面使用D1分辨率下的时间
			if(TimeOsd_size>=0&&TimeOsd_size<OSD_TIME_RES)
			{
				pBmp = &time_str_buf.time_Bmp_param[TimeOsd_size];
			}
			else
			{
				pBmp = &time_str_buf.time_Bmp_param[0];
			}
		}
		else
		{
			pBmp = &time_str_buf.time_Bmp_param[1];
		}
		s32Ret = OSD_Create_Tde_Surface(devid,pBmp);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Create_Tde_Surface failed,ret=0x%x!\n", __LINE__, s32Ret);
			//OSD_Free_String(&BmpData);	
			HI_TDE2_CancelJob(tde_s32Handle);
			return s32Ret;
		}
		{
			s32Ret = Prv_OSD_Set_Time(devid,ch,tde_s32Handle);
			if(s32Ret < 0)
			{
				TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d,Prv_OSD_Set_Time faild 0x%x!\n", __LINE__,s32Ret);
				//OSD_Free_String(&BmpData);	
				HI_TDE2_CancelJob(tde_s32Handle);
				return s32Ret;
			}
		}
		//OSD_Free_String(&BmpData);	
		//关闭任务
		if(s32Ret == OSD_NOT_IN_MODE)
		{
			HI_TDE2_CancelJob(tde_s32Handle);
		}
		else
		{
			s32Ret = Prv_Close_Task(&tde_s32Handle);
			if(s32Ret < 0)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
				return s32Ret;
			}
		}
	}
	for(idx =0;idx<preview_cur_param[vo_dev].ch_num;idx++)
	{
		ch = preview_cur_param[vo_dev].ch_order[idx];
		if (ch >= DEV_CHANNEL_NUM || ch < 0)
		{
			continue;
		}
		//打开任务
		s32Ret = Prv_Open_Task(&tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Open_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
		//清除标志位
		flag = 0;
		if(!(OSD_off_flag[devid][ch] & OSD_NAME_OFF))
		{
			//重新刷新通道名称
			if(OSD_Name_Type[ch]==0)
			{
				s32Ret = OSD_Get_String(OSD_Name_Buf[ch],&BmpData,OSD_GetVoChnFontSize(devid, ch)/*GUI_FONT_PRV*/);
				if(s32Ret != STRINGBMP_ERR_NONE)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Get_Time_String failed,ret=0x%x!\n", __LINE__, s32Ret);
					HI_TDE2_CancelJob(tde_s32Handle);
					return s32Ret;
				}
				s32Ret = Prv_OSD_Set_Ch(devid,ch,&BmpData,tde_s32Handle);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Ch failed,ret=0x%x!\n", __LINE__, s32Ret);
					OSD_Free_String(&BmpData);	
					HI_TDE2_CancelJob(tde_s32Handle);
					return s32Ret;
				}
				OSD_Free_String(&BmpData);
				if(s32Ret != OSD_NOT_IN_MODE)
					flag |=1;
			}
			else if(OSD_Name_Type[ch] > 0 && OSD_Name_Type[ch] <= LINKAGE_MAX_GROUPNUM)
			{
				int groupNo = OSD_Name_Type[ch]-1;
				s32Ret = OSD_Get_String(OSD_GroupName_Buf[groupNo],&BmpData,OSD_GetVoChnFontSize(devid, ch)/*GUI_FONT_PRV*/);
				if(s32Ret != STRINGBMP_ERR_NONE)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Get_Time_String failed,ret=0x%x!\n", __LINE__, s32Ret);
					HI_TDE2_CancelJob(tde_s32Handle);
					return s32Ret;
				}
				s32Ret = Prv_OSD_Set_Ch(devid,ch,&BmpData,tde_s32Handle);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Ch failed,ret=0x%x!\n", __LINE__, s32Ret);
					OSD_Free_String(&BmpData);	
					HI_TDE2_CancelJob(tde_s32Handle);
					return s32Ret;
				}
				OSD_Free_String(&BmpData);
				if(s32Ret != OSD_NOT_IN_MODE)
					flag |=1;
			}
		}
		s32Ret = Prv_Disp_Pic(devid,ch,tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_Ch failed,ret=0x%x!\n", __LINE__, s32Ret);
			HI_TDE2_CancelJob(tde_s32Handle);
			return s32Ret;
		}
		if(s32Ret != OSD_NOT_IN_MODE)
			flag |=1;
		//关闭任务
		if(!flag)
		{
			HI_TDE2_CancelJob(tde_s32Handle);
			continue;
		}
		else
		{
			s32Ret = Prv_Close_Task(&tde_s32Handle);
			if(s32Ret < 0)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
				HI_TDE2_CancelJob(tde_s32Handle);//return s32Ret;///
			}
		}
	}
#endif	
	
	return s32Ret;
#endif
}
//****************************************************
//预览OSD层关闭接口函数
//*****************************************************

int Prv_OSD_Close_fb(unsigned char devid)
{
	int i=0;
#ifdef 	OPEN_PRV_OSD		
	for(i=0;i<DEV_CHANNEL_NUM;i++)
	{
		OSD_off_flag[devid][i] |= OSD_FB_OFF;
	}	
	if(Prv_fd[devid] != -1)
	{	
#if defined(USE_UI_OSD)
			Prv_fd[devid] = -1;
#else
		if(-1==munmap(fb_mmap[devid], fb_memlen[devid]))
		{
			fprintf(stderr, TEXT_COLOR_RED("munmap fail: %s, devid=%d, virtaddr=%#x, memsize=%d, fd=%d\n"), strerror(errno), devid, (unsigned int)fb_mmap[devid], fb_memlen[devid], Prv_fd[devid]);
		}
		close(Prv_fd[devid]);
		Prv_fd[devid] = -1;
#endif
	}
#endif		
	return 0;
	
}
//****************************************************
//预览OSD层打开接口函数
//*****************************************************

int Prv_OSD_Open_fb(unsigned char devid)
{
	int i=0;
	HI_S32 s32Ret=-1;
#ifdef 	OPEN_PRV_OSD	
	s32Ret = Prv_OSD_Init(devid);
	if(s32Ret != PARAM_OK)
	{
        TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Open_fb error, dev:%d \n",__LINE__, devid);
		Prv_fd[devid] = -1;
		return -1;
	}
	for(i=0;i<DEV_CHANNEL_NUM;i++)
	{
		OSD_off_flag[devid][i] &= ~(OSD_FB_OFF);
	}
#endif	
	
	return 0;
}
//****************************************************
//TDE设备打开接口函数
//*****************************************************

int Prv_Open_Dev(void)
{
	//打开TDE设备
	HI_TDE2_Open();
	return 0;
}
//****************************************************
//TDE设备关闭接口函数
//*****************************************************

int Prv_Close_Dev(void)
{
	int i=0;
	//关闭TDE设备
	for(i=0;i<VO_MAX_DEV_NUM;i++)
	{
#if defined(SN9234H1)
		if(i == SPOT_VO_DEV  || i == AD)
#else
		if(i > DHD0)
#endif			
		{
			continue;
		}
		Prv_OSD_Close_fb(i);
	}
	HI_TDE2_Close();
	return 0;
}

int OSD_Set_NameType( int *pNameType)
{
	
	SN_MEMCPY(OSD_Name_Type, sizeof(OSD_Name_Type), pNameType, sizeof(OSD_Name_Type), sizeof(OSD_Name_Type));
	return 0;
}

int OSD_Compare_NameType( int *pNameType)
{
	int i = 0;
	for(i=0;i<DEV_CHANNEL_NUM;i++)
	{
		if(pNameType[i] != OSD_Name_Type[i])
		{
			return 1;
		}
	}
	return 0;
}

//****************************************************
//获取当前预览模式等属性接口函数
//*****************************************************

int OSD_Get_Preview_param(unsigned char devid,int w,int h,unsigned char ch_num,enum PreviewMode_enum prv_mode,unsigned char *pOrder)
{
	if(!pOrder)	return -1;
	preview_cur_param[devid].devid = devid;
	preview_cur_param[devid].w = w;
	preview_cur_param[devid].h = h;
	preview_cur_param[devid].ch_num = ch_num;
	preview_cur_param[devid].prv_mode = prv_mode;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "##################devid = %d,preview_cur_param[devid].w=%d,preview_cur_param[devid].h = %d\n",devid,preview_cur_param[devid].w,preview_cur_param[devid].h);
	SN_MEMCPY(preview_cur_param[devid].ch_order, sizeof(preview_cur_param[devid].ch_order), pOrder, ch_num, ch_num);
	
	return 0;
}

int PRV_Get_CurMode_VoChn(enum PreviewMode_enum *pePreviewMode,int *ch_num,unsigned char *pOrder)
{
#if defined(SN9234H1)
	*pePreviewMode = preview_cur_param[HD].prv_mode;
	*ch_num = preview_cur_param[HD].ch_num;
	SN_MEMCPY(pOrder, SEVENINDEX, preview_cur_param[HD].ch_order, preview_cur_param[HD].ch_num, preview_cur_param[HD].ch_num);
#else
	*pePreviewMode = preview_cur_param[DHD0].prv_mode;
	*ch_num = preview_cur_param[DHD0].ch_num;
	SN_MEMCPY(pOrder, SEVENINDEX, preview_cur_param[DHD0].ch_order, preview_cur_param[DHD0].ch_num, preview_cur_param[DHD0].ch_num);
#endif
	return 0;
}

/*************************************************
Function: //OSD_Set_Rec_Range_NP
Description: //根据当前的N\P制式，配置当前的录像OSD
Calls: 
Called By: //
Input: // np_flag :N\P制式，0表示P制式，1表示N制式
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
int OSD_Set_Rec_Range_NP(unsigned char np_flag)
{
	int i=0,h=0,j=0,flag=0;

	for(i=0;i<g_Max_Vo_Num;i++)
	{
		flag = 0;
		for(j=0;j<REC_OSD_GROUP;j++)
		{
			switch(g_rec_srceen_h[j][i])
			{
				case SCREEN_4CIF_HEIGHT:
				case SCREEN_4CIF_N_HEIGHT:	
					h = (0 == np_flag)?SCREEN_4CIF_HEIGHT: SCREEN_4CIF_N_HEIGHT;
					break;
				case SCREEN_CIF_HEIGHT:
				case SCREEN_CIF_N_HEIGHT:
					h = (0 == np_flag)?SCREEN_CIF_HEIGHT: SCREEN_CIF_N_HEIGHT;
					break;
				case SCREEN_QCIF_HEIGHT:
				case SCREEN_QCIF_N_HEIGHT:	
					h = (0 == np_flag)?SCREEN_QCIF_HEIGHT: SCREEN_QCIF_N_HEIGHT;
					break;
				default:
					h = (0 == np_flag)?SCREEN_CIF_HEIGHT: SCREEN_CIF_N_HEIGHT;
					break;
			}
			if(g_rec_srceen_h[j][i] != h)
			{//如果高度不一样，修改当前录像和遮盖的区域
				g_rec_srceen_h[j][i] = h;
				TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,OSD_Set_Rec_Range_NP g_rec_srceen_h=0x%x!  i=%d\n", __LINE__, h,i);
				if(g_fb_flag)
				{
					pthread_mutex_lock(&mutex_setbmp);
					time_str_buf.Change_flag[j][i]++;
					pthread_mutex_unlock(&mutex_setbmp);
					//Rec_SetOSD_xy(j,i,OSD_Rect[i][OSD_NAME_LAYER].s32Xpos,OSD_Rect[i][OSD_NAME_LAYER].s32Ypos,OSD_Rect[i][OSD_TIME_LAYER].s32Xpos,OSD_Rect[i][OSD_TIME_LAYER].s32Ypos);
				}
				flag = 1;
			}
		}	
		if(flag)
		{
#if defined(SN9234H1)
		OSD_Mask_NPupdate(i);
#endif
		}
	}
	return 0;
}

/*************************************************
Function: //OSD_Get_Rec_Range_Ch
Description: //单通道获取当前编码分辨率接口函数
Calls: 
Called By: //
Input: // chn :通道号
		w : 宽度
		h : 高度
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
int OSD_Get_Rec_Range_Ch(unsigned char rec_group,int chn,int w,int h)
{
//	int s32Ret=0;
	//unsigned char idx=0;
	if(chn <0 || rec_group >= REC_OSD_GROUP)
	{
        printf(TEXT_COLOR_RED("bad parameter, chn:%d, rec_group:%d\n"), chn, rec_group);
        return -1;
	}
    if(chn >= PRV_CHAN_NUM)
	{
        if( ( h != s_slaveVoStat.f_rec_srceen_h[rec_group][chn%PRV_CHAN_NUM]) 
            || ( w != s_slaveVoStat.f_rec_srceen_w[rec_group][chn%PRV_CHAN_NUM]) )
        {
            s_slaveVoStat.f_rec_srceen_h[rec_group][chn%PRV_CHAN_NUM] = h;
            s_slaveVoStat.f_rec_srceen_w[rec_group][chn%PRV_CHAN_NUM] = w;
            Rec_OSD_Init_Ch(chn);
            PRV_Send_RecOsdName2Slave(chn%PRV_CHAN_NUM);
        }
        return 0;
	}
	if(( h == g_rec_srceen_h[rec_group][chn]) && ( w == g_rec_srceen_w[rec_group][chn]))
	{
		return 0;
	}
	else
	{
		g_rec_srceen_h[rec_group][chn] = h;
		g_rec_srceen_w[rec_group][chn] = w;
	}
	if(g_fb_flag)
	{
		pthread_mutex_lock(&mutex_setbmp);
		time_str_buf.Change_flag[rec_group][chn]++;
		pthread_mutex_unlock(&mutex_setbmp);
		//Rec_ResetOSD_BmpInfo(rec_group,chn);
	}
	TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,OSD_Get_Rec_Range_Ch SUC,h=%d w = %d\n", __LINE__, g_rec_srceen_h[rec_group][chn],g_rec_srceen_w[rec_group][chn]);
	return 0;
}
//****************************************************
//获取当前OSD位置函数
//*****************************************************

//目前OSD给固定D1下的模糊区域，所有分辨率都按照D1的显示
static int OSD_Get_Rec_Area_MMI(unsigned char ch,int n_x,int n_y,unsigned char *pStr,int t_x,int t_y,PRV_OSD_AREA_ST *pArea)
{
	//unsigned char icon_idx=0;
	int name_x=n_x,name_y=n_y,time_x=t_x,time_y=t_y;//,len=0,ret =0;
	//PRM_OSD_CFG_CHAN osd_xy;

	//OSD_Time_Str_icon_idx(REC_MAINSTREAM,ch,&icon_idx);
	//通道名称位置
	if(pStr == NULL)
	{
		pArea->ChnNameRect.width = 0;
	}
	else
	{
		pArea->ChnNameRect.width = strlen((char*)pStr) * 12;//name_icon_param[REC_MAINSTREAM][ch].stBitmap.u32Width*SCREEN_DEF_WIDTH/(g_rec_srceen_w[REC_MAINSTREAM][ch]);
	}
	pArea->ChnNameRect.height = OSD_TIME_HEIGTH;//name_icon_param[REC_MAINSTREAM][ch].stBitmap.u32Height*SCREEN_DEF_HEIGHT/(g_rec_srceen_h[REC_MAINSTREAM][ch]);
	
	pArea->TimeRect.width = OSD_TIME_WIDTH;//time_str_buf.time_icon_param[REC_MAINSTREAM][ch].stBitmap.u32Width*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][ch];
	pArea->TimeRect.height = OSD_TIME_HEIGTH;//time_str_buf.time_icon_param[REC_MAINSTREAM][ch].stBitmap.u32Height*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][ch];
	
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############11111 time_x =%d,time_y = %d,pArea->TimeRect.width = %d,pArea->TimeRect.height = %d################\n",time_x,time_y,pArea->TimeRect.width,pArea->TimeRect.height);
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############11111 name_x =%d,name_y = %d,pArea->ChnNameRect.width = %d,pArea->ChnNameRect.height = %d################\n",name_x,name_y,pArea->ChnNameRect.width,pArea->ChnNameRect.height);
		//计算调整后的OSD 区域大小，并返回
	OSD_Cmp_nameAtime(&name_x,&name_y,pArea->ChnNameRect.width,pArea->ChnNameRect.height,&time_x,&time_y,pArea->TimeRect.width ,pArea->TimeRect.height );	
	
	pArea->TimeRect.left = time_x;//*SCREEN_DEF_WIDTH/SCREEN_4CIF_WIDTH;
	pArea->TimeRect.top = time_y;//*SCREEN_DEF_HEIGHT/SCREEN_4CIF_HEIGHT;
	pArea->ChnNameRect.left = name_x;//*SCREEN_DEF_WIDTH/SCREEN_4CIF_WIDTH;
	pArea->ChnNameRect.top = name_y;//*SCREEN_DEF_HEIGHT/SCREEN_4CIF_HEIGHT;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############222 name_x =%d,name_y = %d,time_x = %d,time_y = %d################\n",name_x,name_y,time_x,time_y);
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############2222 pArea->ChnNameRect.left =%d,pArea->ChnNameRect.top = %d,pArea->ChnNameRect.width = %d,pArea->ChnNameRect.height = %d################\n",pArea->ChnNameRect.left,pArea->ChnNameRect.top,pArea->ChnNameRect.width,pArea->ChnNameRect.height);
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############2222 pArea->TimeRect.left =%d,pArea->TimeRect.top = %d,pArea->TimeRect.width = %d,pArea->TimeRect.height = %d################\n",pArea->TimeRect.left,pArea->TimeRect.top,pArea->TimeRect.width,pArea->TimeRect.height);

	//通道名称1位置
	pArea->PictureRect.left = 0;//OSD_Rect[HD][ch][OSD_ALARM_LAYER].s32Xpos;//*SCREEN_DEF_WIDTH/SCREEN_REC_WIDTH;
	pArea->PictureRect.top = 0;//OSD_Rect[HD][ch][OSD_ALARM_LAYER].s32Ypos;//*SCREEN_DEF_HEIGHT/SCREEN_REC_HEIGHT;
	pArea->PictureRect.width = 0;//2*ICON_WIDTH*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][ch] ;
	pArea->PictureRect.height = 0;//ICON_HEIGTH*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][ch];
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############pArea->PictureRect.width = %d,pArea->PictureRect.height = %d################\n",pArea->PictureRect.width,pArea->PictureRect.height);

	//other
	pArea->OtherRect.left = 0;
	pArea->OtherRect.top = 0;
	pArea->OtherRect.width = 0;
	pArea->OtherRect.height = 0;
	return  0;
}
int PRV_GetChnRects_MMI(unsigned char ch, int n_x,int n_y,unsigned char *pStr,int t_x,int t_y,PRV_OSD_AREA_ST *pArea)
{
	if (NULL == pArea || ch>=DEV_CHANNEL_NUM)
	{
		TRACE(SCREEN_REC_HEIGHT, MOD_PRV, TEXT_COLOR_RED("%s fail: ch=%d, pArea=%#x\n"), __FUNCTION__, ch, pArea);
		return -1;
	}
	return OSD_Get_Rec_Area_MMI(ch, n_x,n_y,pStr,t_x,t_y,pArea);
}

static int OSD_Get_Rec_Area(unsigned char ch,PRV_OSD_AREA_ST *pArea)
{
#if    0
	//unsigned char icon_idx=0;
	int name_x=0,name_y=0,time_x=0,time_y=0;

	//OSD_Time_Str_icon_idx(REC_MAINSTREAM,ch,&icon_idx);
	//通道名称位置
	name_x = OSD_Rect[DHD0][ch][OSD_NAME_LAYER].s32Xpos;//*SCREEN_DEF_WIDTH/SCREEN_REC_WIDTH;
	name_y = OSD_Rect[DHD0][ch][OSD_NAME_LAYER].s32Ypos;//*SCREEN_DEF_HEIGHT/SCREEN_REC_HEIGHT;
	pArea->ChnNameRect.width = name_icon_param[REC_MAINSTREAM][ch].stBitmap.u32Width*SCREEN_DEF_WIDTH/(g_rec_srceen_w[REC_MAINSTREAM][ch]);
	pArea->ChnNameRect.height = name_icon_param[REC_MAINSTREAM][ch].stBitmap.u32Height*SCREEN_DEF_HEIGHT/(g_rec_srceen_h[REC_MAINSTREAM][ch]);

	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############11111 name_x =%d,name_y = %d,pArea->ChnNameRect.width = %d,pArea->ChnNameRect.height = %d################\n",name_x,name_y,pArea->ChnNameRect.width,pArea->ChnNameRect.height);
	//pArea->ChnNameRect.width += 16;
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############pArea->ChnNameRect.width = %d,pArea->ChnNameRect.height = %d################\n",pArea->ChnNameRect.width,pArea->ChnNameRect.height);
	//时间位置
	time_x =  OSD_Rect[DHD0][ch][OSD_TIME_LAYER].s32Xpos;//*SCREEN_DEF_WIDTH/SCREEN_REC_WIDTH;
	time_y = OSD_Rect[DHD0][ch][OSD_TIME_LAYER].s32Ypos;//*SCREEN_DEF_HEIGHT/SCREEN_REC_HEIGHT;
	pArea->TimeRect.width = time_str_buf.time_icon_param[REC_MAINSTREAM][ch].stBitmap.u32Width*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][ch];
	pArea->TimeRect.height = time_str_buf.time_icon_param[REC_MAINSTREAM][ch].stBitmap.u32Height*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][ch];
	//pArea->TimeRect.width += 16;
	
	
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############11111 time_x =%d,time_y = %d,pArea->TimeRect.width = %d,pArea->TimeRect.height = %d################\n",time_x,time_y,pArea->TimeRect.width,pArea->TimeRect.height);
	//计算调整后的OSD 区域大小，并返回
	Rec_OSD_Cmp_nameAtime(REC_MAINSTREAM,ch,&name_x,&name_y,name_icon_param[REC_MAINSTREAM][ch].stBitmap.u32Width,name_icon_param[REC_MAINSTREAM][ch].stBitmap.u32Height,
							&time_x,&time_y,time_str_buf.time_icon_param[REC_MAINSTREAM][ch].stBitmap.u32Width,time_str_buf.time_icon_param[REC_MAINSTREAM][ch].stBitmap.u32Height);	
	pArea->TimeRect.left = time_x*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][ch];
	pArea->TimeRect.top = time_y*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][ch];
	pArea->ChnNameRect.left = name_x*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][ch];
	pArea->ChnNameRect.top = name_y*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][ch];
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############11111 name_x =%d,name_y = %d,time_x = %d,time_y = %d################\n",name_x,name_y,time_x,time_y);
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############11111 pArea->ChnNameRect.left =%d,pArea->ChnNameRect.top = %d,pArea->ChnNameRect.width = %d,pArea->ChnNameRect.height = %d################\n",pArea->ChnNameRect.left,pArea->ChnNameRect.top,pArea->ChnNameRect.width,pArea->ChnNameRect.height);
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############11111 pArea->TimeRect.left =%d,pArea->TimeRect.top = %d,pArea->TimeRect.width = %d,pArea->TimeRect.height = %d################\n",pArea->TimeRect.left,pArea->TimeRect.top,pArea->TimeRect.width,pArea->TimeRect.height);

	//通道名称1位置
	pArea->PictureRect.left = OSD_Rect[DHD0][ch][OSD_ALARM_LAYER].s32Xpos;//*SCREEN_DEF_WIDTH/SCREEN_REC_WIDTH;
	pArea->PictureRect.top = OSD_Rect[DHD0][ch][OSD_ALARM_LAYER].s32Ypos;//*SCREEN_DEF_HEIGHT/SCREEN_REC_HEIGHT;
	pArea->PictureRect.width = 2*ICON_WIDTH*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][ch] ;
	pArea->PictureRect.height = ICON_HEIGTH*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][ch];
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%d############pArea->PictureRect.width = %d,pArea->PictureRect.height = %d################\n\n\n",ch,pArea->PictureRect.width,pArea->PictureRect.height);

	//other
	pArea->OtherRect.left = 0;
	pArea->OtherRect.top = 0;
	pArea->OtherRect.width = 0;
	pArea->OtherRect.height = 0;
#else
    //int name_x=0,name_y=0,time_x=0,time_y=0;
    //int name_w=0,name_h=0,time_w=0,time_h=0;
#if defined(SN9234H1)
    REGION_CTRL_PARAM_U unParam_n,unParam_t;
    unsigned char venc_ch=OSD_GetRecGroupChn(REC_MAINSTREAM,ch);

    bzero(&unParam_n, sizeof(unParam_n));
    bzero(&unParam_t, sizeof(unParam_t));
    CHECK(HI_MPI_VPP_ControlRegion(rec_osd_handle[venc_ch][OSD_NAME_LAYER], REGION_GET_SIGNLE_ATTR, &unParam_n));
    CHECK(HI_MPI_VPP_ControlRegion(rec_osd_handle[venc_ch][OSD_TIME_LAYER], REGION_GET_SIGNLE_ATTR, &unParam_t));

    pArea->ChnNameRect.left = unParam_n.stRegionAttr.unAttr.stOverlay.stRect.s32X*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][ch];
    pArea->ChnNameRect.top = unParam_n.stRegionAttr.unAttr.stOverlay.stRect.s32Y*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][ch];
    pArea->ChnNameRect.width = unParam_n.stRegionAttr.unAttr.stOverlay.stRect.u32Width*SCREEN_DEF_WIDTH/(g_rec_srceen_w[REC_MAINSTREAM][ch]) + 2; /* 加两个像素修正除法运算产生的误差，下同！ */
    pArea->ChnNameRect.height = unParam_n.stRegionAttr.unAttr.stOverlay.stRect.u32Height*SCREEN_DEF_HEIGHT/(g_rec_srceen_h[REC_MAINSTREAM][ch]) + 2;

    pArea->TimeRect.left = unParam_t.stRegionAttr.unAttr.stOverlay.stRect.s32X*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][ch];
    pArea->TimeRect.top = unParam_t.stRegionAttr.unAttr.stOverlay.stRect.s32Y*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][ch];
    pArea->TimeRect.width = unParam_t.stRegionAttr.unAttr.stOverlay.stRect.u32Width*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][ch] + 2;
    pArea->TimeRect.height = unParam_t.stRegionAttr.unAttr.stOverlay.stRect.u32Height*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][ch] + 2;

    printf(TEXT_COLOR_YELLOW("N[%d]:\t%d\t%d\t%d\t%d\n"),ch,
        pArea->ChnNameRect.left,pArea->ChnNameRect.top,pArea->ChnNameRect.width,pArea->ChnNameRect.height);
    printf(TEXT_COLOR_YELLOW("T[%d]:\t%d\t%d\t%d\t%d\n"),ch,
        pArea->TimeRect.left,pArea->TimeRect.top,pArea->TimeRect.width,pArea->TimeRect.height);
    //TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############11111 ch=%d,venc = %d,hdl_type = %d, enc_grp=%d ################\n",ch, venc_ch, unParam_n.stRegionAttr.enType,unParam_n.stRegionAttr.unAttr.stOverlay.VeGroup);
    printf(TEXT_COLOR_PURPLE("N[%d]:\t%d\t%d\t%d\t%d\n"),ch,
        unParam_n.stRegionAttr.unAttr.stOverlay.stRect.s32X,
        unParam_n.stRegionAttr.unAttr.stOverlay.stRect.s32Y,
        unParam_n.stRegionAttr.unAttr.stOverlay.stRect.u32Width,
        unParam_n.stRegionAttr.unAttr.stOverlay.stRect.u32Height);
    printf(TEXT_COLOR_PURPLE("T[%d]:\t%d\t%d\t%d\t%d\n"),ch,
        unParam_t.stRegionAttr.unAttr.stOverlay.stRect.s32X,
        unParam_t.stRegionAttr.unAttr.stOverlay.stRect.s32Y,
        unParam_t.stRegionAttr.unAttr.stOverlay.stRect.u32Width,
        unParam_t.stRegionAttr.unAttr.stOverlay.stRect.u32Height);

#else
    RGN_ATTR_S unParam_n,unParam_t;
    unsigned char venc_ch=OSD_GetRecGroupChn(REC_MAINSTREAM,ch);

    bzero(&unParam_n, sizeof(unParam_n));
    bzero(&unParam_t, sizeof(unParam_t));
    CHECK(HI_MPI_RGN_GetAttr(rec_osd_handle[venc_ch][OSD_NAME_LAYER], &unParam_n));
    CHECK(HI_MPI_RGN_GetAttr(rec_osd_handle[venc_ch][OSD_TIME_LAYER],  &unParam_t));

    //pArea->ChnNameRect.left = unParam_n.unAttr.stOverlay.stRect.s32X*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][ch];
   // pArea->ChnNameRect.top = unParam_n.unAttr.stOverlay.stRect.s32Y*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][ch];
    pArea->ChnNameRect.width = unParam_n.unAttr.stOverlay.stSize.u32Width*SCREEN_DEF_WIDTH/(g_rec_srceen_w[REC_MAINSTREAM][ch]) + 2; /* 加两个像素修正除法运算产生的误差，下同！ */
    pArea->ChnNameRect.height = unParam_n.unAttr.stOverlay.stSize.u32Height*SCREEN_DEF_HEIGHT/(g_rec_srceen_h[REC_MAINSTREAM][ch]) + 2;

    //pArea->TimeRect.left = unParam_t.unAttr.stOverlay.stRect.s32X*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][ch];
   // pArea->TimeRect.top = unParam_t.unAttr.stOverlay.stRect.s32Y*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][ch];
    pArea->TimeRect.width = unParam_t.unAttr.stOverlay.stSize.u32Width*SCREEN_DEF_WIDTH/g_rec_srceen_w[REC_MAINSTREAM][ch] + 2;
    pArea->TimeRect.height = unParam_t.unAttr.stOverlay.stSize.u32Height*SCREEN_DEF_HEIGHT/g_rec_srceen_h[REC_MAINSTREAM][ch] + 2;

    printf(TEXT_COLOR_YELLOW("N[%d]:\t%d\t%d\n"),ch,
        pArea->ChnNameRect.width,pArea->ChnNameRect.height);
    printf(TEXT_COLOR_YELLOW("T[%d]:\t%d\t%d\n"),ch,
        pArea->TimeRect.width,pArea->TimeRect.height);
    //TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############11111 ch=%d,venc = %d,hdl_type = %d, enc_grp=%d ################\n",ch, venc_ch, unParam_n.stRegionAttr.enType,unParam_n.stRegionAttr.unAttr.stOverlay.VeGroup);
    printf(TEXT_COLOR_PURPLE("N[%d]:\t%d\t%d\n"),ch,
        unParam_n.unAttr.stOverlay.stSize.u32Width,
        unParam_n.unAttr.stOverlay.stSize.u32Height);
    printf(TEXT_COLOR_PURPLE("T[%d]:\t%d\t%d\n"),ch,
        unParam_t.unAttr.stOverlay.stSize.u32Width,
        unParam_t.unAttr.stOverlay.stSize.u32Height);
#endif
    //通道名称1位置
    pArea->PictureRect.left = 0;
    pArea->PictureRect.top = 0;
    pArea->PictureRect.width = 0;
    pArea->PictureRect.height = 0;

    //other
    pArea->OtherRect.left = 0;
    pArea->OtherRect.top = 0;
    pArea->OtherRect.width = 0;
    pArea->OtherRect.height = 0;
#endif
	return  0;
}



int PRV_GetChnRects(unsigned char ch, PRV_OSD_AREA_ST *pArea)
{
	if (NULL == pArea || ch>=PRV_CHAN_NUM)
	{
		TRACE(SCREEN_REC_HEIGHT, MOD_PRV, TEXT_COLOR_RED("%s fail: ch=%d, pArea=%#x\n"), __FUNCTION__, ch, pArea);
		return -1;
	}
	return OSD_Get_Rec_Area(ch,pArea);
}
//****************************************************
//OSD部分初始化函数
//*****************************************************
#if 1 /*2010-9-19 GUI双屏：AD使用G1层！*/
unsigned int s_g1colorkey = PRVDISPLAY_COLORKEY;
unsigned int s_gui_alpha = 0xff;

int OSD_G1_SetColorKey(unsigned int colorkey)
{
	if (g1fd == -1)
	{
		s_g1colorkey = colorkey;
		printf(TEXT_COLOR_RED("G1 not open now!, set colorkey will take effect later!"));
	}
	else/*设置图形层colorkey*/
	{
		HIFB_COLORKEY_S stColorKey = {0};
		stColorKey.bKeyEnable = HI_TRUE;
#if defined(SN9234H1)		
		stColorKey.bMaskEnable = HI_FALSE;
#endif		
		stColorKey.u32Key = colorkey;
		CHECK(ioctl(g1fd, FBIOPUT_COLORKEY_HIFB, &stColorKey));
		printf(TEXT_COLOR_PURPLE("G1 color key: %#x\n"), colorkey);
	}
	RET_SUCCESS("");
}
int PRV_OSD_SetGuiAlpha(unsigned char alpha)
{
#if defined(Hi3531)||defined(Hi3535)
	return 0;
#endif
	HIFB_ALPHA_S stAlpha;
	
	s_gui_alpha = alpha;
	
	stAlpha.bAlphaChannel = 0;
	stAlpha.bAlphaEnable = 1;
	stAlpha.u8Alpha0 = s_gui_alpha;
	stAlpha.u8Alpha1 = s_gui_alpha;
	stAlpha.u8GlobalAlpha = s_gui_alpha;
	stAlpha.u8Reserved = 0;
	
	if (g1fd == -1)
	{
		printf(TEXT_COLOR_YELLOW("G1 not open now!, set alpha will take effect later!"));
	}
	else/*设置图形层colorkey*/
	{
		if (ioctl(g1fd, FBIOPUT_ALPHA_HIFB, &stAlpha) < 0)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV, TEXT_COLOR_RED("set screen framebuffer alpha failed! fd=%d"), g1fd);
		}
		printf(TEXT_COLOR_PURPLE("G1 alpha: %#x\n"), s_gui_alpha);
	}
	
	if (g_guifd == -1)
	{
		printf(TEXT_COLOR_YELLOW("GUI not open now!, set alpha will take effect later!"));
	}
	else/*设置图形层colorkey*/
	{
		if (ioctl(g_guifd, FBIOPUT_ALPHA_HIFB, &stAlpha) < 0)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV, TEXT_COLOR_RED("set screen framebuffer alpha failed! fd=%d"), g_guifd);
		}
		printf(TEXT_COLOR_PURPLE("Gui alpha: %#x\n"), s_gui_alpha);
	}
	RET_SUCCESS("");
}
int PRV_OSD_SetGuiShow(unsigned int flag)
{
	HI_BOOL bShow;
	
	if (flag)
	{
		bShow = HI_TRUE;
	}
	else
	{
		bShow = HI_FALSE;
	}
	
	if (g1fd > 0)
	{
		if(0 > ioctl(g1fd, FBIOPUT_SHOW_HIFB, &bShow))
		{
			printf(TEXT_COLOR_RED("Show G1 Layer failed!\n"));
			return -1;
		}
	}
	if (g_guifd > 0)
	{
		if(0 > ioctl(g_guifd, FBIOPUT_SHOW_HIFB, &bShow))
		{
			printf(TEXT_COLOR_RED("Show GUI Layer failed!\n"));
			return -1;
		}
	}
	RET_SUCCESS("");
}

int OSD_G1_close(void)
{
	if (g1fd != -1)
	{
		if(0!=close(g1fd))
		{
			perror(TEXT_COLOR_RED("close G1 layer fail!"));
			return -1;
		}
		g1fd = -1;
	}
	else
	{
		printf(TEXT_COLOR_RED("G1 already closed! fd=%d\n"), g1fd);
	}
	return 0;
}
int OSD_G1_open(void)
{
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	HI_U32 w = 0;
	HI_U32 h= 0;
#if defined(SN9234H1)
	char *glayer1 = "/dev/fb4";
#else
	char *glayer1 = "/dev/fb5";
#endif

	if (g1fd > 0)
	{
		printf(TEXT_COLOR_RED("G1 already opened! fd=%d\n"), g1fd);
		return 0;
	}
	
#if 1
	/* 1. open framebuffer device overlay 0 */
	g1fd = open(glayer1, O_RDWR, 0);
	if(g1fd < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"open %s failed!\n", glayer1);
		return -1;
	}
#endif
#if 1
	/* 2. set the screen original position */
	PRV_RECT_S stDspRect;
	HIFB_POINT_S stPoint = {0,0};
	//PRV_GetVoDspRect((HD==s_VoDevCtrlDflt)?s_VoSecondDev:HD, &stDspRect);
#if defined(SN9234H1)
	PRV_GetVoDspRect(HD, &stDspRect);
	stPoint.u32PosX = stDspRect.s32X;
	stPoint.u32PosY = stDspRect.s32Y;
	printf(TEXT_COLOR_PURPLE("set screen original show position (%d %d)ok!\n"),stPoint.u32PosX,stPoint.u32PosY);

#else	
	PRV_GetVoDspRect(DHD0, &stDspRect);
	stPoint.s32XPos= stDspRect.s32X;
	stPoint.s32YPos= stDspRect.s32Y;
	printf(TEXT_COLOR_PURPLE("set screen original show position (%d %d)ok!\n"),stPoint.s32XPos,stPoint.s32YPos);

#endif	
	//printf(TEXT_COLOR_YELLOW("g1 original position : x=%d, y=%d\n"), stPoint.u32PosX, stPoint.u32PosY);
	if (ioctl(g1fd, FBIOPUT_SCREEN_ORIGIN_HIFB, &stPoint) < 0)
	{
		perror("set screen original show position failed!");
		close(g1fd);
		return -1;
	}
#endif
#if 0
	/* 3.set alpha: not transparent */
	HIFB_ALPHA_S stAlpha = {0};
	stAlpha.bAlphaEnable = HI_TRUE;
	stAlpha.u8Alpha0 = 1;
	stAlpha.u8Alpha1 = 255;
	if (ioctl(g1fd, FBIOPUT_ALPHA_HIFB,  &stAlpha) < 0)
	{
		perror("Set alpha failed!");
		close(g1fd);
		return -1;
	}
#endif
#if 1
	//extern unsigned int Get_Fb_ColorKey();
	//s_g1colorkey = Get_Fb_ColorKey();
	/*设置图形层colorkey*/
	{
		HIFB_COLORKEY_S stColorKey;
		stColorKey.bKeyEnable = HI_TRUE;
#if defined(SN9234H1)		
		stColorKey.bMaskEnable = HI_FALSE;
#endif	
		stColorKey.u32Key = s_g1colorkey;//0x001F;/*蓝色*/
		CHECK(ioctl(g1fd, FBIOPUT_COLORKEY_HIFB, &stColorKey));
	}
#endif
#if 1
	/* 4. get the variable screen info */
	if (ioctl(g1fd, FBIOGET_VSCREENINFO, &var) < 0)
	{
		perror("Get variable screen info failed!");
		close(g1fd);
		return -1;
	}
	w = var.xres;
	h = var.yres;
	printf(TEXT_COLOR_PURPLE("G1 layer: w=%d, h=%d\n"), w, h);
#endif
#if 1
	/* 5. modify the variable screen info
	the pixel format: ARGB1555
	note: here use pingpang buff mechanism
	*/
	static struct fb_bitfield g_r16 = {10, 5, 0};
	static struct fb_bitfield g_g16 = {5, 5, 0};
	static struct fb_bitfield g_b16 = {0, 5, 0};
	static struct fb_bitfield g_a16 = {15, 1, 0};
	
	w = stDspRect.u32Width;
	h = stDspRect.u32Height;
	printf(TEXT_COLOR_PURPLE("G1 layer: w=%d, h=%d\n"), w, h);
	
	var.xres = w;
	var.yres = h;
	var.xres_virtual = var.xres;
	var.yres_virtual = var.yres;// * 2;
	
	var.transp= g_a16;
	var.red = g_r16;
	var.green = g_g16;
	var.blue = g_b16;
	var.bits_per_pixel = 16;
	var.activate = FB_ACTIVATE_FORCE;
#endif
#if 1
	/* 6. set the variable screeninfo */
	if (ioctl(g1fd, FBIOPUT_VSCREENINFO, &var) < 0)
	{
		perror("Put variable screen info failed!");
		printf(TEXT_COLOR_RED("var.xres=%d\nvar.yres=%d\nvar.xres_virtual=%d\nvar.yres_virtual=%d\n,var.bit_per_pixel=%d\n,var.activate=%d(FB_ACTIVATE_FORCE=%d,FB_ACTIVATE_NOW=%d)"),
			var.xres,var.yres,var.xres_virtual,var.yres_virtual,var.bits_per_pixel,var.activate,FB_ACTIVATE_FORCE,FB_ACTIVATE_NOW);
		close(g1fd);
		return -1;
	}
#endif
#if 1
	/* 7. get the fix screen info */
	if (ioctl(g1fd, FBIOGET_FSCREENINFO, &fix) < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Get fix screen info failed!\n");
		close(g1fd);
		return -1;
	}
#endif
	/* create surface */
	g_fb_stScreen2.enColorFmt = TDE2_COLOR_FMT_ARGB1555;
	g_fb_stScreen2.u32PhyAddr = fix.smem_start;
	g_fb_stScreen2.u32Width = w;
	g_fb_stScreen2.u32Height = h;
	g_fb_stScreen2.u32Stride = fix.line_length;
	g_fb_stScreen2.bAlphaMax255 = HI_TRUE;
	g_fb_stScreen2.bAlphaExt1555 = HI_TRUE;
	g_fb_stScreen2.u8Alpha0 = 0;
	g_fb_stScreen2.u8Alpha1 = 255;
	
	PRV_OSD_SetGuiShow(HI_TRUE);

	PRV_CalcGuiResUnit();

	Set_FB_Flicker(0,0,g_fb_stScreen.u32Width,g_fb_stScreen.u32Height);
		
	return 0;
}
#endif

int OSD_init(unsigned char time_type)
{
	//int i=0;
	HI_S32 s32Ret = 0, i = 0;	
	//PRV_VO_SLAVE_STAT_S tmp;
	//初始化时间图片信息
	if(is_init_first == 0)
	{
		s32Ret = Rec_Osd_TimeInfo_Init(time_type);
		if(s32Ret == HI_FAILURE)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_Osd_TimeInfo_Init failed,ret=0x%x!\n", __LINE__, s32Ret);
			return HI_FAILURE;
		}
		is_init_first = 1;
		//打开TDE设备
		Prv_Open_Dev();
	}
	//OSD_G1_open();
	//get_OSD_param_init(&tmp);
#ifdef 	OPEN_PRV_OSD
	TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Init start\n", __LINE__);
	//初始化预览OSD
	for(i = 0; i < VO_MAX_DEV_NUM; i++)
	{
#if defined(SN9234H1)
		//初始化预览OSD
		if(i == SPOT_VO_DEV || i == AD)
#else
		//初始化预览OSD
		if(i > DHD0)
#endif			
		{
			continue;
		}
		s32Ret = Prv_OSD_Init(i);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Init failed,ret=0x%x!  i=%d\n", __LINE__, s32Ret,i);
			Prv_Close_Dev();
			goto startrec;
		}
	}
startrec:	
#endif	
#ifdef OPEN_REC_OSD	
	//初始化rec OSD
	//get_OSD_param_init();
	s32Ret = Rec_OSD_Init();
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_OSD_Init failed,ret=0x%x!\n", __LINE__, s32Ret);
		Rec_OSD_Close();
		return s32Ret;
	}	
	
#endif		
	g_fb_flag = 1;	
	for(i = 0; i < VO_MAX_DEV_NUM; i++)
	{
#if defined(SN9234H1)
		if(i == SPOT_VO_DEV|| i == AD)
#else
		if(i > DHD0)
#endif			
		{
			continue;
		}
		Prv_Disp_OSD(i);
	}
	return s32Ret;
}
//****************************************************
//OSD部分控制显示、隐藏函数
//*****************************************************

int OSD_Ctl(unsigned char ch, unsigned char on, unsigned char osd_type)
{	
	HI_S32 s32Ret = 0,i=0,flag=0;
	unsigned char type = osd_type;
#ifdef OPEN_REC_OSD		
	unsigned char flag_type;
#endif
	//TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,OSD_Ctl  ch = %d,on= %d  type = %d  ,OSD_off_flag[ch] = %d\n", __LINE__,ch,on,type,OSD_off_flag[ch]);
	if(!g_fb_flag)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD init failed\n", __LINE__);
		return OSD_INIT_ERR;
	}
	if(osd_type == OSD_TIME_TYPE)
		type = OSD_TIME_LAYER;
	else if(OSD_GetType_Index(ch,osd_type, &type) == HI_FAILURE)
	{
		printf(TEXT_COLOR_YELLOW("type=%d not correct!\n"), type);
		return type;
	}
#ifdef OPEN_REC_OSD		
	if(osd_type != OSD_TIME_TYPE)
	{
		if(ch >= PRV_CHAN_NUM)
		{
			goto startprv;
		}
		switch(osd_type)
		{
			case OSD_ALARM_TYPE: 
			case OSD_ALARM_MD_TYPE:
				flag_type = OSD_ALARM_OFF;
				break;
			case OSD_REC_TYPE:
			case OSD_REC_MANUAL_TYPE:
			case OSD_REC_ALARM_TYPE:
				flag_type = OSD_REC_OFF; 
				break;
			case OSD_TIME_TYPE: 
				flag_type = OSD_TIME_OFF; 
				break;
			case OSD_NAME_TYPE: 
				flag_type = OSD_NAME_OFF; 
				break;
			default:
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,invalid osd_type:%d\n", __LINE__, osd_type);
			    return osd_type;
		}
		//rec部分
		pthread_mutex_lock(&mutex_setbmp);
		s32Ret = Rec_OSD_Ctl(REC_CTRL_ALL, ch, on, type);
		for(i = 0; i < PRV_VO_DEV_NUM; i++)
		{
#if defined(SN9234H1)	
			if(i == SPOT_VO_DEV  || i == AD)
#else
			if(i > DHD0)
#endif				
			{
				continue;
			}
			if(on)
			{
				OSD_off_flag[i][ch] &= ~flag_type;
			}
			else
			{
				OSD_off_flag[i][ch] |= flag_type;
			}
		}
		pthread_mutex_unlock(&mutex_setbmp);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_OSD_Ctl failed,ret=0x%x!\n", __LINE__, s32Ret);
			goto startprv;
		}
	}
startprv:
#endif	
#ifdef 	OPEN_PRV_OSD	
	//预览部分
	if(on)
	{
		TDE_HANDLE tde_s32Handle=-1;
		//打开任务
		s32Ret = Prv_Open_Task(&tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Open_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
		for(i = 0; i < VO_MAX_DEV_NUM; i++)
		{
#if defined(SN9234H1)	
			if(i == SPOT_VO_DEV  || i == AD)
#else
			if(i > DHD0)
#endif	
			{
				continue;
			}
#if !defined(USE_UI_OSD)
			if (Prv_fd[i] <= 0)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Prv_fd[%d] = %d\n", i, Prv_fd[i]);
				continue;
			}
#endif		
			s32Ret = Prv_OSD_Ctl_On(i, ch, type, tde_s32Handle);
			if(s32Ret < 0)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Ctl_On failed,ret=0x%x!\n", __LINE__, s32Ret);
				HI_TDE2_CancelJob(tde_s32Handle);
				return s32Ret;
			}
			if(s32Ret != OSD_NOT_IN_MODE)
				flag |= 1;
		}
		//关闭任务
		if(!flag)
		{
			HI_TDE2_CancelJob(tde_s32Handle);
			return 0;
		}
		s32Ret = Prv_Close_Task(&tde_s32Handle);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
			return s32Ret;
		}
		
	}
	else
	{
		for(i = 0;i < VO_MAX_DEV_NUM; i++)
		{
#if defined(SN9234H1)	
			if(i == SPOT_VO_DEV  || i == AD)
#else
			if(i > DHD0)
#endif		
			{
				continue;
			}
#if !defined(USE_UI_OSD)
			if (Prv_fd[i] <= 0)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Prv_fd[%d] = %d\n", i, Prv_fd[i]);
				continue;
			}
#endif			
			s32Ret = Prv_OSD_Ctl_Off(i, ch, type, &OSD_Rect[i][ch][type], 1);
			if(s32Ret < 0)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Ctl_Off failed,ret=0x%x!\n", __LINE__, s32Ret);
				return s32Ret;
			}
			if(type != OSD_TIME_LAYER)
			{
				s32Ret= PRV_Osd_Chn_reflesh(i,ch);
				if(s32Ret < 0)
				{
					TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
					return s32Ret;
				}
			}
		}
	}
#endif		
	return s32Ret;
}
//****************************************************
//OSD部分设置时间函数
//*****************************************************

int OSD_Set_Time(char * str, char * qstr)
{
	HI_S32 s32Ret = 0,i=0,changeflag=0;
#ifdef OPEN_REC_OSD
	HI_S32 j = 0;
#endif
	//struct timeval	sttime,endtime;
	//STRING_BMP_ST BmpData={0};
	if(!g_fb_flag)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD init failed\n", __LINE__);
		return OSD_INIT_ERR;
	}
	
				
	//gettimeofday(&sttime,0); 
	//rec部分
	if(strcmp(str, (char*)(time_str_buf.Time_Str)))
	{//如果当前时间与前一次时间不一致，那么需要再获取字符
		for(i = 0; i < OSD_TIME_RES; i++)
		{
			s32Ret = Pic_Joint(i, str, qstr);
			if(s32Ret != 0)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Pic_Joint failed,ret=0x%x!\n", __LINE__, s32Ret);
				return HI_FAILURE;
			}
		}
		changeflag = 1;
	}
#ifdef OPEN_REC_OSD
	for(j = 0; j < REC_OSD_GROUP; j++)
	{
		for(i = 0;i < PRV_CHAN_NUM; i++)
		{
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############pTime = %s,i=%d,j=%d################\n",pTime,i,j);
			//更新字符
			pthread_mutex_lock(&mutex_setbmp);
			Rec_SetOSD_Time_Icon(j,i);
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "############icon_idx = %d################\n",idx);
			s32Ret = Rec_SetOSD_Bmp(j,i,OSD_TIME_LAYER,&time_str_buf.time_icon_param[j][i]);
			if(s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_SetOSD_Bmp failed,ret=0x%x!\n", __LINE__, s32Ret);
				//OSD_Free_String(&BmpData);
                pthread_mutex_unlock(&mutex_setbmp);
				goto startprv;
			}
			//OSD_Free_String(&BmpData);
			if(time_str_buf.Change_flag[j][i])
			{//如果当前标志位大于0，那么说明已经把需要修改的OSD内容都更新了，那么重置标志位
				time_str_buf.Change_flag[j][i] = 0;
			}
			pthread_mutex_unlock(&mutex_setbmp);
		}		
	}	
startprv:	
#endif	

#ifdef 	OPEN_PRV_OSD
	if(changeflag)
	{
		s32Ret = Prv_Disp_Time(str);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"Line:%d, Prv_Disp_Time faild 0x%x!\n", __LINE__,s32Ret);
			return s32Ret;
		}
	}		
#endif		
	//SN_STRNCPY(OSD_Time_Buf,MAX_BMP_STR_LEN,str,MAX_BMP_STR_LEN);
	if(changeflag)
	{
		SN_MEMCPY(time_str_buf.Time_Str, MAX_BMP_STR_LEN, str, MAX_BMP_STR_LEN, MAX_BMP_STR_LEN);
		SN_MEMCPY(time_str_buf.qTime_Str, MAX_BMP_STR_LEN, qstr, MAX_BMP_STR_LEN, MAX_BMP_STR_LEN);
	}
	//gettimeofday(&endtime,0); 
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "************************Sample_Cascade_Master close end time: %d.%d ,start time : %d.%d****************\n", endtime.tv_sec,endtime.tv_usec,sttime.tv_sec,sttime.tv_usec);

	return s32Ret;
}
#if 0
//****************************************************
//OSD部分设置时间位置接口函数
//*****************************************************

int OSD_Set_Time_xy(unsigned char ch,int x,int y)
{	
	HI_S32 s32Ret=0,j=0;
	TDE2_RECT_S stTmpRect=OSD_Rect[ch][OSD_TIME_LAYER];	
	unsigned char  venc_ch=ch;

	if(!g_fb_flag)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD init failed\n", __LINE__);
		return OSD_INIT_ERR;
	}
#ifdef OPEN_REC_OSD
	OSD_Rect[ch][OSD_TIME_LAYER].s32Xpos = x;
	OSD_Rect[ch][OSD_TIME_LAYER].s32Ypos= y;
	for(j=0;j<REC_OSD_GROUP;j++)
	{
		venc_ch = OSD_GetRecGroupChn(j,ch);
		CHECK_RET(HI_MPI_VPP_DestroyRegion(rec_osd_handle[venc_ch][OSD_NAME_LAYER]));	//注码流
		s32Ret = Rec_SetOSD_Time_xy(j,ch,x,y);
		if(0>s32Ret)
		{
			OSD_Rect[ch][OSD_TIME_LAYER] = stTmpRect;
			Rec_SetOSD_Time_xy(j,ch,OSD_Rect[ch][OSD_TIME_LAYER].s32Xpos,OSD_Rect[ch][OSD_TIME_LAYER].s32Ypos);
			return s32Ret;
		}
	}
#endif	
	return 0;
	
}
#endif
//****************************************************
//OSD部分设置通道名称函数
//*****************************************************

int OSD_Set_Ch(unsigned char ch, char * str)
{
	HI_S32 s32Ret=0,i=0;//,flag=0;
	//TDE_HANDLE tde_s32Handle=-1;
	//STRING_BMP_ST BmpData={0};
//	unsigned char  venc_ch=ch;
	char name_tmp[CHANNEL_NAME_LEN];
	if(!g_fb_flag || str == NULL)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD init failed\n", __LINE__);
		return OSD_INIT_ERR;
	}
	if(!strcmp(str,(char *)OSD_Name_Buf[ch]))
	{//字符串相同，那么不进行修改
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD_Set_Ch the same str \n", __LINE__);
		return 0;
	}
	SN_STRNCPY(name_tmp,CHANNEL_NAME_LEN,(char *)OSD_Name_Buf[ch],CHANNEL_NAME_LEN);
	SN_STRNCPY((char *)OSD_Name_Buf[ch],CHANNEL_NAME_LEN,str,CHANNEL_NAME_LEN);
#ifdef OPEN_REC_OSD
	if(ch >= PRV_CHAN_NUM)
	{
        	Rec_OSD_Init_Ch(ch);
        	PRV_Send_RecOsdName2Slave(ch%PRV_CHAN_NUM);
		goto startprv;
	}
	//rec部分
	TRACE(SCI_TRACE_NORMAL,MOD_PRV,"Line:%d,OSD_Set_Ch the dif str str= %s \n", __LINE__,str);
	for(i=0;i<REC_OSD_GROUP;i++)
	{
		pthread_mutex_lock(&mutex_setbmp);
		time_str_buf.Change_flag[i][ch]++;
		pthread_mutex_unlock(&mutex_setbmp);
	}
#if 0	
	for(i=0;i<REC_OSD_GROUP;i++)
	{
		s32Ret = Rec_SetOSD_CH(i,ch,str);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_SetOSD_CH failed,ret=0x%x!\n", __LINE__, s32Ret);
#if 1
			/*配置通道名称失败后恢复设置*/
			Rec_SetOSD_CH(i,ch, OSD_Name_Buf[ch]);
			if(!(OSD_off_flag[s_VoDevCtrlDflt][ch] & OSD_NAME_OFF))
			{
				venc_ch = OSD_GetRecGroupChn(i,ch);
				CHECK(HI_MPI_VPP_ControlRegion(rec_osd_handle[venc_ch][OSD_NAME_LAYER], REGION_SHOW, NULL));
			}
#endif
			return s32Ret;
			//goto startprv;
		}
	}
#endif	
startprv:

#endif		
#ifdef 	OPEN_PRV_OSD	
	//预览部分
	for(i=0;i<VO_MAX_DEV_NUM;i++)
	{
#if defined(SN9234H1)	
			if(i == SPOT_VO_DEV  || i == AD)
#else
			if(i > DHD0)
#endif			
		{
			continue;
		}
#if !defined(USE_UI_OSD)
		if (Prv_fd[i] <= 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Prv_fd[%d] = %d\n", i, Prv_fd[i]);
			continue;
		}
#endif		
		Prv_OSD_Ctl_Off(i,ch,OSD_NAME_LAYER,&OSD_Rect[i][ch][OSD_NAME_LAYER],0);
	}
	
	for(i=0;i<VO_MAX_DEV_NUM;i++)
	{
#if defined(SN9234H1)	
			if(i == SPOT_VO_DEV  || i == AD)
#else
			if(i > DHD0)
#endif	
		{
			continue;
		}
		s32Ret = PRV_Osd_Chn_reflesh(i,ch);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_Close_Task failed,ret=0x%x!\n", __LINE__, s32Ret);
			SN_STRNCPY((char *)OSD_Name_Buf[ch],CHANNEL_NAME_LEN,name_tmp,CHANNEL_NAME_LEN);
			return s32Ret;
		}
	}	
#endif	
	//SN_STRNCPY(OSD_Name_Buf[ch],CHANNEL_NAME_LEN,str,CHANNEL_NAME_LEN);
	return s32Ret;
}
#if 0
//****************************************************
//OSD部分设置通道位置
//*****************************************************
int OSD_Set_CH_xy(unsigned char ch,int x,int y)
{
	HI_S32 s32Ret=0,i=0;
	if(!g_fb_flag)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,OSD init failed\n", __LINE__);
		return OSD_INIT_ERR;
	}
	
#ifdef OPEN_REC_OSD
	//rec部分
	if(ch >= PRV_CHAN_NUM)
	{
		goto startprv;
	}
#if 1
	TDE2_RECT_S tmpRect = OSD_Rect[ch][OSD_NAME_LAYER];
	
	for(i=0;i<REC_OSD_GROUP;i++)
	{
		OSD_Rect[ch][OSD_NAME_LAYER].s32Xpos = x;
		OSD_Rect[ch][OSD_NAME_LAYER].s32Ypos = y;
		s32Ret = Rec_Create_region(i,ch,OSD_NAME_LAYER, OSD_Rect[ch][OSD_NAME_LAYER].s32Xpos,OSD_Rect[ch][OSD_NAME_LAYER].s32Ypos,name_icon_param[i][ch].stBitmap.u32Width,name_icon_param[i][ch].stBitmap.u32Height,OSD_BG_ALPHAL,OSD_FG_ALPHAL,OSD_BG_COLOR);
		if (HI_SUCCESS!=s32Ret)
		{
			OSD_Rect[ch][OSD_NAME_LAYER] = tmpRect;
			Rec_Create_region(i,ch,OSD_NAME_LAYER, OSD_Rect[ch][OSD_NAME_LAYER].s32Xpos,OSD_Rect[ch][OSD_NAME_LAYER].s32Ypos,name_icon_param[i][ch].stBitmap.u32Width,name_icon_param[i][ch].stBitmap.u32Height,OSD_BG_ALPHAL,OSD_FG_ALPHAL,OSD_BG_COLOR);
			goto startprv;
		}
		OSD_Rect[ch][OSD_NAME_LAYER] = tmpRect;/*接下的Prv_OSD_Set_CH_xy判断用，故此还原。*/
		s32Ret = Rec_SetOSD_CH(i,ch,OSD_Name_Buf[ch]);
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_SetOSD_CH failed,ret=0x%x!\n", __LINE__, s32Ret);
			goto startprv;
			//goto startprv;
		}
	}
#else
	s32Ret = Rec_SetOSD_CH_xy(ch,x,y);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_SetOSD_CH_xy failed,ret=0x%x!\n", __LINE__, s32Ret);
		//goto startprv;
		return s32Ret;
	}
#endif
startprv:
#endif		
#ifdef 	OPEN_PRV_OSD	
	//预览部分
	s32Ret = Prv_OSD_Set_CH_xy(ch,x,y);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_CH_xy failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
#endif		
	
	return 0;
}
#endif
/*************************************************
Function: //OSD_Set_xy
Description: //录像部分OSD，时间和通道名称位置刷新
Calls: 
Called By: //
Input: // ch :通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
int OSD_Set_xy(unsigned char ch,int name_x,int name_y,int time_x,int time_y)
{
	int rec_group=0,s32Ret=0;
	int n_x = name_x,n_y=name_y,t_x = time_x, t_y = time_y;
	//首先对X,Y进行范围检测
	if(n_x >= SCREEN_DEF_WIDTH)
	{
		n_x = SCREEN_DEF_WIDTH-1;
	}
	if(n_x < 0)
	{
		n_x = 0;
	}
	if(t_x >= SCREEN_DEF_WIDTH)
	{
		t_x = SCREEN_DEF_WIDTH-1;
	}
	if(t_x < 0)
	{
		t_x = 0;
	}
	if(n_y > SCREEN_DEF_HEIGHT-1)
	{
		n_y = SCREEN_DEF_HEIGHT-1;
	}
	if(n_y < 0)
	{
		n_y = 0;
	}
	if(t_y > SCREEN_DEF_HEIGHT-1)
	{
		t_y = SCREEN_DEF_HEIGHT-1;
	}
	if(t_y < 0)
	{
		t_y = 0;
	}
#ifdef OPEN_REC_OSD	
	if(ch >= PRV_CHAN_NUM)
	{
		goto startprv;
	}
#if defined(SN9234H1)
	OSD_Rect[HD][ch][OSD_TIME_LAYER].s32Xpos = t_x;
	OSD_Rect[HD][ch][OSD_TIME_LAYER].s32Ypos= t_y;
	OSD_Rect[AD][ch][OSD_TIME_LAYER].s32Xpos = t_x;
	OSD_Rect[AD][ch][OSD_TIME_LAYER].s32Ypos= t_y;
	OSD_Rect[SD][ch][OSD_TIME_LAYER].s32Xpos = t_x;
	OSD_Rect[SD][ch][OSD_TIME_LAYER].s32Ypos = t_y;
#else
	OSD_Rect[DHD0][ch][OSD_TIME_LAYER].s32Xpos = t_x;
	OSD_Rect[DHD0][ch][OSD_TIME_LAYER].s32Ypos= t_y;
	OSD_Rect[DSD0][ch][OSD_TIME_LAYER].s32Xpos = t_x;
	OSD_Rect[DSD0][ch][OSD_TIME_LAYER].s32Ypos= t_y;

#endif

startprv:	
#endif	
#ifdef 	OPEN_PRV_OSD	
	//预览部分
	s32Ret = Prv_OSD_Set_CH_xy(ch,n_x,n_y);
	if(s32Ret < 0)
	{
		TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Prv_OSD_Set_CH_xy failed,ret=0x%x!\n", __LINE__, s32Ret);
		return s32Ret;
	}
#endif	
	//printf(TEXT_COLOR_PURPLE("OSD_Set_xy  stsrcrect: %d,%d,%d,%d,\n"), OSD_Rect[HD][ch][OSD_NAME_LAYER].s32Xpos, OSD_Rect[HD][ch][OSD_NAME_LAYER].s32Ypos, OSD_Rect[HD][ch][OSD_NAME_LAYER].u32Width, OSD_Rect[HD][ch][OSD_NAME_LAYER].u32Height);
	for(rec_group=0;rec_group<REC_OSD_GROUP;rec_group++)
	{
		/*
		s32Ret = Rec_SetOSD_xy(rec_group,ch,n_x,n_y,t_x,t_y);	
		if(s32Ret < 0)
		{
			TRACE(SCI_TRACE_HIGH,MOD_PRV,"Line:%d,Rec_SetOSD_xy failed,ret=0x%x!\n", __LINE__, s32Ret);
			goto startprv;
		}*/
		pthread_mutex_lock(&mutex_setbmp);
		time_str_buf.Change_flag[rec_group][ch]++;
		pthread_mutex_unlock(&mutex_setbmp);
	}
	
	return 0;
}

int Fb_clear_step1(void)
{
	if (sizeof(Prv_fd)/sizeof(Prv_fd[0]) < 3)
	{
		fprintf(stderr, "%s, array size smaller than 3!", __FUNCTION__);
		return -1;
	}

	if ((Prv_fd[0] = open("/dev/fb0", O_RDWR)) == -1)//DHD0
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"open frame buffer device:/dev/fb0 error\n");
	}

	if ((Prv_fd[1] = open("/dev/fb2", O_RDWR)) == -1)//DSD0
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"open frame buffer device:/dev/fb2 error\n");
	}
#if defined(SN9234H1)
	if ((Prv_fd[2] = open("/dev/fb3", O_RDWR)) == -1)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"open frame buffer device:/dev/fb3 error\n");
	}
#endif
	return 0;
}
int Fb_clear_step2(void)
{
	if (sizeof(Prv_fd)/sizeof(Prv_fd[0]) < 3)
	{
		fprintf(stderr, "%s, array size smaller than 3!", __FUNCTION__);
		return -1;
	}

	if (Prv_fd[0] != -1)
	{
		close(Prv_fd[0]);
		Prv_fd[0] = -1;
	}

	if (Prv_fd[1] != -1)
	{
		close(Prv_fd[1]);
		Prv_fd[1] = -1;
	}

	if (Prv_fd[2] != -1)
	{
		close(Prv_fd[2]);
		Prv_fd[2] = -1;
	}

	return 0;
}
