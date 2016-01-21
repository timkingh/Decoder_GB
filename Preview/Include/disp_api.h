/******************************************************************************

  Copyright (C), 
  
	******************************************************************************
	File Name     : disp_api.h
	Version       : Initial Draft
	Author        : 
	Created       : 2010/06/09
	Description   : preview module header file
	History       :
	1.Date        : 
    Author        : 
    Modification  : Created file
	
******************************************************************************/

#ifndef __DISP_API_H__
#define __DISP_API_H__

#include "global_api.h"
#include "global_def.h"
#include "global_err.h"
#include "global_msg.h"
#include "global_str.h"
#include "prv_err.h"
#include "prv_comm.h"
#if defined(SN9234H1)
#include "mkp_vd.h"
#else
#include "hi_comm_vpss.h"	
#endif
#include "hi_tde_type.h"
#include "hi_common.h"

#include "hi_comm_sys.h"
#include "hi_comm_vb.h"
#include "hi_comm_ai.h"
#include "hi_comm_ao.h"
#include "hi_comm_aio.h"
#include "hi_comm_vi.h"
#include "hi_comm_vo.h"
#include "hi_comm_vdec.h"
#include "hi_comm_adec.h"
#include "hi_comm_video.h"
#include "hi_type.h"

#include "mpi_sys.h"
#include "mpi_vi.h"
#include "mpi_vo.h"
#include "mpi_ao.h"
#include "mpi_vdec.h"
#include "mpi_adec.h"
#include "mpi_vb.h"


#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* End of #ifdef __cplusplus */

typedef enum PreviewMode_enum PRV_PREVIEW_MODE_E;
/************************ MACROS HEAR *************************/

#define STATIC static		//控制本文件内函数是否允许外部引用

#if (CHANNEL_NUM==16)
#define PRV_VO_MAX_MOD SixteenScene//VO_MAX_DEV_NUM				//输出画面最大模式
#elif (CHANNEL_NUM==8)
#define PRV_VO_MAX_MOD NineScene//VO_MAX_DEV_NUM				//输出画面最大模式
#elif (CHANNEL_NUM==4)
#define PRV_VO_MAX_MOD FourScene//VO_MAX_DEV_NUM				//输出画面最大模式
#endif


#define PRV_VO_CHN_NUM CHANNEL_NUM//16 //VIU_MAX_CHN_NUM				//每个输出设备的通道数
#define PRV_VI_CHN_NUM 4 //VIU_MAX_CHN_NUM_PER_DEV		//每个输入设备的通道数
#define PRV_VO_MAX_DEV 		PRV_VO_DEV_NUM  //VO_MAX_PHY_DEV				/* max physical device number(HD,AD,SD) */

#define PRV_DFLT_CHN_PRIORITY 1						//预览通道所用的默认优先级

#define PRV_PREVIEW_LAYOUT_DIV 12					//预览画面单位分割数


#define PRV_656_DEV		2							//采集输入设备2
#define PRV_656_DEV_1		3							//采集输入设备3

#define PRV_HD_DEV				0							//级联输入设备ID号
#define PRV_IMAGE_SIZE_W		720
#define PRV_IMAGE_SIZE_H_P		576
#define PRV_IMAGE_SIZE_H_N		480

#define OSD_ALARM_LAYER	0		//视频丢失图标层
#define OSD_REC_LAYER	1		//定时录像图标层
#define OSD_TIME_LAYER	2		//时间图标层
#define OSD_NAME_LAYER	3		//通道名称图标层
#define OSD_CLICKVISUAL_LAYER	4

#define DISP_PIP_X		250		//显示参数界面上小画面的起始坐标x，相对1024*768
#define DISP_PIP_Y		204		//显示参数界面上小画面的起始坐标y，相对1024*768
#define DISP_PIP_W		365		//显示参数界面上小画面的起始坐标w，相对1024*768
#define DISP_PIP_H		275		//显示参数界面上小画面的起始坐标h，相对1024*768

#define PRV_CVBS_EDGE_CUT_W 30
#define PRV_CVBS_EDGE_CUT_H 16


#define DISP_DOUBLE_DISP	1	//双屏显示
#define DISP_NOT_DOUBLE_DISP	0	//单屏显示

#define OSD_CLEAR_LAYER	25
#if defined(SN9234H1)
#define SPOT_VO_DEV			SD
#else
#define SPOT_VO_DEV			DSD0
#endif
#define SPOT_VO_CHAN		0
#define SPOT_PCI_CHAN		9

#define PRV_CHAN_NUM 		(PRV_VO_CHN_NUM/PRV_CHIP_NUM)		//预览模块通道参数
#define PRV_CTRL_VOCHN	PRV_VO_CHN_NUM + 1

#if defined(SN9234H1)
typedef enum hiVOU_DEV_E
{
	HD = 0,
	DHD0 = 0,	
	AD = 1,
	SD = 2,
	VOU_DEV_BUTT
}VOU_DEV_E;
#elif defined(Hi3535)
typedef enum hiVOU_DEV_E
{
	HD = 0,
	DHD0 = 0,
	DHD1 = 1,
	DSD0 = 2
//	VOU_DEV_BUTT
}VOU_DEV_E;
typedef enum hiVOU_LAY_E
{
	
	VHD0 = 0,
	VHD1 = 1,
	PIP  = 2,
	VSD0 = 3
//	VOU_DEV_BUTT
}VOU_LAY_E;
#else
typedef enum hiVOU_DEV_E
{	
	HD = 0,
	DHD0 = 0,
	DHD1 = 1,
	DSD0 = 2,
	DSD1 = 3,
	DSD2 = 4,
	DSD3 = 5,
	DSD4 = 6,
	DSD5 = 7
//	VOU_DEV_BUTT
}VOU_DEV_E;
#endif

#if defined(Hi3535)
typedef enum hiHIFB_GRAPHIC_LAYER_E
{
	G4 = 0,
	G1 = 1,
	G2 = 2,
	G3 = 3,
	G0 = 4,
	HC = 4,
	HIFB_GRAPHIC_LAYER_BUTT
}HIFB_GRAPHIC_LAYER_E;
#else
typedef enum hiHIFB_GRAPHIC_LAYER_E
{
	G0 = 0,
	G1 = 1,
	G2 = 2,
	G3 = 3,
	G4 = 4,
	HC = 4,
	HIFB_GRAPHIC_LAYER_BUTT
}HIFB_GRAPHIC_LAYER_E;
#endif

typedef enum CtrlFlag_enum
{
	PRV_CTRL_REGION_SEL = 0,	//区域选择控制
	PRV_CTRL_ZOOM_IN,			//电子放大控制
	PRV_CTRL_PTZ,				//云台控制
	PRV_CTRL_PB,				//回放控制
	PRV_CTRL_BUTT
}PRV_CTRL_FLAG_E;	//控制状态标志枚举类型定义
typedef enum TimeOut_Type_enum
{
	PRV_INIT = 0,		//初始化
	PRV_LAY_OUT,			//画面切换
}PRV_TIMEROUT_TYPE_E;	//预览状态枚举类型定义

typedef struct __PRV_STATE_INFO_S__
{
	HI_BOOL					bIsInit;					//预览初始化状态
	HI_BOOL					bslave_IsInit;					//预览从片初始化状态
	HI_U8					bIsReply;	//预览从片回复状态，
	HI_U8					bIsNpfinish;	//预览是否NP切换完成
	HI_BOOL					bIsOsd_Init;				//OSD是否已经初始化
	HI_BOOL					bIsRe_Init;				//OSD是否重新初始化
	HI_BOOL					bIsVam_rsp;				//是否回复VAM初始化完成消息
	HI_U8					TimeoutCnt;			//超时次数
	HI_U8					bIsSlaveConfig;			//从片是否处于配置状态
	HI_U8					bIsTimerState;			//是否处于定时状态
	HI_U8					TimerType;			//超时类型
	HI_S32					f_timer_handle;			//定时器句柄
	HI_U8					g_slave_OK;			//从片返回的状态
	HI_U8					g_zoom_first_in;			//电子放大初次进入标志
	SN_MSG 					*Prv_msg_Cur;
}PRV_STATE_INFO_S,*PPRV_STATE_INFO_S;		//预览模块状态机结构体
typedef struct __PRV_VO_SLAVE_STAT_S__
{
	PRV_PREVIEW_MODE_E		enPreviewMode;					//预览模式
	HI_S32					s32PreviewIndex;				//多画面预览开始位置
	HI_S32					s32SingleIndex;					//单画面预览开始位置
	HI_BOOL					bIsSingle;						//是否是单画面
	HI_U8					enVideoNorm;						//N\P制式
	PRM_AREA_HIDE 			cover_info[PRV_CHAN_NUM];		//从片遮盖信息
	HI_U8 					slave_OSD_off_flag[PRV_CHAN_NUM];//从片OSD状态标志位
	Preview_Point  			slave_OSD_Rect[PRV_CHAN_NUM][REGION_NUM];		//从片OSD位置
	HI_U32 					f_rec_srceen_h[REC_OSD_GROUP][PRV_CHAN_NUM];
	HI_U32				 	f_rec_srceen_w[REC_OSD_GROUP][PRV_CHAN_NUM];
	
	HI_U32 					slave_BmpData_name_w[REC_OSD_GROUP][PRV_CHAN_NUM];			//从片通道名称图片长
	HI_U32 					slave_BmpData_name_h[REC_OSD_GROUP][PRV_CHAN_NUM];			//从片通道名称图片宽
	HI_U32 					slave_BmpData_name_size[REC_OSD_GROUP][PRV_CHAN_NUM];			//从片通道名称图片数据大小
}PRV_VO_SLAVE_STAT_S,*PPRV_VO_SLAVE_STAT_S;		//从片需要信息结构体定义

typedef struct __PRV_VO_DEV_STAT_S__
{
	VO_PUB_ATTR_S			stVoPubAttr;					//VO设备公共属性
	VO_VIDEO_LAYER_ATTR_S	stVideoLayerAttr;				//VO设备视频层属性
	PRV_PREVIEW_STAT_E		enPreviewStat;					//预览状态
	PRV_PREVIEW_MODE_E		enPreviewMode;					//预览模式
	PRV_CTRL_FLAG_E			enCtrlFlag;						//控制状态标志
	VO_CHN					as32ChnOrder[PRV_PREVIEW_MODE_NUM][SEVENINDEX];	//预览通道顺序，包括通道是否隐藏，无效通道值表示隐藏该位置的通道。
	VO_CHN					as32ChnpollOrder[PRV_PREVIEW_MODE_NUM][SEVENINDEX];	//预览通道顺序，包括通道是否隐藏，无效通道值表示隐藏该位置的通道。
	AO_CHN					AudioChn[7];//解码器下使用，4种预览模式下对应的音频通道
	HI_S32					s32PreviewIndex;				//多画面预览开始位置
	HI_S32					s32SingleIndex;					//单画面预览开始位置
	HI_S32					s32AlarmChn;					//报警弹出通道
	HI_S32					s32CtrlChn;						//控制状态通道
	HI_BOOL					bIsAlarm;						//是否有通道报警
	HI_BOOL					bIsSingle;						//是否是单画面(解码器中，用此变量区分是单画面还是双击进入单画面)
	HI_BOOL					s32DoubleIndex;					//是否是鼠标双击
	RECT_S					Pip_rect;						//画中画的位置
}PRV_VO_DEV_STAT_S,*PPRV_VO_DEV_STAT_S;		//VO设备属性和状态信息结构体定义

#if 0
#define NOVIDEO_FILE "/res/pic_704_576_p420_novideo.yuv"
#define NOVIDEO_FILE_EN "/res/pic_704_576_p420_novideo_en.yuv"

#define NOVIDEO_FILE_P "/res/pic_704_576_p420_novideo.yuv"
#define NOVIDEO_FILE_P_EN "/res/pic_704_576_p420_novideo_en.yuv"

#define NOVIDEO_FILE_N "/res/pic_704_480_p420_novideo.yuv"
#define NOVIDEO_FILE_N_EN "/res/pic_704_480_p420_novideo_en.yuv"
#endif

extern char   *weekday[];
extern char   *weekday_en[];

extern HI_U32 s_u32GuiWidthDflt;							//默认GUI宽
extern HI_U32 s_u32GuiHeightDflt;							//默认GUI高
extern VO_DEV s_VoDevCtrlDflt;
extern HI_U32 g_Max_Vo_Num;
extern PRV_VO_SLAVE_STAT_S s_slaveVoStat;


//////////////////////////////////////解码器///////////////////////////////
#define	BUFFER_NUM				200


//无视频信号图片信息
#define NVR_NOVIDEO_FILE		"/res/nvr_novideo.jpg"
#define DVS_NOCONFIG_FILE		"/res/dvs_noconfig.jpg"
#define NVR_NOVIDEO_FILE_1	"/res/nvr_novideo.jpg"
#define NVR_NOVIDEO_FILE_2	"/res/nvr_novideo2.jpg"
#define NVR_NOVIDEO_FILE_TEST_1	"/res/gif-1.jpg"
#define NVR_NOVIDEO_FILE_TEST_2	"/res/gif-2.jpg"

#define NOVIDEO_IMAGWIDTH  		352 		//图像宽度
#define NOVIDEO_IMAGHEIGHT 		80  		//图像宽度
#define VLOSSPICBUFFSIZE 		(NOVIDEO_IMAGWIDTH * NOVIDEO_IMAGHEIGHT)      //码流数据大小

#define NOVIDEO_VDECWIDTH   	352     //为无视频型号创建的解码器高 
#define NOVIDEO_VDECHEIGHT  	288      //为无视频型号创建的解码器宽


//#define CHIPNUM 2
#define DecAdec  				10//音频解码通道
#define DetVLoss_VdecChn 		30//视频丢失图片数据解码通道
#define NoConfig_VdecChn 		(DetVLoss_VdecChn + 1)//未配置图片解码通道

#define H264ENC 				96
#define JPEGENC 				26

#define TimeInterval 			(500)//音、视频允许的最大时间间隔,超过这个间隔时，需要同步(ms)
#define MAXFRAMELEN 			(4*1024*1024)//(2.5*1024*1024)

#if(DEV_TYPE == DEV_SN_9234_H_1)
#define RefFrameNum 			4
#else
#define RefFrameNum 			4
#endif

typedef struct
{
	HI_U32 height;    // 视频数据高 
	HI_U32 width;  // 视频数据宽
	HI_U32 vdoType;  //视频数据编码类型
	HI_U32 framerate;  //视频数帧率 	
}PRV_VIDEOINFO;

typedef struct
{
	HI_S32 adoType;	//音频数据编码类型 
	HI_U32 samrate;	// 音频数据采样率
	HI_U32 soundchannel;  // 音频数据声道数
	HI_U32 PtNumPerFrm;	  //每帧采样点个数
	//unsigned  int bitwide;   // 音频数据位宽
}PRV_AUDIOINFO;

typedef struct
{
	int pre_frame[DEV_CHANNEL_NUM];
	int is_same[DEV_CHANNEL_NUM];
	int is_first[DEV_CHANNEL_NUM];
	int is_disgard[DEV_CHANNEL_NUM];
	HI_S32 TotalLength1[DEV_CHANNEL_NUM];
	HI_U8* VideoDataStream1[DEV_CHANNEL_NUM];
	HI_U8* VideoDataStream2[DEV_CHANNEL_NUM];
}g_PRVSendChnInfo; 

typedef struct
{
	HI_S32 IsLocalVideo;		//本地视频输入标识，1:本地视频，0:IPC视频
	HI_S32 VoChn;				// 对应输出通道编号	
	HI_S32 SlaveId;				// 预览该通道所属片Id  0:主片 1:从1， 2:从,2，3:从3 
	
	//以下成员为数字通道专用
	HI_S32 VdecChn;				//对应解码通道，DetVLoss_Vdec(30):无视频，其他>=0，对应的有效解码通道
	HI_S32 VdecCap;				//对应单通道的性能大小，取决于分辨率	
	HI_S32 IsBindVdec[PRV_VO_DEV_NUM];//是否绑定Vo.-1:未绑定，0:绑定DetVLoss_Vdec(30)，1:绑定正常解码通道或VI:0(与从片级联)
	UINT8 CurChnIndex; 			// 对应IPC视频通道号，起始值为0
	UINT8 IsHaveVdec;			//是否创建解码通道，0:没有，1:有
	UINT8 IsConnect;			//是否与IPC设备连接
	UINT8 bIsStopGetVideoData;	// 当前通道暂停获取数据,1:暂停，0:正常 
	UINT8 bIsWaitIFrame;		//是否从公共Client缓冲区中获取下一个I帧
	UINT8 bIsWaitGetIFrame;		//是否从预览缓冲区中获取下一个I帧
	UINT8 bIsDouble;			//是否双击
	UINT8 PrvType;				// 预览类型改变0:无变化 ，1:流畅型，2:实时型
	UINT8 MccCreateingVdec;
	UINT8 MccReCreateingVdec;
	UINT8 IsChooseSlaveId;
	UINT8 IsDiscard;				//性能超出时，对应通道数据丢弃	
	UINT8 bIsPBStat;				//即时回放状态
	HI_U32 u32RefFrameNum;			//参考帧数目
	PRV_VIDEOINFO VideoInfo;		//IPC音频数据信息
	PRV_AUDIOINFO AudioInfo;		//IPC视频数据信息
}g_PRVVoChnInfo;   //各个解码通道的状态信息结构体

typedef struct
{
	UINT8 FirstHaveVideoData[MAX_IPC_CHNNUM];//写第一帧视频数据标识
	UINT8 FirstHaveAudioData[MAX_IPC_CHNNUM];//写第一个音频数据标识
	UINT8 FirstGetData[MAX_IPC_CHNNUM];//读取第一帧视频数据标识
	UINT8 IsGetFirstData[MAX_IPC_CHNNUM];
	UINT8 IsStopGetVideoData[MAX_IPC_CHNNUM];
	UINT8 BeginSendData[MAX_IPC_CHNNUM];
	UINT8 bIsPBStat_StopWriteData[MAX_IPC_CHNNUM];//回放状态下，FTPC暂不写数据
	UINT8 bIsPBStat_BeyondCap[MAX_IPC_CHNNUM];
	int VideoDataCount[MAX_IPC_CHNNUM];
	int AudioDataCount[MAX_IPC_CHNNUM];
	int SendAudioDataCount[MAX_IPC_CHNNUM];
	HI_S64 VideoDataTimeLag[MAX_IPC_CHNNUM];
	HI_S64 AudioDataTimeLag[MAX_IPC_CHNNUM];
}g_PRVVoChnState;

typedef struct
{
	HI_U64	PreGetVideoPts;		//从缓冲队列0取出前一数据的PTS(未转换)
	HI_U64	CurGetVideoPts;		//从缓冲队列0取出当前数据的PTS(未转换)
	HI_U64  PreVideoPts;  		//该本地缓冲取出前一数据的PTS (未转换)
	HI_U64  CurVideoPts;   		//该本地缓冲取出当前数据的PTS(未转换)
	HI_U64  PreSendPts;  		//往解码器送的前一数据的PTS(已转换)
	HI_U64	CurSendPts;			//往解码器送的当前数据的PTS(已转换)
	HI_U64  PreVoChnPts;		//输出前一数据的PTS	
	HI_U64	CurVoChnPts;		//输出当前数据的PTS	
	HI_U64	DevicePts;			//人为控制数据发送当前帧的PTS 
	HI_U64  DeviceIntervalPts;	//正常PTS帧间隔
	HI_U64	IntervalPts;		//相邻两帧送解码器数据的PTS间隔 
	HI_U64  ChangeIntervalPts;	//PTS间隔变化时，对应送数据的时间戳
	HI_U64	FirstVideoPts;		//第一帧视频数据的时间戳
	HI_U64	FirstAudioPts;		//第一帧音频数据的时间戳
	HI_U64	PreGetAudioPts;
	HI_U64  BaseVideoPts;		//用于计算500ms缓冲数据的起始时间，回放不用
	HI_U64  IFrameOffectPts;  	// 相邻I帧间隔，以微妙为单位 
	HI_U64  pFrameOffectPts; 	// 相邻P帧间隔，以微妙为单位 
	HI_U64  CurShowPts;        	//回放进度条显示时间戳，需要转换为具体时间 
	time_t  StartPts;
	time_t 	CurTime;
	PRM_ID_TIME QueryStartTime;	//进入回放时，查询文件的起始时间
	PRM_ID_TIME QueryFinalTime;	//进入回放时，查询文件的起始时间
	PRM_ID_TIME StartPrm;		//当前回放文件的起始时间	，切换文件时会更新
	PRM_ID_TIME EndPrm;		//当前回放文件的起始时间	，切换文件时会更新
	
}g_PRVPtsinfo;   /* 时间信息结构体*/

typedef struct
{
	UINT8 SlaveCreatingVdec[DEV_CHANNEL_NUM];//从片是否正在创建解码通道
	UINT8 SlaveIsDesingVdec[DEV_CHANNEL_NUM];//从片是否正在销毁解码通道
	
}g_PRVSlaveState;

 typedef struct
{
	int  SlaveId;
	int  s32StreamChnIDs;//主从片传输数据ID号
	int  EncType;//解码通道解码类型
	int  chn;//数字通道号，起始值0
	int  VoChn;//对应的输出通道号，对于值为chn+LOCALVEDIONUM
	int  VdecChn;
	int  height;//分辨率:高
	int  width;//分辨率:宽
	int  VdecCap;
}PRV_MccCreateVdecReq;

typedef struct
{
	int SlaveId;		 /* 从片Id */
	int VoChn;
	int VdecChn;
	int Result; 	 /* 回放响应，-1表示失败，0表示成功 */
}PRV_MccCreateVdecRsp;

typedef struct
{
	int VoChn;
	int VdecChn;	/* 表示需要重新创建的通道号 */
	int height;		/* 新的高宽*/
	int width;
}PRV_ReCreateVdecIND;

typedef struct
{
	int SlaveId;
	int VoChn;
	int VdecChn;    /* 表示需要重新创建的通道号 */
	int height; 	/* 新的高宽*/
	int width;	
	int EncType;
	int VdecCap;
}PRV_MccReCreateVdecReq;

typedef struct
{
	int SlaveId;		 /* 从片Id */
	int VoChn;
	int VdecChn;      /* 表示需要重新创建的通道号 */
	int height; 	// 新的高宽
	int width;
	int VdecCap;
	int Result;      /* 创建通道结果；0：成功  -1：失败 */
}PRV_MccReCreateVdecRsp;

typedef struct
{
	int chn;
	int NewPtNumPerFrm;
}PRV_ReCreateAdecIND;

typedef struct
{
	int VoChn;
}PRV_MccGetPtsReq;

typedef struct
{
	int VoChn;
	HI_U64 u64CurPts;
	int Result;
}PRV_MccGetPtsRsp;

typedef struct
{
	int VdecChn;
}PRV_MccDestroyVdecReq;

typedef struct
{
	int VdecChn;
	int Result;
}PRV_MccDestroyVdecRsp;

typedef struct
{
	int VdecChn;
}PRV_MccReSetVdecIND;

typedef struct
{
	int VdecChn;
}PRV_MccQueryVdecReq;

typedef struct
{
	int VdecChn;
	HI_U32 DecodeStreamFrames;
	HI_U32 LeftStreamFrames;
}PRV_MccQueryVdecRsp;

extern int IsAdecBindAo;
extern int IsCreateAdec;
extern int IsAudioOpen;

extern g_PRVVoChnInfo VochnInfo[DEV_CHANNEL_NUM];//IPC 视频输出通道的通道信息
extern g_PRVSlaveState SlaveState;
extern g_PRVPtsinfo PtsInfo[MAX_IPC_CHNNUM];
extern g_PRVVoChnState VoChnState;
extern int g_PrvType;
extern int CurSlaveCap;
extern int CurMasterCap;
extern int CurCap;
extern int Achn;
extern int IsCreatingAdec;
extern int CurIPCCount;
extern int CurSlaveChnCount;
extern HI_U32 PtNumPerFrm;
extern int MasterToSlaveChnId;
extern pthread_mutex_t send_data_mutex;
extern sem_t sem_VoPtsQuery, sem_PlayPause, sem_PrvGetData, sem_PrvSendData, sem_PBGetData, sem_PBSendData;

extern AVPacket *PRV_OldVideoData[DEV_CHANNEL_NUM];
extern VDEC_CHN PRV_OldVdec[DEV_CHANNEL_NUM];
extern int PRV_CurIndex;
extern int PRV_SendDataLen;
extern int IsUpGrade;
extern time_t Probar_time[DEV_CHANNEL_NUM];

//////////////////////////////////////解码器end///////////////////////////////


//////////////////////////////////////远程回放///////////////////////////////
#define G726_BPS MEDIA_G726_16K         /* MEDIA_G726_16K, MEDIA_G726_24K ... */
#define AMR_FORMAT AMR_FORMAT_MMS  /* AMR_FORMAT_MMS, AMR_FORMAT_IF1, AMR_FORMAT_IF2*/
#define AMR_MODE AMR_MODE_MR74         /* AMR_MODE_MR122, AMR_MODE_MR102 ... */
#define AUDIO_ADPCM_TYPE ADPCM_TYPE_DVI4

#define  VOMAX   (36)                          /* 视频输出通道最大值 */
#define  MAXSLAVECNT    (3)			/* 最大从片数目 */
#define  CLIPCNT    (4)			   /*  最大剪辑次数*/ 
#define  CLIPFILECNT   (2)				/* 最大保存文件列表数 */ 
#define AOCHN      (0)           			   /* 音频输出通道号 */
#define ADECHN     (1)                        /* 音频解码通道 */
#define MaxVoDevId    (2)				   /* 视频输出设备2 */
#define VOWIDTH        (1280)               /* 标准VO宽度 */
#define VOHEIGH        (720)               /* 标准VO高度 */
#define SLAVEWIDTH    (PRV_BT1120_SIZE_W)
#define SLAVEHEIGHT   (PRV_BT1120_SIZE_H)
#define Pal_QCifVdecWidth  (176)      /* P制QCif解码宽度 */
#define Pal_QCifVdecHeight  (144)      /* P制QCif解码高度 */
#define Pal_D1VdecWidth	(704)          /* P制D1 解码宽度*/
#define Pal_D1VdecHeight	 (576)        /* P制D1解码高度 */
#define _720PVdecWidth 	(1280)
#define _720PVdecHeight	(720)
#define _1080PVdecWidth 	(1920)
#define _1080PVdecHeight	(1080)
#define D1_PIXEL 	(Pal_D1VdecWidth * Pal_D1VdecHeight)
#define _720P_PIXEL ((_720PVdecWidth + 10) * (_720PVdecHeight + 10))
#define _1080P_PIXEL ((_1080PVdecWidth + 10) * (_1080PVdecHeight + 10))
#define PalDec_IntervalPts      (40000)   // P制相邻两帧数据时间戳间隔


#define buffersize  (_1080PVdecHeight * _1080PVdecWidth *2) /* 存放码流数据缓存地址大小 */
#define TESTPICBUFFSIZE		(Pal_D1VdecWidth * Pal_D1VdecHeight * 2) 
#define DEC_PCIV_VDEC_STREAM_BUF_LEN   (2 * 1024 * 1024)


enum READ_DATA_TYPE
{
	TYPE_NORMAL =1,	// 普通数据
	TYPE_NEXTIFRAME,	// 下一个I帧
	TYPE_PREIFRAME	//上一个I帧
}READ_DATA_TYPE;

enum SYN_STATE
{
	SYN_NOPLAY = 1,     // 未开始回放状态（初始状态）
	SYN_WAITPLAY,       // 等待 
	SYN_PLAYING,        //正在回放 
	SYN_OVER            // 结束 
}SYN_STATE;//通道回放状态

enum  PLAY_STATE
{
	PLAY_EXIT = 1,     	// 在普通预览界面
	PLAY_INSTANT,		//存在即时回放通道(即时回放状态)
	PLAY_ENTER,     	//进入回放界面，还未开始播放
	PLAY_PROCESS,     	//只要有通道收到回放数据，即为播放状态
	PLAY_STOP,     		//所有通道播放完成或停止播放
}PRCO_STATE;//回放状态

typedef struct         
{
	UINT8 PlayBackState;	//回放状态
	UINT8 IsSingle;			//是否单通道回放
   	UINT8 DBClickChn;        //单路放大画面通道 
   	UINT8 bISDB;             //是否单路放大回放 0非 1是
   	UINT8 FullScreenId;    	//是否全屏显示
   	UINT8 ImagCount;		//回放画面数
   	UINT8 IsPlaySound;
	UINT8 IsPause;
	UINT8 InstantPbChn;
	UINT8 ZoomChn;
	UINT8 IsZoom;
   	HI_S32 SubWidth;
	HI_S32 SubHeight;	
}g_PlayInfo; //回放信息结构体 
typedef struct
{
   UINT8 FullScreenId;
   UINT8 ImagCount;
   UINT8 Single;
   UINT8 VoChn;
   UINT8 flag;
}PRV_MccPBCtlReq;
typedef struct
{
  int result;
  UINT8 flag;
}PRV_MccPBCtlRsp;


typedef struct
{
	UINT8 CurPlayState;     // 当前回放状态
	UINT8 CurSpeedState;    // 当前回放速度
	UINT8 QuerySlaveId;
	UINT8 SendDataType;		 //当前通道送数据类型 
	UINT8 RealType;			//实时回放类型 
	UINT8 SynState;         //多路回放通道所处的状态
	UINT8 bIsResetFile;
}g_ChnPlayStateInfo;  //各个通道回放信息结构体



typedef struct
{
	UINT8   SlaveId;		 	// 从片Id 
	UINT8   IsSingle;		    //是否单通道回放
	UINT8   ImageCount;         // 回放画面数 
	UINT8   StreamChnIDs;		
	UINT16  subwidth;
	UINT16  subheight;
	UINT8   reserve[4];
} PlayBack_MccOpenReq;

typedef struct
{
	UINT8 SlaveId;		 //从片Id 
	int result_DEV; 	 // 回放响应，-1表示失败，0表示成功 
	UINT8 reserve[2];
} PlayBack_MccOpenRsp;

typedef struct
{
	int subwidth;
	int subheight;
	int bIsSingle; 
	int FullScreenId;
}PlayBack_MccFullScreenReq;

typedef struct
{
	int SlaveId;
}PlayBack_MccFullScreenRsp;

typedef struct
{
	int VoChn;
}PlayBack_MccGetVdecVoInfoReq;

typedef struct
{
	int VoChn;
	int Result;
	HI_U32 LeftStreamFrames;
	HI_U32 DecodeStreamFrames;
	HI_U64 u64CurPts;	
}PlayBack_MccGetVdecVoInfoRsp;

typedef struct
{
	int VoChn;
}PlayBack_MccQueryStateReq;

typedef struct
{
	int VoChn;
}PlayBack_MccQueryStateRsp;

typedef struct
{
	int bIsFullScreen;
	int bIsSingleShow;
	int VoChn;
	int bIsRsp;
}PlayBack_MccZoomReq;

typedef struct
{
	int SlaveId;
	int result;
}PlayBack_MccZoomRsp;

typedef struct
{
	int VoChn;
}PlayBack_MccCleanVoChnReq;

 typedef struct
 {
	 int VoChn;
 }PlayBack_MccPauseReq;

 typedef struct
 {
	 int VoChn;
	 int result;
 }PlayBack_MccPauseRsp;

 typedef struct
{
	int  SlaveId;
	int  EncType;			//解码通道解码类型
	int  VoChn;
	int  VdecChn;
	int  s32VdecHeight;		//分辨率:高
	int  s32VdecWidth;		//分辨率:宽
}PlayBack_MccPBCreateVdecReq;

typedef struct
{
	int SlaveId;		 	//从片Id 
	int VoChn;
	int VdecChn;
	int Result; 	 		//回放响应，-1表示失败，0表示成功 
}PlayBack_MccPBCreateVdecRsp;


typedef struct
{
	int SlaveId;
	int EncType;
	int VoChn;
	int VdecChn;
	int s32VdecHeight;		//分辨率:高
	int s32VdecWidth;		//分辨率:宽
}PlayBack_MccPBReCreateVdecReq;

typedef struct
{
	int VdecChn;      	//表示需要重新创建的通道号 
	int Result;      	//创建通道结果；0：成功  -1：失败 
}PlayBack_MccPBReCreateVdecRsp;

typedef struct
{
	int VdecChn;
}PlayBack_MccPBDestroyVdecReq;

typedef struct
{
	int VdecChn;
	int Result;
}PlayBack_MccPBDestroyVdecRsp;


typedef struct
{
	int  SlaveId;		 /* 从片Id */
	int  ImShowCount;         /* 回放画面数 */
	int  subwidth;
	int  subheight;
	int  IsPlay[DEV_DEC_TOTALVOCHN];  /* 各输出通道回放标识，1表示回放，0表示不回放 */
	int  PosId[DEV_DEC_TOTALVOCHN];   /* 各个输出通道位置 */
}PlayBack_MccReSetVoAttrReq;

typedef struct
{
	int  SlaveId;
	int  VoChn;
}PlayBack_MccProsBarReq;

typedef struct
{
	int  SlaveId;
	int  VoChn;
	int  Result;
}PlayBack_MccProsBarRsp;

//////////////////////////////////////远程回放end////////////////////////////


//----------------------------------------------
//OSD字符显示接口函数
//
//
int PRV_GetVoChnIndex(int VoChn);
int PRV_VoChnIsInCurLayOut(VO_DEV VoDev, VO_CHN VoChn);

int Prv_OSD_Show(unsigned char devid,unsigned char on);
int OSD_Set_Rec_Range_NP(unsigned char np_flag);
int OSD_Get_Rec_Range_Ch(unsigned char rec_group,int chn,int w,int h);
int OSD_Get_Preview_param(unsigned char devid,int w,int h,unsigned char ch_num,enum PreviewMode_enum prv_mode,unsigned char *pOrder);
int OSD_Set_NameType( int *pNameType);
int OSD_Compare_NameType( int *pNameType);
int OSD_Update_GroupName();

//int Prv_Set_Flicker(unsigned char on);

int Prv_Disp_OSD(unsigned char devid);
int Prv_OSD_Close_fb(unsigned char devid);
int Prv_OSD_Open_fb(unsigned char devid);	

int OSD_init(unsigned char time_type);
int OSD_Set_Time(char * str, char * qstr);
int OSD_Ctl(unsigned char ch,unsigned char on,unsigned char type);
//int OSD_Set_Time_xy(unsigned char ch,int x,int y);
int OSD_Set_Ch(unsigned char ch, char * str);
//int OSD_Set_CH_xy(unsigned char ch,int x,int y);
int OSD_Set_xy(unsigned char ch,int name_x,int name_y,int time_x,int time_y);

int Prv_Rec_Slave_OsdStr_Create(unsigned char ch,PRV_VO_SLAVE_STAT_S *p_Slave_info);



//***********************************************
//OSD_MASK
//
//
int OSD_Mask_update(unsigned char ch,const PRM_SCAPE_RECT* prect,unsigned char cov_num);
int OSD_Mask_Ctl(unsigned char ch,unsigned char on);
int OSD_Mask_disp_Ctl(unsigned char ch,unsigned char on);
int OSD_Mask_Ch_init(unsigned char ch);
int OSD_Mask_init(PPRV_VO_SLAVE_STAT_S pslave);
int OSD_Mask_Ch_Close(unsigned char ch);
int OSD_Mask_Close(void);
HI_S32 PRV_ReCreateVdecChn(HI_S32 chn, HI_S32 EncType, HI_S32 new_height, HI_S32 new_width, HI_U32 u32RefFrameNum, HI_S32 NewVdeCap);

int PRV_GetDoubleIndex();
HI_S32 PRV_GetPrvStat();
int PRV_GetDoubleToSingleIndex();
HI_S32 PRV_CreateVdecChn_EX(HI_S32 chn);
HI_BOOL PRV_GetVoiceTalkState();
HI_VOID PRV_FindMasterChnReChooseSlave(int ExtraCap, int index, int TmpIndex[]);
int PRV_FindMaster_Min(int ExtraCap, int index, int TmpIndex[]);
HI_VOID PRV_MasterChnReChooseSlave(int index);
void PRV_PBStateInfoInit(int chn);
void PRV_PBPlayInfoInit();
HI_S32 PRV_AUDIO_AoBindAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn);
HI_S32 PRV_PreviewInit(HI_VOID);
int PRV_SetPreviewVoDevInMode(int s32ChnCount);
void PlayBack_GetPlaySize(HI_U32 *Width, HI_U32 *Height);
HI_S32 PlayBack_QueryPbStat(HI_U32 Vochn);

extern int tw2865Init(unsigned char sysmode);
extern int Preview_GetAVstate(unsigned char ch);
extern HI_S32 PRV_TW2865_CfgV(VIDEO_NORM_E enVideoMode,VI_WORK_MODE_E enWorkMode);
extern HI_S32 PRV_TW2865_CfgAudio(AUDIO_SAMPLE_RATE_E enSample);
extern int Preview_SetVideo_x(unsigned char ch,unsigned char x_data);
extern int Preview_SetVideo_y(unsigned char ch,unsigned char y_data);
extern int Preview_SetVideo_Hue(unsigned char ch,unsigned char hue_data);
extern int Preview_SetVideo_Cont(unsigned char ch,unsigned char cont_data);
extern int Preview_SetVideo_Brt(unsigned char ch,unsigned char brt_data);
extern int Preview_SetVideo_Sat(unsigned char ch,unsigned char sat_data);
extern int Preview_SetVideoParam(unsigned char ch,const PRM_DISPLAY_CFG_CHAN *pInfo);
extern int GetVideoInputInfo(unsigned char ch,unsigned char *pInput_mode);

extern int tw2865_master_ain5_cfg(char flag);
extern int tw2865_ain_cfg(unsigned int chip, unsigned char mask);
extern int tw2865_master_pb_cfg(char flag);
extern int tw2865_master_tk_pb_switch(char flag);
extern int tw2865_special_reg_check(void);

extern HI_BOOL PRV_GetVoiceTalkState();

extern int PRV_Set_AudioMap(int ch, int pAudiomap);
extern int PRV_Set_AudioPreview_Enable(const BOOLEAN *pbAudioPreview);
extern HI_S32 PRV_AudioPreviewCtrl(const unsigned char *pchn, HI_U32 u32ChnNum);
extern HI_S32 PRV_GetVoPrvMode(VO_DEV VoDev, PRV_PREVIEW_MODE_E *pePreviewMode);//成功返回HI_SUCCESS,失败返回HI_FAILURE
//extern HI_S32 PRV_GetPrvMode(PRV_PREVIEW_MODE_E *pePreviewMode);//成功返回HI_SUCCESS,失败返回HI_FAILURE
extern HI_S32 PRV_GetVoChnRect(VO_DEV VoDev, VO_CHN VoChn, RECT_S *pstRect);
extern HI_S32 PRV_GetVoDevDispSize(VO_DEV VoDev, SIZE_S *pstSize);
extern HI_S32 PRV_GetVoDevImgSize(VO_DEV VoDev, SIZE_S *pstSize);
extern HI_S32 PRV_DisableDigChnAudio(HI_VOID);
extern HI_S32 PRV_EnableDigChnAudio(HI_VOID);
extern HI_S32 PRV_EnableAudioPreview(HI_VOID);
extern HI_S32 PRV_DisableAudioPreview(HI_VOID);
extern HI_S32 PRV_GetLocalAudioState(int chn);
extern HI_S32 PRV_LocalAudioOutputChange(int chn);
extern int PRV_TestAi(void);
extern HI_S32 PRV_TkPbSwitch(HI_S32 s32Flag);

extern HI_S32 PRV_BindGuiVo(int dev);//绑定G4图形层到输出设备。dev只能为0-HD或1-AD

extern int PRV_OSD_SetGuiShow(unsigned int flag);
extern int OSD_G1_close(void);
extern int OSD_G1_open(void);
extern int Get_Fb_param_exit(void);

extern int Fb_clear_step1(void);
extern int Fb_clear_step2(void);
extern void set_TimeOsd_xy();
extern int get_OSD_param_init(PPRV_VO_SLAVE_STAT_S pSlave_state);
#if defined(Hi3531)||defined(Hi3535)
HI_S32 PRV_VO_UnBindVpss(VO_DEV VoDev,VO_CHN VoChn,VPSS_GRP VpssGrp,VPSS_CHN VpssChn);
HI_S32 PRV_VO_BindVpss(VO_DEV VoDev, VO_CHN VoChn, VPSS_GRP VpssGrp, VPSS_CHN VpssChn);
extern int FBSetAlphaKey(unsigned char alpha,int enable);
#endif
extern int Slave_Get_Time_InitInfo(Rec_Osd_Time_Info *pSlave_Time_Info,int Info_len);
extern void ScmChnCtrlReq(int flag);
extern int Ftpc_PlayFileCurTime(UINT8 ImageId, PRM_ID_TIME *StartPrmTime, PRM_ID_TIME *EndPrmTime);
extern int Ftpc_PlayFileCurTime_Sec(UINT8 ImageId, time_t *StartTime, time_t *EndTime);
extern int Ftpc_PlayFileAllTime(UINT8 ImageId, PRM_ID_TIME *StartPrmTime, PRM_ID_TIME *EndPrmTime);
extern HI_S32 PRV_AUDIO_AoUnbindAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif
