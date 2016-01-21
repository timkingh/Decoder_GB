/******************************************************************************

  Copyright (C), Star-Net

 ******************************************************************************
  File Name     : disp_api.c
  Version       : Initial Draft
  Author        : 
  Created       : 2010/06/09
  Description   : preview module implement file
  History       : 针对6116进行相应的修改，配置界面不同，功能上需要进行修改
  1.Date        : 2011.3.11
    Author      : chenyao
    Modification: Created file
	Modification: NVR	by luofeng
	Modification: Decorder	by luofeng
******************************************************************************/


/************************ HEADERS HEAR *************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/mman.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <mpi_pciv.h>
#include <mpi_vi.h>
#include <hi_comm_vo.h>
#include "disp_api.h"
#include "dec_api.h"
#include "PlayBack_api.h"
#include "mdin241.h"
#include "hifb.h"
#include "sample_common.h"

#define PRV_BT1120_SIZE_H_P	PRV_BT1120_SIZE_H
#define PRV_BT1120_SIZE_H_N	PRV_BT1120_SIZE_H
#define ALIGN_BACK(x, a)              ((a) * (((x) / (a))))
STATIC HI_S32 PRV_VoInit(HI_VOID);

#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* End of #ifdef __cplusplus */

//4路gui使用的设备号为1，这样在同显时，影响不大，需要一些匹配，但是不同显时，就不行了，需要改动

/************************ DATA TYPE HEAR *************************/
//是否接入从片
#if defined(SN6116HE) || defined(SN6116LE) || defined(SN6108HE) || defined(SN8616D_LE)|| defined(SN8616M_LE) || defined(SN9234H1)
#define SN_SLAVE_ON
#endif

//从片消息发送标志
#define SN_SLAVE_MSG 1
#if defined(Hi3531)||defined(Hi3535)
#define PIP_VIDEOLAYER
#endif



/************************ GLOBALS HEAR *************************/
#if defined(SN6104) || defined(SN8604D) || defined(SN8604M) || defined(SN6108) || defined(SN8608D) || defined(SN8608M)
static MPP_SYS_CONF_S s_stSysConfDflt = {
	.u32AlignWidth = 64,

};
#else
static MPP_SYS_CONF_S s_stSysConfDflt = {
	.u32AlignWidth = 16,	/* 16 or 64 */	
};
#endif


extern int ScmGetIsListCtrlStat();

/************************************************************************/
/*   《Hi3511／Hi3512 媒体处理软件开发指南.pdf》 P(6-9):

	视频缓存池的功能：主要向媒体业务提供大块物理内存，负责内存的分配和回收，充
	分发挥内存缓存池的作用，让物理内存资源在各个媒体处理模块中合理使用。MPP 系
	统从Hi3511/Hi3512 保留内存区MMZ（Media Memory Zone）中申请物理内存创建视
	频缓存池，缓存池由大小相等、物理地址连续的缓存块组成。假设缓存块个数用
	BlkCnt 表示，缓存块大小用BlkSize 表示，那么创建一个缓存池的过程即为向MMZ 申
	请大小为BlkCnt%BlkSize 的连续物理内存空间，再将缓存池等分为BlkCnt 个缓存块。
	所有缓存池由MPP 系统的VB 模块管理，其他业务模块再向其申请缓存块以获取相应
	内存资源。

	缓存池分为公共缓存池和私有缓存池，如图6-3 所示。MPP 系统初始化前，必须先初
	始化所有公共缓存池，即用户调用HI_MPI_VB_SetConf 接口对公共缓存池内的缓存块
	个数和缓存块大小进行配置，再调用HI_MPI_VB_Init 接口将缓存池初始化（系统内部
	从MMZ 中申请内存创建所有公共缓存池）；MPP 系统初始化之后，各模块根据具体
	业务创建私有缓存池以分配相关资源，另外，用户调用HI_MPI_VB_CreatePool 创建的
	缓存池也是私有缓存池。
  
	公共缓存池内的缓存块主要供VI、VENC 及PCIV 模块缓存图像Buffer。系统将从所
	有公共缓存块搜寻出空闲的最接近大小的缓存块供各模块使用，因此根据具体业务配
	置出合适大小的缓存块能有效的利用系统内存资源，例如系统运行时VI 会有D1、
	Half-D1、CIF 等大小的通道配置，那么就需要分别配置出这些大小的缓存池，以图像
	像素格式YUV420 为例，BlkSize = Stride * Height * 1.5。

	公共缓存池配置中另外一项配置为缓存池中缓存块的个数，如果配置数目不够，将可
	能导致MPP 模块的正常业务受到影响（例如无法捕获图像、编码丢帧等）。目前缓存
	块个数可以按照以下原则配置：
	# 每个 VI 通道需要3 个VI 帧图像大小的缓存块。
	# 每个视频编码通道组需要 3 个主码流大小的缓存块（因此双码流和主次码流的配
	置是不一样的）。
	# 每个与 VI 绑定的VO 通道需要2 个VI 通道大小的缓存块。
	# 主片每个 PCIV 通道需要的缓存块个数即为配置的PCI 的Buffer 个数，缓存块大
	小为配置的PCI 目标图像大小。
	# 需要考虑用户调用 HI_MPI_VB_GetBlock 从公共缓存池中获取的缓存块所占用的
	资源。

	由于缓存块可以被多个模块间共用，因此实际需要的缓存块可能比以上说明的要少。
	一般情况下，参考以上说明进行配置即可，例如单片1 路CIF 的编码加预览业务需要
	的CIF 大小缓存块个数为：3+3+2=8 块；如果对内存资源利用率有较高要求，可以根
	据实际业务负荷将每通道的缓存块数目减少1~2 块，可以通过查看/proc/umap/vb 来了
	解系统实际运行中视频缓存池的使用情况，通过查看/proc/umap/vi 中的VbFail 了解VI
	是否有获取缓存块失败。

	私有缓存池主要用于视频编码、视频解码模块内部为每一路通道缓存帧图像Buffer，
	而私有缓存池来源于空闲MMZ 空间，另外MPP 系统中也会单独从MMZ 中获取内
	存，如果空闲MMZ 空间不足，将导致创建通道等功能启用失败，因此用户配置MMZ
	时，除了公共缓存池外还要预留一定内存空间。目前MPP 系统内除公共缓存池外主要
	其他MMZ 内存资源需求如下：
	# 创建一路 H.264 编码通道，需要MMZ 空间为图像大小%4。
	# 创建一路 H.264 解码通道，需要MMZ 空间为图像大小%（参考帧数目+4）。
	# 创建一路 MJPEG 编码通道，需要MMZ 空间约200K 左右。
	# 创建一路 MJPEG 解码通道，需要MMZ 空间为图像大小%3。
	# 其他如音频、MD、VPP 等模块也需要从MMZ 中申请适当内存，总共约5M 左右。
                                                                  */
/************************************************************************/
/* 16CIF Series Products: 
 *     VI: 6D1 + 10CIF  
 *     VencGroup: 16CIF(主次码流)
 *     VoChn: 3Dev 16Chn + 16Chn + 1Chn
 *     PCIV: none
*/

#if defined(SN9234H1)
static VB_CONF_S s_stVbConfDflt = {
	.u32MaxPoolCnt = VB_MAX_POOLS,
	.astCommPool = {
		//	{704 * 576 * 2, 16 * 2},		/*VB[0]: D1*/
		//	{1280 * 720 * 2, 8 * 2},		/*VB[0]: 720P*/
		//	{1920 * 1088 * 2, 4 * 2},	/*VB[0]: 1080P*/
				
		//	{704 * 576 * 2, 10},		/*VB[1]: 2CIF*/
		//	{352 * 288 * 1.5, 80},			/*VB[2]: CIF*/

		//	{174 * 144 * 1.5, 24},			/*VB[3]: 1/16D1*/
		//	{720 * 576 * 2, 10},			/*VB[4]: D1*/
		//	{240 * 192 * 2, 0},			/*VB[5]: 1/9D1*/
		//	{360 * 288 * 2, 0},			/*VB[6]: 1/4D1*/
		//	{800 * 600 * 2, 0}				/*VB[7]: 800*600*/
		//	{PRV_BT1120_SIZE_W * PRV_BT1120_SIZE_H* 2, 25},			/*PCI DMA*/
			{1920 * 1088 * 2, 10},			/*PCI DMA*/
		
 		//	{1280 * 1024 * 2, 25},			/*VB[3]: 1280*1024*/
		// 	{1440 * 900 * 2, 0},			/*VB[4]: 1440*900*/
		// 	{1366 * 768 * 2, 0},			/*VB[5]: 1366*768*/
		// 	{1024 * 768 * 2, 2},			/*VB[6]: 1024*768*/
		// 	{800 * 600 * 2, 0}				/*VB[7]: 800*600*/
		},
};

#else
static VB_CONF_S s_stVbConfDflt = {
	.u32MaxPoolCnt = VB_MAX_POOLS,
	.astCommPool = {
			{720 * 576 * 2, 5},
		//	{720 * 576 * 1.5, 16 * 2},	/*VB[0]: D1*/
		//	{1280 * 720 * 2, 8 * 2},		/*VB[0]: 720P*/
		//	{1920 * 1080 * 2, 4 * 2},		/*VB[0]: 1080P*/
				
		//	{704 * 576 * 2, 10},			/*VB[1]: 2CIF*/
		//	{352 * 288 * 1.5, 80},			/*VB[2]: CIF*/

		//	{174 * 144 * 1.5, 24},			/*VB[3]: 1/16D1*/
		//	{720 * 576 * 2, 10},			/*VB[4]: D1*/
		//	{240 * 192 * 2, 0},			/*VB[5]: 1/9D1*/
		//	{360 * 288 * 2, 0},			/*VB[6]: 1/4D1*/
		//	{800 * 600 * 2, 0}				/*VB[7]: 800*600*/
		//	{PRV_BT1120_SIZE_W * PRV_BT1120_SIZE_H* 1.5, 25},			/*PCI DMA*/
 		//	{1280 * 1024 * 2, 3},			/*VB[3]: 1280*1024*/
		// 	{1440 * 900 * 2, 0},			/*VB[4]: 1440*900*/
		// 	{1366 * 768 * 2, 0},			/*VB[5]: 1366*768*/
		// 	{1024 * 768 * 2, 2},			/*VB[6]: 1024*768*/
		// 	{800 * 600 * 2, 0}				/*VB[7]: 800*600*/
		},
	
};
#endif

static PRV_STATE_INFO_S s_State_Info =
{
	.bIsInit = 0,
	.bslave_IsInit =0,
	.bIsReply = 1,
	.bIsNpfinish = 0,
	.bIsOsd_Init = 0,
	.bIsRe_Init = 0,
	.bIsVam_rsp = 1,	//	默认状态为已回复消息
	.TimeoutCnt = 0,
	.bIsTimerState = 0,
	.bIsSlaveConfig = 0,
	.f_timer_handle = -1,
	.g_zoom_first_in = 0,
	.Prv_msg_Cur = NULL,
};
/*static*/ PRV_VO_SLAVE_STAT_S s_slaveVoStat={
	.enPreviewMode = PRV_VO_MAX_MOD,
	.s32PreviewIndex = 0,
	.s32SingleIndex = 0,
	.bIsSingle = HI_FALSE,
	.enVideoNorm = 0,  //N/P制式配置：0-PAL, 1-NTSC
};

#if defined(SN9234H1)
static PRV_VO_DEV_STAT_S s_astVoDevStatDflt[] = {
	{/* HD */
		.stVoPubAttr = {
				.u32BgColor = 0x000000,									//设备背景色，表示方法RGB888。
				.enIntfType = VO_INTF_BT1120,								//接口类型典型配置
				.enIntfSync = VO_OUTPUT_1080P25,					//接口时序典型配
				.stSyncInfo = {0},										//自定义接口时序结构体
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 1024, 768},					//视频显示区域矩形结构体。
				.stImageSize	= {PRV_IMAGE_SIZE_W, PRV_IMAGE_SIZE_H_P},							//图像分辨率结构体，即合成画面尺寸。
				.u32DispFrmRt	= 25,									//视频显示帧率。
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_422,		//视频层输入像素格式，SPYCbCr420 或者SPYCbCr422。
				.s32PiPChn		= VO_DEFAULT_CHN,						//画面合成路径标识。默认值为VO_DEFAULT_CHN。
			},
		
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = PRV_VO_MAX_MOD,
		.enCtrlFlag = PRV_CTRL_BUTT,			 
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	}/* END HD */,

	{/* AD */
		.stVoPubAttr = {
				.u32BgColor = 0x000000,
				.enIntfType = VO_INTF_CVBS,
				.enIntfSync = VO_OUTPUT_PAL,
				.stSyncInfo = {0},
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 720-PRV_CVBS_EDGE_CUT_W*2, 576-PRV_CVBS_EDGE_CUT_H*2},/*此处修正SN6104在CVBS上的GUI超出显示范围的BUG。*/
				.stImageSize	= {PRV_IMAGE_SIZE_W, PRV_IMAGE_SIZE_H_P},
				.u32DispFrmRt	= 25,
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,
				.s32PiPChn		= VO_DEFAULT_CHN,
			},
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = PRV_VO_MAX_MOD,
		.enCtrlFlag = PRV_CTRL_BUTT,			 
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	}/* END AD */,

	{/* SD */
		.stVoPubAttr = {
				.u32BgColor = 0x000000,
				.enIntfType = VO_INTF_CVBS,
				.enIntfSync = VO_OUTPUT_PAL,
				.stSyncInfo = {0},
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 720-PRV_CVBS_EDGE_CUT_W*2, 576-PRV_CVBS_EDGE_CUT_H*2},	
				.stImageSize	= {PRV_IMAGE_SIZE_W, PRV_IMAGE_SIZE_H_P},
				.u32DispFrmRt	= 25,
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,
				.s32PiPChn		= VO_DEFAULT_CHN,
			},
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = SingleScene,//SixteenScene,
		.enCtrlFlag = PRV_CTRL_BUTT,
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	}/* END SD */
};
#elif defined(SN9300)
static PRV_VO_DEV_STAT_S s_astVoDevStatDflt[] = {
	{/* HD */


		.stVoPubAttr = {
				.u32BgColor = 0x000000,									//设备背景色，表示方法RGB888。
				.enIntfType = VO_INTF_VGA |VO_INTF_HDMI,								//接口类型典型配置
				.enIntfSync = VO_OUTPUT_1024x768_60,					//接口时序典型配
				.stSyncInfo = {0},										//自定义接口时序结构体
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 1024, 768},					//视频显示区域矩形结构体。
				.stImageSize	= {1024, 768},							//图像分辨率结构体，即合成画面尺寸。
				.u32DispFrmRt	= 25,									//视频显示帧率。
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,		//视频层输入像素格式，SPYCbCr420 或者SPYCbCr422。?
				.bDoubleFrame = HI_FALSE,
				.bClusterMode = HI_FALSE,
			},
		
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = PRV_VO_MAX_MOD,
		.enCtrlFlag = PRV_CTRL_BUTT,			 
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	}/* END HD */,

	{/* AD */
		.stVoPubAttr = {
				.u32BgColor = 0x000000,									//设备背景色，表示方法RGB888。
				.enIntfType = VO_INTF_VGA | VO_INTF_HDMI,								//接口类型典型配置
				.enIntfSync = VO_OUTPUT_1080P60,					//接口时序典型配
				.stSyncInfo = {0},										//自定义接口时序结构体
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 1024, 768},					//视频显示区域矩形结构体。
				.stImageSize	= {PRV_IMAGE_SIZE_W, PRV_IMAGE_SIZE_H_P},							//图像分辨率结构体，即合成画面尺寸。
				.u32DispFrmRt	= 25,									//视频显示帧率。
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,		//视频层输入像素格式，SPYCbCr420 或者SPYCbCr422。?
				.bDoubleFrame = HI_FALSE,
				.bClusterMode = HI_FALSE,
			},
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = PRV_VO_MAX_MOD,
		.enCtrlFlag = PRV_CTRL_BUTT,			 
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	}/* END AD */,

	{/* SD */
		.stVoPubAttr = {
				.u32BgColor = 0x000000,
				.enIntfType = VO_INTF_CVBS,
				.enIntfSync = VO_OUTPUT_PAL,
				.stSyncInfo = {0},
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 720-PRV_CVBS_EDGE_CUT_W*2, 576-PRV_CVBS_EDGE_CUT_H*2},	
				.stImageSize	= {PRV_IMAGE_SIZE_W, PRV_IMAGE_SIZE_H_P},
				.u32DispFrmRt	= 25,
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,
				.bDoubleFrame = HI_FALSE,
				.bClusterMode = HI_FALSE,
			},
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = SingleScene,//SixteenScene,
		.enCtrlFlag = PRV_CTRL_BUTT,
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	}
};

#else
static PRV_VO_DEV_STAT_S s_astVoDevStatDflt[] = {
	{/* DHD0 */

		.stVoPubAttr = {
				.u32BgColor = 0x000000,									//设备背景色，表示方法RGB888。
				.enIntfType = VO_INTF_VGA |VO_INTF_HDMI,								//接口类型典型配置
				.enIntfSync = VO_OUTPUT_1024x768_60,					//接口时序典型配
				.stSyncInfo = {0},										//自定义接口时序结构体
				.bDoubleFrame = HI_FALSE,
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 1024, 768},					//视频显示区域矩形结构体。
				.stImageSize	= {1024, 768},							//图像分辨率结构体，即合成画面尺寸。
				.u32DispFrmRt	= 25,									//视频显示帧率。
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,		//视频层输入像素格式，SPYCbCr420 或者SPYCbCr422。?
			},

		
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = PRV_VO_MAX_MOD,
		.enCtrlFlag = PRV_CTRL_BUTT,			 
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	}/* END DHD0 */,

	{/* DHD1 */
		.stVoPubAttr = {
				.u32BgColor = 0x000000,									//设备背景色，表示方法RGB888。
				.enIntfType = VO_INTF_VGA | VO_INTF_HDMI,								//接口类型典型配置
				.enIntfSync = VO_OUTPUT_1080P50,					//接口时序典型配
				.stSyncInfo = {0},										//自定义接口时序结构体
				.bDoubleFrame = HI_FALSE,
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 1024, 768},					//视频显示区域矩形结构体。
				.stImageSize	= {PRV_IMAGE_SIZE_W, PRV_IMAGE_SIZE_H_P},							//图像分辨率结构体，即合成画面尺寸。
				.u32DispFrmRt	= 25,									//视频显示帧率。
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,		//视频层输入像素格式，SPYCbCr420 或者SPYCbCr422。?
			},
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = PRV_VO_MAX_MOD,
		.enCtrlFlag = PRV_CTRL_BUTT,			 
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	}/* END DHD1*/,

	{/* DSD0 */
		.stVoPubAttr = {
				.u32BgColor = 0x000000,
				.enIntfType = VO_INTF_CVBS,
				.enIntfSync = VO_OUTPUT_PAL,
				.stSyncInfo = {0},
				.bDoubleFrame = HI_FALSE,
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 720-PRV_CVBS_EDGE_CUT_W*2, 576-PRV_CVBS_EDGE_CUT_H*2},	
				.stImageSize	= {PRV_IMAGE_SIZE_W, PRV_IMAGE_SIZE_H_P},
				.u32DispFrmRt	= 25,
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,
			},
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = SingleScene,//SixteenScene,
		.enCtrlFlag = PRV_CTRL_BUTT,
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	},/* END DSD0 */
	{/* DSD1 */
		.stVoPubAttr = {
				.u32BgColor = 0x000000,
				.enIntfType = VO_INTF_CVBS,
				.enIntfSync = VO_OUTPUT_PAL,
				.stSyncInfo = {0},
				.bDoubleFrame = HI_FALSE,
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 720-PRV_CVBS_EDGE_CUT_W*2, 576-PRV_CVBS_EDGE_CUT_H*2},	
				.stImageSize	= {PRV_IMAGE_SIZE_W, PRV_IMAGE_SIZE_H_P},
				.u32DispFrmRt	= 25,
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,
			},
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = SingleScene,//SixteenScene,
		.enCtrlFlag = PRV_CTRL_BUTT,
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	},
	{/* DSD2 */
		.stVoPubAttr = {
				.u32BgColor = 0x000000,
				.enIntfType = VO_INTF_CVBS,
				.enIntfSync = VO_OUTPUT_PAL,
				.stSyncInfo = {0},
				.bDoubleFrame = HI_FALSE,
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 720-PRV_CVBS_EDGE_CUT_W*2, 576-PRV_CVBS_EDGE_CUT_H*2},	
				.stImageSize	= {PRV_IMAGE_SIZE_W, PRV_IMAGE_SIZE_H_P},
				.u32DispFrmRt	= 25,
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,
			},
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = SingleScene,//SixteenScene,
		.enCtrlFlag = PRV_CTRL_BUTT,
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	},
	{/* DSD3 */
		.stVoPubAttr = {
				.u32BgColor = 0x000000,
				.enIntfType = VO_INTF_CVBS,
				.enIntfSync = VO_OUTPUT_PAL,
				.stSyncInfo = {0},
				.bDoubleFrame = HI_FALSE,
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 720-PRV_CVBS_EDGE_CUT_W*2, 576-PRV_CVBS_EDGE_CUT_H*2},	
				.stImageSize	= {PRV_IMAGE_SIZE_W, PRV_IMAGE_SIZE_H_P},
				.u32DispFrmRt	= 25,
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,
			},
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = SingleScene,//SixteenScene,
		.enCtrlFlag = PRV_CTRL_BUTT,
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	},
	{/* DSD4 */
		.stVoPubAttr = {
				.u32BgColor = 0x000000,
				.enIntfType = VO_INTF_CVBS,
				.enIntfSync = VO_OUTPUT_PAL,
				.stSyncInfo = {0},
				.bDoubleFrame = HI_FALSE,
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 720-PRV_CVBS_EDGE_CUT_W*2, 576-PRV_CVBS_EDGE_CUT_H*2},	
				.stImageSize	= {PRV_IMAGE_SIZE_W, PRV_IMAGE_SIZE_H_P},
				.u32DispFrmRt	= 25,
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,
			},
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = SingleScene,//SixteenScene,
		.enCtrlFlag = PRV_CTRL_BUTT,
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	},
	{/* DSD5 */
		.stVoPubAttr = {
				.u32BgColor = 0x000000,
				.enIntfType = VO_INTF_CVBS,
				.enIntfSync = VO_OUTPUT_PAL,
				.stSyncInfo = {0},
				.bDoubleFrame = HI_FALSE,
			},
		.stVideoLayerAttr = {
				.stDispRect		= {0, 0, 720-PRV_CVBS_EDGE_CUT_W*2, 576-PRV_CVBS_EDGE_CUT_H*2},	
				.stImageSize	= {PRV_IMAGE_SIZE_W, PRV_IMAGE_SIZE_H_P},
				.u32DispFrmRt	= 25,
				.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_420,
			},
		.enPreviewStat = PRV_STAT_NORM,
		.enPreviewMode = SingleScene,//SixteenScene,
		.enCtrlFlag = PRV_CTRL_BUTT,
		.s32PreviewIndex = 0,
		.s32SingleIndex = 0,
		.s32AlarmChn = 0,
		.s32CtrlChn = 0,
		.bIsAlarm = HI_FALSE,
		.bIsSingle = HI_FALSE,
	}
};
#endif

#if defined(SN9234H1)
static VO_CHN_ATTR_S s_stVoChnAttrDflt = {
	.u32Priority	= PRV_DFLT_CHN_PRIORITY,					//视频通道叠加优先级，优先级高的在上层。取值范围：[0, 31]。动态属性。
	.stRect			= {0, 0, 352, 288},							//通道矩形显示区域。以屏幕的左上角为原点。该矩形的左上角座标必须是2 对齐，且该矩形区域必须在屏幕范围之内。动态属性。
	.bZoomEnable	= HI_TRUE,									//缩放开关标识。取值范围：.. HI_TRUE：将输入图像缩放成stRect 定义的尺寸在屏幕上显示。.. HI_FALSE：输入图像上剪裁stRect 定义的矩形区域进行显示。动态属性。
	.bDeflicker		= HI_FALSE,									//通道抗闪烁开关。取值范围：.. HI_TRUE：将输入图像做抗闪烁处理后显示。.. HI_FALSE：不对通道输入图像做抗闪烁处理，直接显示。动态属性。
};
#endif

//static VO_ZOOM_RATIO_S s_astZoomAttrDflt[] = {{0,0,	352,288},{0,0,	352,288}};

#if defined(Hi3520)
static VI_PUB_ATTR_S s_stViDevPubAttrDflt = {
	.enInputMode	= VI_MODE_BT656,							//视频输入接口模式。静态属性。
	.enWorkMode		= VI_WORK_MODE_4D1,							//视频输入工作模式。静态属性。
	.enViNorm		= VIDEO_ENCODING_MODE_PAL,					//接口制式。静态属性。
	.bIsChromaChn	= HI_FALSE,									//是否色度通道。
	.bChromaSwap	= HI_FALSE,									//是否色度数据交换。
};

static VI_CHN_ATTR_S s_stViChnAttrDflt = {
	.stCapRect			= {8, 0, 704, 288},						//通道捕获区域属性。动态属性。见《Hi3520／Hi3515媒体处理软件开发参考2.pdf》P3-11,3-12
	.enCapSel			= VI_CAPSEL_BOTH,						//帧场选择。动态属性。
	.bDownScale			= HI_FALSE,								//1/2 水平压缩选择。动态属性。
	.bChromaResample	= HI_FALSE,								//色度重采样选择。动态属性。
	.bHighPri			= HI_FALSE,								//高优先级选择。动态属性。
	.enViPixFormat		= PIXEL_FORMAT_YUV_SEMIPLANAR_420,		//像素格式。动态属性。
};

VO_DEV s_VoDevCtrlDflt = HD;								//默认时GUI所控制的输出设备。
static VO_DEV s_VoDevAlarmDflt = HD;						//默认报警画面弹出所在输出设备。
VO_DEV s_VoSecondDev = AD;									//当前设备VO的第2输出设备

#elif defined(Hi3535)
static VI_DEV_ATTR_S s_stViDevPubAttrDflt = {
	.enIntfMode	= VI_MODE_BT656,							//视频输入接口模式。静态属性。
	.enWorkMode = VI_WORK_MODE_1Multiplex,
	.au32CompMask[0] = 0xFF000000,
	.au32CompMask[1] = 0x0,
	.enScanMode = VI_SCAN_INTERLACED,
	.s32AdChnId[0] = -1,
	.s32AdChnId[1] = -1,
	.s32AdChnId[2] = -1,
	.s32AdChnId[3] = -1,
};
static VI_CHN_ATTR_S s_stViChnAttrDflt = {
	.stCapRect			= {8, 0, 704, 288},						//通道捕获区域属性。动态属性。见《Hi3520／Hi3515媒体处理软件开发参考2.pdf》P3-11,3-12
	.enCapSel			= VI_CAPSEL_BOTH,						//帧场选择。动态属性。
	//.bChromaResample	= HI_FALSE,								//色度重采样选择。动态属性。
	.enPixFormat		= PIXEL_FORMAT_YUV_SEMIPLANAR_420,		//像素格式。动态属性。
	.bMirror = HI_FALSE,
//	.bFilp = HI_FALSE,
//	.bChromaResample = HI_FALSE,
	.s32SrcFrameRate = 25,
	.s32DstFrameRate = 25,
};	
/*static*/ VO_DEV s_VoDevCtrlDflt = DHD0;						//默认时GUI所控制的输出设备。
static VO_DEV s_VoDevAlarmDflt = DHD0;							//默认报警画面弹出所在输出设备。
VO_DEV s_VoSecondDev = DSD0;

#else
static VI_DEV_ATTR_S s_stViDevPubAttrDflt = {
	.enIntfMode	= VI_MODE_BT656,							//视频输入接口模式。静态属性。
	.enWorkMode = VI_WORK_MODE_1Multiplex,
	.au32CompMask[0] = 0xFF000000,
	.au32CompMask[1] = 0x0,
	.enScanMode = VI_SCAN_INTERLACED,
	.s32AdChnId[0] = -1,
	.s32AdChnId[1] = -1,
	.s32AdChnId[2] = -1,
	.s32AdChnId[3] = -1,
};

static VI_CHN_ATTR_S s_stViChnAttrDflt = {
	.stCapRect			= {8, 0, 704, 288},						//通道捕获区域属性。动态属性。见《Hi3520／Hi3515媒体处理软件开发参考2.pdf》P3-11,3-12
	.enCapSel			= VI_CAPSEL_BOTH,						//帧场选择。动态属性。
	.bChromaResample	= HI_FALSE,								//色度重采样选择。动态属性。
	.enPixFormat		= PIXEL_FORMAT_YUV_SEMIPLANAR_420,		//像素格式。动态属性。
	.bMirror = HI_FALSE,
//	.bFilp = HI_FALSE,
	.bChromaResample = HI_FALSE,
	.s32SrcFrameRate = 25,
	.s32FrameRate = 25,
};
/*static*/ VO_DEV s_VoDevCtrlDflt = DHD0;						//默认时GUI所控制的输出设备。
static VO_DEV s_VoDevAlarmDflt = DHD0;							//默认报警画面弹出所在输出设备。
VO_DEV s_VoSecondDev = DSD0;									//当前设备VO的第2输出设备
#endif

HI_U32 s_u32GuiWidthDflt	= 1280;								//默认GUI宽
HI_U32 s_u32GuiHeightDflt	= 1024;								//默认GUI高

HI_U32 g_Max_Vo_Num=0;
UINT8	PRV_CurDecodeMode = 0;//当前解码模式

static HI_S32 s_s32NPFlagDflt = 0;								//系统默认N/P制式配置：0-PAL, 1-NTSC
static HI_S32 s_s32ViWorkMode = 0;//VI采集图像大小配置：0-D1, 1-CIF
static HI_S32 s_s32VGAResolution = VGA_1024X768;
#if defined(SN9234H1)
static char *s_devfb[] = {"/dev/fb0", "/dev/fb1", "/dev/fb2", "/dev/fb3", "/dev/fb4"};
static int bHaveM240 = 1;
static int CurrertPciv = 9;
static int IsOSDAlarmOn[MAX_IPC_CHNNUM] = {0};//是否有无视频信号报警图标0:无，1:有
#else
static char *s_devfb[] = {"/dev/fb0", "/dev/fb1", "/dev/fb2", "/dev/fb3", "/dev/fb4","/dev/fb5","/dev/fb6"};
#endif
UINT8 PB_Full_id = 0;
static HI_S32 OldCtrlChn = -1;
static HI_S32 IsDispInPic = 0;//是否在显示"显示管理"画中画。为了区分进入通道控制参数为1和9分支
static unsigned char  s_OSD_Time_type = 0;		//osd时间格式 

static VIDEO_FRAME_INFO_S s_stUserFrameInfo_P;		//无视频信号时，P制式下的图片信息
static VIDEO_FRAME_INFO_S s_stUserFrameInfo_N;		//无视频信号时，N制式下的图片信息

static pthread_mutex_t s_osd_mutex = PTHREAD_MUTEX_INITIALIZER;		//OSD时间信号量
static pthread_mutex_t s_Reset_vo_mutex = PTHREAD_MUTEX_INITIALIZER;	
sem_t sem_SendNoVideoPic, sem_VoPtsQuery, sem_PlayPause, sem_PrvGetData, sem_PrvSendData, sem_PBGetData, sem_PBSendData;
PRV_AUDIOINFO CurPlayAudioInfo;//当前播放音频的音频参数
static int CurAudioChn = -1;//当前播放音频的通道
static int PreAudioChn = -1;
static int CurAudioPlayStat = 0;//当前播放的音频状态(0:关,1:开)
static int IsTest = 0;//生产测试标志
static int DoubleToSingleIndex = -1;
static int LayoutToSingleChn = -1;//数字键盘控制数字健切换到单画面
static unsigned char OutPutMode = StretchMode;//显示模式:拉伸、等比例、智能
static PRV_DecodeState g_DecodeState[PRV_VO_CHN_NUM];
time_t Probar_time[DEV_CHANNEL_NUM];
PRM_LINKAGE_GROUP_CFG g_PrmLinkAge_Cfg[LINKAGE_MAX_GROUPNUM];

//标识用户已经主动选择播放某个通道的音频，此时无论是
//1、主动连接新的一路IPC，
//2、画面切换(切换后选择播放音频的通道在画面中布局)
//均不再选择播放画面中第一个有视频的通道的音频，而是默认继续播放用户选择的通道的音频
static int IsChoosePlayAudio = 0;
/* 以下是各预览布局设定，约定数值是以PRV_PREVIEW_LAYOUT_DIV为一个长和高的单位。 */
int MccCreateingVdecCount = 0;
int MccReCreateingVdecCount = 0;

static RECT_S s_astPreviewLayout1[1] = {
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1,
	}
};/* 单画面 */

static RECT_S s_astPreviewLayout2[2] = {
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 2/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 2/4,
	}
};/* 2画面 */
static RECT_S s_astPreviewLayout3[3] = {
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1,
	}
};/* 3画面 */

static RECT_S s_astPreviewLayout4[4] = {
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/2,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/2,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/2,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/2,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/2,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/2,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/2,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/2,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/2,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/2,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/2,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/2,
	}
};/* 4画面 */

static RECT_S s_astPreviewLayout5[5] = {
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	}
};/* 5画面 */

static RECT_S s_astPreviewLayout6[6] = {
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 2/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	}
};/* 6画面 */

static RECT_S s_astPreviewLayout7[7] = {
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	}
};/* 7画面 */


static RECT_S s_astPreviewLayout8[8] = {
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 3/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	}
};/* 8画面 */

static RECT_S s_astPreviewLayout9[9] = {
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/3,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/3,
	}
};/* 9画面*/

static RECT_S s_astPreviewLayout16[16] = {
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 0/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 0/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 2/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	},
	{
		.s32X		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.s32Y		= PRV_PREVIEW_LAYOUT_DIV * 3/4,
		.u32Width	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
		.u32Height	= PRV_PREVIEW_LAYOUT_DIV * 1/4,
	}
};/* 16画面*/


/************************ FUNCTIONS HEAR *************************/
#if defined(SN9234H1)
HI_S32 PRV_start_pciv(PCIV_CHN PcivChn);
STATIC HI_S32 PRV_PrevInitSpotVo( HI_U32 u32Index);
STATIC HI_S32 PRV_InitSpotVo(void);
HI_S32 PRV_RefreshSpotOsd(int chan);
#endif

STATIC HI_S32 PRV_Chn2Index(VO_DEV VoDev, VO_CHN VoChn, HI_U32 *pu32Index,VO_CHN *pOrder);
STATIC HI_S32 PRV_DisableAllVoChn(VO_DEV VoDev);
STATIC HI_S32 PRV_EnableAllVoChn(VO_DEV VoDev);

static HI_BOOL s_bIsSysInit = HI_FALSE;//HI_FALSE;

#if defined(SN9234H1)
/*************************************************
Function: //PRV_SetM240_Display
Description: // 设置M240的输入输出分辨率
Calls: //
Called By: // 
Input: // Resolution		分辨率
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
int PRV_Init_M240(void)
{

	struct stat buf;
	int result;
	result = stat("/dev/mdin240", &buf);
	if(result != 0)
	{
		perror("Problem getting information");
		if(errno == ENOENT)
		{
			bHaveM240 = 0;
			//printf("-----------Can't find mdin241 device!\n");
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "Can't find mdin241 device!\n");
		}
	}
	if(bHaveM240)
	{
		bHaveM240 = 1;
		s_astVoDevStatDflt[HD].stVoPubAttr.enIntfType 		= VO_INTF_BT1120;
		s_astVoDevStatDflt[HD].stVideoLayerAttr.enPixFormat	= PIXEL_FORMAT_YUV_SEMIPLANAR_422;	
		//printf("Detect mdin241 ok!\n");
	}	
	return 0;
}

/*************************************************
Function: //PRV_SetM240_Display
Description: // 设置M240的输入输出分辨率
Calls: //
Called By: // 
Input: // Resolution		分辨率
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
int PRV_SetM240_Display(int Resolution)
{
	int fd;
	_mdin240ioctlpara_ para;

	TRACE(SCI_TRACE_NORMAL, MOD_PRV,"PRV_SetM240_Display = %d\n", Resolution);
	//printf("--------------bHaveM240: %d\n",bHaveM240);
	
	if(!bHaveM240)
		return 0;

	para.Vadj.brightness = 145;
	para.Vadj.contrast = 150;
	para.Vadj.saturation = 128;
	para.Vadj.hue = 128;
	para.Vadj.sharpness = 15;
	
	para.Vadj.r_gain = 128; 
	para.Vadj.g_gain = 128; 
	para.Vadj.b_gain = 128; 
	para.Vadj.r_offset = 128; 
	para.Vadj.g_offset = 128; 
	para.Vadj.b_offset = 128;
	switch(Resolution)
	{
		case VGA_720P:
			//para.Vres.vi = MVI_1024x768p60;
			para.Vres.vi = MVI_720p60;
			para.Vres.vo = MVO_720p60;			
			break;
		case VGA_1080P:
			//para.Vres.vi = MVI_1024x768p60;
			para.Vres.vi = MVI_720p60;
			para.Vres.vo = MVO_1080p60;			
			break;
		case VGA_1024X768:
			//para.Vres.vi = MVI_1024x768p60;
			para.Vres.vi = MVI_720p60;
			para.Vres.vo = MVO_1024x768p60;			
			break;
		case VGA_1280X1024:
			//para.Vres.vi = MVI_1024x768p60;
			para.Vres.vi = MVI_720p60;
			para.Vres.vo = MVO_1280x1024p60;	
			break;
		case VGA_800X600:
			para.Vres.vi = MVI_800x600p60;
			para.Vres.vo = MVO_1024x768p60;			
			break;
		case VGA_1366x768:
			para.Vres.vi = MVI_1440x900p60;
			para.Vres.vo = MVO_1440x900p60;			
			break;
		case VGA_1440x900:
			para.Vres.vi = MVI_1440x900p60;
			para.Vres.vo = MVO_1440x900p60;			
			break;
	}

	fd = open("/dev/mdin240", O_RDWR|O_NONBLOCK);
	if(fd < 0)
	{
		perror("open error!\n");
		//printf("--------------Open /dev/mdin240 error\n");
		return -1;
	}
	ioctl(fd, MDIN241_IOCTL_SET_RESOLUTION, &para);
	//ioctl(fd, MDIN241_IOCTL_SET_PARAMETER, &para);
	close(fd);

	return 0;
}

#endif
#if defined(Hi3531)||defined(Hi3535)
/******************************************************************************
* function : vdec group unbind vpss chn
******************************************************************************/
HI_S32 PRV_VDEC_UnBindVpss(VDEC_CHN VdChn, VPSS_GRP VpssGrp)
{
	HI_S32 s32Ret = HI_SUCCESS;
	MPP_CHN_S stSrcChn;
	MPP_CHN_S stDestChn;

	stSrcChn.enModId = HI_ID_VDEC;
	stSrcChn.s32DevId = 0;
	stSrcChn.s32ChnId = VdChn;

	stDestChn.enModId = HI_ID_VPSS;
	stDestChn.s32DevId = VpssGrp;
	stDestChn.s32ChnId = 0;
	
//	printf("-----------------------PRV_VDEC_UnBindVpss,vdechn:%d,vpssgrp:%d\n",VdChn,VpssGrp);
	s32Ret = HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"HI_MPI_SYS_UnBind failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}

	return HI_SUCCESS;
}
/******************************************************************************
* function : Set vpss system memory location
******************************************************************************/
HI_S32 PRV_VPSS_MemConfig()
{
    HI_CHAR * pcMmzName;
    MPP_CHN_S stMppChnVpss;
    HI_S32 s32Ret, i;

    /*vpss group max is 64, not need config vpss chn.*/
    for(i=0;i<64;i++)
    {
        stMppChnVpss.enModId  = HI_ID_VPSS;
        stMppChnVpss.s32DevId = i;
        stMppChnVpss.s32ChnId = 0;

        pcMmzName = NULL;  
     
        /*vpss*/
        s32Ret = HI_MPI_SYS_SetMemConf(&stMppChnVpss, pcMmzName);
        if (HI_SUCCESS != s32Ret)
        {
            TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Vpss HI_MPI_SYS_SetMemConf ERR !\n");
            return HI_FAILURE;
        }
    }
    return HI_SUCCESS;
}


/******************************************************************************
* function : Set system memory location
******************************************************************************/
HI_S32 PRV_VDEC_MemConfig(HI_VOID)
{
    HI_S32 i = 0;
    HI_S32 s32Ret = HI_SUCCESS;

    HI_CHAR * pcMmzName;
    MPP_CHN_S stMppChnVDEC;

    /* VDEC chn max is 32*/
    for(i=0;i<32;i++)
    {
        stMppChnVDEC.enModId = HI_ID_VDEC;
        stMppChnVDEC.s32DevId = 0;
        stMppChnVDEC.s32ChnId = i;

        pcMmzName = NULL;  

        s32Ret = HI_MPI_SYS_SetMemConf(&stMppChnVDEC,pcMmzName);
        if (s32Ret)
        {
            TRACE(SCI_TRACE_NORMAL, MOD_PRV,"HI_MPI_SYS_SetMemConf ERR !\n");
            return HI_FAILURE;
        }
    }  

    return HI_SUCCESS;
}



/******************************************************************************
* function : Set system memory location
******************************************************************************/
HI_S32 PRV_VO_MemConfig(VO_DEV VoDev)
{
    HI_S32 s32Ret = HI_SUCCESS;
    MPP_CHN_S stMppChnVO;
	HI_CHAR * pcMmzName;
    /* config vo dev */
    stMppChnVO.enModId  = HI_ID_VOU;
    stMppChnVO.s32DevId = VoDev;
    stMppChnVO.s32ChnId = 0;
	pcMmzName = NULL; 
    s32Ret = HI_MPI_SYS_SetMemConf(&stMppChnVO, pcMmzName);
    if (s32Ret)
    {
        TRACE(SCI_TRACE_HIGH, MOD_VAM, "HI_MPI_SYS_SetMemConf ERR !\n");
        return HI_FAILURE;
    } 
    
    return HI_SUCCESS;
}

/******************************************************************************
* function : Set region memory location
******************************************************************************/
HI_S32 PRV_RGN_MemConfig(HI_VOID)
{
    HI_S32 i = 0;
    HI_S32 s32Ret = HI_SUCCESS;

    HI_CHAR * pcMmzName;
    MPP_CHN_S stMppChnRGN;

	/*the max chn of vpss,grp and venc is 64*/
    for(i=0; i<RGN_HANDLE_MAX; i++)
    {
        stMppChnRGN.enModId  = HI_ID_RGN;
        stMppChnRGN.s32DevId = 0;
        stMppChnRGN.s32ChnId = 0;

        pcMmzName = NULL;  
   
        s32Ret = HI_MPI_SYS_SetMemConf(&stMppChnRGN,pcMmzName);
        if (s32Ret)
        {
           	TRACE(SCI_TRACE_HIGH, MOD_VAM,"HI_MPI_SYS_SetMemConf ERR !\n");
            return HI_FAILURE;
        }
    }
    
    return HI_SUCCESS;
}


/******************************************************************************
* function : Set venc memory location
******************************************************************************/
HI_S32 PRV_VENC_MemConfig(HI_VOID)
{
    HI_S32 i = 0;
    HI_S32 s32Ret;

    HI_CHAR * pcMmzName;
    MPP_CHN_S stMppChnVENC;
    MPP_CHN_S stMppChnGRP;

    /* group, venc max chn is 64*/
    for(i=0;i<64;i++)
    {
        stMppChnGRP.enModId  = HI_ID_GROUP;
        stMppChnGRP.s32DevId = i;
        stMppChnGRP.s32ChnId = 0;

        stMppChnVENC.enModId = HI_ID_VENC;
        stMppChnVENC.s32DevId = 0;
        stMppChnVENC.s32ChnId = i;

        pcMmzName = NULL;  

        /*grp*/
        s32Ret = HI_MPI_SYS_SetMemConf(&stMppChnGRP,pcMmzName);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("HI_MPI_SYS_SetMemConf failed with %#x!\n", s32Ret);
            return HI_FAILURE;
        } 

        /*venc*/
        s32Ret = HI_MPI_SYS_SetMemConf(&stMppChnVENC,pcMmzName);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("HI_MPI_SYS_SetMemConf with %#x!\n", s32Ret);
            return HI_FAILURE;
        }
    }
    
    return HI_SUCCESS;
}
#endif


#if defined(SN9234H1)
//UI增加输出参数的配置界面
int PRV_SetVo_Display(BYTE brightness, BYTE contrast, BYTE saturation, BYTE hue, BYTE sharpness)
{
#if 0
	int fd;
	_mdin240ioctlpara_ para;
	//printf("PRV_SetVo_Display: %d,%d,%d,%d,%d\n", brightness, contrast, saturation, hue, sharpness);
	para.Vadj.brightness = brightness;
	para.Vadj.contrast = contrast;
	para.Vadj.saturation = saturation;
	para.Vadj.hue = hue;
	para.Vadj.sharpness = sharpness;
	para.Vadj.r_gain = 128; 
	para.Vadj.g_gain = 128; 
	para.Vadj.b_gain = 128; 
	para.Vadj.r_offset = 128; 
	para.Vadj.g_offset = 128; 
	para.Vadj.b_offset = 128;
	fd = open("/dev/mdin240", O_RDWR|O_NONBLOCK);
	if(fd < 0)
	{
		perror("open error!\n");
		//printf("--------------Open /dev/mdin240 error\n");
		return -1;
	}
	ioctl(fd, MDIN241_IOCTL_SET_PARAMETER, &para);
	close(fd);
	//printf("end PRV_SetVo_Display");
	return 0;
#endif
	VO_CSC_S stPubCSC;
	stPubCSC.enCSCType = VO_CSC_LUMA;
	stPubCSC.u32Value = brightness;
	CHECK_RET(HI_MPI_VO_SetDevCSC(HD, &stPubCSC));

	stPubCSC.enCSCType = VO_CSC_CONTR;
	if(contrast <= 2)// 1:显示异常，0:全黑，规避
		contrast = 2;
	stPubCSC.u32Value = contrast; 
	CHECK_RET(HI_MPI_VO_SetDevCSC(HD, &stPubCSC));

	stPubCSC.enCSCType = VO_CSC_HUE;	
	if(hue == 99)//99显示异常，规避
		hue = 100;
	stPubCSC.u32Value = hue; 
	CHECK_RET(HI_MPI_VO_SetDevCSC(HD, &stPubCSC));

	stPubCSC.enCSCType = VO_CSC_SATU;
	stPubCSC.u32Value = saturation; 
	CHECK_RET(HI_MPI_VO_SetDevCSC(HD, &stPubCSC));	
	return 0;

}

#else
//UI增加输出参数的配置界面
int PRV_SetVo_Display(BYTE brightness, BYTE contrast, BYTE saturation, BYTE hue, BYTE sharpness)
{
	VO_CSC_S stPubCSC;
	stPubCSC.enCscMatrix = VO_CSC_MATRIX_IDENTITY;
	stPubCSC.u32Luma = brightness;
	stPubCSC.u32Contrast = contrast;
	stPubCSC.u32Hue = hue;
	stPubCSC.u32Satuature = saturation;
#if defined(Hi3535)
	CHECK_RET(HI_MPI_VO_SetVideoLayerCSC(VHD0, &stPubCSC));
#else
	CHECK_RET(HI_MPI_VO_SetDevCSC(DHD0, &stPubCSC));
#endif
	
	return 0;
}
#endif

int PRV_GetPlayBackState()
{
	if(s_astVoDevStatDflt[DHD0].enPreviewStat == PRV_STAT_PB)
	{
		return 1;
	}

	return OK;
}

int PRV_GetDoubleIndex()
{
	if(s_astVoDevStatDflt[DHD0].s32DoubleIndex == 1)
	{
		return 1;
	}

	return OK;
}
int PRV_GetSingleIndex()
{
	if(s_astVoDevStatDflt[DHD0].s32DoubleIndex == 1)
	{
		return s_astVoDevStatDflt[DHD0].s32SingleIndex;
	}
	else
	{
		return -1;
	}
}

int PRV_GetDoubleToSingleIndex()
{
	return DoubleToSingleIndex;
}

#if defined(Hi3535)
HI_VOID	SAMPLE_COMM_VDEC_ModCommPoolConf(VB_CONF_S *pstModVbConf, PAYLOAD_TYPE_E enType, SIZE_S *pstSize)
{
    HI_S32 PicSize=0, PmvSize=0,PicSize1=0, PmvSize1=0;
	
	memset(pstModVbConf, 0, sizeof(VB_CONF_S));    
    pstModVbConf->u32MaxPoolCnt = 8;
	
    VB_PIC_BLK_SIZE(pstSize->u32Width, pstSize->u32Height, enType, PicSize);	
    pstModVbConf->astCommPool[0].u32BlkSize = PicSize;
    pstModVbConf->astCommPool[0].u32BlkCnt  = 80;

	/* if the VDEC channel of H264 support to decode B frame, then you should  allocate PmvBuffer */
    VB_PMV_BLK_SIZE(pstSize->u32Width, pstSize->u32Height, PmvSize);
    pstModVbConf->astCommPool[1].u32BlkSize = PmvSize;
    pstModVbConf->astCommPool[1].u32BlkCnt  = 80;

	VB_PIC_BLK_SIZE(1920, 1080, enType, PicSize1);	
    pstModVbConf->astCommPool[2].u32BlkSize = PicSize1;
    pstModVbConf->astCommPool[2].u32BlkCnt  = 20;

	VB_PMV_BLK_SIZE(1920, 1080, PmvSize1);
    pstModVbConf->astCommPool[3].u32BlkSize = PmvSize1;
    pstModVbConf->astCommPool[3].u32BlkCnt  = 20;

	VB_PIC_BLK_SIZE(2048, 1536, enType, PicSize1);	
    pstModVbConf->astCommPool[4].u32BlkSize = PicSize1;
    pstModVbConf->astCommPool[4].u32BlkCnt  = 16;

	VB_PMV_BLK_SIZE(2048, 1536, PmvSize1);
    pstModVbConf->astCommPool[5].u32BlkSize = PmvSize1;
    pstModVbConf->astCommPool[5].u32BlkCnt  = 8;

	VB_PIC_BLK_SIZE(4000, 3000, enType, PicSize1);	
    pstModVbConf->astCommPool[6].u32BlkSize = PicSize1;
    pstModVbConf->astCommPool[6].u32BlkCnt  = 4;

	VB_PMV_BLK_SIZE(4000, 3000, PmvSize1);
    pstModVbConf->astCommPool[7].u32BlkSize = PmvSize1;
    pstModVbConf->astCommPool[7].u32BlkCnt  = 4;

}

HI_S32	SAMPLE_COMM_VDEC_InitModCommVb(VB_CONF_S *pstModVbConf)
{
	HI_S32 ret = HI_SUCCESS;
	ret = HI_MPI_VB_ExitModCommPool(VB_UID_VDEC);
	if(ret != HI_SUCCESS)
	{
		printf("HI_MPI_VB_ExitModCommPool error\n");
		return HI_FAILURE;
	}
	ret = HI_MPI_VB_SetModPoolConf(VB_UID_VDEC, pstModVbConf);
	if(ret != HI_SUCCESS)
	{
		printf("HI_MPI_VB_SetModPoolConf error\n");
		return HI_FAILURE;
	}
	ret = HI_MPI_VB_InitModCommPool(VB_UID_VDEC);
	if(ret != HI_SUCCESS)
	{
		printf("HI_MPI_VB_InitModCommPool error\n");
		return HI_FAILURE;
	}
    return HI_SUCCESS;
}
#endif


/*************************************************
Function: //PRV_SysInit
Description: // 视频层初始化
Calls: //
Called By: // Preview_Init，预览初始化调用
Input: // 无
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
int PRV_SysInit(void)
{
	CHECK(HI_MPI_SYS_Exit());
#if defined(Hi3535)
	int i = 0;
	for(i=0;i<VB_MAX_USER;i++)
	{
		 HI_MPI_VB_ExitModCommPool(i);
	}
#endif

	CHECK(HI_MPI_VB_Exit());

    CHECK_RET(HI_MPI_VB_SetConf(&s_stVbConfDflt));
	
    CHECK_RET(HI_MPI_VB_Init());

	CHECK_RET(HI_MPI_SYS_SetConf(&s_stSysConfDflt));
    
	CHECK_RET(HI_MPI_SYS_Init());
#if defined(Hi3535)
	HI_S32 s32Ret = HI_SUCCESS; 
	VB_CONF_S stVbConf;
	SIZE_S stSize;
	PAYLOAD_TYPE_E enType;
	enType = PT_H264;
	memset(&stVbConf,0,sizeof(VB_CONF_S));
	stSize.u32Width = 704;
	stSize.u32Height = 576;
	SAMPLE_COMM_VDEC_ModCommPoolConf(&stVbConf, enType, &stSize);	 
	s32Ret = SAMPLE_COMM_VDEC_InitModCommVb(&stVbConf);
	if(s32Ret != HI_SUCCESS)
	{			
		printf("init mod common vb fail for %#x!\n", s32Ret);
		return -1;;
	}
#endif	

#if defined(Hi3531)|| defined(Hi3535)
	PRV_VoInit();  //这里底色变黑	
	CHECK_RET(PRV_VPSS_MemConfig());
	CHECK_RET(PRV_VDEC_MemConfig());
	CHECK_RET(PRV_VO_MemConfig(DHD0));
	CHECK_RET(PRV_RGN_MemConfig());
#endif	
#if defined(Hi3531)
	CHECK_RET(SAMPLE_COMM_VI_MemConfig(SAMPLE_VI_MODE_16_D1));
#endif
//	CHECK_RET(PRV_VENC_MemConfig());
	s_bIsSysInit = HI_TRUE;

	RET_SUCCESS("");
}
/*************************************************
Function: //PRV_SysExit
Description: // 视频层退出
Calls: //
Called By: //exit_mpp_sys，关机时调用
Input: // 无
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_VOID PRV_SysExit(HI_VOID)
{
	CHECK(HI_MPI_SYS_Exit());
	CHECK(HI_MPI_VB_Exit());
}

#if defined(SN9234H1)
STATIC HI_S32 PRV_SetDevCsc(VO_DEV VoDev)
{
	PRM_Vo_DISPLAY_CFG stVo_DispCfg;
	stVo_DispCfg.brightness = 48;
	stVo_DispCfg.contrast = 52;
	stVo_DispCfg.saturation = 60;
	stVo_DispCfg.hue = 42;
	
	if(IsTest == 0)
	{
		if(ERROR == GetParameter(PRM_ID_VO_DISPLAY_CFG, NULL, &stVo_DispCfg, sizeof(PRM_Vo_DISPLAY_CFG), 0, SUPER_USER_ID, NULL))
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Get PRM_ID_VO_DISPLAY_CFG Parameter Error\n");
		}
	}
	PRV_SetVo_Display(stVo_DispCfg.brightness, stVo_DispCfg.contrast, stVo_DispCfg.saturation, stVo_DispCfg.hue, 0);

//	printf("========%d, %d, %d, %d============\n", stVo_DispCfg.brightness, stVo_DispCfg.contrast, stVo_DispCfg.saturation, stVo_DispCfg.hue);
	return HI_SUCCESS;
}
#else
STATIC HI_S32 PRV_SetDevCsc(VO_DEV VoDev)
{
	PRM_Vo_DISPLAY_CFG stVo_DispCfg;
	memset(&stVo_DispCfg,0,sizeof(PRM_Vo_DISPLAY_CFG));
	stVo_DispCfg.Luma = 50;
	stVo_DispCfg.contrast = 50;
	stVo_DispCfg.saturation = 50;
	stVo_DispCfg.hue = 50;
	
	if(IsTest == 0)
	{
		if(ERROR == GetParameter(PRM_ID_VO_DISPLAY_CFG, NULL, &stVo_DispCfg, sizeof(PRM_Vo_DISPLAY_CFG), 0, SUPER_USER_ID, NULL))
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Get PRM_ID_VO_DISPLAY_CFG Parameter Error\n");
		}
	}
	PRV_SetVo_Display(stVo_DispCfg.Luma, stVo_DispCfg.contrast, stVo_DispCfg.saturation, stVo_DispCfg.hue, 0);

//	printf("========Luma:%d, contrast:%d, saturation:%d, hue:%d============\n", stVo_DispCfg.Luma, stVo_DispCfg.contrast, stVo_DispCfg.saturation, stVo_DispCfg.hue);
	return HI_SUCCESS;
}
#endif
#if defined(Hi3531)||defined(Hi3535)
int PRV_SetVga_Display(BYTE brightness, BYTE contrast, BYTE saturation, BYTE hue, BYTE Gain, BYTE sharpness)
{
	VO_VGA_PARAM_S pstVgaParam;
	if(contrast < 10)
		contrast = 10;
#if defined(Hi3535)
	pstVgaParam.stCSC.enCscMatrix = 3;
	pstVgaParam.stCSC.u32Luma = brightness;
	pstVgaParam.stCSC.u32Contrast = contrast; 
	pstVgaParam.stCSC.u32Hue = hue;
	pstVgaParam.stCSC.u32Satuature = saturation;
	pstVgaParam.u32Gain = Gain;
#else
	pstVgaParam.enCscMatrix = 3;
	pstVgaParam.u32Luma = brightness;
	pstVgaParam.u32Contrast = contrast; 
	pstVgaParam.u32Hue = hue;
	pstVgaParam.u32Satuature = saturation;
	pstVgaParam.u32Gain = Gain;
#endif
	HI_MPI_VO_SetVgaParam(DHD0,&pstVgaParam);
	return HI_SUCCESS;
}
STATIC HI_S32 PRV_SetVgaParam(VO_DEV VoDev)
{
	PRM_Vo_DISPLAY_CFG stVo_DispCfg;
	memset(&stVo_DispCfg,0,sizeof(PRM_Vo_DISPLAY_CFG));
	stVo_DispCfg.VgaLuma = 50;
	stVo_DispCfg.Vgacontrast = 50; 
	stVo_DispCfg.Vgahue = 50;
	stVo_DispCfg.Vgasaturation = 50;
	stVo_DispCfg.Gain = 10;
	
	if(IsTest == 0)
	{
		if(ERROR == GetParameter(PRM_ID_VO_DISPLAY_CFG, NULL, &stVo_DispCfg, sizeof(PRM_Vo_DISPLAY_CFG), 0, SUPER_USER_ID, NULL))
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Get PRM_ID_VO_DISPLAY_CFG Parameter Error\n");
		}
	}
	PRV_SetVga_Display(stVo_DispCfg.VgaLuma, stVo_DispCfg.Vgacontrast, stVo_DispCfg.Vgasaturation, stVo_DispCfg.Vgahue,stVo_DispCfg.Gain,0);

//	printf("========VgaLuma:%d, VgaContrast:%d, VgaSatuature:%d, VgaHue:%d,Gain:%d============\n", stVo_DispCfg.VgaLuma, stVo_DispCfg.Vgacontrast, stVo_DispCfg.Vgasaturation, stVo_DispCfg.Vgahue,stVo_DispCfg.Gain);
	return HI_SUCCESS;
}
#endif
#if defined(SN9234H1)
/*************************************************
Function: //PRV_EnableVoDev
Description: //  配置开启指定的VO设备，不包含对VO上的通道的操作。
Calls: //
Called By: //PRV_VoInit，
		//PRV_ResetVoDev，
Input: // 无
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_EnableVoDev(HI_S32 DevId)
{
	if (AD == DevId || SD == DevId)
	{
		//RET_SUCCESS("");
		s_astVoDevStatDflt[DevId].stVoPubAttr.enIntfSync = (0 == s_s32NPFlagDflt) ? VO_OUTPUT_PAL : VO_OUTPUT_NTSC;
	}
	CHECK_RET(HI_MPI_VO_SetPubAttr(DevId, &s_astVoDevStatDflt[DevId].stVoPubAttr));
	if (AD == DevId)
	{	
		VO_SCREEN_FILTER_S filter;
		HI_MPI_VO_GetScreenFilter(DevId, &filter);
//		printf("HI_MPI_VO_GetScreenFilter1 filter.enHFilter = %d, filter.enVFilter=%d\n", filter.enHFilter, filter.enVFilter);
		filter.enHFilter = VO_SCREEN_HFILTER_8M; 
		filter.enVFilter = VO_SCREEN_VFILTER_8M;

		HI_MPI_VO_GetScreenFilter(DevId, &filter);
//		printf("HI_MPI_VO_GetScreenFilter2 filter.enHFilter = %d, filter.enVFilter=%d\n", filter.enHFilter, filter.enVFilter);
		
	}
	CHECK_RET(PRV_SetDevCsc(DevId));
	
	CHECK_RET(HI_MPI_VO_Enable(DevId));

	RET_SUCCESS("");
} 

#else
/*************************************************
Function: //PRV_EnableVoDev
Description: //  配置开启指定的VO设备，不包含对VO上的通道的操作。
Calls: //
Called By: //PRV_VoInit，
		//PRV_ResetVoDev，
Input: // 无
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_EnableVoDev(HI_S32 DevId)
{
	HI_U32 s32Ret=0;
	HI_U32 u32DispBufLen = 12;
	if(DevId != DHD0)
		RET_SUCCESS();
	s32Ret = HI_MPI_VO_SetDispBufLen(DevId, u32DispBufLen);
	if (s32Ret != HI_SUCCESS)
	{
	//	printf("Set display buf len failed with error code %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	if(DevId >= DSD0)
	{
		if(s_s32NPFlagDflt == VIDEO_ENCODING_MODE_PAL)
			s_astVoDevStatDflt[DevId].stVoPubAttr.enIntfSync = VO_OUTPUT_PAL;
		else
			s_astVoDevStatDflt[DevId].stVoPubAttr.enIntfSync = VO_OUTPUT_NTSC;

	}
	CHECK_RET(HI_MPI_VO_SetPubAttr(DevId, &s_astVoDevStatDflt[DevId].stVoPubAttr));
	
	CHECK_RET(HI_MPI_VO_Enable(DevId));
#if defined(Hi3531)	
	HI_U32 u32Toleration = 200;
	s32Ret = HI_MPI_VO_SetPlayToleration (DevId, u32Toleration);
	if (s32Ret != HI_SUCCESS)
	{
	//	printf("Set play toleration failed with error code %#x!\n", s32Ret);
		return HI_FAILURE;
	}
#endif
	CHECK_RET(PRV_SetDevCsc(DevId));

	RET_SUCCESS("");
} 
#endif

/*************************************************
Function: //PRV_DisableVoDev
Description: //  关闭指定的VO设备，不包含对VO设备上的通道的操作。
   			请先调用PRV_DisableVideoLayer()关闭VO上的视频层。
Calls: PRV_DisableVideoLayer
Called By: //PRV_VoInit，
		//PRV_ResetVoDev，
Input: // VO设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_DisableVoDev(HI_S32 DevId)
{
    CHECK_RET(HI_MPI_VO_Disable(DevId));

	RET_SUCCESS("");
} 

/*************************************************
Function: //PRV_EnableViDev
Description: //   配置并开启指定的VI设备，不包含对VI上的通道的操作。请确保VI是已经关闭的，
   否则调用失败，且也没必要再启用VI。
Calls: 
Called By: //
Input: // VI设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_EnableViDev(VI_DEV ViDev)
{
#if defined(SN9234H1)
	CHECK_RET(HI_MPI_VI_SetPubAttr(ViDev, &s_stViDevPubAttrDflt));
    CHECK_RET(HI_MPI_VI_Enable(ViDev));
#else
	CHECK_RET(HI_MPI_VI_SetDevAttr(ViDev, &s_stViDevPubAttrDflt));
    CHECK_RET(HI_MPI_VI_EnableDev(ViDev));
#endif
	RET_SUCCESS("");
}

/************************************************************************/
/* 启动所有VI设备。
                                                                     */
/************************************************************************/
#if 0
STATIC HI_S32 PRV_EnableAllViDev(HI_VOID)
{
	HI_S32 i;

	for (i = 0; i<PRV_VI_DEV_NUM)
	{
		PRV_EnableViDev(i);
	}

	RET_SUCCESS("");
}
#endif
/*************************************************
Function: //PRV_DisableViDev
Description: //关闭指定的VI设备，不包括对VI上的通道的操作。请确保VI上的通道都已经关闭
Calls: //
Called By: // Prv_ViInit，预览初始化VI调用
Input: //  VI设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/ 
STATIC HI_S32 PRV_DisableViDev(VI_DEV ViDev)
{
#if defined(SN9234H1)
    CHECK_RET(HI_MPI_VI_Disable(ViDev));
#else
    CHECK_RET(HI_MPI_VI_DisableDev(ViDev));
#endif
	RET_SUCCESS("");
}

/************************************************************************/
/* 关闭所有VI设备。
                                                                     */
/************************************************************************/
#if 0
STATIC HI_S32 PRV_DisableAllViDev(HI_VOID)
{
	HI_S32 i;

	for (i = 0; i<PRV_VI_DEV_NUM)
	{
		PRV_DisableViDev(i);
	}

	RET_SUCCESS("");
}
#endif


/*************************************************
Function: //PRV_ResetVideoLayer
Description: //重新配置image层次的大小，单画面时，保持imagesize和VI输入一致
提高显示质量。
Calls: //
Called By: // Prv_VoInit，预览初始化VI调用
Input: //  VO设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/ 
HI_S32 PRV_ResetVideoLayer(VO_DEV VoDev)
{
	//if(VoDev >= PRV_VO_MAX_DEV)
#if defined(SN9234H1)
	if(VoDev != HD)
#else
	if(VoDev > DHD0)
#endif		
	{
		RET_SUCCESS("");
	}
	PRV_DisableAllVoChn(VoDev);
	CHECK_RET(HI_MPI_VO_DisableVideoLayer(VoDev));
#if defined(SN9234H1)
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = PRV_BT1120_SIZE_W;
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = PRV_BT1120_SIZE_H;	
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = PRV_BT1120_SIZE_W;
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = PRV_BT1120_SIZE_H;
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.u32DispFrmRt = (0 == s_s32NPFlagDflt) ? 25 : 30;
#else
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32X = 0;
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32Y = 0;
	switch(VoDev)
	{
		case DHD0:
		{
			switch(s_astVoDevStatDflt[VoDev].stVoPubAttr.enIntfSync)
			{
				case VO_OUTPUT_1080P50:
				case VO_OUTPUT_1080P60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1920;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 1080;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 1920;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 1080;	
					break;
				case VO_OUTPUT_720P50:
				case VO_OUTPUT_720P60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1280;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 720;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 1280;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 720;	
					break;
				case VO_OUTPUT_1024x768_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1024;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 768;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 1024;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 768;	
					break;
				
				case VO_OUTPUT_1280x1024_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1280;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 1024;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 1280;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 1024;	
					break;
				case VO_OUTPUT_800x600_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 800;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 600;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 800;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 600;	
					break;
				case VO_OUTPUT_1366x768_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1366;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 768;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 1366;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 768;	
					break;
				case VO_OUTPUT_1440x900_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1440;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 900;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 1440;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 900;	
					break;
				default:
					RET_FAILURE("Unsupport VGA Resolution");
			}
			break;
		}
		case DSD0:
#if defined(Hi3531)
		case DSD1:
#endif
		{
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32X = 30;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32Y = 16;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 720;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = (s_s32NPFlagDflt == VIDEO_ENCODING_MODE_PAL) ? 576 : 480;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width -= 2*s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32X;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height -= 2*s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32Y;
		
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
			break;
		}
	}
	
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.u32DispFrmRt = (VIDEO_ENCODING_MODE_PAL == s_s32NPFlagDflt) ? 25 : 30;
#endif
	CHECK_RET(HI_MPI_VO_SetVideoLayerAttr(VoDev, &s_astVoDevStatDflt[VoDev].stVideoLayerAttr));
	CHECK_RET(HI_MPI_VO_EnableVideoLayer(VoDev));
	PRV_EnableAllVoChn(VoDev);
#if defined(Hi3531)||defined(Hi3535)	
	CHECK_RET(PRV_SetDevCsc(VoDev));	
	CHECK_RET(PRV_SetVgaParam(VoDev));
#endif	
	RET_SUCCESS("");
}

#if defined(Hi3531)||defined(Hi3535)
HI_S32 PRV_EnablePipLayer(VO_DEV VoDev)
{
#if defined(Hi3535)
	HI_S32 s32Ret = 0;
	HI_U32 u32Priority = 1;

	VO_VIDEO_LAYER_ATTR_S stPipLayerAttr;
	s32Ret = HI_MPI_VO_BindVideoLayer(PIP,VoDev);
	if (HI_SUCCESS != s32Ret)
	{
		printf("HI_MPI_VO_PipLayerBindDev failed with %#x!\n", s32Ret);
		return -1;
	}
	s32Ret = HI_MPI_VO_GetVideoLayerAttr(PIP,&stPipLayerAttr);
	if (s32Ret != HI_SUCCESS)
	{
		printf("Get pip video layer attributes failed with errno %#x!\n", s32Ret);
		return -1;
	}
	stPipLayerAttr.stDispRect.s32X = 0;
	stPipLayerAttr.stDispRect.s32Y = 0;
	stPipLayerAttr.stDispRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
	stPipLayerAttr.stDispRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
	stPipLayerAttr.stImageSize.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
	stPipLayerAttr.stImageSize.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
	stPipLayerAttr.u32DispFrmRt = 25;
	stPipLayerAttr.enPixFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
	s32Ret = HI_MPI_VO_SetVideoLayerAttr(PIP,&stPipLayerAttr);
	if (HI_SUCCESS != s32Ret)
	{
		printf("HI_MPI_VO_SetPipLayerAttr failed with %#x!\n", s32Ret);
		return -1;
	}
	s32Ret = HI_MPI_VO_SetVideoLayerPriority(PIP, u32Priority);
	if (s32Ret != HI_SUCCESS)
	{
		printf("HI_MPI_VO_SetVideoLayerPriority failed with errno %#x!\n", s32Ret);
		return -1;
	}
	s32Ret = HI_MPI_VO_EnableVideoLayer(PIP);
	if (HI_SUCCESS != s32Ret)
	{
		printf("HI_MPI_VO_EnablePipLayer failed with %#x!\n", s32Ret);
		return -1;
	}
	return 0;
#else
	HI_S32 s32Ret = 0;
	VO_VIDEO_LAYER_ATTR_S stPipLayerAttr;
	s32Ret = HI_MPI_VO_PipLayerBindDev(VoDev);
    if (HI_SUCCESS != s32Ret)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VO_PipLayerBindDev failed with %#x!\n", s32Ret);
        return -1;
    }
	s32Ret = HI_MPI_VO_GetPipLayerAttr(&stPipLayerAttr);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Get pip video layer attributes failed with errno %#x!\n", s32Ret);
		return -1;
	}
 	stPipLayerAttr.stDispRect.s32X = 0;
    stPipLayerAttr.stDispRect.s32Y = 0;
	stPipLayerAttr.stDispRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
	stPipLayerAttr.stDispRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
    stPipLayerAttr.stImageSize.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
    stPipLayerAttr.stImageSize.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
	stPipLayerAttr.u32DispFrmRt = 25;
    stPipLayerAttr.enPixFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    s32Ret = HI_MPI_VO_SetPipLayerAttr(&stPipLayerAttr);
    if (HI_SUCCESS != s32Ret)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VO_SetPipLayerAttr failed with %#x!\n", s32Ret);
        return -1;
    }
    
    s32Ret = HI_MPI_VO_EnablePipLayer();
    if (HI_SUCCESS != s32Ret)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VO_EnablePipLayer failed with %#x!\n", s32Ret);
        return -1;
    }
	return 0;
#endif
}
#endif
/*************************************************
Function: //PRV_EnableVideoLayer
Description: //配置并启动指定VO设备上的视频层。
Calls: //
Called By: // Prv_VoInit，预览初始化VI调用
Input: //  VO设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/ 
#if defined(SN6104) || defined(SN8604D) || defined(SN8604M) || defined(SN6108) || defined(SN8608D) || defined(SN8608M)
STATIC HI_S32 PRV_EnableVideoLayer(VO_DEV VoDev)
{
	
	//printf("defined SN6108, SN8608D......\n");
	//printf("s_astVoDevStatDflt[VoDev].stVoPubAttr.enIntfSync: %d\n", s_astVoDevStatDflt[VoDev].stVoPubAttr.enIntfSync);
	switch(VoDev)
	{
	case HD:
		{
			switch(s_astVoDevStatDflt[VoDev].stVoPubAttr.enIntfSync)
			{
				case VO_OUTPUT_1024x768_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1024;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 768;
					break;
				case VO_OUTPUT_1280x1024_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1280;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 1024;
					break;
				case VO_OUTPUT_800x600_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 800;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 600;
					break;
				case VO_OUTPUT_1440x900_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1440;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 900;
					break;
				case VO_OUTPUT_1366x768_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1366;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 768;
					break;
				default:
					RET_FAILURE("unknown enIntfSync of HD!!");
			}
#if defined(SN8604D) || defined(SN8604M) || defined(SN8608D) || defined(SN8608M)
			//NVR系列HD设备，图像分辨率与显示分辨率设为一致
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
#else
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 720;//s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = (0 == s_s32NPFlagDflt) ? 576 : 480;
#endif
			//printf("#############PRV_EnableVideoLayer s_s32NPFlagDflt = %d ,stImageSize h  =%d ,######################\n",s_s32NPFlagDflt,s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height);
		}
		break;
	case AD:
	case SD:
		return;
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32X = 30;
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32Y = 16;
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 720;
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = (0 == s_s32NPFlagDflt) ? 576 : 480;
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width -= 2*s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32X;
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height -= 2*s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32Y;

		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
		break;
	default:
		RET_FAILURE("bad  VoDev!!");
	}

	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.u32DispFrmRt = (0 == s_s32NPFlagDflt) ? 25 : 30;

    CHECK_RET(HI_MPI_VO_SetVideoLayerAttr(VoDev, &s_astVoDevStatDflt[VoDev].stVideoLayerAttr));

	CHECK_RET(HI_MPI_VO_EnableVideoLayer(VoDev));

	//printf("width : %d, height : %d\n", s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width, s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height);
	RET_SUCCESS("");
}

#else
STATIC HI_S32 PRV_EnableVideoLayer(VO_DEV VoDev)
{
#if defined(SN9234H1)
	int Resolution  = s_s32VGAResolution;
	
	switch(VoDev)
	{
		case HD:
	 /*	     VO_CSC_S stPubCSC;
	            stPubCSC.enCSCType = VO_CSC_LUMA;
	            stPubCSC.u32Value = 50;
	            CHECK_RET(HI_MPI_VO_SetDevCSC(HD,&stPubCSC));
 
	            stPubCSC.enCSCType = VO_CSC_CONTR;
	            stPubCSC.u32Value = 50;
	            CHECK_RET(HI_MPI_VO_SetDevCSC(HD,&stPubCSC));

	            stPubCSC.enCSCType = VO_CSC_HUE;
	            stPubCSC.u32Value = 50;
	            CHECK_RET(HI_MPI_VO_SetDevCSC(HD,&stPubCSC));

	            stPubCSC.enCSCType = VO_CSC_SATU;
	            stPubCSC.u32Value = 50;
	            CHECK_RET(HI_MPI_VO_SetDevCSC(HD,&stPubCSC));
	*/
#if defined(SN8608D_LE) || defined(SN8608M_LE) || defined(SN8616D_LE) || defined(SN8616M_LE) || defined(SN9234H1)
			//NVR系列图像分辨率与显示分辨率设为一致
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = PRV_BT1120_SIZE_W;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = PRV_BT1120_SIZE_H;		 
#else	
			if(s_astVoDevStatDflt[VoDev].bIsSingle)
			{
				s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = PRV_SINGLE_SCREEN_W;
				s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = PRV_SINGLE_SCREEN_H;		
			}
			else
			{
				s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = PRV_BT1120_SIZE_W;
				s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = PRV_BT1120_SIZE_H;		
			}
#endif
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = PRV_BT1120_SIZE_W;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = PRV_BT1120_SIZE_H;
		
			break;
		case AD:
		case SD:
			RET_SUCCESS("");
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32X = PRV_CVBS_EDGE_CUT_W;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32Y = PRV_CVBS_EDGE_CUT_H;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 720;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = (0 == s_s32NPFlagDflt) ? 576 : 480;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width -= 2*s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32X;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height -= 2*s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32Y;

			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
			break;
		default:
			RET_FAILURE("bad  VoDev!!");
	}

	if(VoDev == HD)
	{
		PRV_SetM240_Display(Resolution);
	}

#else
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32X = 0;
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32Y = 0;
	
	switch(VoDev)
	{
		case DHD0:
		{
			switch(s_astVoDevStatDflt[VoDev].stVoPubAttr.enIntfSync)
			{
				case VO_OUTPUT_1080P50:
				case VO_OUTPUT_1080P60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1920;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 1080;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 1920;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 1080;	
					break;
				case VO_OUTPUT_720P50:
				case VO_OUTPUT_720P60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1280;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 720;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 1280;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 720;	
					break;
				case VO_OUTPUT_1024x768_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1024;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 768;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 1024;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 768;	
					break;
				
				case VO_OUTPUT_1280x1024_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1280;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 1024;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 1280;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 1024;	
					break;
				case VO_OUTPUT_800x600_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 800;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 600;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 800;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 600;	
					break;
				case VO_OUTPUT_1366x768_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1366;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 768;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 1366;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 768;	
					break;
				case VO_OUTPUT_1440x900_60:
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 1440;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = 900;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = 1440;
					s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = 900;	
					break;
				default:
					RET_FAILURE("Unsupport VGA Resolution");
			}
			break;
		}
		case DSD0:
#if defined(Hi3531)
		case DSD1:
#endif
		{
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32X = 30;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32Y = 16;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width = 720;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height = (0 == s_s32NPFlagDflt) ? 576 : 480;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width -= 2*s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32X;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height -= 2*s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32Y;
		
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
			break;
		}
		default:
			RET_FAILURE("bad  VoDev!!");
	}
#endif	
#if defined(SN6108HE) || defined(SN6108LE) || defined(SN6116HE) || defined(SN6116LE)
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.u32DispFrmRt = (0 == s_s32NPFlagDflt) ? 25 : 30;
#else
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.u32DispFrmRt = 25;
#endif

	TRACE(SCI_TRACE_NORMAL, MOD_PRV,"PRV_EnableVideoLayer: vo=%d, s_astVoDevStatDflt[VoDev].stVoPubAttr.enIntfSync=%d, (w=%d, h=%d, w=%d, h=%d)\n", VoDev, s_astVoDevStatDflt[VoDev].stVoPubAttr.enIntfSync, 
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width,
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height,
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width,
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height);
#if defined(Hi3531)||defined(Hi3535)
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32X = 0;
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.s32Y = 0;
	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.enPixFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
#endif	
    CHECK_RET(HI_MPI_VO_SetVideoLayerAttr(VoDev, &s_astVoDevStatDflt[VoDev].stVideoLayerAttr));

	CHECK_RET(HI_MPI_VO_EnableVideoLayer(VoDev));
	
#ifdef PIP_VIDEOLAYER
	/*******************PIP*************************************/
	PRV_EnablePipLayer(DHD0);
#endif
#if defined(Hi3535)
	HI_U32 u32Toleration = 200;
	HI_U32 s32Ret = 0;

	s32Ret = HI_MPI_VO_SetPlayToleration (VoDev, u32Toleration);
	if (s32Ret != HI_SUCCESS)
	{
		printf("Set play toleration failed with error code %#x!\n", s32Ret);
		return HI_FAILURE;
	}
#endif

/******************************************************************************/
#if defined(Hi3531)||defined(Hi3535)
	CHECK_RET(PRV_SetDevCsc(VoDev));
#endif
	RET_SUCCESS("");
}
#endif

/*************************************************
Function: //PRV_DisableVideoLayer
Description: //关闭指定VO设备上的视频层。请先调用HI_MPI_VO_DisableChn()关闭VO上的所有通道。
Calls: 
Called By: //
Input: // VO设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
HI_S32 PRV_DisableVideoLayer(VO_DEV VoDev)
{
	CHECK_RET(HI_MPI_VO_DisableVideoLayer(VoDev));

#ifdef PIP_VIDEOLAYER
#if defined(Hi3535)
	CHECK_RET(HI_MPI_VO_DisableVideoLayer(PIP));
#else
	CHECK_RET(HI_MPI_VO_DisablePipLayer());
//	CHECK_RET(HI_MPI_VO_PipLayerUnBindDev(VoDev));
#endif
#endif
	RET_SUCCESS("");
}


/*************************************************
Function: //PRV_EnableAllViChn
Description: //配置并启动指定VI设备上的所有通道。
Calls: 
Called By: //
Input: // VI设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_EnableAllViChn(VI_DEV ViDev)
{
#if defined(SN9234H1)
	HI_S32 i;
	HI_U32 u32SrcFrmRate;

	u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == s_stViDevPubAttrDflt.enViNorm) ? 25: 30;
	s_stViChnAttrDflt.stCapRect.u32Height = (0 == s_s32NPFlagDflt) ? 288 : 240;
	s_stViChnAttrDflt.enCapSel = (0 == s_s32ViWorkMode) ? VI_CAPSEL_BOTH : VI_CAPSEL_BOTTOM;
	s_stViChnAttrDflt.bDownScale = (0 == s_s32ViWorkMode) ? HI_FALSE : HI_TRUE;

	for (i = 0; i < PRV_VI_CHN_NUM; i++)
	{
		//printf("###########ViChn = %d ,ViDev = %d######################\n",i,ViDev);
		CHECK_RET(HI_MPI_VI_SetChnAttr(ViDev, i, &s_stViChnAttrDflt));
		CHECK_RET(HI_MPI_VI_EnableChn(ViDev, i));
		CHECK_RET(HI_MPI_VI_SetSrcFrameRate(ViDev, i, u32SrcFrmRate));
		CHECK_RET(HI_MPI_VI_SetFrameRate(ViDev, i, u32SrcFrmRate));
	}

#else
	HI_S32 i=0;
//	HI_U32 u32SrcFrmRate;

//	u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == s_stViDevPubAttrDflt.enViNorm) ? 25: 30;
	s_stViChnAttrDflt.stCapRect.u32Height = (0 == s_s32NPFlagDflt) ? 288 : 240;
	s_stViChnAttrDflt.enCapSel = (0 == s_s32ViWorkMode) ? VI_CAPSEL_BOTH : VI_CAPSEL_BOTTOM;
//	s_stViChnAttrDflt.bDownScale = (0 == s_s32ViWorkMode) ? HI_FALSE : HI_TRUE;

	for (i = 0; i < PRV_VI_CHN_NUM; i++)
	{
		//printf("###########ViChn = %d ,ViDev = %d######################\n",i,ViDev);
		CHECK_RET(HI_MPI_VI_SetChnAttr(i, &s_stViChnAttrDflt));
		CHECK_RET(HI_MPI_VI_EnableChn(i));
	//	CHECK_RET(HI_MPI_VI_SetSrcFrameRate(ViDev, i, u32SrcFrmRate));
	//	CHECK_RET(HI_MPI_VI_SetFrameRate(ViDev, i, u32SrcFrmRate));
	}
#endif
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_DisableAllViChn
Description: // 关闭指定VI设备上的所有通道。
Calls: 
Called By: //
Input: // VI设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_DisableAllViChn(VI_DEV ViDev)
{
	HI_S32 i=0;

	for (i = 0; i < PRV_VI_CHN_NUM; i++)
	{
#if defined(SN9234H1)		
		CHECK_RET(HI_MPI_VI_DisableChn(ViDev, i));
#else
		CHECK_RET(HI_MPI_VI_DisableChn(i));
#endif
	}

	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_ReadFrame
Description: // 无视频信号图片设置   
Calls: 
Called By: //
Input: // fp: 无视频信号图片文件路径
		pY: Y分量起始地址
		pU:U分量起始地址
		pV:V分量起始地址
		width:图片宽度
		height:图片高度
		stride:图片Y分量跨度
		stride2:图片U\V分量跨度
Output: // 无
Return: //
Others: // 其它说明
************************************************************************/
static HI_VOID PRV_ReadFrame(FILE * fp, HI_U8 * pY, HI_U8 * pU, HI_U8 * pV,
                                              HI_U32 width, HI_U32 height, HI_U32 stride, HI_U32 stride2)
{
    HI_U8 * pDst;

    HI_U32 u32Row;

    pDst = pY;
    for ( u32Row = 0; u32Row < height; u32Row++ )
    {
        fread( pDst, width, 1, fp );
        pDst += stride;
    }
    
    pDst = pU;
    for ( u32Row = 0; u32Row < height/2; u32Row++ )
    {
        fread( pDst, width/2, 1, fp );
        pDst += stride2;
    }
    
    pDst = pV;
    for ( u32Row = 0; u32Row < height/2; u32Row++ )
    {
        fread( pDst, width/2, 1, fp );
        pDst += stride2;
    }
}

/*************************************************
Function: //PRV_PlanToSemi
Description: // convert planar YUV420 to sem-planar YUV420  
Calls: 
Called By: //
Input: // pY: Y分量起始地址
		pU:U分量起始地址
		pV:V分量起始地址
		picWidth:图片宽度
		picHeight:图片高度
		yStride:图片Y分量跨度
		vStride:图片V分量跨度
		uStride:图片U分量跨度
Output: // 无
Return: //
Others: // 其它说明
************************************************************************/
static HI_S32 PRV_PlanToSemi(HI_U8 *pY, HI_S32 yStride, 
                       HI_U8 *pU, HI_S32 uStride,
                       HI_U8 *pV, HI_S32 vStride, 
                       HI_S32 picWidth, HI_S32 picHeight)
{
    HI_S32 i;
    HI_U8* pTmpU, *ptu;
    HI_U8* pTmpV, *ptv;
    HI_S32 s32HafW = uStride >>1 ;
    HI_S32 s32HafH = picHeight >>1 ;
    HI_S32 s32Size = s32HafW*s32HafH;
        
	if (NULL == pY || NULL == pU || NULL == pV)
	{
		RET_FAILURE("paramter NULL prt !!!");
	}
	
	pTmpU = SN_MALLOC( s32Size ); 
    if (NULL == pTmpU)
    {
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, TEXT_COLOR_RED("SN_MALLOC failed! pTmpU=%#x\n"), (int)pTmpU);
		return HI_FAILURE;
    }
	ptu = pTmpU;
	
	pTmpV = SN_MALLOC( s32Size ); 
    if (NULL == pTmpV)
    {
		SN_FREE(pTmpU);
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, TEXT_COLOR_RED("SN_MALLOC failed! pTmpV=%#x\n"), (int)pTmpV);
		return HI_FAILURE;
    }
	ptv = pTmpV;

    SN_MEMCPY(pTmpU, s32Size, pU, s32Size, s32Size);
    SN_MEMCPY(pTmpV, s32Size, pV, s32Size, s32Size);
    
    for(i = 0;i<s32Size>>1;i++)
    {
        *pU++ = *pTmpV++;
        *pU++ = *pTmpU++;
        
    }
    for(i = 0;i<s32Size>>1;i++)
    {
        *pV++ = *pTmpV++;
        *pV++ = *pTmpU++;        
    }

    SN_FREE( ptu );
    SN_FREE( ptv );

    return HI_SUCCESS;
}

/*************************************************
Function: //PRV_GetVFrame_FromYUV
Description: // 从YUV文件读取视频帧信息 (注意只支持planar 420)
Calls: 
Called By: //
Input: // pszYuvFile: 无视频信号图片文件路径
		u32Width:图片宽度
		u32Height:图片高度
		u32Stride:图片行跨度
		pstVFrameInfo:返回图片帧信息
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
/*static*/ HI_S32 PRV_GetVFrame_FromYUV(HI_CHAR *pszYuvFile, HI_U32 u32Width, HI_U32 u32Height,HI_U32 u32Stride, VIDEO_FRAME_INFO_S *pstVFrameInfo)
{
    HI_U32             u32LStride;
    HI_U32             u32CStride;
    HI_U32             u32LumaSize;
    HI_U32             u32ChrmSize;
    HI_U32             u32Size;
    VB_BLK VbBlk;
    HI_U32 u32PhyAddr;
    HI_U8 *pVirAddr;

	/* 打开YUV文件 */
	FILE *pYUVFile;
	pYUVFile = fopen(pszYuvFile, "rb");
	if (!pYUVFile)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, TEXT_COLOR_RED("open yvu file ") TEXT_COLOR_YELLOW("%s") TEXT_COLOR_RED(" fail!\n"), pszYuvFile);
		RET_FAILURE("");
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, TEXT_COLOR_GREEN("open yuv file ") TEXT_COLOR_YELLOW("%s") TEXT_COLOR_GREEN(" success!\n"), pszYuvFile);
	}

    u32LStride  = u32Stride;
    u32CStride  = u32Stride;
    
    u32LumaSize = (u32LStride * u32Height);
    u32ChrmSize = (u32CStride * u32Height) >> 2;/* YUV 420 */
    u32Size = u32LumaSize + (u32ChrmSize << 1);

    /* alloc video buffer block ---------------------------------------------------------- */
#if defined(SN9234H1)
    VbBlk = HI_MPI_VB_GetBlock(VB_INVALID_POOLID, u32Size);
#else
    VbBlk = HI_MPI_VB_GetBlock(VB_INVALID_POOLID, u32Size,NULL);
#endif
    if (VB_INVALID_HANDLE == VbBlk)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VB_GetBlock err! size:%d\n",u32Size);
		fclose(pYUVFile);
        return -1;
    }
    u32PhyAddr = HI_MPI_VB_Handle2PhysAddr(VbBlk);
    if (0 == u32PhyAddr)
    {
		fclose(pYUVFile);
        return -1;
    }
    pVirAddr = (HI_U8 *) HI_MPI_SYS_Mmap(u32PhyAddr, u32Size);
    if (NULL == pVirAddr)
    {
		fclose(pYUVFile);
        return -1;
    }

    pstVFrameInfo->u32PoolId = HI_MPI_VB_Handle2PoolId(VbBlk);
    if (VB_INVALID_POOLID == pstVFrameInfo->u32PoolId)
    {
		fclose(pYUVFile);
        return -1;
    }
    TRACE(SCI_TRACE_NORMAL, MOD_PRV, "pool id :%d, phyAddr:%x,virAddr:%x\n" ,pstVFrameInfo->u32PoolId,u32PhyAddr,(int)pVirAddr);
    
    pstVFrameInfo->stVFrame.u32PhyAddr[0] = u32PhyAddr;
    pstVFrameInfo->stVFrame.u32PhyAddr[1] = pstVFrameInfo->stVFrame.u32PhyAddr[0] + u32LumaSize;
    pstVFrameInfo->stVFrame.u32PhyAddr[2] = pstVFrameInfo->stVFrame.u32PhyAddr[1] + u32ChrmSize;

    pstVFrameInfo->stVFrame.pVirAddr[0] = pVirAddr;
    pstVFrameInfo->stVFrame.pVirAddr[1] = pstVFrameInfo->stVFrame.pVirAddr[0] + u32LumaSize;
    pstVFrameInfo->stVFrame.pVirAddr[2] = pstVFrameInfo->stVFrame.pVirAddr[1] + u32ChrmSize;

    pstVFrameInfo->stVFrame.u32Width  = u32Width;
    pstVFrameInfo->stVFrame.u32Height = u32Height;
    pstVFrameInfo->stVFrame.u32Stride[0] = u32LStride;
    pstVFrameInfo->stVFrame.u32Stride[1] = u32CStride;
    pstVFrameInfo->stVFrame.u32Stride[2] = u32CStride;  
    pstVFrameInfo->stVFrame.enPixelFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420;        
    pstVFrameInfo->stVFrame.u32Field = VIDEO_FIELD_FRAME;  

    /* read Y U V data from file to the addr ----------------------------------------------*/
    PRV_ReadFrame(pYUVFile, pstVFrameInfo->stVFrame.pVirAddr[0], 
                               pstVFrameInfo->stVFrame.pVirAddr[1], pstVFrameInfo->stVFrame.pVirAddr[2],
                               pstVFrameInfo->stVFrame.u32Width, pstVFrameInfo->stVFrame.u32Height, 
                               pstVFrameInfo->stVFrame.u32Stride[0], pstVFrameInfo->stVFrame.u32Stride[1] >> 1 );

    /* convert planar YUV420 to sem-planar YUV420 -----------------------------------------*/
    PRV_PlanToSemi(pstVFrameInfo->stVFrame.pVirAddr[0], pstVFrameInfo->stVFrame.u32Stride[0],
                pstVFrameInfo->stVFrame.pVirAddr[1], pstVFrameInfo->stVFrame.u32Stride[1],
                pstVFrameInfo->stVFrame.pVirAddr[2], pstVFrameInfo->stVFrame.u32Stride[1],
                pstVFrameInfo->stVFrame.u32Width, pstVFrameInfo->stVFrame.u32Height);
    
    HI_MPI_SYS_Munmap((HI_VOID*)u32PhyAddr, u32Size);
	fclose(pYUVFile);
    return 0;
}
/*************************************************
Function: //PRV_EnableAllVoChn
Description: // 配置并启动指定VO设备上的所有通道。
Calls: 
Called By: //
Input: // // VO设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_EnableAllVoChn(VO_DEV VoDev)
{
#if defined(SN9234H1)
	HI_S32 i;

	//for (i=0; i<g_Max_Vo_Num; i++)
	for (i = 0; i < DEV_CHANNEL_NUM; i++)	
	{
		CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, i, &s_stVoChnAttrDflt));
		//CHECK_RET(HI_MPI_VO_SetZoomInWindow(VoDev, i, &s_astZoomAttrDflt));
		//printf("HI_MPI_VO_EnableChn  VoDev=%d ,i=%d\n",VoDev, i);

		if((VoDev == SPOT_VO_DEV) && (i>0))
		{
			continue;
		}
		CHECK_RET(HI_MPI_VO_EnableChn(VoDev, i));
	}
#else

	HI_S32 i = 0;
	HI_S32 u32Square = sqrt(DEV_CHANNEL_NUM);
	VO_CHN_ATTR_S stChnAttr;
	HI_S32 u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
	HI_S32 u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;

	for (i = 0; i < PRV_VO_CHN_NUM; i++)	
	{
		stChnAttr.stRect.s32X       = ALIGN_BACK((u32Width/u32Square) * (i%u32Square), 2);
        stChnAttr.stRect.s32Y       = ALIGN_BACK((u32Height/u32Square) * (i/u32Square), 2);
        stChnAttr.stRect.u32Width   = ALIGN_BACK(u32Width/u32Square, 2);
        stChnAttr.stRect.u32Height  = ALIGN_BACK(u32Height/u32Square, 2);
        stChnAttr.u32Priority       = 0;
        stChnAttr.bDeflicker        = HI_FALSE;
		//CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, i, &s_stVoChnAttrDflt));
		CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, i, &stChnAttr));
		//CHECK_RET(HI_MPI_VO_SetZoomInWindow(VoDev, i, &s_astZoomAttrDflt));
		//printf("HI_MPI_VO_EnableChn  VoDev=%d ,i=%d\n",VoDev, i);

		CHECK_RET(HI_MPI_VO_EnableChn(VoDev, i));
	}
#endif
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_DisableAllVoChn
Description: // 关闭指定VO设备上的所有通道。
Calls: 
Called By: //
Input: // // VO设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_DisableAllVoChn(VO_DEV VoDev)
{
	HI_S32 i=0;
//关闭通道时，所以的通道禁止一次
	for (i = 0; i < VO_MAX_CHN_NUM; i++)
	{
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########HI_MPI_VO_DisableChn VoDev=%d g_Max_Vo_Num=%d,i=%d  !###########\n",VoDev,g_Max_Vo_Num,i);
		CHECK_RET(HI_MPI_VO_DisableChn(VoDev, i));
	}
	

	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_HideAllVoChn
Description: //  隐藏指定VO设备上的所有通道。
Calls: 
Called By: //
Input: // // VO设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
HI_S32 PRV_HideAllVoChn(VO_DEV VoDev)
{
	HI_S32 i=0;
#if defined(SN9234H1)
	for (i = 0; i < g_Max_Vo_Num; i++)
#else
	for (i = 0; i < PRV_VO_CHN_NUM; i++)
#endif		
	{
#if defined(Hi3535)
		CHECK_RET(HI_MPI_VO_HideChn(VoDev, i));
#else
		CHECK_RET(HI_MPI_VO_ChnHide(VoDev, i));
#endif
	}

	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_ClearAllVoChnBuf
Description: //  清除所有VO通道上的buffer
Calls: 
Called By: //
Input: // // VO设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_ClearAllVoChnBuf(VO_DEV VoDev)
{
	HI_S32 i=0;
	
	for (i=0; i < PRV_VO_CHN_NUM; i++)
	{
		//printf("###########PRV_ClearAllVoChnBuf: VoDev = %d ,i=%d,PRV_CHAN_NUM=%d###################\n",VoDev,i,PRV_CHAN_NUM);
		CHECK_RET(HI_MPI_VO_ClearChnBuffer(VoDev, i, HI_TRUE));
	}
	
	RET_SUCCESS("");
}
#if defined(SN9234H1)
STATIC HI_S32 PRV_ViUnBindAllVoChn(VO_DEV VoDev)
{
		HI_S32 j = 0;
#if defined(SN6116HE)||defined(SN6116LE) ||defined(SN8616D_LE) || defined(SN8616M_LE) || defined(SN9234H1)
	for(j = 0; j < LOCALVEDIONUM; j++)
	{
		if(j < PRV_VI_CHN_NUM)
			CHECK_RET(HI_MPI_VI_UnBindOutput(PRV_656_DEV_1, j, VoDev,  j));
		else if(j >= PRV_VI_CHN_NUM && j < PRV_VI_CHN_NUM * 2)
			CHECK_RET(HI_MPI_VI_UnBindOutput(PRV_656_DEV, j, VoDev,  j));
		else 
			CHECK_RET(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, VoDev,	j));
	}
	
	//printf("########PRV_UnBindAllVoChn  suc!###########\n");
#elif defined(SN6108HE)
	for(j = 0; j < LOCALVEDIONUM; j++)
	{
		if(j < PRV_VI_CHN_NUM)
			CHECK_RET(HI_MPI_VI_UnBindOutput(PRV_656_DEV_1, j, VoDev,  j));
		else 
			CHECK_RET(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, VoDev,	j));
	}
	
#elif defined(SN6108LE) || defined(SN8608D_LE) || defined(SN8608M_LE)
	for(j = 0; j < LOCALVEDIONUM; j++)
	{
		if(j < PRV_VI_CHN_NUM)
			CHECK_RET(HI_MPI_VI_UnBindOutput(PRV_656_DEV_1, j, VoDev,  j));
		else
			CHECK_RET(HI_MPI_VI_UnBindOutput(PRV_656_DEV, j, VoDev,  j));
	}
#else
	HI_S32 i;

	for (i = 0; i < PRV_VI_DEV_NUM; i++)
	{
		for (j = 0; j < ((LOCALVEDIONUM - i * PRV_VI_CHN_NUM) > PRV_VI_CHN_NUM ? PRV_VI_CHN_NUM : (LOCALVEDIONUM - i * PRV_VI_CHN_NUM)); j++)
		{
			//printf("########PRV_UnBindAllVoChn  i=%d,j=%d,VoDev=%d,chn=%d!###########\n",i,j,VoDev,i*PRV_VI_CHN_NUM + j);
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########PRV_UnBindAllVoChn  i=%d,j=%d,VoDev=%d,chn=%d!###########\n",i,j,VoDev,i*PRV_VI_CHN_NUM + j);
			CHECK_RET(HI_MPI_VI_UnBindOutput(i, j, VoDev, i * PRV_VI_CHN_NUM + j));
		}
	}
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "########PRV_UnBindAllVoChn  suc!###########\n");
#endif
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_VdecUnBindAllVoChn
Description: //  解绑定所有VDEC与VO的绑定
Calls: 
Called By: //
Input: // // VO设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
***************************************************/
HI_S32 PRV_VdecUnBindAllVoChn1(VO_DEV VoDev)
{
	HI_S32 i = 0;	
	//if(VoDev >= SD || VoDev < 0)
	//	return HI_FAILURE;
	//int index = PRV_GetVoChnIndex(VoChn);
	//if(VochnInfo[index].IsLocalVideo != 0)
	//	return HI_FAILURE;
	for(i = LOCALVEDIONUM; i < DEV_CHANNEL_NUM; i++)
	{
	
		//if((VochnInfo[i].IsBindVdec[VoDev] != -1))
		{
			#if 0
			if(VoDev == SD)	
			{
				CHECK_RET(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[index].VdecChn, VoDev, SPOT_VO_CHAN));
				break;
			}
			#endif
			//printf("------VoDev: %d--------UnBind---i: %d, SlaveId: %d ---VoChn: %d, VdecChn: %d, IsBindVdec: %d\n", VoDev, i, VochnInfo[i].SlaveId, VochnInfo[i].VoChn, VochnInfo[i].VdecChn, VochnInfo[i].IsBindVdec[VoDev]);

			if(VochnInfo[i].SlaveId == PRV_MASTER)
				(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[i].VdecChn, VoDev, VochnInfo[i].VoChn));
			else if(VochnInfo[i].SlaveId > PRV_MASTER)			
				(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, VoDev, VochnInfo[i].VoChn));
			
			VochnInfo[i].IsBindVdec[VoDev] = -1;
		}
	}
	RET_SUCCESS("");
}

HI_S32 PRV_VdecBindAllVoChn(VO_DEV VoDev)
{
	HI_S32 i = 0;	
	//if(VoDev >= SD || VoDev < 0)
	//	return HI_FAILURE;
	//int index = PRV_GetVoChnIndex(VoChn);
	//if(VochnInfo[index].IsLocalVideo != 0)
	//	return HI_FAILURE;
	for(i = LOCALVEDIONUM; i < DEV_CHANNEL_NUM; i++)
	{
	
		//if((VochnInfo[i].IsBindVdec[VoDev] == -1))
		{

			//printf("------VoDev: %d--------Bind---i: %d, SlaveId: %d ---VoChn: %d, VdecChn: %d, IsBindVdec: %d\n", VoDev, i, VochnInfo[i].SlaveId, VochnInfo[i].VoChn, VochnInfo[i].VdecChn, VochnInfo[i].IsBindVdec[VoDev]);
			//通道控制状态下(显示管理)，不需要绑定无网络视频通道(被画中画覆盖)
			if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL
				&& (VochnInfo[i].VdecChn == DetVLoss_VdecChn || VochnInfo[i].VdecChn == NoConfig_VdecChn)			
				&& VoDev == s_VoDevCtrlDflt
				&& VochnInfo[i].VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn)
			{
				continue;
			}
			if(VochnInfo[i].SlaveId == PRV_MASTER)
				HI_MPI_VDEC_BindOutput(VochnInfo[i].VdecChn, VoDev, VochnInfo[i].VoChn);
			else if(VochnInfo[i].SlaveId > PRV_MASTER)			
				HI_MPI_VI_BindOutput(PRV_HD_DEV, 0, VoDev, VochnInfo[i].VoChn);
			
			VochnInfo[i].IsBindVdec[VoDev] = (VochnInfo[i].VdecChn == DetVLoss_VdecChn || VochnInfo[i].VdecChn == NoConfig_VdecChn) ? 0 : 1;
		}
	}
	
	RET_SUCCESS("");

}

#endif
#if defined(Hi3531)||defined(Hi3535)


int PRV_VPSS_ResetWH(int VpssGrp, int VdChn, int u32MaxW, int u32MaxH)
{
    HI_S32 s32Ret = 0;
	HI_S32 i = 0;	
	VPSS_CHN VpssChn;
    VPSS_GRP_ATTR_S stGrpAttr;
	VPSS_CHN_ATTR_S stChnAttr;
	VPSS_GRP_PARAM_S stVpssParam;
	if(VdChn == DetVLoss_VdecChn || VdChn == NoConfig_VdecChn)
	{
		u32MaxW = 2048;
		u32MaxH = 1536;
	}
	if(u32MaxW<=2048&&u32MaxH<=1536)
	{
		u32MaxW = 2048;
		u32MaxH = 1536;
	}
	s32Ret = HI_MPI_VPSS_GetGrpAttr(VpssGrp, &stGrpAttr);
	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"HI_MPI_VPSS_GetGrpAttr-------failed with %#x!\n", s32Ret);
		return s32Ret;
	}
	
	//printf("PRV_VPSS_ResetWH,VpssGrp:%d,VdChn:%d,u32MaxW = %d,u32MaxH = %d,stGrpAttr.u32MaxW=%d,stGrpAttr.u32MaxH=%d\n",VpssGrp,VdChn,u32MaxW,u32MaxH,stGrpAttr.u32MaxW,stGrpAttr.u32MaxH);
	if((stGrpAttr.u32MaxW!=u32MaxW || stGrpAttr.u32MaxH!=u32MaxH)&&(stGrpAttr.u32MaxW>2048 ||stGrpAttr.u32MaxH>1536|| u32MaxW>2048||u32MaxH>1536))
	{
#if defined(Hi3535)	
		if(VpssGrp == PRV_CTRL_VOCHN)
		{
			CHECK(PRV_VO_UnBindVpss(PIP, VpssGrp, VpssGrp, VPSS_BSTR_CHN));
		}
		else
		{
			CHECK(PRV_VO_UnBindVpss(DHD0, VpssGrp, VpssGrp, VPSS_BSTR_CHN));
		}

		s32Ret = HI_MPI_VPSS_StopGrp(VpssGrp);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"HI_MPI_VPSS_StopGrp-------failed with %#x!\n", s32Ret);
			return HI_FAILURE;
		}
		for(i = 0; i < VPSS_MAX_CHN_NUM; i++)
		{
			s32Ret = HI_MPI_VPSS_DisableChn(VpssGrp, i);
			if (s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_HIGH, MOD_PRV,"HI_MPI_VPSS_DisableChn-------failed with %#x!\n", s32Ret);
				return HI_FAILURE;
			}
		}
		s32Ret = HI_MPI_VPSS_DestroyGrp(VpssGrp);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"HI_MPI_VPSS_DestroyGrp-------failed with %#x!\n", s32Ret);
			return HI_FAILURE;
		}
		
		stGrpAttr.u32MaxW = u32MaxW;
		stGrpAttr.u32MaxH = u32MaxH;
		stGrpAttr.bDciEn = HI_FALSE;
		stGrpAttr.bIeEn = HI_FALSE;
		stGrpAttr.bNrEn = HI_FALSE;
		stGrpAttr.bHistEn = HI_FALSE;
		stGrpAttr.enDieMode = VPSS_DIE_MODE_NODIE;
		stGrpAttr.enPixFmt = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
		
		/*** create vpss group ***/
		s32Ret = HI_MPI_VPSS_CreateGrp(VpssGrp, &stGrpAttr);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_CreateGrp failed with %#x!\n", s32Ret);
			return HI_FAILURE;
		}
	
		/*** set vpss param ***/
		s32Ret = HI_MPI_VPSS_GetGrpParam(VpssGrp,  &stVpssParam);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "failed with %#x!\n", s32Ret);
			return HI_FAILURE;
		}
		
		s32Ret = HI_MPI_VPSS_SetGrpParam(VpssGrp,  &stVpssParam);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "failed with %#x!\n", s32Ret);
			return HI_FAILURE;
		}
		
		/*** enable vpss chn, with frame ***/
		stChnAttr.bSpEn    = HI_FALSE;
		stChnAttr.bBorderEn = HI_FALSE;
		stChnAttr.stBorder.u32BottomWidth = 0;
		stChnAttr.stBorder.u32LeftWidth = 0;
		stChnAttr.stBorder.u32RightWidth = 0;
		stChnAttr.stBorder.u32TopWidth = 0;
		stChnAttr.stBorder.u32Color = 0;
		
		for(i = 0; i < VPSS_MAX_CHN_NUM; i++)
		{
			VpssChn = i;
			/* Set Vpss Chn attr */
			
			s32Ret = HI_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn, &stChnAttr);
			if (s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_SetChnAttr failed with %#x\n", s32Ret);
				return HI_FAILURE;
			}
	
			s32Ret = HI_MPI_VPSS_EnableChn(VpssGrp, VpssChn);
			if (s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_EnableChn failed with %#x\n", s32Ret);
				return HI_FAILURE;
			}
		}
	
		/*** start vpss group ***/
		s32Ret = HI_MPI_VPSS_StartGrp(VpssGrp);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_StartGrp failed with %#x\n", s32Ret);
			return HI_FAILURE;
		}
		if(VpssGrp == PRV_CTRL_VOCHN)
		{
			CHECK(PRV_VO_BindVpss(PIP, VpssGrp, VpssGrp, VPSS_BSTR_CHN));
			CHECK_RET(HI_MPI_VO_ShowChn(PIP, VpssGrp));
			CHECK_RET(HI_MPI_VO_EnableChn(PIP, VpssGrp));
		}
		else
		{
			CHECK(PRV_VO_BindVpss(DHD0, VpssGrp, VpssGrp, VPSS_BSTR_CHN));
			CHECK_RET(HI_MPI_VO_ShowChn(DHD0, VpssGrp));
			CHECK_RET(HI_MPI_VO_EnableChn(DHD0, VpssGrp));
		}
#else	
		
		CHECK(PRV_VO_UnBindVpss(DHD0, VpssGrp, VpssGrp, VPSS_PRE0_CHN));
		s32Ret = HI_MPI_VPSS_StopGrp(VpssGrp);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"HI_MPI_VPSS_StopGrp-------failed with %#x!\n", s32Ret);
			return HI_FAILURE;
		}
		for(i = 0; i < VPSS_MAX_CHN_NUM; i++)
		{
			s32Ret = HI_MPI_VPSS_DisableChn(VpssGrp, i);
			if (s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_HIGH, MOD_PRV,"HI_MPI_VPSS_DisableChn-------failed with %#x!\n", s32Ret);
				return HI_FAILURE;
			}
		}
		s32Ret = HI_MPI_VPSS_DestroyGrp(VpssGrp);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"HI_MPI_VPSS_DestroyGrp-------failed with %#x!\n", s32Ret);
			return HI_FAILURE;
		}
		stGrpAttr.u32MaxW = u32MaxW;
		stGrpAttr.u32MaxH = u32MaxH;
		stGrpAttr.bDrEn = HI_FALSE;
		stGrpAttr.bDbEn = HI_FALSE;
		stGrpAttr.bIeEn = HI_TRUE;
		stGrpAttr.bNrEn = HI_TRUE;
		stGrpAttr.bHistEn = HI_FALSE;
		stGrpAttr.enDieMode = VPSS_DIE_MODE_AUTO;
		stGrpAttr.enPixFmt = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
		
		/*** create vpss group ***/
		s32Ret = HI_MPI_VPSS_CreateGrp(VpssGrp, &stGrpAttr);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_CreateGrp failed with %#x!\n", s32Ret);
			return HI_FAILURE;
		}
	
		/*** set vpss param ***/
		s32Ret = HI_MPI_VPSS_GetGrpParam(VpssGrp,  &stVpssParam);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "failed with %#x!\n", s32Ret);
			return HI_FAILURE;
		}
		
		stVpssParam.u32MotionThresh = 0;
		
		s32Ret = HI_MPI_VPSS_SetGrpParam(VpssGrp,  &stVpssParam);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "failed with %#x!\n", s32Ret);
			return HI_FAILURE;
		}
		
		/*** enable vpss chn, with frame ***/
		stChnAttr.bSpEn    = HI_FALSE;
		stChnAttr.bFrameEn = HI_FALSE;
		stChnAttr.stFrame.u32Color[VPSS_FRAME_WORK_LEFT]   = 0xff00;
		stChnAttr.stFrame.u32Color[VPSS_FRAME_WORK_RIGHT]  = 0xff00;
		stChnAttr.stFrame.u32Color[VPSS_FRAME_WORK_BOTTOM] = 0xff00;
		stChnAttr.stFrame.u32Color[VPSS_FRAME_WORK_TOP]    = 0xff00;
		stChnAttr.stFrame.u32Width[VPSS_FRAME_WORK_LEFT]   = 2;
		stChnAttr.stFrame.u32Width[VPSS_FRAME_WORK_RIGHT]  = 2;
		stChnAttr.stFrame.u32Width[VPSS_FRAME_WORK_TOP]    = 2;
		stChnAttr.stFrame.u32Width[VPSS_FRAME_WORK_BOTTOM] = 2;
		for(i = 0; i < VPSS_MAX_CHN_NUM; i++)
		{
			VpssChn = i;
			/* Set Vpss Chn attr */
			
			s32Ret = HI_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn, &stChnAttr);
			if (s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_SetChnAttr failed with %#x\n", s32Ret);
				return HI_FAILURE;
			}
	
			s32Ret = HI_MPI_VPSS_EnableChn(VpssGrp, VpssChn);
			if (s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_EnableChn failed with %#x\n", s32Ret);
				return HI_FAILURE;
			}
		}
	
		/*** start vpss group ***/
		s32Ret = HI_MPI_VPSS_StartGrp(VpssGrp);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_StartGrp failed with %#x\n", s32Ret);
			return HI_FAILURE;
		}
		CHECK(PRV_VO_BindVpss(DHD0, VpssGrp, VpssGrp, VPSS_PRE0_CHN));
		CHECK_RET(HI_MPI_VO_ChnShow(DHD0, VpssGrp));
		CHECK_RET(HI_MPI_VO_EnableChn(DHD0, VpssGrp));
#endif	
		
	}
	else
	{
		//printf("The same wh,u32MaxW = %d,u32MaxH = %d\n",u32MaxW,u32MaxH);
	}
	return HI_SUCCESS;
}

HI_S32 PRV_AUDIO_AoBindAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn)
{
	HI_S32 s32Ret = 0;
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_ADEC;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = AdChn;
    stDestChn.enModId = HI_ID_AO;
    stDestChn.s32DevId = AoDev;
    stDestChn.s32ChnId = AoChn;
    
    s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn); 
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_AUDIO_AoBindAdec failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	return HI_SUCCESS;
}

/******************************************************************************
* function : Ao unbind Adec
******************************************************************************/
HI_S32 PRV_AUDIO_AoUnbindAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn)
{
	HI_S32 s32Ret = 0;
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_ADEC;
    stSrcChn.s32ChnId = AdChn;
    stSrcChn.s32DevId = 0;
    stDestChn.enModId = HI_ID_AO;
    stDestChn.s32DevId = AoDev;
    stDestChn.s32ChnId = AoChn;
    
    s32Ret =  HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn); 
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_AUDIO_AoUnbindAdec failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	return HI_SUCCESS;
}

HI_S32 PRV_VO_BindVpss(VO_DEV VoDev, VO_CHN VoChn, VPSS_GRP VpssGrp, VPSS_CHN VpssChn)
{
    HI_S32 s32Ret = HI_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;

    stSrcChn.enModId = HI_ID_VPSS;
    stSrcChn.s32DevId = VpssGrp;
    stSrcChn.s32ChnId = VpssChn;

    stDestChn.enModId = HI_ID_VOU;
    stDestChn.s32DevId = VoDev;
    stDestChn.s32ChnId = VoChn;

    s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_VO_BindVpss failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }

    return s32Ret;
}

/******************************************************************************
* function : vdec group bind vpss chn
******************************************************************************/
HI_S32 PRV_VDEC_BindVpss(VDEC_CHN VdChn, VPSS_GRP VpssGrp)
{
    HI_S32 s32Ret = HI_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;

    stSrcChn.enModId = HI_ID_VDEC;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = VdChn;

    stDestChn.enModId = HI_ID_VPSS;
    stDestChn.s32DevId = VpssGrp;
    stDestChn.s32ChnId = 0;
	
	//printf("+++++++++++++++++++++++++PRV_VDEC_BindVpss,vdechn:%d,vpssgrp:%d\n",VdChn,VpssGrp);
    s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"%s line:%d VdChn:%d,VpssGrp:%d,PRV_VDEC_BindVpss HI_MPI_SYS_Bind failed with %#x!\n", __FUNCTION__, __LINE__, VdChn,VpssGrp,s32Ret);
        return s32Ret;
    }

    return HI_SUCCESS;
}

HI_S32 PRV_VO_UnBindVpss(VO_DEV VoDev,VO_CHN VoChn,VPSS_GRP VpssGrp,VPSS_CHN VpssChn)
{
    HI_S32 s32Ret = HI_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;

    stSrcChn.enModId = HI_ID_VPSS;
    stSrcChn.s32DevId = VpssGrp;
    stSrcChn.s32ChnId = VpssChn;

    stDestChn.enModId = HI_ID_VOU;
    stDestChn.s32DevId = VoDev;
    stDestChn.s32ChnId = VoChn;

    s32Ret = HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }

    return s32Ret;
}
HI_S32 PRV_VoUnBindAllVpss(VO_DEV VoDev)
{
	//if(VoDev >= PRV_VO_MAX_DEV)
	if(VoDev > DHD0)
		RET_FAILURE("Invalid VoDev!!!");
	int i = 0;
	VPSS_CHN VpssChn = 0;
	
#if defined(Hi3535)
	VpssChn = VPSS_BSTR_CHN;
#else
	if(VoDev == DHD0)
	{
		VpssChn = VPSS_PRE0_CHN;
	}
	else
	{
		VpssChn = VPSS_BYPASS_CHN;
	}
#endif
	
	for(i = 0; i < PRV_VO_CHN_NUM; i++)
	{
		PRV_VO_UnBindVpss(VoDev, i, i, VpssChn);
	}
	return 0;
}

HI_S32 PRV_VoBindAllVpss(VO_DEV VoDev)
{
	if(VoDev >= PRV_VO_MAX_DEV)
	if(VoDev > DHD0)
		RET_FAILURE("Invalid VoDev!!!");
	int i = 0;
	VPSS_CHN VpssChn = 0;
	
#if defined(Hi3535)
	VpssChn = VPSS_BSTR_CHN;
#else
	if(VoDev == DHD0)
	{
		VpssChn = VPSS_PRE0_CHN;
	}
	else
	{
		VpssChn = VPSS_BYPASS_CHN;
	}
#endif
	
	for(i = 0; i < PRV_VO_CHN_NUM; i++)
	{
		PRV_VO_BindVpss(VoDev, i, i, VpssChn);
	}
	return 0;
}
/*************************************************
Function: //PRV_VdecUnBindAllVoChn
Description: //  解绑定所有VDEC与VO的绑定
Calls: 
Called By: //
Input: // // VO设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
***************************************************/
HI_S32 PRV_VdecUnBindAllVpss(VO_DEV VoDev)
{
	HI_S32 i = 0;	
	for(i = 0; i < PRV_VO_CHN_NUM; i++)
	{
		if(VochnInfo[i].IsBindVdec[VoDev] != -1)
		{
			PRV_VDEC_UnBindVpss(VochnInfo[i].VdecChn, i);				
			PRV_VPSS_ResetWH(i,VochnInfo[i].VdecChn,704,576);
			VochnInfo[i].IsBindVdec[VoDev] = -1;
		}
	}
	RET_SUCCESS("");
}

HI_S32 PRV_VdecBindAllVpss(VO_DEV VoDev)
{
	HI_S32 i = 0;	
	//if(VoDev >= SD || VoDev < 0)
	//	return HI_FAILURE;
	//int index = PRV_GetVoChnIndex(VoChn);
	//if(VochnInfo[index].IsLocalVideo != 0)
	//	return HI_FAILURE;
	for(i = LOCALVEDIONUM; i < DEV_CHANNEL_NUM; i++)
	{
	
		if((VochnInfo[i].IsBindVdec[VoDev] == -1))
		{

			//printf("------VoDev: %d--------UnBind---i: %d, SlaveId: %d ---VoChn: %d, VdecChn: %d, IsBindVdec: %d\n", VoDev, i, VochnInfo[i].SlaveId, VochnInfo[i].VoChn, VochnInfo[i].VdecChn, VochnInfo[i].IsBindVdec[VoDev]);
			//通道控制状态下(显示管理)，不需要绑定无网络视频通道(被画中画覆盖)
			if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL
				&& VochnInfo[i].VdecChn == DetVLoss_VdecChn				
				&& VoDev == s_VoDevCtrlDflt
				&& VochnInfo[i].VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn)
			{
				continue;
			}
			if(VochnInfo[i].SlaveId == PRV_MASTER)
			{
				//HI_S32 s32Ret = HI_SUCCESS;
				//printf("VochnInfo[i].VdecChn:%d, VochnInfo[i].VoChn:%d\n", VochnInfo[i].VdecChn, VochnInfo[i].VoChn);
				PRV_VDEC_UnBindVpss(VochnInfo[i].VdecChn, VochnInfo[i].VoChn);
				CHECK(PRV_VDEC_BindVpss(VochnInfo[i].VdecChn, VochnInfo[i].VoChn));
			}
//			else if(VochnInfo[i].SlaveId > PRV_MASTER)			
//				CHECK(HI_MPI_VI_BindOutput(PRV_HD_DEV, 0, VoDev, VochnInfo[i].VoChn));
			
			VochnInfo[i].IsBindVdec[VoDev] = (VochnInfo[i].VdecChn == DetVLoss_VdecChn) ? 0 : 1;
		}
	}
	
	RET_SUCCESS("");

}

/*****************************************************************************
* function : start vpss. VPSS chn with frame
*****************************************************************************/
HI_S32 PRV_VPSS_Start(VPSS_GRP VpssGrp)
{
#if defined(Hi3535)
	VPSS_CHN VpssChn;
	VPSS_GRP_ATTR_S stGrpAttr;
	VPSS_CHN_ATTR_S stChnAttr;
	VPSS_GRP_PARAM_S stVpssParam;
	HI_S32 s32Ret = 0, i = 0;
//	VPSS_GRP_ATTR_S *pstVpssGrpAttr = NULL;
	/*** Set Vpss Grp Attr ***/

	stGrpAttr.u32MaxW = 2048;
	stGrpAttr.u32MaxH = 1536;
	stGrpAttr.bDciEn = HI_FALSE;
	stGrpAttr.bIeEn = HI_FALSE;
	stGrpAttr.bNrEn = HI_FALSE;
	stGrpAttr.bHistEn = HI_FALSE;
	stGrpAttr.enDieMode = VPSS_DIE_MODE_NODIE;
	stGrpAttr.enPixFmt = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
	
	/*** create vpss group ***/
	s32Ret = HI_MPI_VPSS_CreateGrp(VpssGrp, &stGrpAttr);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_CreateGrp failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}

	/*** set vpss param ***/
	s32Ret = HI_MPI_VPSS_GetGrpParam(VpssGrp,  &stVpssParam);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	
	s32Ret = HI_MPI_VPSS_SetGrpParam(VpssGrp,  &stVpssParam);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	
	/*** enable vpss chn, with frame ***/
	stChnAttr.bSpEn    = HI_FALSE;
	stChnAttr.bBorderEn = HI_FALSE;
	stChnAttr.stBorder.u32BottomWidth = 0;
	stChnAttr.stBorder.u32LeftWidth = 0;
	stChnAttr.stBorder.u32RightWidth = 0;
	stChnAttr.stBorder.u32TopWidth = 0;
	stChnAttr.stBorder.u32Color = 0;
	
	for(i = 0; i < VPSS_MAX_CHN_NUM; i++)
	{
		VpssChn = i;
		/* Set Vpss Chn attr */
		
		s32Ret = HI_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn, &stChnAttr);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_SetChnAttr failed with %#x\n", s32Ret);
			return HI_FAILURE;
		}

		s32Ret = HI_MPI_VPSS_EnableChn(VpssGrp, VpssChn);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_EnableChn failed with %#x\n", s32Ret);
			return HI_FAILURE;
		}
	}

	/*** start vpss group ***/
	s32Ret = HI_MPI_VPSS_StartGrp(VpssGrp);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_StartGrp failed with %#x\n", s32Ret);
		return HI_FAILURE;
	}
	
	return HI_SUCCESS;
#else

    VPSS_CHN VpssChn;
    VPSS_GRP_ATTR_S stGrpAttr;
    VPSS_CHN_ATTR_S stChnAttr;
    VPSS_GRP_PARAM_S stVpssParam;
    HI_S32 s32Ret = 0, i = 0;
//	VPSS_GRP_ATTR_S *pstVpssGrpAttr = NULL;
    /*** Set Vpss Grp Attr ***/

    //stGrpAttr.u32MaxW = 2048;
    //stGrpAttr.u32MaxH = 1536;
    stGrpAttr.u32MaxW = 2560;
    stGrpAttr.u32MaxH = 1920;
    stGrpAttr.bDrEn = HI_FALSE;
    stGrpAttr.bDbEn = HI_FALSE;
    stGrpAttr.bIeEn = HI_TRUE;
    stGrpAttr.bNrEn = HI_TRUE;
    stGrpAttr.bHistEn = HI_FALSE;
    stGrpAttr.enDieMode = VPSS_DIE_MODE_AUTO;
    stGrpAttr.enPixFmt = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
	
    /*** create vpss group ***/
    s32Ret = HI_MPI_VPSS_CreateGrp(VpssGrp, &stGrpAttr);
    if (s32Ret != HI_SUCCESS)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_CreateGrp failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }

    /*** set vpss param ***/
    s32Ret = HI_MPI_VPSS_GetGrpParam(VpssGrp,  &stVpssParam);
    if (s32Ret != HI_SUCCESS)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
    
    stVpssParam.u32MotionThresh = 0;
    
    s32Ret = HI_MPI_VPSS_SetGrpParam(VpssGrp,  &stVpssParam);
    if (s32Ret != HI_SUCCESS)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
	
    /*** enable vpss chn, with frame ***/
	stChnAttr.bSpEn    = HI_FALSE;
	stChnAttr.bFrameEn = HI_FALSE;
	stChnAttr.stFrame.u32Color[VPSS_FRAME_WORK_LEFT]   = 0xff00;
	stChnAttr.stFrame.u32Color[VPSS_FRAME_WORK_RIGHT]  = 0xff00;
	stChnAttr.stFrame.u32Color[VPSS_FRAME_WORK_BOTTOM] = 0xff00;
	stChnAttr.stFrame.u32Color[VPSS_FRAME_WORK_TOP]    = 0xff00;
	stChnAttr.stFrame.u32Width[VPSS_FRAME_WORK_LEFT]   = 2;
	stChnAttr.stFrame.u32Width[VPSS_FRAME_WORK_RIGHT]  = 2;
	stChnAttr.stFrame.u32Width[VPSS_FRAME_WORK_TOP]    = 2;
	stChnAttr.stFrame.u32Width[VPSS_FRAME_WORK_BOTTOM] = 2;
    for(i = 0; i < VPSS_MAX_CHN_NUM; i++)
    {
        VpssChn = i;
        /* Set Vpss Chn attr */
        
        s32Ret = HI_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn, &stChnAttr);
        if (s32Ret != HI_SUCCESS)
        {
            TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_SetChnAttr failed with %#x\n", s32Ret);
            return HI_FAILURE;
        }

        s32Ret = HI_MPI_VPSS_EnableChn(VpssGrp, VpssChn);
        if (s32Ret != HI_SUCCESS)
        {
            TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_EnableChn failed with %#x\n", s32Ret);
            return HI_FAILURE;
        }
    }

    /*** start vpss group ***/
    s32Ret = HI_MPI_VPSS_StartGrp(VpssGrp);
    if (s32Ret != HI_SUCCESS)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VPSS_StartGrp failed with %#x\n", s32Ret);
        return HI_FAILURE;
    }
	
    return HI_SUCCESS;
#endif
}

HI_S32 PRV_VPSS_Stop(VPSS_GRP VpssGrp)
{
    HI_S32 s32Ret = 0;
	HI_S32 i = 0;
	s32Ret = HI_MPI_VPSS_StopGrp(VpssGrp);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"HI_MPI_VPSS_StopGrp-------failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	for(i = 0; i < VPSS_MAX_CHN_NUM; i++)
	{
		s32Ret = HI_MPI_VPSS_DisableChn(VpssGrp, i);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"HI_MPI_VPSS_DisableChn-------failed with %#x!\n", s32Ret);
			return HI_FAILURE;
		}
	}
	s32Ret = HI_MPI_VPSS_DestroyGrp(VpssGrp);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"HI_MPI_VPSS_DestroyGrp-------failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}

	return HI_SUCCESS;
}

#endif


/*************************************************
Function: //PRV_ViInit
Description: //所有VI设备及其上的通道初始化。
Calls: 
Called By: //
Input: // 无
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_ViInit(HI_VOID)
{
	HI_S32 i = 0;
	if(LOCALVEDIONUM > 0)
	{
#if defined(SN9234H1)
		s_stViDevPubAttrDflt.enViNorm = (0 == s_s32NPFlagDflt) ? VIDEO_ENCODING_MODE_PAL : VIDEO_ENCODING_MODE_NTSC;
		s_stViDevPubAttrDflt.enWorkMode = (0 == s_s32ViWorkMode) ? VI_WORK_MODE_4D1 : VI_WORK_MODE_4HALFD1;

		PRV_TW2865_CfgV(s_stViDevPubAttrDflt.enViNorm, s_stViDevPubAttrDflt.enWorkMode);
#else
		s_stViDevPubAttrDflt.enWorkMode = (0 == s_s32ViWorkMode) ? VI_WORK_MODE_4Multiplex: VI_WORK_MODE_1Multiplex;
#endif
	//对于2片3520，那么需要初始化0、1、2，3输入设备，以实现16D1的输入，其中0、1两个用于高清输入
	//对于4片3520的方案，那么每片3520只需要用到输入设备0、1、3就可以了，其中0、1两个用于高清输入
		//初始化输入设备
	//#if defined(SN6116HE)||defined(SN6116LE)||defined(SN6108HE)	|| defined(SN8616D_LE)|| defined(SN8616M_LE) || defined(SN9234H1)
#if defined(SN_SLAVE_ON)
		s_stViDevPubAttrDflt.enInputMode = VI_MODE_BT656;
		s_stViDevPubAttrDflt.bChromaSwap = HI_FALSE;
		s_stViDevPubAttrDflt.bIsChromaChn = HI_FALSE;
		s_stViChnAttrDflt.stCapRect.s32X= 8;
		s_stViChnAttrDflt.stCapRect.u32Width= 704;	
		//for(i = PRV_656_DEV; i < PRV_VI_DEV_NUM; i++)
		for(i = (LOCALVEDIONUM > PRV_VI_CHN_NUM ? PRV_656_DEV : PRV_656_DEV_1); i < PRV_VI_DEV_NUM; i++)
#else
		//for(i = 0;i < PRV_VI_DEV_NUM; i++)
		for(i = 0; i < (LOCALVEDIONUM/PRV_VI_CHN_NUM + 1); i++)
#endif
		{
			PRV_DisableAllViChn(i);
			usleep(100000);
			PRV_DisableViDev(i);

			CHECK_RET(PRV_EnableViDev(i));
			CHECK_RET(PRV_EnableAllViChn(i));
		}
	}
//#if defined(SN6116HE)||defined(SN6116LE)||defined(SN6108HE)	|| defined(SN8616D_LE)|| defined(SN8616M_LE)|| defined(SN9234H1)
#if defined(SN_SLAVE_ON)
	HI_U32 u32SrcFrmRate;

	//初始化高清接口输入设备
	PRV_DisableAllViChn(PRV_HD_DEV);
	usleep(100000);
	PRV_DisableViDev(PRV_HD_DEV);

	s_stViDevPubAttrDflt.enInputMode = VI_MODE_BT1120_PROGRESSIVE;
	s_stViDevPubAttrDflt.bChromaSwap = HI_TRUE;
	s_stViDevPubAttrDflt.bIsChromaChn = HI_FALSE;
	CHECK_RET(PRV_EnableViDev(PRV_HD_DEV));
	
	u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == s_stViDevPubAttrDflt.enViNorm) ? 25: 30;
	s_stViChnAttrDflt.enCapSel = (0 == s_s32ViWorkMode) ? VI_CAPSEL_BOTH : VI_CAPSEL_BOTTOM;
	s_stViChnAttrDflt.bDownScale = (0 == s_s32ViWorkMode) ? HI_FALSE : HI_TRUE;
	s_stViChnAttrDflt.stCapRect.s32X= 0;
	s_stViChnAttrDflt.stCapRect.u32Width= PRV_BT1120_SIZE_W;
	s_stViChnAttrDflt.stCapRect.u32Height = PRV_BT1120_SIZE_H;
	//s_stViChnAttrDflt.stCapRect.u32Width= 704;
	//s_stViChnAttrDflt.stCapRect.u32Height = 576;
	CHECK_RET(HI_MPI_VI_SetChnAttr(PRV_HD_DEV, 0, &s_stViChnAttrDflt));
	CHECK_RET(HI_MPI_VI_EnableChn(PRV_HD_DEV, 0));
	CHECK_RET(HI_MPI_VI_SetSrcFrameRate(PRV_HD_DEV, 0, u32SrcFrmRate));
	CHECK_RET(HI_MPI_VI_SetFrameRate(PRV_HD_DEV, 0, u32SrcFrmRate));
#endif	
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_VoInit
Description: //所有VO设备及其上的视频层和通道初始化。
Calls: 
Called By: //
Input: // 无
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_VoInit(HI_VOID)
{
#if defined(SN_SLAVE_ON)	
	//禁用级联
	if (HI_SUCCESS != HI_MPI_VI_DisableCascade(0,0))
	{
		RET_FAILURE("Disalbe vi cascade filter failed!!");
	}
#endif	

	HI_S32 i;
	//只需设置DHD0(VGA输出)和DSD0(CVBS输出)
	for (i = 0; i < PRV_VO_DEV_NUM; i++)
	{
		PRV_DisableAllVoChn(i);
		PRV_DisableVideoLayer(i);
		PRV_DisableVoDev(i);

#if defined(SN9234H1)
		if(i < AD)
#else
		if(i > DHD0)
			continue;
		if(i != DHD0 && i != DSD0)
			continue;
#endif		
		{
		PRV_EnableVoDev(i);
		PRV_EnableVideoLayer(i);
		PRV_EnableAllVoChn(i);
		}
	}

#if defined(SN_SLAVE_ON)	
	//启用级联
	//HI_MPI_VI_EnableCascade(PRV_HD_DEV,0);
	if (HI_SUCCESS != HI_MPI_VI_EnableCascade(0,0))
	{
		RET_FAILURE("Enalbe vi cascade filter failed!!");
	}
#endif	

	RET_SUCCESS("");
}
/*************************************************
Function: //PRV_Msg_Cpy
Description: //保存当前消息信息
Calls: 
Called By: //
Input: // msg_req :消息结构体
Output: // 无
Return: //无
Others: // 其它说明
************************************************************************/
#if  defined(SN_SLAVE_ON)
static int PRV_Msg_Cpy(const SN_MSG *msg_req)
{
	int ret=0;
	if(s_State_Info.Prv_msg_Cur)
	{
		SN_FREE(s_State_Info.Prv_msg_Cur);//释放已完成的消息结构体指针
	}	
	s_State_Info.Prv_msg_Cur = (SN_MSG *)SN_MALLOC(sizeof(SN_MSG)+msg_req->size);
	if(s_State_Info.Prv_msg_Cur == NULL)
	{//
		ret = -1;
		return ret;
	}
	s_State_Info.Prv_msg_Cur->size= msg_req->size;
	s_State_Info.Prv_msg_Cur->source = msg_req->source;
	s_State_Info.Prv_msg_Cur->dest= msg_req->dest;
	s_State_Info.Prv_msg_Cur->user= msg_req->user;
	s_State_Info.Prv_msg_Cur->xid= msg_req->xid;
	s_State_Info.Prv_msg_Cur->thread= msg_req->thread;
	s_State_Info.Prv_msg_Cur->msgId= msg_req->msgId;
	if(s_State_Info.Prv_msg_Cur->para)
		SN_MEMCPY((char *)s_State_Info.Prv_msg_Cur->para,msg_req->size,(char *)msg_req->para,msg_req->size,msg_req->size);
	return ret;
}
#endif
/************************************************************************/
/*       视频丢失判断                                           */
/************************************************************************/
static int Loss_State_Pre=0;
static int Loss_State_Cur=0;
extern int FWK_GetLostState();
//static int except_alarm[PRV_CHAN_NUM]={0};  //输入输出制式不匹配
int Vedio_Loss_State(unsigned char ch)
{
	int ret=0;
	Loss_State_Cur = FWK_GetLostState();
	ret = Loss_State_Cur;
	return ret;
}
/*************************************************
Function: //PRV_VLossDet
Description: //   视频丢失处理     
Calls: 
Called By: //
Input: //
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 时间线程，没500毫秒调用一次，设置无视频信号
************************************************************************/

static int PRV_VLossDet(void)
{
    int i,is_lost=-1,is_change=0,try_cnt=0;
	VedioLoss_State  loss_state; 
	//return 0;
    for (i = 0; i < LOCALVEDIONUM; i++)
    {
		try_cnt = 0;
		is_change = (Loss_State_Pre>>i)&0x01;
try:		
        is_lost = Preview_GetAVstate(i);
		if(is_lost == -1)
		{
			RET_FAILURE("Preview_GetAVstate error!!!");
		}
#if 1
//增加视频检测去抖功能
		if(is_lost != is_change)
		{
			if(try_cnt < 2)
			{
				try_cnt++;
				usleep(10000);	//延时10毫秒后，再次查询
				goto try;
			}
		}
		/*
		else
		{
			if(try_cnt <2 && try_cnt>0)
			{//如果测试相同，未达到测试次数，那么继续测试
				try_cnt++;
				usleep(10000);	//延时10毫秒后，再次查询
				goto try;
			}
		}*/
#endif	
		if(is_lost != is_change)
		{
			if (is_lost)
	        {
#if defined(SN6116HE) ||defined(SN6116LE) || defined(SN6108HE) || defined(SN6108LE) || defined(SN8608D_LE) || defined(SN8608M_LE) || defined(SN8616D_LE) || defined(SN8616M_LE)|| defined(SN9234H1)
	        	if(i < PRV_CHAN_NUM)
	        	{//如果为前8个通道，那么在主片配置无视频信号，如果是后8路，需要发送消息给从片，让从片配置无视频吸纳好哦
					if(i>= PRV_VI_CHN_NUM)
					{	
#if defined(SN9234H1)
						//如果为通道5到8，那么配置输入设备2
		            	CHECK(HI_MPI_VI_EnableUserPic(PRV_656_DEV, i%PRV_VI_CHN_NUM));
#else
						//如果为通道5到8，那么配置输入设备2
		            	CHECK(HI_MPI_VI_EnableUserPic(i%PRV_VI_CHN_NUM));
#endif
					}
					else
					{
#if defined(SN9234H1)
						//如果为通道1到4，那么配置输入设备3
						CHECK(HI_MPI_VI_EnableUserPic(PRV_656_DEV_1, i%PRV_VI_CHN_NUM));
#else
						//如果为通道1到4，那么配置输入设备3
						CHECK(HI_MPI_VI_EnableUserPic(i%PRV_VI_CHN_NUM));
#endif
					}
	        	}
#if defined(SN_SLAVE_ON)				
				else	
				//如果为通道9到16，那么发送消息给从片。
				//不等待从片返回信息
				{
					Prv_Slave_Vloss_Ind slave_req;
					slave_req.chn = i;
					slave_req.state = HI_TRUE;
					//printf("@####################loss i = %d##################\n",i);
					SN_SendMccMessageEx(PRV_SLAVE_1,SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_VLOSS_IND, &slave_req, sizeof(Prv_Slave_Vloss_Ind));		
				}
#endif
#else
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@####################i = %d, vichn=%d, videv=%d##################\n",i, i/PRV_VI_CHN_NUM,i%PRV_VI_CHN_NUM);
#if defined(SN9234H1)
				CHECK(HI_MPI_VI_EnableUserPic(i/PRV_VI_CHN_NUM, i%PRV_VI_CHN_NUM));
#else
				CHECK(HI_MPI_VI_EnableUserPic(i%PRV_VI_CHN_NUM));
#endif
#endif
				Loss_State_Cur = Loss_State_Cur | (1<<i);
	        }
	        else
	        {
#if defined(SN6116HE) ||defined(SN6116LE) || defined(SN6108HE) || defined(SN6108LE) || defined(SN8608D_LE) || defined(SN8608M_LE) || defined(SN8616D_LE) || defined(SN8616M_LE) || defined(SN9234H1)
	        	//printf("@####################i = %d##################\n",i);
	        	if(i< PRV_CHAN_NUM)
	        	{//如果为前8个通道，那么在主片取消无视频信号，如果是后8路，需要发送消息给从片，让从片取消无视频吸纳好哦
					if(i>= PRV_VI_CHN_NUM)
					{
#if defined(SN9234H1)
		            	CHECK(HI_MPI_VI_DisableUserPic(PRV_656_DEV, i%PRV_VI_CHN_NUM));
#else
		            	CHECK(HI_MPI_VI_DisableUserPic(i%PRV_VI_CHN_NUM));
#endif
					}
					else
					{
#if defined(SN9234H1)
						CHECK(HI_MPI_VI_DisableUserPic(PRV_656_DEV_1, i%PRV_VI_CHN_NUM));
#else						
						CHECK(HI_MPI_VI_DisableUserPic(i%PRV_VI_CHN_NUM));
#endif
					}
	        	}
#if defined(SN_SLAVE_ON)				
				else	
				//如果为通道9到16，那么发送消息给从片。
				//不等待从片返回信息
				{
					Prv_Slave_Vloss_Ind slave_req;
					slave_req.chn = i;
					slave_req.state = HI_FALSE;
					//printf("@####################unloss i = %d##################\n",i);
					SN_SendMccMessageEx(PRV_SLAVE_1,0xFFFFFFFF, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_VLOSS_IND, &slave_req, sizeof(Prv_Slave_Vloss_Ind));		
				}
#endif				
#else
#if defined(SN9234H1)
				CHECK(HI_MPI_VI_DisableUserPic(i/PRV_VI_CHN_NUM, i%PRV_VI_CHN_NUM));
#else
				CHECK(HI_MPI_VI_DisableUserPic(i%PRV_VI_CHN_NUM));
#endif
#endif
				Loss_State_Cur = Loss_State_Cur &( ~(1<<i));
	        }      
		} 
#if 0		
		if(!is_lost)/*有视频，则进行制式检查！*/
		{
			if(PRV_Compare_Stand(i)>0)
			{
				ch |= 1<<i;
			}
		}
#endif		
    }
	if (Loss_State_Pre != Loss_State_Cur) //如果有视频丢失，那么发送视频丢失报警
	{
		loss_state.loss_state = Loss_State_Cur;
		SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_ALM, 0, 0, MSG_ID_VEDIO_LOSS_IND, &loss_state, sizeof(loss_state));
		SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_VAM, 0, 0, MSG_ID_VEDIO_LOSS_IND, &loss_state, sizeof(loss_state));
		Loss_State_Pre = Loss_State_Cur;
	}
#if 0	
	if (ch)
	{
		ALM_EXP_ALARM_ST stAlarmInfo;
		stAlarmInfo.u8ExpType = EXP_VIDEO_STANDARD;
		stAlarmInfo.u32Info[0] = ch;
		stAlarmInfo.u32Info[1] = 0;
		SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_ALM, 0, 0, MSG_ID_ALM_EXCEPTION_ALARM_REQ, &stAlarmInfo, sizeof(stAlarmInfo));
	}
#endif	
    return 0;
}

#if defined(SN9234H1)
/*************************************************
Function: //PRV_NVRChnVLossDet
Description: //   数字通道视频丢失处理     
Calls: 
Called By: //
Input: //
Output: // 
Return: //无
Others: // 时间更新线程，每500毫秒调用一次，显示/隐藏"无视频信号"报警图片
************************************************************************/
void PRV_NVRChnVLossDet()
{
	if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_PB || s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_PIC)
		return;
	if(!s_State_Info.bIsOsd_Init)//OSD未初始化
		return;
	int i = LOCALVEDIONUM, s32Ret = 0;
	for(; i < DEV_CHANNEL_NUM; i++)
	{
		//指定的通道在当前预览画面中
		if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[i].VoChn))
		{
			
			if(VochnInfo[i].VdecChn == DetVLoss_VdecChn//指定通道无视频信号
				&& 0 == IsOSDAlarmOn[i - LOCALVEDIONUM])//且没有无视频报警图标
			{
				//显示报警图标
				s32Ret = OSD_Ctl(VochnInfo[i].VoChn, 1, OSD_ALARM_TYPE);
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH, MOD_PRV, "PRV_NVRChnVLossDet OSD_Ctl faild 0x%x!\n",s32Ret);
					continue;
				}
				IsOSDAlarmOn[i - LOCALVEDIONUM] = 1;
			}
			else if(VochnInfo[i].VdecChn != DetVLoss_VdecChn//指定通道有视频信号
				&& 1 == IsOSDAlarmOn[i - LOCALVEDIONUM])//且显示了无视频报警图标
			{
				//隐藏报警图标
				s32Ret = OSD_Ctl(VochnInfo[i].VoChn, 0, OSD_ALARM_TYPE);
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH, MOD_PRV, "PRV_NVRChnVLossDet OSD_Ctl faild 0x%x!\n",s32Ret);
					continue;
				}
				
				IsOSDAlarmOn[i - LOCALVEDIONUM] = 0;
			}
		}
	}
}

#endif

/*************************************************
Function: //PRV_ReadNvrNoVideoPic
Description: //  保存"无网络视频"图片的信息
Calls: 
Called By: //
Input: // // fileName图片文件名
Output: // Buffer[]图片信息
Return: //图片信息长度
Others: // 其它说明
************************************************************************/
HI_S32 PRV_ReadNvrNoVideoPic(char *fileName, unsigned char Buffer[])
{
	int dataLen = 0;
	
	FILE* fp = NULL;
	fp = fopen(fileName, "r");
	if(fp == NULL)
	{
		perror("fopen");
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Open file: %s failed!\n", fileName);
		return -1;
	}
	dataLen = fread(Buffer, sizeof(char), VLOSSPICBUFFSIZE, fp);
	//printf("----------datalen: %d, fileName: %s\n", dataLen, fileName);
	return dataLen;
}

#if defined(SN9234H1)
/*************************************************
Function: //PRV_NvrNoVideoDet
Description: //  无网络视频信号检测。
			将无网络视频图片专用解码通道与输出通道绑定
Calls: 
Called By: //
Input: // // 无
Output: // 无
Return: //无
Others: // 其它说明
************************************************************************/

void PRV_NvrNoVideoDet()
{
	HI_S32 i = 0, j = 0, s32Ret = 0;
#if defined (SN8604D) || defined (SN8604M)
	for(j = LOCALVEDIONUM; j < DEV_CHANNEL_NUM; j++)
	{
		if(VochnInfo[j].VdecChn == DetVLoss_VdecChn/* && VochnInfo[j].IsBindVdec[HD] == -1*/)
		{	
			s32Ret = HI_MPI_VDEC_BindOutput(VochnInfo[j].VdecChn, HD, j);
			#if 0
			if(s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "------------Vdec: %d Bind Vo: %d fail\n", VochnInfo[j].VdecChn, j);
			}
			else
			{				
				VochnInfo[j].IsBindVdec[HD] = 0;
			}
			#endif
		}
	}
	for(j = LOCALVEDIONUM; j < DEV_CHANNEL_NUM; j++)
	{
		if(VochnInfo[j].VdecChn == DetVLoss_VdecChn /*&& VochnInfo[j].IsBindVdec[s_VoSecondDev] == -1*/)
		{	
			s32Ret = HI_MPI_VDEC_BindOutput(VochnInfo[j].VdecChn, s_VoSecondDev, j);
			#if 0
			if(s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "------------Vdec: %d Bind Vo: %d fail\n", VochnInfo[j].VdecChn, j);
			}
			else
			{				
				VochnInfo[j].IsBindVdec[s_VoSecondDev] = 0;
			}
			#endif
		}
	}
#else
	for(i = 0; i < PRV_VO_DEV_NUM; i++)
	{
		if(i == SPOT_VO_DEV || i == AD)
			continue;
		for(j = LOCALVEDIONUM; j < DEV_CHANNEL_NUM; j++)
		{
			if(VochnInfo[j].VdecChn == DetVLoss_VdecChn/* && VochnInfo[j].IsBindVdec[i] == -1*/)
			{	
				s32Ret = HI_MPI_VDEC_BindOutput(VochnInfo[j].VdecChn, i, j);
				#if 0
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "------------Vdec: %d Bind Vo: %d fail\n", VochnInfo[j].VdecChn, j);
				}
				else
				{				
					VochnInfo[j].IsBindVdec[i] = 0;
				}
				#endif
			}
		}
	}
#endif
}

#endif

/*************************************************
Function: //PRV_BindHifbVo
Description: //设置GUI层的输出绑定关系。
Calls: //
Called By: // Prv_VoInit，预览初始化VI调用
Input: // dev: VO设备ID
		hifb:FB设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/ 
static HI_S32 PRV_BindHifbVo(int dev, int hifb)
{
#if defined(SN9234H1)
	VD_BIND_S stBindAtrr;
	int ret;
	int fd;
	
	if (HD != dev && AD != dev)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "bad parameter: dev\n");
		RET_FAILURE("");
	}
	fd = open("/dev/vd", O_RDWR, 0);
	if(fd < 0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "open vd failed!\n");
		RET_FAILURE("");
	}
	stBindAtrr.DevId = dev;
	stBindAtrr.s32GraphicId = hifb;
	
	ret = ioctl(fd, VD_SET_GRAPHIC_BIND, &stBindAtrr);
	if (ret != 0)
	{
		//fprintf(stderr, "VD_SET_GRAPHIC_BIND error: %d[%#x010]: %s\n", ret, ret, strerror(ret));
		close(fd);
		RET_FAILURE("");
	}
	close(fd);
#elif defined(Hi3535)
	HI_MPI_VO_UnBindGraphicLayer(hifb, dev);
	HI_MPI_VO_UnBindGraphicLayer(3, dev);
	HI_MPI_VO_BindGraphicLayer(hifb, dev);
#else
	VOU_GFX_BIND_LAYER_E enGfxBindLayer;
	if (dev > DSD0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "bad parameter: dev\n");
		RET_FAILURE("");
	}
	for (enGfxBindLayer = GRAPHICS_LAYER_G4; enGfxBindLayer < GRAPHICS_LAYER_BUTT; ++enGfxBindLayer)
	{
		HI_MPI_VO_GfxLayerUnBindDev(enGfxBindLayer, dev);
	}
	HI_MPI_VO_GfxLayerBindDev(hifb, dev);
#endif	
	RET_SUCCESS("");
}
/*************************************************
Function: //PRV_BindGuiVo
Description: //设置GUI层即G1图形层的输出绑定关系。
Calls: //
Called By: // Prv_VoInit，预览初始化VI调用
Input: // dev: VO设备ID
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/ 
HI_S32 PRV_BindGuiVo(int dev)
{
#if defined(SN9234H1)	
	CHECK_RET(PRV_BindHifbVo(dev, G1));
#endif	
	RET_SUCCESS("");
}
/*************************************************
Function: //PRV_Get_Max_chnnum
Description: //   计算当前预览模式下顺序数组最大通道数
Calls: 
Called By: //
Input: //ePreviewMode:预览模式
		pMax_chn_num:返回当前最大通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_Get_Max_chnnum(PRV_PREVIEW_MODE_E ePreviewMode, HI_U32 *pMax_chn_num)
{
	switch(ePreviewMode)
	{		
		case SingleScene:
			*pMax_chn_num = 1;
			break;
		case ThreeScene:
			*pMax_chn_num = 3;
			break;
		case FourScene:
		case LinkFourScene:
			*pMax_chn_num = 4;
			break;
		case FiveScene:
			*pMax_chn_num = 5;
			break;
		case SevenScene:
			*pMax_chn_num = 7;
			break;
		case NineScene:
		case LinkNineScene:
			*pMax_chn_num = 9;
			break;
		case SixteenScene:
			*pMax_chn_num = 16;
			break;
		default:
			RET_FAILURE("Invalid Parameter: enPreviewMode");
	}
		RET_SUCCESS("");
}


int PRV_Check_LinkageGroup(int VoDev,PRV_PREVIEW_MODE_E enPreviewMode)
{
	int i = 0,j = 0,Ret = 0;	
	unsigned int Max_num = 0;
	int MaxVoChn = 0,sqrtVo = 0,VoChn = 0,index0 = 0,index1 = 0,index2 = 0;
	int OsdNameType[DEV_CHANNEL_NUM];
	SN_MEMSET(OsdNameType,0,sizeof(OsdNameType));
	int u32Index = s_astVoDevStatDflt[s_VoDevCtrlDflt].s32PreviewIndex;
	CHECK_RET(PRV_Get_Max_chnnum(enPreviewMode, &Max_num));
	if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1||s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat != PRV_STAT_NORM||s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsAlarm==HI_TRUE||s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle==HI_TRUE)
	{
		Ret = OSD_Compare_NameType(OsdNameType);
		OSD_Set_NameType(OsdNameType);
		return Ret;
	}
	switch(enPreviewMode)
	{
		case SingleScene:
			MaxVoChn = 1;
			sqrtVo = 0;
			break;
		case TwoScene:
			MaxVoChn = 2;
			sqrtVo = 2;
			break;
		case ThreeScene:
			MaxVoChn = 3;
			sqrtVo = 3;
			break;
		case FourScene:
		case LinkFourScene:
			MaxVoChn = 4;
			sqrtVo = 2;
			break;
		case FiveScene:
			MaxVoChn = 5;
			sqrtVo = 2;
			break;
		case SixScene:
			MaxVoChn = 6;
			sqrtVo = 1;
			break;
		case SevenScene:
			MaxVoChn = 7;
			sqrtVo = 2;
			break;
		case EightScene:
			MaxVoChn = 8;
			sqrtVo = 1;
			break;
		case NineScene:
		case LinkNineScene:
			MaxVoChn = 9;
			sqrtVo = 3;
			break;
		case SixteenScene:
			MaxVoChn = 16;
			sqrtVo = 4;
			break;
		default:
			RET_FAILURE("Invalid Parameter: PRV_UpdateChnPrevState");
	}

	for (i = 0; i < MaxVoChn && u32Index + i < MaxVoChn; i++)
    {
		//点位控制和被动解码下下，无论通道是否隐藏，通道顺序如何，各个通道均显示
		index0 = -1;
		index1 = -1;
		index2 = -1;
		VoChn = s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][(u32Index + i) % Max_num];
		
		//printf("i: %d, u32ChnNum: %d, PRV_CurDecodeMode: %d, Vochn: %d\n", i, u32ChnNum, PRV_CurDecodeMode, VoChn);
		/* 判断VoChn是否有效 */
		if (VoChn < 0 || VoChn >= g_Max_Vo_Num)
		{
			continue;//break;//
		}
		index0 = PRV_GetVoChnIndex(VoChn);
		if(i<MaxVoChn-1)
		{
			VoChn = s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][(u32Index + i+1) % Max_num];
			if (VoChn < 0 || VoChn >= g_Max_Vo_Num)
			{
				continue;//break;//
			}
			index1 = PRV_GetVoChnIndex(VoChn);
		}
		if(sqrtVo>2 && i<MaxVoChn-2)
		{
			VoChn = s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][(u32Index + i+2) % Max_num];
			if (VoChn < 0 || VoChn >= g_Max_Vo_Num)
			{
				continue;//break;//
			}
			index2 = PRV_GetVoChnIndex(VoChn);
		}
		for(j=0;j<LINKAGE_MAX_GROUPNUM;j++)
		{
			if((g_PrmLinkAge_Cfg[j].DevNum == 3) && (sqrtVo>=3)&&(i%sqrtVo<=sqrtVo-3))
			{
				
				
				if((g_PrmLinkAge_Cfg[j].GroupChn[0].DevChn!=0 && g_PrmLinkAge_Cfg[j].GroupChn[0].SerialNo!=0 \
						&& index0==g_PrmLinkAge_Cfg[j].GroupChn[0].DevChn-1 && SCM_GetChnSwitchSerialNo(index0)==g_PrmLinkAge_Cfg[j].GroupChn[0].SerialNo-1)\
					&&(g_PrmLinkAge_Cfg[j].GroupChn[1].DevChn!=0 && g_PrmLinkAge_Cfg[j].GroupChn[1].SerialNo!=0 \
						&& index1==g_PrmLinkAge_Cfg[j].GroupChn[1].DevChn-1 && SCM_GetChnSwitchSerialNo(index1)==g_PrmLinkAge_Cfg[j].GroupChn[1].SerialNo-1)\
					&&(g_PrmLinkAge_Cfg[j].GroupChn[2].DevChn!=0 && g_PrmLinkAge_Cfg[j].GroupChn[2].SerialNo!=0 \
						&& index2==g_PrmLinkAge_Cfg[j].GroupChn[2].DevChn-1 && SCM_GetChnSwitchSerialNo(index2)==g_PrmLinkAge_Cfg[j].GroupChn[2].SerialNo-1))
				{
					OsdNameType[index0] = j+1;
					OsdNameType[index1] = -1;
					OsdNameType[index2] = -1;
					i += 2;
					break;
				}
			}
			else if((g_PrmLinkAge_Cfg[j].DevNum == 2)&&(sqrtVo>=2)&&(((i%sqrtVo<=sqrtVo-2)&&enPreviewMode!=SevenScene)||((i%sqrtVo!=0)&&enPreviewMode==SevenScene)))
			{
				if(((g_PrmLinkAge_Cfg[j].GroupChn[0].DevChn!=0 && g_PrmLinkAge_Cfg[j].GroupChn[0].SerialNo!=0 && index0==g_PrmLinkAge_Cfg[j].GroupChn[0].DevChn-1 && SCM_GetChnSwitchSerialNo(index0)==g_PrmLinkAge_Cfg[j].GroupChn[0].SerialNo-1)\
						&&(g_PrmLinkAge_Cfg[j].GroupChn[1].DevChn!=0 && g_PrmLinkAge_Cfg[j].GroupChn[1].SerialNo!=0 && index1==g_PrmLinkAge_Cfg[j].GroupChn[1].DevChn-1 && SCM_GetChnSwitchSerialNo(index1)==g_PrmLinkAge_Cfg[j].GroupChn[1].SerialNo-1))\
					||((g_PrmLinkAge_Cfg[j].GroupChn[0].DevChn!=0 && g_PrmLinkAge_Cfg[j].GroupChn[0].SerialNo!=0 && index0==g_PrmLinkAge_Cfg[j].GroupChn[0].DevChn-1 && SCM_GetChnSwitchSerialNo(index0)==g_PrmLinkAge_Cfg[j].GroupChn[0].SerialNo-1)\
						&&(g_PrmLinkAge_Cfg[j].GroupChn[2].DevChn!=0 && g_PrmLinkAge_Cfg[j].GroupChn[2].SerialNo!=0 && index1==g_PrmLinkAge_Cfg[j].GroupChn[2].DevChn-1 && SCM_GetChnSwitchSerialNo(index1)==g_PrmLinkAge_Cfg[j].GroupChn[2].SerialNo-1))\
					||((g_PrmLinkAge_Cfg[j].GroupChn[1].DevChn!=0 && g_PrmLinkAge_Cfg[j].GroupChn[1].SerialNo!=0 && index0==g_PrmLinkAge_Cfg[j].GroupChn[1].DevChn-1 && SCM_GetChnSwitchSerialNo(index0)==g_PrmLinkAge_Cfg[j].GroupChn[1].SerialNo-1)\
						&&(g_PrmLinkAge_Cfg[j].GroupChn[2].DevChn!=0 && g_PrmLinkAge_Cfg[j].GroupChn[2].SerialNo!=0 && index1==g_PrmLinkAge_Cfg[j].GroupChn[2].DevChn-1 && SCM_GetChnSwitchSerialNo(index1)==g_PrmLinkAge_Cfg[j].GroupChn[2].SerialNo-1)))
				{
					if(enPreviewMode == SevenScene)
					{
						printf("SevenScene,i:%d,sqrtVo:%d,%d\n",i,sqrtVo,i%sqrtVo);
						if(i%sqrtVo != 0)
						{
							OsdNameType[index0] = j+1;
							OsdNameType[index1] = -1;
							i++;
							break;
						}
					}
					else
					{
						OsdNameType[index0] = j+1;
						OsdNameType[index1] = -1;
						i++;
						break;
					}
				}
			}
		}
	}

	Ret = OSD_Compare_NameType(OsdNameType);
	OSD_Set_NameType(OsdNameType);
	return Ret;
}

/*************************************************
Function: //PRV_Chn2Index
Description: //   计算通道号所在索引位置
Calls: 
Called By: //
Input: //VoDev:设备号
		VoChn:通道号
		pu32Index:返回当前通道号对应索引号
		pOrder : 通道顺序
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
****************************************************/
STATIC HI_S32 PRV_Chn2Index(VO_DEV VoDev, VO_CHN VoChn, HI_U32 *pu32Index,VO_CHN *pOrder)
{
	HI_S32 i=0;//,idx=0;
	HI_U32 Max_num;
	if (pu32Index == NULL)
	{
		RET_FAILURE("Invalid Parameter: NULL");
	}
#if defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
#else
	if(VoDev > DHD0)
#endif		
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
#if defined(SN9234H1)
	if (VoDev < 0 || VoDev >= PRV_VO_MAX_DEV)
#else
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
#endif		
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, TEXT_COLOR_YELLOW("Invalid Parameter: VoDev:%d"), VoDev);
		RET_FAILURE("");
	}

	if (VoChn < 0 || VoChn >= g_Max_Vo_Num)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, TEXT_COLOR_YELLOW("Invalid Parameter: VoChn:%d"), VoChn);
	}
	//获取当前预览模式下最大通道数
	//CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[VoDev].enPreviewMode,&Max_num));
	//Max_num = SIXINDEX;
	if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
	{
#if defined(SN9234H1)		
		CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[HD].enPreviewMode,&Max_num));
#else
		CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[DHD0].enPreviewMode,&Max_num));
#endif
		if(VoChn < Max_num)
		{
			*pu32Index = VoChn;
			return HI_SUCCESS;
		}
		else
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "PRV_CurDecodeMode == PassiveDecode, VoChn: %d > Max_num: %d\n", VoChn, Max_num);
			return HI_FAILURE;
		}
	}
	Max_num = CHANNEL_NUM;

	for (i = 0; i < Max_num; i++)
	{
		if (VoChn == pOrder[i])
		{
			*pu32Index = i;
			//printf("!!!!!!!!!*pu32Index  = %d  VoChn = %d!!!!!!!!!!!\n",*pu32Index,VoChn);
			break;
		}
	}
	if (SIXINDEX == i)
	{
		RET_FAILURE("VoChn not found! is hiden??");
	}

	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_Point2Index
Description: //   计算通道号所在索引位置
Calls: 
Called By: //
Input: //pstPoint:当前的横纵坐标位置
		pu32Index:返回当前通道号对应索引号
		pOrder : 通道顺序
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
****************************************************/
STATIC HI_S32 PRV_Point2Index(const Preview_Point *pstPoint, HI_U32 *pu32Index,VO_CHN *pOrder)
{
	HI_U32 i, u32ChnNum;
	RECT_S *pstLayout = NULL;
	HI_U32 Max_num;
	
	//获取当前预览模式下最大通道数
	CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode,&Max_num));

	if (NULL == pstPoint || pu32Index == NULL)
	{
		RET_FAILURE("Invalid Parameter: NULL");
	}

	if (s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsAlarm)
	{//是否处于报警状态
		CHECK_RET(PRV_Chn2Index(s_VoDevCtrlDflt, s_astVoDevStatDflt[s_VoDevCtrlDflt].s32AlarmChn, pu32Index,pOrder));
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "---- alarm chn preview ----");
		RET_SUCCESS("");
	}
	if (s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle)
	{//是否单画面模式
		*pu32Index = s_astVoDevStatDflt[s_VoDevCtrlDflt].s32SingleIndex;
		if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
		{
			*pu32Index = DoubleToSingleIndex;
		}
		RET_SUCCESS("");
	}
	if (pstPoint->x < 0 || pstPoint->y <0)
	{
		*pu32Index = s_astVoDevStatDflt[s_VoDevCtrlDflt].s32PreviewIndex;
		if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
		{
			*pu32Index = 0;
		}
		//RET_SUCCESS("---- point not in screen : means using remote control ----");
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, TEXT_COLOR_GREEN("---- point not in screen : means using remote control ----")"X: %d, Y: %d\n", pstPoint->x, pstPoint->y);
		RET_SUCCESS("");
	}

	switch(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode)
	{
		case SingleScene:
			u32ChnNum = 1;
			pstLayout = s_astPreviewLayout1;
			break;
		case TwoScene:
			u32ChnNum = 2;
			pstLayout = s_astPreviewLayout2;
			break;
		case ThreeScene:
			u32ChnNum = 3;
			pstLayout = s_astPreviewLayout3;
			break;
		case FourScene:
		case LinkFourScene:
			u32ChnNum = 4;
			pstLayout = s_astPreviewLayout4;
			break;
		case FiveScene:
			u32ChnNum = 5;
			pstLayout = s_astPreviewLayout5;
			break;
		case SixScene:
			u32ChnNum = 6;
			pstLayout = s_astPreviewLayout6;
			break; 
		case SevenScene:
			u32ChnNum = 7;
			pstLayout = s_astPreviewLayout7;
			break;
		case EightScene:
			u32ChnNum = 8;
			pstLayout = s_astPreviewLayout8;
			break;
		case NineScene:
		case LinkNineScene:
			u32ChnNum = 9;
			pstLayout = s_astPreviewLayout9;
			break;
		case SixteenScene:
			u32ChnNum = 16;
			pstLayout = s_astPreviewLayout16;
			break;
		default:
			RET_FAILURE("Invalid Parameter: enPreviewMode");
	}
	//查找符合当前位置的通道索引ID
	for (i = 0; i<u32ChnNum; i++)
	{
		if (
			(pstLayout[i].s32X * s_u32GuiWidthDflt) / (PRV_PREVIEW_LAYOUT_DIV) <= pstPoint->x
			&& (pstLayout[i].s32Y * s_u32GuiHeightDflt) / PRV_PREVIEW_LAYOUT_DIV <= pstPoint->y
			&& ((pstLayout[i].s32X + pstLayout[i].u32Width) * s_u32GuiWidthDflt) / PRV_PREVIEW_LAYOUT_DIV >= pstPoint->x
			&& ((pstLayout[i].s32Y + pstLayout[i].u32Height) * s_u32GuiHeightDflt) / PRV_PREVIEW_LAYOUT_DIV >= pstPoint->y
			)
		{
			if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
			{
				*pu32Index = i;
				return HI_SUCCESS;
			}
			*pu32Index = i + s_astVoDevStatDflt[s_VoDevCtrlDflt].s32PreviewIndex;
			break;
		}
	}
	//判断当前ID是否超出范围，或者处于隐藏通道中
	if (u32ChnNum == i || *pu32Index >= Max_num)
	{
		RET_FAILURE("---- Point NOT in any chn! ----");
	}
	if (pOrder[*pu32Index] < 0 || pOrder[*pu32Index] >= g_Max_Vo_Num)
	{
		RET_FAILURE("---- Point is in hiden chn! ----");
	}
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, TEXT_COLOR_GREEN("%s")": index = " TEXT_COLOR_PURPLE("%d") ", chn = " TEXT_COLOR_PURPLE("%d")  ",x = " TEXT_COLOR_PURPLE("%d") " ,y = " TEXT_COLOR_PURPLE("%d") "\n", __FUNCTION__, *pu32Index, s_astVoDevStatDflt[s_VoDevCtrlDflt].as32ChnOrder[s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode][*pu32Index],pstPoint->x,pstPoint->y);
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_VoChnIsInCurLayOut
Description: //   指定输出通道是否在当前显示的画面中
Calls: 
Called By: //
Input: //VoDev:设备号
		VoChn:通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
****************************************************/
//指定输出通道是否在当前显示的画面中
int PRV_VoChnIsInCurLayOut(int VoDev, int VoChn)
{
	int u32ChnNum = 0, i = 0;
	PRV_PREVIEW_MODE_E enPreviewMode = s_astVoDevStatDflt[VoDev].enPreviewMode;
	HI_U32 u32Index = (s_astVoDevStatDflt[VoDev].bIsSingle == HI_TRUE) ? s_astVoDevStatDflt[VoDev].s32SingleIndex : s_astVoDevStatDflt[VoDev].s32PreviewIndex;
	HI_U32 Max_num = 0;

#if defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
#else
	if(VoDev > DHD0)
#endif		
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
#if defined(SN9234H1)
	if(VoDev < 0 || VoDev >= PRV_VO_MAX_DEV)
#else
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
#endif
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, TEXT_COLOR_RED("Invalid VoDev: %d\n"), VoDev);
		RET_FAILURE("");
	}
	if(VoChn < 0 || VoChn >= DEV_CHANNEL_NUM)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, TEXT_COLOR_RED("Invalid VoChn: %d\n"), VoChn);
		RET_FAILURE("");
	}
	if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_PB || s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_PIC)
		RET_FAILURE("In PB or PIC State");
	if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL)
	{
		if(VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn)
			return HI_SUCCESS;
		else 
			return HI_FAILURE;
	}
	
	if(s_astVoDevStatDflt[VoDev].bIsAlarm)		
	{
		if(VoChn == s_astVoDevStatDflt[VoDev].s32AlarmChn)
			return HI_SUCCESS;
		else 
			return HI_FAILURE;
	}
	
	if(s_astVoDevStatDflt[VoDev].bIsSingle)
	{
		if((enPreviewMode != SingleScene) && s_astVoDevStatDflt[VoDev].s32DoubleIndex == 0)
			enPreviewMode = SingleScene;
#if(IS_DECODER_DEVTYPE == 1)
		if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
		{
			if(s_astVoDevStatDflt[VoDev].s32DoubleIndex == 1)//双击状态进入单画面
			{
				if(VoChn == DoubleToSingleIndex)
					return HI_SUCCESS;
				else
					return HI_FAILURE;

			}
			else//切换到单画面，只与通道0有关
			{
				if(VoChn == 0)
					return HI_SUCCESS;
				else
					return HI_FAILURE;
			}
		}
#endif
		
		if(VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][s_astVoDevStatDflt[VoDev].s32SingleIndex])
			return HI_SUCCESS;
		else
			return HI_FAILURE;
	}
	if(LayoutToSingleChn == VoChn)
		return HI_SUCCESS;
	//获取当前预览模式下最大通道数
	CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[VoDev].enPreviewMode,&Max_num));

	switch(enPreviewMode)
	{
		case SingleScene:
			u32ChnNum = 1;
			break;
		case TwoScene:
			u32ChnNum = 2;
			break;
		case ThreeScene:
			u32ChnNum = 3;
			break;
		case FourScene:
		case LinkFourScene:
			u32ChnNum = 4;
			break;
		case FiveScene:
			u32ChnNum = 5;
			break;
		case SixScene:
			u32ChnNum = 6;
			break;
		case SevenScene:
			u32ChnNum = 7;
			break;
		case EightScene:
			u32ChnNum = 8;
			break;
		case NineScene:
		case LinkNineScene:
			u32ChnNum = 9;
			break;
		case SixteenScene:
			u32ChnNum = 16;
			break;
		default:
			RET_FAILURE("Invalid Parameter: enPreviewMode");
	}
#if(IS_DECODER_DEVTYPE == 1)
	//被动解码和点位控制下，通道是顺序排列的
	if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
	{
		if(VoChn < u32ChnNum)
		{
			return HI_SUCCESS;
		}
		else
		{
			return HI_FAILURE;
		}
	}
#endif
	if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_NORM)
	{
		for(i = 0; i < u32ChnNum; i++)
		{
			if(VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][(u32Index+i) % Max_num])
				return HI_SUCCESS;
		}
	}
	return HI_FAILURE;
}
/*************************************************
Function: //PRV_VLossInCurLayOut
Description: //   判断当前画面布局中是否包含未连接IPC的通道
Calls: 
Called By: //
Input: //无
Output: //无
Return: //0:包含未连接IPC的通道("无网络视频")，1:不包含
Others: // 其它说明
****************************************************/
HI_S32 PRV_VLossInCurLayOut()
{
	int u32ChnNum = 0, i = 0, index = 0;
	VO_CHN VoChn = 0;
#if defined(SN9234H1)
	VO_DEV VoDev = HD;
#else
	VO_DEV VoDev = DHD0;
#endif
	PRV_PREVIEW_MODE_E enPreviewMode = s_astVoDevStatDflt[VoDev].enPreviewMode;
	HI_U32 u32Index = (s_astVoDevStatDflt[VoDev].bIsSingle == HI_TRUE) ? s_astVoDevStatDflt[VoDev].s32SingleIndex : s_astVoDevStatDflt[VoDev].s32PreviewIndex;
	HI_U32 Max_num;
	//获取当前预览模式下最大通道数
	CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[VoDev].enPreviewMode,&Max_num));
	if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL)
	{
		index = PRV_GetVoChnIndex(s_astVoDevStatDflt[VoDev].s32CtrlChn);
		if(index < 0)
			RET_FAILURE("------ERR: Invalid Index!");
		if(VochnInfo[index].VdecChn == DetVLoss_VdecChn)
			return HI_SUCCESS;
		else 
			return HI_FAILURE;
	}
	
	if(s_astVoDevStatDflt[VoDev].bIsAlarm)		
	{
		index = PRV_GetVoChnIndex(s_astVoDevStatDflt[VoDev].s32AlarmChn);
		if(index < 0)
			RET_FAILURE("------ERR: Invalid Index!");
		if(VochnInfo[index].VdecChn == DetVLoss_VdecChn)
			return HI_SUCCESS;
		else 
			return HI_FAILURE;
	}
	
	if(s_astVoDevStatDflt[VoDev].bIsSingle)
	{
		if((enPreviewMode != SingleScene) && s_astVoDevStatDflt[VoDev].s32DoubleIndex == 0)
			enPreviewMode = SingleScene;

		index = PRV_GetVoChnIndex(s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][s_astVoDevStatDflt[VoDev].s32SingleIndex]);
		if(index < 0)
			RET_FAILURE("------ERR: Invalid Index!");
		if(VochnInfo[index].VdecChn == DetVLoss_VdecChn)
			return HI_SUCCESS;
		else
			return HI_FAILURE;
	}
	else if(LayoutToSingleChn == VoChn)
	{
		index = PRV_GetVoChnIndex(VoChn);
		if(index < 0)
			RET_FAILURE("------ERR: Invalid Index!");
		if(VochnInfo[index].VdecChn == DetVLoss_VdecChn)
			return HI_SUCCESS;
		else
			return HI_FAILURE;
		
	}

	switch(enPreviewMode)
	{
		case SingleScene:
			u32ChnNum = 1;
			break;
		case TwoScene:
			u32ChnNum = 2;
			break;
		case ThreeScene:
			u32ChnNum = 3;
			break;
		case FourScene:
		case LinkFourScene:
			u32ChnNum = 4;
			break;
		case FiveScene:
			u32ChnNum = 5;
			break;
		case SixScene:
			u32ChnNum = 6;
			break;
		case SevenScene:
			u32ChnNum = 7;
			break;
		case EightScene:
			u32ChnNum = 8;
			break;
		case NineScene:
		case LinkNineScene:
			u32ChnNum = 9;
			break;
		case SixteenScene:
			u32ChnNum = 16;
			break;
		default:
			RET_FAILURE("Invalid Parameter: enPreviewMode");
	}

	for(i = 0; i < u32ChnNum; i++)
	{
#if defined(SN9234H1)
		VoChn = s_astVoDevStatDflt[HD].as32ChnOrder[enPreviewMode][(u32Index+i) % Max_num];
#else		
		VoChn = s_astVoDevStatDflt[DHD0].as32ChnOrder[enPreviewMode][(u32Index+i) % Max_num];
#endif
		index = PRV_GetVoChnIndex(VoChn);
		if(index < 0)
			continue;		
		if(VochnInfo[index].VdecChn == DetVLoss_VdecChn)
			return HI_SUCCESS;
	}
	return HI_FAILURE;

}
#if (IS_DECODER_DEVTYPE == 1)

#else
/*************************************************
Function: //PRV_GetValidChnIdx
Description: 查找下一个或上一个有显示的通道索引。
Calls: 
Called By: //
Input: //VoDev:设备号
		u32Index:当前索引号
		pu32Index:返回有显示索引号
		s32Dir :上一屏、下一屏方向
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_GetValidChnIdx(VO_DEV VoDev, HI_U32 u32Index, HI_S32 *pu32Index, HI_S32 s32Dir,VO_CHN *pOrder,VO_CHN *pPollOrder)
{
	int i;
	VO_CHN VoChn;
	HI_U32 Max_num;
		
	//获取当前预览模式下最大通道数
	CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[VoDev].enPreviewMode,&Max_num));
	
	if (NULL == pu32Index)
	{
		RET_FAILURE("Null ptr!");
	}
	if (u32Index >= Max_num)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, TEXT_COLOR_RED("bad u32Index: %d\n"), u32Index);
		RET_FAILURE("");
	}
#if defined(SN8608D_LE) || defined(SN8608M_LE) || defined(SN8616D_LE) ||defined(SN8616M_LE) || defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
#endif
	if (VoDev<0 || VoDev>=PRV_VO_MAX_DEV)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, TEXT_COLOR_RED("bad VoDev: %d\n"), VoDev);
		RET_FAILURE("");
	}
				
	if (0 == s32Dir)
	{
		for (i=1; i<=Max_num; i++)
		{
			VoChn = pOrder[(i+u32Index)%Max_num];
			if (VoChn>=0 && VoChn<g_Max_Vo_Num)
			{
				if(pPollOrder[(i+u32Index)%Max_num])
				{//如果当前画面需要轮询
					*pu32Index = (i+u32Index)%Max_num;
					//printf("###############VoChn = %d,,,,,,*pu32Index =%d######################\n",VoChn,*pu32Index);
					RET_SUCCESS("");
				}	
			}
		}
	}
	else
	{
		for (i=1; i<=Max_num; i++)
		{
			VoChn = pOrder[(PRV_VO_CHN_NUM+u32Index-i)%Max_num];
			if (VoChn>=0 && VoChn<g_Max_Vo_Num)
			{
				if(pPollOrder[(Max_num+u32Index-i)%Max_num])
				{//如果当前画面需要轮询
					*pu32Index = (PRV_VO_CHN_NUM+u32Index-i)%Max_num;
					RET_SUCCESS("");
				}
			}
		}
	}
	RET_FAILURE("No valid chn available!");
}

/*************************************************
Function: //PRV_GetFirstChn
Description://  获取VO画面的第1个画面通道号
Calls: 
Called By: //
Input: //VoDev:设备号
		pVoChn:返回的通道号
Output: // pVoChn:返回的通道号
Return: //成功返回HI_SUCCESS
		失败返回HI_FAILURE
Others: // 其它说明
************************************************************************/

HI_S32 PRV_GetFirstChn(VO_DEV VoDev, VO_CHN *pVoChn)
{
	if (NULL == pVoChn)
	{
		RET_FAILURE("Null ptr!");
	}
//1、判断状态：enPreviewStat
	switch (s_astVoDevStatDflt[VoDev].enPreviewStat)
	{
		case PRV_STAT_NORM:
		{
			if(s_astVoDevStatDflt[VoDev].bIsAlarm)
			{//报警状态
				*pVoChn = s_astVoDevStatDflt[VoDev].s32AlarmChn;
			}
			else if (s_astVoDevStatDflt[VoDev].enPreviewMode == SingleScene || (s_astVoDevStatDflt[VoDev].enPreviewMode != SingleScene  && s_astVoDevStatDflt[VoDev].bIsSingle &&  (s_astVoDevStatDflt[VoDev].s32DoubleIndex==0)))
			{//如果处于单画面状态
				
				*pVoChn = s_astVoDevStatDflt[VoDev].as32ChnOrder[SingleScene][s_astVoDevStatDflt[VoDev].s32SingleIndex];
			}
			else if(s_astVoDevStatDflt[VoDev].enPreviewMode != SingleScene  && s_astVoDevStatDflt[VoDev].bIsSingle && s_astVoDevStatDflt[VoDev].s32DoubleIndex)
			{//如果处于鼠标双击状态下
				
				*pVoChn = s_astVoDevStatDflt[VoDev].as32ChnOrder[s_astVoDevStatDflt[VoDev].enPreviewMode][s_astVoDevStatDflt[VoDev].s32SingleIndex];
			}
			else
			{//如果处于多画面下
				*pVoChn = s_astVoDevStatDflt[VoDev].as32ChnOrder[s_astVoDevStatDflt[VoDev].enPreviewMode][s_astVoDevStatDflt[VoDev].s32PreviewIndex];
			}
		}	
			break;
		case PRV_STAT_PB:
		case PRV_STAT_PIC:	
		{
			RET_FAILURE("in pb or pic stat!");
		}	
			break;
		case PRV_STAT_CTRL:
		{
			*pVoChn = s_astVoDevStatDflt[VoDev].s32CtrlChn;
		}
			break;
		default:
			RET_FAILURE("err stat!");
			break;
	}	
	//printf("######*pVoChn =%d,s_astVoDevStatDflt[VoDev].enPreviewMode=%d,s_astVoDevStatDflt[VoDev].bIsSingle=%d ,s_astVoDevStatDflt[VoDev].s32DoubleIndex =%d ,s_astVoDevStatDflt[VoDev].s32SingleIndex=%d##################\n",*pVoChn ,s_astVoDevStatDflt[VoDev].enPreviewMode,s_astVoDevStatDflt[VoDev].bIsSingle,s_astVoDevStatDflt[VoDev].s32DoubleIndex ,s_astVoDevStatDflt[VoDev].s32SingleIndex);
	RET_SUCCESS("");
}
#endif
/*************************************************
Function: //PRV_GetVoChnIndex
Description: //  根据输出通道号，查找对应的VochnInfo数组下标
Calls: 
Called By: //
Input: // // VoChn输出通道号
Output: // 无
Return: //-1未找到，其他:对应的下标
Others: // 其它说明
************************************************************************/

HI_S32 PRV_GetVoChnIndex(int VoChn)
{
	int index = -1, i = 0;

	for(i = 0; i < DEV_CHANNEL_NUM; i++)
	{
		if(VoChn == VochnInfo[i].VoChn)
		{
			index = i;
			break;
		}
	}
	return index;
}
//
HI_S32 PRV_DisableDigChnAudio(HI_VOID)
{
#if defined(SN9234H1)	
	AUDIO_DEV AoDev = 0;
	AO_CHN AoChn = 0;
	//if(IsAdecBindAo)
	{
		(HI_MPI_AO_UnBindAdec(AoDev, AoChn, DecAdec));
		//IsAdecBindAo = 0;
	}
#else
#if defined(Hi3535)
	AUDIO_DEV AoDev = 0;
#else
	AUDIO_DEV AoDev = 4;
#endif
	AO_CHN AoChn = 0;
	if(IsAdecBindAo)
	{
		CHECK(PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, DecAdec));
		IsAdecBindAo = 0;
	}
#endif	
	Achn = -1;
	return HI_SUCCESS;
}



/*************************************************
Function: //PRV_PlayAudio
Description: //  播放音频
			查找当前预览模式下第一个有画面的数字通道或第一个模拟通道，
			并播放此通道对应的音频
Calls: 
Called By: //
Input: // // VoDev输出设备
Output: // 无
Return: //文档错误码
Others: // 其它说明
************************************************************************/
HI_S32 PRV_PlayAudio(VO_DEV VoDev)
{

	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s line:%d, Achn=%d, CurAudioChn=%d, PreAudioChn=%d, IsChoosePlayAudio=%d", __FUNCTION__, __LINE__, Achn, CurAudioChn, PreAudioChn, IsChoosePlayAudio);

	VO_CHN VoChn = 0; 
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif
	AO_CHN AoChn = 0;
	HI_S32 index1 = 0, index2 = 0, bIsVoiceTalkOn = 0;
	HI_U32 Max_num = 0, u32ChnNum = 0;
	PRV_PREVIEW_MODE_E	enPreviewMode = s_astVoDevStatDflt[VoDev].enPreviewMode;

	if(HI_TRUE == PRV_GetVoiceTalkState())
	{
		bIsVoiceTalkOn = 1;
		return HI_SUCCESS;
	}
	//被动下所有音频操作均无效
	if(PRV_CurDecodeMode == PassiveDecode)
		return HI_SUCCESS;
	//即时回放下，只回放即时回放通道音频，其他操作无效
	g_PlayInfo	stPlayInfo;
	PRV_GetPlayInfo(&stPlayInfo);
	if(stPlayInfo.PlayBackState == PLAY_INSTANT)
		return HI_SUCCESS;
	//获取当前预览模式下最大通道数
	CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[VoDev].enPreviewMode, &Max_num));
	
	if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_PB
		|| s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_PIC)
	{
		Achn = -1;
#if defined(SN9234H1)
		//if(IsAdecBindAo)
		{
			(HI_MPI_AO_UnBindAdec(AoDev, AoChn, DecAdec));
			//IsAdecBindAo = 0;
		}
#else
		if(IsAdecBindAo)
		{
			CHECK(PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, DecAdec));
			IsAdecBindAo = 0;
		}
#endif		
		if(LOCALVEDIONUM > 0)
			PRV_DisableAudioPreview();
		RET_FAILURE("In PB or PIC Stat");
	}
	else if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL)
	{
		VoChn = s_astVoDevStatDflt[VoDev].s32CtrlChn;
		index1 = PRV_GetVoChnIndex(VoChn);
	}	
	else
	{
		if(s_astVoDevStatDflt[VoDev].bIsAlarm)		
		{
			VoChn = s_astVoDevStatDflt[VoDev].s32AlarmChn;
			index1 = PRV_GetVoChnIndex(VoChn);
		}

		else if(s_astVoDevStatDflt[VoDev].bIsSingle)
		{
			//区分双击进入单画面和画面布局切换进入单画面
			if(enPreviewMode != SingleScene && s_astVoDevStatDflt[VoDev].s32DoubleIndex == 0)
				enPreviewMode = SingleScene;

			VoChn = s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][s_astVoDevStatDflt[VoDev].s32SingleIndex];
			index1 = PRV_GetVoChnIndex(VoChn);
		}
		else if(LayoutToSingleChn >= 0)
		{
			VoChn = LayoutToSingleChn;
			index1 = PRV_GetVoChnIndex(VoChn);			
		}		
		else if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_NORM)
		{
			switch(enPreviewMode)
			{
				case SingleScene:
					u32ChnNum = 1;
					VoChn = s_astVoDevStatDflt[VoDev].AudioChn[0];
					break;
				case ThreeScene:
					u32ChnNum = 3;
					VoChn = s_astVoDevStatDflt[VoDev].AudioChn[4];
					break;
				case FourScene:
				case LinkFourScene:
					u32ChnNum = 4;
					VoChn = s_astVoDevStatDflt[VoDev].AudioChn[1];
					break;
				case FiveScene:
					u32ChnNum = 5;
					VoChn = s_astVoDevStatDflt[VoDev].AudioChn[5];
					break;
				case SevenScene:
					u32ChnNum = 7;
					VoChn = s_astVoDevStatDflt[VoDev].AudioChn[6];
					break;
				case NineScene:
				case LinkNineScene:
					u32ChnNum = 9;
					VoChn = s_astVoDevStatDflt[VoDev].AudioChn[2];
					break;
				case SixteenScene:
					u32ChnNum = 16;
					VoChn = s_astVoDevStatDflt[VoDev].AudioChn[3];
					break;
				default:
					RET_FAILURE("Invalid Parameter: enPreviewMode");
			}
			index1 = PRV_GetVoChnIndex(VoChn);			
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "enPreviewMode: %d----index1: %d, VoChn: %d, IsChoosePlayAudio: %d\n", enPreviewMode, index1, VoChn, IsChoosePlayAudio);
			if(index1 < 0 && IsChoosePlayAudio != 1)//未配置且未选择音频通道
			{
				Achn = -1;
				CurAudioChn= -1;
#if defined(SN9234H1)
				//if(IsAdecBindAo)
				{
					(HI_MPI_AO_UnBindAdec(AoDev, AoChn, DecAdec));
					//IsAdecBindAo = 0;
				}
#else
				if(IsAdecBindAo)
				{
					CHECK(PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, DecAdec));
					IsAdecBindAo = 0;
				}
#endif
				RET_FAILURE("Not Config Audio Chn");
			}
		
		}
		
		if(index1 < 0 && IsChoosePlayAudio != 1)
			RET_FAILURE("------ERR: Invalid Index1!");
	
	}

	
	if(index1 >= 0 && VochnInfo[index1].IsHaveVdec//为数字通道创建音频解码通道
		&& VochnInfo[index1].AudioInfo.adoType != -1
		&& !IsCreateAdec)//第一次创建音频解码通道
	{		
		//音频解码通道
		//printf("----------Create Audio channel--%d!!\n", Achn);
		if(HI_SUCCESS == PRV_StartAdecAo(VochnInfo[index1]))
		{
			IsCreateAdec = 1;
#if defined(Hi3531)||defined(Hi3535)			
			IsAdecBindAo = 1;
#endif
			Achn = index1;
			CurPlayAudioInfo = VochnInfo[index1].AudioInfo;
		}
		else
		{
			IsCreateAdec = 0;
#if defined(Hi3531)||defined(Hi3535)			
			IsAdecBindAo = 0;
#endif
			Achn = -1;
			RET_FAILURE("Create Adec");
		}
		
		if(bIsVoiceTalkOn)
		{
#if defined(SN9234H1)
			HI_MPI_AO_UnBindAdec(AoDev, AoChn, DecAdec);
#else
			if(IsAdecBindAo)
			{
				CHECK(PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, DecAdec));
				IsAdecBindAo = 0;
			}
#endif			
		}
		
	}
	//用户主动选择播放音频的通道后，画面切换时，播放用户选择的音频
	index2 = PRV_GetVoChnIndex(PreAudioChn);
	if(index2 >= 0
		&& 1 == IsChoosePlayAudio
		&& HI_SUCCESS == PRV_VoChnIsInCurLayOut(VoDev, VochnInfo[index2].VoChn))
	{
		//if(VochnInfo[index2].AudioInfo.adoType != CurPlayAudioInfo.adoType	//编码类型改变
		//	|| VochnInfo[index2].AudioInfo.PtNumPerFrm != CurPlayAudioInfo.PtNumPerFrm)//每帧采样点个数改变
		if(VochnInfo[index2].AudioInfo.adoType >= 0)
		{
			if(IsCreateAdec)
			{
				CHECK(PRV_StopAdec());
				CHECK(PRV_StartAdecAo(VochnInfo[index2]));
				IsCreateAdec = 1;
			}
			CurPlayAudioInfo = VochnInfo[index2].AudioInfo;
			
		}
		else
		{
			return HI_FAILURE;
		}
#if defined(SN9234H1)
		//if(!IsAdecBindAo)
		{
			(HI_MPI_AO_BindAdec(AoDev, AoChn, DecAdec));
			//IsAdecBindAo = 1;
		}
#else
		if(!IsAdecBindAo)
		{
			CHECK(PRV_AUDIO_AoBindAdec(AoDev, AoChn, DecAdec));
			
			IsAdecBindAo = 1;
		}
#endif		
		//用户之前选择播放的通道在当前画面中，则保持此画面所有通道的播放状态
		CurAudioChn = PreAudioChn;
		Achn = ((CurAudioPlayStat>>CurAudioChn)&0x01) ? index2 : -1;
		CurAudioPlayStat = 0;
		if(Achn >= 0)
			CurAudioPlayStat = 1<<CurAudioChn;
		if(bIsVoiceTalkOn)
		{
#if defined(SN9234H1)
			HI_MPI_AO_UnBindAdec(AoDev, AoChn, DecAdec);
#else
			if(IsAdecBindAo)
			{
				CHECK(PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, DecAdec));
				IsAdecBindAo = 0;
			}
#endif			
		}
		return HI_SUCCESS;
	}
	
	if(index1 < 0)
		RET_FAILURE("------ERR: Invalid Index1!");
	
	if(VochnInfo[index1].IsLocalVideo)//模拟通道
	{	
#if defined(SN9234H1)
		//if(IsAdecBindAo)
		{
			(HI_MPI_AO_UnBindAdec(AoDev, AoChn, DecAdec));
			Achn = -1;
			//IsAdecBindAo = 0;
		}
#else
		if(IsAdecBindAo)
		{
			CHECK(PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, DecAdec));
			Achn = -1;
			IsAdecBindAo = 0;
		}
#endif		
		if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_NORM 
			|| s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL)
		{
			PRV_AudioPreviewCtrl((const unsigned char *)&VoChn, 1);
		}
		else
		{
			PRV_DisableAudioPreview();
		}
	}
	else//数字通道
	{
		if(IsAudioOpen //音频总开关开启
			&& VochnInfo[index1].IsHaveVdec)//有数据
		{
			if(index1 != Achn)//通道号改变
			{
				//if((VochnInfo[index1].AudioInfo.adoType != 0 && VochnInfo[index1].AudioInfo.adoType != CurPlayAudioInfo.adoType	)//编码类型改变
				//	|| VochnInfo[index1].AudioInfo.PtNumPerFrm != CurPlayAudioInfo.PtNumPerFrm)//每帧采样点个数改变
				if(VochnInfo[index1].AudioInfo.adoType > -1)
				{
					if(IsCreateAdec)
					{
						CHECK(PRV_StopAdec());
						IsCreateAdec = 0;
						CHECK(PRV_StartAdecAo(VochnInfo[index1]));
						IsCreateAdec = 1;
					}
					
#if defined(Hi3531)||defined(Hi3535)					
					IsAdecBindAo = 1;
#endif
					CurPlayAudioInfo = VochnInfo[index1].AudioInfo;					
				}
				else
				{
					Achn = -1;
					return HI_FAILURE;
				}
			}			
			CurAudioChn = VochnInfo[index1].VoChn;
			CurAudioPlayStat |= (1<<CurAudioChn);
			
			if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_NORM 
				|| s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL)
			{
#if defined(SN9234H1)
				//if(!IsAdecBindAo)
				{
					(HI_MPI_AO_BindAdec(AoDev, AoChn, DecAdec));
					//IsAdecBindAo = 1;
				}
#else
				if(!IsAdecBindAo)
				{
					CHECK(PRV_AUDIO_AoBindAdec(AoDev, AoChn, DecAdec));
					IsAdecBindAo = 1;
				}
#endif				
			}
			Achn = index1;//无需做解绑定操作，只需往解码器发送新通道的音频数据				

		}
		else
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "IsAudioOpen: %d, VochnInfo[index1].IsHaveVdec: %d\n", IsAudioOpen, VochnInfo[index1].IsHaveVdec);
			CurAudioChn = -1;
			Achn = -1;
		}
		
		if(bIsVoiceTalkOn)
		{
#if defined(SN9234H1)
			HI_MPI_AO_UnBindAdec(AoDev, AoChn, DecAdec);
#else
			if(IsAdecBindAo)
			{
				CHECK(PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, DecAdec));
				IsAdecBindAo = 0;
			}
#endif			
		}
		
	}
	return HI_SUCCESS;
}

//提供给MMI调用，获取指定通道号的音频状态
//MMI根据返回值，相应改变指定通道中音频播放小图标状态(可用或不可用)
int PRV_GetAudioState(int chn)
{
	if(chn < 0 || chn >= DEV_CHANNEL_NUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "---------Invalid channel: %d, line: %d\n", chn, __LINE__);
		return HI_FAILURE;		
	}
	if(PRV_CurDecodeMode == PassiveDecode)
		return HI_FAILURE;
	
	HI_S32 s32Ret = 0, index = 0;
	index = PRV_GetVoChnIndex(chn);
	if(index < 0)
		RET_FAILURE("------ERR: Invalid Index!");

	if(!IsAudioOpen)//音频总开关如果是关闭
	{	
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Not Open Audio!!\n");
		return HI_FAILURE;
	}
	
	if(PRV_GetVoiceTalkState())
	{	
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VoiceTalk Open Now!!\n");
		return HI_FAILURE;
	}
	if(VochnInfo[index].IsLocalVideo)//本地模拟通道
	{
		s32Ret = PRV_GetLocalAudioState(chn);
		if(HI_FAILURE == s32Ret)
			return s32Ret;
		else if(CurAudioChn == VochnInfo[index].VoChn)
		{
			if((CurAudioPlayStat>>CurAudioChn)&0x01)//播放状态
				return 1;
			else//没有播放
				return 0;
		}
		
	}
	else//数字通道
	{
		if(IsCreateAdec == 0)
		{
			PRV_StopAdec();
			VochnInfo[chn].AudioInfo.PtNumPerFrm = PtNumPerFrm;
			PRV_StartAdecAo(VochnInfo[chn]);	
			IsCreateAdec = 1;
			HI_MPI_ADEC_ClearChnBuf(DecAdec);
		}
		
		if((VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn))//无视频信号
		{		
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "---------VochnInfo[index].VdecChn: %d\n", VochnInfo[index].VdecChn);
			return HI_FAILURE;
		}
		if(VochnInfo[index].AudioInfo.adoType < 0 || 0 == VochnInfo[index].AudioInfo.PtNumPerFrm)//对应的音频编码类型参数不合法
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "---------Invalid Audio Para---index: %d, adoType: %d, PtNumPerFrm: %d\n", index, VochnInfo[index].AudioInfo.adoType, VochnInfo[index].AudioInfo.PtNumPerFrm);
			return HI_FAILURE;
		}
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----------VoChn: %d, CurAudioChn: %d\n", VochnInfo[index].VoChn, CurAudioChn);
		if(CurAudioChn == VochnInfo[index].VoChn)
		{
			if((CurAudioPlayStat>>CurAudioChn)&0x01)//播放状态
				return 1;
			else//没有播放
				return 0;
		}
	}
	return HI_SUCCESS;
}


//指定通道的音频小图标为可用，可以通过它播放/关闭指定通道音频
HI_S32 PRV_AudioOutputChange(int chn)
{
	int index = 0;
	if(chn < 0 || chn > CHANNEL_NUM)	
	{
		Achn = -1;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------chn: %d\n", chn);
	}	
	index = PRV_GetVoChnIndex(chn);
	if(index < 0)
		RET_FAILURE("------ERR: Invalid Index!");
	if(VochnInfo[index].IsLocalVideo)//本地模拟通道
	{
		PRV_LocalAudioOutputChange(chn);
		CurAudioChn = VochnInfo[index].VoChn;
		PreAudioChn = CurAudioChn;
		CurAudioPlayStat = 1<<CurAudioChn;
	}
	else
	{	
		if(CurAudioChn != VochnInfo[index].VoChn)//指定的通道不是当前播放音频的通道
		{			
			//if(VochnInfo[index].AudioInfo.adoType != CurPlayAudioInfo.adoType	//编码类型改变
			//	|| VochnInfo[index].AudioInfo.PtNumPerFrm != CurPlayAudioInfo.PtNumPerFrm)//每帧采样点个数改变
			if(VochnInfo[index].AudioInfo.adoType >= 0)
			{
				//重建音频解码通道
				if(IsCreateAdec)
				{
					CHECK(PRV_StopAdec());
					IsCreateAdec = 0;
					CHECK(PRV_StartAdecAo(VochnInfo[index]));
					IsCreateAdec = 1;
					HI_MPI_ADEC_ClearChnBuf(DecAdec);
				}
				CurPlayAudioInfo = VochnInfo[index].AudioInfo;
								
			}
			else
			{
				return HI_FAILURE;
			}
			CurAudioChn = VochnInfo[index].VoChn;
			PreAudioChn = CurAudioChn;
			CurAudioPlayStat = 0;//重新选择播放音频通道后，清除之前播放状态，重新设置
			CurAudioPlayStat = 1<<CurAudioChn;
			Achn = index;
		}
		else//指定的通道正是正在播放音频的通道，则开启/关闭指定通道音频
		{
			if((CurAudioPlayStat>>CurAudioChn)&(0x01))//如果当前是开启的，则关闭
			{
				CurAudioPlayStat = 0;
				Achn = -1;	
				CurAudioChn = 0;

			}
			else//当前是关闭，则开启
			{
				CurAudioPlayStat = 1<<CurAudioChn;			
				Achn = index;			
			}
		}
	}
	//printf("--------Achn: %d, CurAudioChn: %d,  CurAudioPlayStat: %d\n", Achn, CurAudioChn, CurAudioPlayStat);
	RET_SUCCESS("");
}

HI_S32 PRV_EnableDigChnAudio(HI_VOID)
{
#if defined(SN9234H1)
	AUDIO_DEV AoDev = 0;
	AO_CHN AoChn = 0;
	HI_S32 s32Ret = 0;
	//if(!IsAdecBindAo)
	{
		(HI_MPI_AO_BindAdec(AoDev, AoChn, DecAdec));
		//IsAdecBindAo = 1;
	}
#else
#if defined(Hi3535)
	AUDIO_DEV AoDev = 0;
#else
	AUDIO_DEV AoDev = 4;
#endif
	AO_CHN AoChn = 0;
	HI_S32 s32Ret = 0;
	if(!IsAdecBindAo)
	{
		CHECK_RET(PRV_AUDIO_AoBindAdec(AoDev, AoChn, DecAdec));
		IsAdecBindAo = 1;
	}
#endif	
	s32Ret = PRV_GetAudioState(CurAudioChn);//判断之前播放的音频通道播放状态
	if(s32Ret == 1)
		Achn = CurAudioChn;
	else
		Achn = -1;
	
	return HI_SUCCESS;
}
 /*************************************************
  Function: //PRV_ReSetVoRect
  Description: //  根据传入的预览显示模式以及视频源的分辨率，重新计算Vo输出的显示区域
  Calls: 
  Called By: //
  Input:int output_mode: 预览显示模式，参考枚举PRV_PreviewOutMode_enum；
		  width: 视频源分辨率的宽；
 		  hight: 视频源分辨率的高；
  		  src: 当前Vo的显示区域，取决于通道显示位置；

  Return: /重新计算后的显示区域
  Others: //在涉及Vo显示区域的设置时，需要根据当前预览模式，视频源分辨率，调用此接口重新获取Vo的具体显示区域
  ************************************************************************/

RECT_S PRV_ReSetVoRect(int output_mode, int width, int hight, RECT_S src)
{
	int stwidth = 0;
	int sthight = 0; 
	RECT_S dest;

	if(src.u32Width <= 0 || src.u32Height <= 0
		|| width <= 0 || hight <= 0)
		return src;
	//printf("++++++output_mode: %d\n", output_mode);
	//printf("------src.s32X: %d, src.s32Y: %d, src.u32Width: %d, src.u32Height: %d\n", src.s32X, src.s32Y, src.u32Width, src.u32Height);	
	//printf("------width: %d, hightt: %d\n", width, hight);	
	switch(output_mode)
	{
		case StretchMode:
			dest = src;
			break;
		case SameScaleMole:
		{
			if(src.u32Width * hight >= width * src.u32Height)//通道区域宽/高比值大于视频源分辨率比值
			{
				stwidth = src.u32Height * width / hight;
				dest.s32X      = (src.u32Width - stwidth) / 2 + src.s32X;
				dest.s32Y      = src.s32Y;
				dest.u32Width  = stwidth;
				dest.u32Height = src.u32Height;
			}		
			else
			{
				sthight = src.u32Width * hight / width;
				dest.s32X      = src.s32X;
				dest.s32Y      = (src.u32Height - sthight) / 2 + src.s32Y;
				dest.u32Width  = src.u32Width;
				dest.u32Height = sthight;						
			}
		}
			break;

		case IntelligentMode:
            if(width > hight && src.u32Width < src.u32Height)
            {
                if(src.u32Width * hight >= width * src.u32Height)//通道区域宽/高比值大于视频源分辨率比值
			    {
				   stwidth = src.u32Height * width / hight;
				   dest.s32X      = (src.u32Width - stwidth) / 2 + src.s32X;
				  dest.s32Y      = src.s32Y;
				  dest.u32Width  = stwidth;
				  dest.u32Height = src.u32Height;
			    }		
			    else
			    {
				  sthight = src.u32Width * hight / width;
				  dest.s32X      = src.s32X;
				  dest.s32Y      = (src.u32Height - sthight) / 2 + src.s32Y;
				  dest.u32Width  = src.u32Width;
				  dest.u32Height = sthight;						
			    }
			}
			else if(width > hight && src.u32Width >= src.u32Height)
			{
				dest = src;
			}
			else if(width < hight && src.u32Width < src.u32Height)
			{
                 dest = src;
			}
			else
			{
				if(src.u32Width * hight >= width * src.u32Height)//通道区域宽/高比值大于视频源分辨率比值
			    {
				   stwidth = src.u32Height * width / hight;
				   dest.s32X      = (src.u32Width - stwidth) / 2 + src.s32X;
				  dest.s32Y      = src.s32Y;
				  dest.u32Width  = stwidth;
				  dest.u32Height = src.u32Height;
			    }		
			    else
			    {
				  sthight = src.u32Width * hight / width;
				  dest.s32X      = src.s32X;
				  dest.s32Y      = (src.u32Height - sthight) / 2 + src.s32Y;
				  dest.u32Width  = src.u32Width;
				  dest.u32Height = sthight;						
			    }
			}
			break;
			
		default:			
			dest = src;
			break;	
	}
	//printf("------dest.s32X: %d, dest.s32Y: %d, dest.u32Width: %d, dest.u32Height: %d\n", dest.s32X, dest.s32Y, dest.u32Width, dest.u32Height);
	return dest;
}

/*************************************************
Function: //PRV_VLossVdecBindVoChn
Description: //  绑定无视频解码通道与VO输出通道
Calls: 
Called By: //
Input://VoDev输出设备
	//VoChn输出通道号
	//u32Index输出通道VoChn的索引号
	//enPreviewMode预览模式
Output: // 无
Return: //文档错误码
Others: //从PRV_PreviewVoDevInMode接口中分离出来，在通道断开连接后，
	    //不需要调用PRV_PreviewVoDevInMode刷新所有通道，只需调用此接口刷新断开连接的通道
************************************************************************/
HI_S32 PRV_VLossVdecBindVoChn(VO_DEV VoDev, VO_CHN VoChn, int u32Index, PRV_PREVIEW_MODE_E enPreviewMode)
{
	int i = 0, u32ChnNum = 0, w = 0, h = 0;
	int index = PRV_GetVoChnIndex(VoChn);
	if(index < 0)
		RET_FAILURE("------ERR: Invalid Index!");

#if defined(Hi3531)||defined(Hi3535)	
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
	{
		RET_FAILURE("Invalid Parameter: VoDev or u32Index");
	}
	if(VoDev == DHD1)
		VoDev = DSD0;

	RECT_S stSrcRect,stDestRect;
#endif
	RECT_S *pstLayout = NULL;	
	VO_CHN_ATTR_S stVoChnAttr;
	HI_U32 Max_num = 0;
	
	w = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
	h = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
	//获取当前预览模式下最大通道数
	CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[VoDev].enPreviewMode,&Max_num));

	//画中画显示设置
	if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL && VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn)
	{
		if(s_astVoDevStatDflt[VoDev].enCtrlFlag == PRV_CTRL_REGION_SEL && IsDispInPic == 1//"显示管理"画中画时，连接的IPC断开，重设显示区域显示"无网络图片"
			&& VoDev == s_VoDevCtrlDflt)//主设备才需要显示画中画，辅设备不需要
		{
#if defined(Hi3535)
			CHECK_RET(HI_MPI_VO_GetChnAttr(PIP, VoChn, &stVoChnAttr));
#else
			CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
#endif
			stVoChnAttr.stRect.s32X 	 = s_astVoDevStatDflt[VoDev].Pip_rect.s32X * w/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
			stVoChnAttr.stRect.s32Y 	 = s_astVoDevStatDflt[VoDev].Pip_rect.s32Y * h/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
			stVoChnAttr.stRect.u32Width  = 3 + s_astVoDevStatDflt[VoDev].Pip_rect.u32Width * w/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
			stVoChnAttr.stRect.u32Height = 4 + s_astVoDevStatDflt[VoDev].Pip_rect.u32Height * h/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
#if defined(SN9234H1)
			stVoChnAttr.u32Priority = 4;
#else
			stVoChnAttr.u32Priority = 1;
#endif

			//stVoChnAttr.stRect.s32X 	 = stVoChnAttr.stRect.s32X + (stVoChnAttr.stRect.u32Width - (NOVIDEO_IMAGWIDTH * stVoChnAttr.stRect.u32Width / w)) / 2;
			//stVoChnAttr.stRect.s32Y 	 = stVoChnAttr.stRect.s32Y + (stVoChnAttr.stRect.u32Height - (NOVIDEO_IMAGHEIGHT * stVoChnAttr.stRect.u32Height/ h))/ 2;
			//stVoChnAttr.stRect.u32Width  = (NOVIDEO_IMAGWIDTH * stVoChnAttr.stRect.u32Width / w);
			//stVoChnAttr.stRect.u32Height = (NOVIDEO_IMAGHEIGHT * stVoChnAttr.stRect.u32Height / h);
#if defined(Hi3531)||defined(Hi3535)
			if(stVoChnAttr.stRect.u32Height<32)
			{				
				stVoChnAttr.stRect.s32Y = stVoChnAttr.stRect.s32Y - (32-stVoChnAttr.stRect.u32Height)/2;
				stVoChnAttr.stRect.u32Height = 32;
			}
#endif			
			stVoChnAttr.stRect.s32X &= (~0x01);
			stVoChnAttr.stRect.s32Y &= (~0x01);
			stVoChnAttr.stRect.u32Width &= (~0x01);
			stVoChnAttr.stRect.u32Height &= (~0x01);
#if defined(Hi3535)	
			CHECK_RET(HI_MPI_VO_SetChnAttr(PIP, PRV_CTRL_VOCHN, &stVoChnAttr));
#else
			CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, PRV_CTRL_VOCHN, &stVoChnAttr));
#endif
#if defined(SN9234H1)
			CHECK(HI_MPI_VDEC_BindOutput(/*DetVLoss_VdecChn*/VochnInfo[index].VdecChn, VoDev, PRV_CTRL_VOCHN));			
#else
			PRV_VPSS_ResetWH(PRV_CTRL_VOCHN,NoConfig_VdecChn,704,576);
			CHECK(PRV_VDEC_BindVpss(NoConfig_VdecChn, PRV_CTRL_VOCHN));	
#endif
			sem_post(&sem_SendNoVideoPic);				
			//return HI_SUCCESS;//主设备单画面中"无视频"图片会被覆盖，为了效果，不需要绑定
		}
		else if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_ZOOM_IN && VoDev == s_VoDevCtrlDflt)//电子放大时，连接的IPC断开，设置小窗口区域显示"无网络图片"
		{
#if defined(Hi3535)	
			CHECK_RET(HI_MPI_VO_GetChnAttr(PIP, VoChn, &stVoChnAttr));
#else
			CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
#endif
			stVoChnAttr.stRect.s32X      = w*3/4;
			stVoChnAttr.stRect.s32Y      = h*3/4;
			stVoChnAttr.stRect.u32Width  = w*1/4;
			stVoChnAttr.stRect.u32Height = h*1/4;

			//stVoChnAttr.stRect.s32X 	 = stVoChnAttr.stRect.s32X + (stVoChnAttr.stRect.u32Width - (NOVIDEO_IMAGWIDTH * stVoChnAttr.stRect.u32Width / w)) / 2;
			//stVoChnAttr.stRect.s32Y 	 = stVoChnAttr.stRect.s32Y + (stVoChnAttr.stRect.u32Height - (NOVIDEO_IMAGHEIGHT * stVoChnAttr.stRect.u32Height/ h))/ 2;
			//stVoChnAttr.stRect.u32Width  = (NOVIDEO_IMAGWIDTH * stVoChnAttr.stRect.u32Width / w);
			//stVoChnAttr.stRect.u32Height = (NOVIDEO_IMAGHEIGHT * stVoChnAttr.stRect.u32Height / h);
#if defined(Hi3531)||defined(Hi3535)
			stVoChnAttr.u32Priority = 1;
			if(stVoChnAttr.stRect.u32Height < 32)
			{
				stVoChnAttr.stRect.s32Y = stVoChnAttr.stRect.s32Y - (32 - stVoChnAttr.stRect.u32Height)/2;
				stVoChnAttr.stRect.u32Height = 32;
			}
#endif			
			stVoChnAttr.stRect.s32X &= (~0x01);
			stVoChnAttr.stRect.s32Y &= (~0x01);
			stVoChnAttr.stRect.u32Width &= (~0x01);
			stVoChnAttr.stRect.u32Height &= (~0x01);
			CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, PRV_CTRL_VOCHN, &stVoChnAttr));
#if defined(SN9234H1)
			CHECK(HI_MPI_VDEC_BindOutput(/*DetVLoss_VdecChn*/VochnInfo[index].VdecChn, VoDev, PRV_CTRL_VOCHN));
#else
			PRV_VPSS_ResetWH(PRV_CTRL_VOCHN,NoConfig_VdecChn,704,576);
			CHECK(PRV_VDEC_BindVpss(NoConfig_VdecChn, PRV_CTRL_VOCHN));
#endif
			sem_post(&sem_SendNoVideoPic);				
		}
	}

	if((s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL && VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn)
		|| (s_astVoDevStatDflt[VoDev].bIsAlarm && VoChn == s_astVoDevStatDflt[VoDev].s32AlarmChn))		
	{
		u32ChnNum = 1;
		pstLayout = s_astPreviewLayout1;
		i = 0;
	}
	else if(s_astVoDevStatDflt[VoDev].bIsSingle)
	{
		if((enPreviewMode != SingleScene) && s_astVoDevStatDflt[VoDev].s32DoubleIndex == 0)
			enPreviewMode = SingleScene;
		if(VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][s_astVoDevStatDflt[VoDev].s32SingleIndex])
		{
			u32ChnNum = 1;
			pstLayout = s_astPreviewLayout1;
			i = 0;
		}
		if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
		{
			u32ChnNum = 1;
			pstLayout = s_astPreviewLayout1;
			i = 0;
		}
	}
	else if(LayoutToSingleChn == VoChn)
	{
		u32ChnNum = 1;
		pstLayout = s_astPreviewLayout1;
		i = 0;
	}
	else if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_NORM
		|| pstLayout == NULL)
	{
	    switch(enPreviewMode)
		{
			case SingleScene:
				u32ChnNum = 1;
				pstLayout = s_astPreviewLayout1;
				break;
			case TwoScene:
				u32ChnNum = 2;
				pstLayout = s_astPreviewLayout2;
				break;
			case ThreeScene:
				u32ChnNum = 3;
				pstLayout = s_astPreviewLayout3;
				break;
			case FourScene:
			case LinkFourScene:
				u32ChnNum = 4;
				pstLayout = s_astPreviewLayout4;
				break;
			case FiveScene:
				u32ChnNum = 5;
				pstLayout = s_astPreviewLayout5;
				break;
			case SixScene:
				u32ChnNum = 6;
				pstLayout = s_astPreviewLayout6;
				break;
			case SevenScene:
				u32ChnNum = 7;
				pstLayout = s_astPreviewLayout7;
				break;
			case EightScene:
				u32ChnNum = 8;
				pstLayout = s_astPreviewLayout8;
				break;
			case NineScene:
			case LinkNineScene:
				u32ChnNum = 9;
				pstLayout = s_astPreviewLayout9;
				break;
			case SixteenScene:
				u32ChnNum = 16;
				pstLayout = s_astPreviewLayout16;
				break;
			default:
				RET_FAILURE("Invalid Parameter: enPreviewMode");
		}
		for(i = 0; i < u32ChnNum && u32Index+i < Max_num; i++)
		{
			if(VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][(u32Index+i) % Max_num])
			{
				break;
			}
		}
		if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
			i = VoChn;
	}
	CHECK(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
#if defined(Hi3531)||defined(Hi3535)
	stVoChnAttr.u32Priority = 0;
#endif
	if((enPreviewMode == NineScene || enPreviewMode == LinkNineScene || enPreviewMode == ThreeScene || enPreviewMode == FiveScene
		|| enPreviewMode == SevenScene)&& s_astVoDevStatDflt[VoDev].bIsAlarm!=1 && s_astVoDevStatDflt[VoDev].bIsSingle!=1)
	{
	//解决9画面预览时相邻两列画面之间存在缝隙的问题
		while(w%6 != 0)
			w++;
		while(h%6 != 0)
			h++;
	}
	if(pstLayout != NULL && i < u32ChnNum)
	{
		stVoChnAttr.stRect.s32X 	 = (w * pstLayout[i].s32X) / PRV_PREVIEW_LAYOUT_DIV;
		stVoChnAttr.stRect.s32Y 	 = (h * pstLayout[i].s32Y) / PRV_PREVIEW_LAYOUT_DIV;
		stVoChnAttr.stRect.u32Width  = (w * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
		stVoChnAttr.stRect.u32Height = (h * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;
		if(enPreviewMode == NineScene || enPreviewMode == LinkNineScene)
		{ 
			if((i + 1) % 3 == 0)//最后一列
				stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			if(i > 5 && i < 9)//最后一行
				stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		}
		if( enPreviewMode == ThreeScene )
		{ 
			if( i == 2)//最后一列
				stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			//最后一行
				stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		}
		if( enPreviewMode == FiveScene )
		{ 
			if( i > 1 )//最后一列
				stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			if(i==0 || i==1 || i==4)//最后一行
				stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		}
		if(enPreviewMode == SevenScene)
		{ 
			if(i==2 || i==4 || i==6)//最后一列
				stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			if(i==0 || i==5 || i==6)//最后一行
				stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		}
		//stVoChnAttr.stRect.s32X 	 = stVoChnAttr.stRect.s32X + (stVoChnAttr.stRect.u32Width - (NOVIDEO_IMAGWIDTH * pstLayout[i].u32Width / PRV_PREVIEW_LAYOUT_DIV)) / 2;
		//stVoChnAttr.stRect.s32Y 	 = stVoChnAttr.stRect.s32Y + (stVoChnAttr.stRect.u32Height - (NOVIDEO_IMAGHEIGHT * pstLayout[i].u32Height / PRV_PREVIEW_LAYOUT_DIV))/ 2;
		//stVoChnAttr.stRect.u32Width  = (NOVIDEO_IMAGWIDTH * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
		//stVoChnAttr.stRect.u32Height = (NOVIDEO_IMAGHEIGHT * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;
#if defined(SN9234H1)
		stVoChnAttr.stRect.s32X 	 &= (~0x01);
		stVoChnAttr.stRect.s32Y 	 &= (~0x01);
		stVoChnAttr.stRect.u32Width  &= (~0x01);
		stVoChnAttr.stRect.u32Height &= (~0x01);

#else
		if(stVoChnAttr.stRect.u32Height < 32)
		{
			stVoChnAttr.stRect.s32Y = stVoChnAttr.stRect.s32Y - (32 - stVoChnAttr.stRect.u32Height)/2;
			stVoChnAttr.stRect.u32Height = 32;
		}

		if(VochnInfo[index].VdecChn != DetVLoss_VdecChn)//非无网络画面时进行显示坐标转换
		{
			stSrcRect.s32X		 = stVoChnAttr.stRect.s32X;
			stSrcRect.s32Y		 = stVoChnAttr.stRect.s32Y;
			stSrcRect.u32Width	 = stVoChnAttr.stRect.u32Width;
			stSrcRect.u32Height  = stVoChnAttr.stRect.u32Height;

			stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
		
			stVoChnAttr.stRect.s32X 	 = stDestRect.s32X		& (~0x01);
			stVoChnAttr.stRect.s32Y 	 = stDestRect.s32Y		& (~0x01);
			stVoChnAttr.stRect.u32Width  = stDestRect.u32Width	& (~0x01);
			stVoChnAttr.stRect.u32Height = stDestRect.u32Height & (~0x01);
		}
		else
		{
			stVoChnAttr.stRect.s32X &= (~0x01);
			stVoChnAttr.stRect.s32Y &= (~0x01);
			stVoChnAttr.stRect.u32Width &= (~0x01);
			stVoChnAttr.stRect.u32Height &= (~0x01);
		}
#endif
	}
	CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stVoChnAttr));
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VochnInfo[index].VdecChn: %d, i: %d,X: %d, Y: %d, W: %d, H: %d\n", VochnInfo[index].VdecChn, i, stVoChnAttr.stRect.s32X, stVoChnAttr.stRect.s32Y, stVoChnAttr.stRect.u32Width, stVoChnAttr.stRect.u32Height);

	//主设备"显示管理"时，单画面不需要绑定"无网络"图片解码通道
	if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL && IsDispInPic == 1 && VoDev == s_VoDevCtrlDflt)
		return HI_SUCCESS;
#if defined(SN9234H1)
	CHECK(HI_MPI_VO_SetChnFrameRate(VoDev, VoChn, 30));
	(HI_MPI_VDEC_BindOutput(/*DetVLoss_VdecChn*/VochnInfo[index].VdecChn, VoDev, VoChn));
#else
	CHECK(HI_MPI_VO_SetChnFrameRate(VoDev, VoChn, 25));
	if(VochnInfo[index].IsBindVdec[DHD0] != 0/* && VochnInfo[index].IsBindVdec[DSD0] != 0*/)
	{
		PRV_VPSS_ResetWH(VoChn,VochnInfo[index].VdecChn,704,576);
		CHECK(PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VoChn));
	}
#endif	
	VochnInfo[index].IsBindVdec[VoDev] = 0;
	if(VochnInfo[index].VdecChn == DetVLoss_VdecChn)
		sem_post(&sem_SendNoVideoPic);				
	return HI_SUCCESS;
			
}

//主片上显示的通道与解码通道绑定
HI_S32 PRV_BindVoChnInMaster(VO_DEV VoDev, VO_CHN VoChn, int u32Index, PRV_PREVIEW_MODE_E enPreviewMode)
{
	if(PRV_STAT_PB == s_astVoDevStatDflt[VoDev].enPreviewStat || PRV_STAT_PIC == s_astVoDevStatDflt[VoDev].enPreviewStat)
		RET_FAILURE("In PB or Pic Stat!");
	int i = 0, u32ChnNum = 0, w = 0, h = 0;
	int index = PRV_GetVoChnIndex(VoChn);
	if(index < 0)
		RET_FAILURE("------ERR: Invalid Index!");
#if defined(Hi3531)||defined(Hi3535)	
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
	{
		RET_FAILURE("Invalid Parameter: VoDev or u32Index");
	}
	if(VoDev == DHD1)
		VoDev = DSD0;
#endif	
	VO_CHN_ATTR_S stVoChnAttr;
	RECT_S *pstLayout = NULL;
	HI_U32 Max_num = 0;
	RECT_S stSrcRect, stDestRect;
	
	w = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
	h = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
	
	//获取当前预览模式下最大通道数
	CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[VoDev].enPreviewMode, &Max_num));

	//画中画显示设置
	if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL && VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn)
	{
		if(s_astVoDevStatDflt[VoDev].enCtrlFlag == PRV_CTRL_REGION_SEL && IsDispInPic == 1//"显示管理"画中画时，连接上IPC
			&& VoDev == s_VoDevCtrlDflt)//主设备才需要显示画中画，辅设备不需要
		{
			CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
			stVoChnAttr.stRect.s32X 	 = s_astVoDevStatDflt[VoDev].Pip_rect.s32X * w/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
			stVoChnAttr.stRect.s32Y 	 = s_astVoDevStatDflt[VoDev].Pip_rect.s32Y * h/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
			stVoChnAttr.stRect.u32Width  = 3 + s_astVoDevStatDflt[VoDev].Pip_rect.u32Width * w/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
			stVoChnAttr.stRect.u32Height = 4 + s_astVoDevStatDflt[VoDev].Pip_rect.u32Height * h/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
#if defined(SN9234H1)
			//stVoChnAttr.u32Priority = 4;
			#if 0
			stSrcRect.s32X		 = stVoChnAttr.stRect.s32X;
			stSrcRect.s32Y		 = stVoChnAttr.stRect.s32Y;
			stSrcRect.u32Width	 = stVoChnAttr.stRect.u32Width;
			stSrcRect.u32Height  = stVoChnAttr.stRect.u32Height;
			
			stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
			stVoChnAttr.stRect.s32X 	 = stDestRect.s32X 		& (~0x01);
			stVoChnAttr.stRect.s32Y 	 = stDestRect.s32Y 		& (~0x01);
			stVoChnAttr.stRect.u32Width  = stDestRect.u32Width  & (~0x01);
			stVoChnAttr.stRect.u32Height = stDestRect.u32Height & (~0x01);
			//printf("line: %d------stSrcRect.s32X: %d, stSrcRect.s32Y: %d, stSrcRect.u32Width: %d, stSrcRect.u32Height: %d\n", __LINE__, stSrcRect.s32X, stSrcRect.s32Y, stSrcRect.u32Width, stSrcRect.u32Height); 
			//printf("line: %d------stDestRect.s32X: %d, stDestRect.s32Y: %d, stDestRect.u32Width: %d, stDestRect.u32Height: %d\n", __LINE__, stDestRect.s32X, stDestRect.s32Y, stDestRect.u32Width, stDestRect.u32Height);
			#endif
			CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, PRV_CTRL_VOCHN, &stVoChnAttr));
			
			CHECK(HI_MPI_VO_SetChnFrameRate(VoDev, PRV_CTRL_VOCHN, 30));
			CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, VoDev, PRV_CTRL_VOCHN));
#else
			stVoChnAttr.u32Priority = 1;
	
			stVoChnAttr.stRect.s32X      &= (~0x1);
			stVoChnAttr.stRect.s32Y      &= (~0x1);
			stVoChnAttr.stRect.u32Width  &= (~0x1);
			stVoChnAttr.stRect.u32Height &= (~0x1);
#if defined(Hi3535)
			CHECK_RET(HI_MPI_VO_SetChnAttr(PIP, PRV_CTRL_VOCHN, &stVoChnAttr));
			CHECK(HI_MPI_VO_SetChnFrameRate(PIP, PRV_CTRL_VOCHN, 25));
#else
			CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, PRV_CTRL_VOCHN, &stVoChnAttr));
			CHECK(HI_MPI_VO_SetChnFrameRate(VoDev, PRV_CTRL_VOCHN, 25));
#endif
			PRV_VPSS_ResetWH(PRV_CTRL_VOCHN,VochnInfo[index].VdecChn,VochnInfo[index].VideoInfo.width,VochnInfo[index].VideoInfo.height);
			CHECK(PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, PRV_CTRL_VOCHN));
#endif			
		}
		else if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_ZOOM_IN && VoDev == s_VoDevCtrlDflt)//电子放大时，连接的IPC断开，设置小窗口区域显示"无网络图片"
		{
			CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
			stVoChnAttr.stRect.s32X      = w*3/4;
			stVoChnAttr.stRect.s32Y      = h*3/4;
			stVoChnAttr.stRect.u32Width  = w*1/4;
			stVoChnAttr.stRect.u32Height = h*1/4;

			stSrcRect.s32X		 = stVoChnAttr.stRect.s32X;
			stSrcRect.s32Y		 = stVoChnAttr.stRect.s32Y;
			stSrcRect.u32Width	 = stVoChnAttr.stRect.u32Width;
			stSrcRect.u32Height  = stVoChnAttr.stRect.u32Height;

			stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);

			stVoChnAttr.stRect.s32X 	 = stDestRect.s32X		& (~0x01);
			stVoChnAttr.stRect.s32Y 	 = stDestRect.s32Y		& (~0x01);
			stVoChnAttr.stRect.u32Width  = stDestRect.u32Width	& (~0x01);
			stVoChnAttr.stRect.u32Height = stDestRect.u32Height & (~0x01);
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "x: %d, y: %d, w: %d, h: %d\n", stVoChnAttr.stRect.s32X, stVoChnAttr.stRect.s32Y, stVoChnAttr.stRect.u32Width, stVoChnAttr.stRect.u32Height);
#if defined(Hi3535)
			CHECK_RET(HI_MPI_VO_SetChnAttr(PIP, PRV_CTRL_VOCHN, &stVoChnAttr));
			CHECK(HI_MPI_VO_SetChnFrameRate(PIP, PRV_CTRL_VOCHN, 30));
#else
			CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, PRV_CTRL_VOCHN, &stVoChnAttr));
			CHECK(HI_MPI_VO_SetChnFrameRate(VoDev, PRV_CTRL_VOCHN, 30));
#endif
#if defined(SN9234H1)
			CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, VoDev, PRV_CTRL_VOCHN));
#else
			PRV_VPSS_ResetWH(PRV_CTRL_VOCHN,VochnInfo[index].VdecChn,VochnInfo[index].VideoInfo.width,VochnInfo[index].VideoInfo.height);
			CHECK(PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, PRV_CTRL_VOCHN));
#endif
		}
	}
	
	if((s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL && VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn)
		|| (s_astVoDevStatDflt[VoDev].bIsAlarm && VoChn == s_astVoDevStatDflt[VoDev].s32AlarmChn))	
	{
		u32ChnNum = 1;
		pstLayout = s_astPreviewLayout1;
		i = 0;
	}
	else if(s_astVoDevStatDflt[VoDev].bIsSingle )
	{
		if((enPreviewMode != SingleScene) && s_astVoDevStatDflt[VoDev].s32DoubleIndex == 0)
			enPreviewMode = SingleScene;

		if(VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][s_astVoDevStatDflt[VoDev].s32SingleIndex])
		{
			u32ChnNum = 1;
			pstLayout = s_astPreviewLayout1;
			i = 0;
		}		
		if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
		{
			u32ChnNum = 1;
			pstLayout = s_astPreviewLayout1;
			i = 0;
		}
	}
	else if(LayoutToSingleChn == VoChn)
	{
		u32ChnNum = 1;
		pstLayout = s_astPreviewLayout1;
		i = 0;
	}	
	else if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_NORM
		|| pstLayout == NULL)
	{
	    switch(enPreviewMode)
		{
			case SingleScene:
				u32ChnNum = 1;
				pstLayout = s_astPreviewLayout1;
				break;
			case TwoScene:
				u32ChnNum = 2;
				pstLayout = s_astPreviewLayout2;
				break;
			case ThreeScene:
				u32ChnNum = 3;
				pstLayout = s_astPreviewLayout3;
				break;
			case FourScene:
			case LinkFourScene:
				u32ChnNum = 4;
				pstLayout = s_astPreviewLayout4;
				break;
			case FiveScene:
				u32ChnNum = 5;
				pstLayout = s_astPreviewLayout5;
				break;
			case SixScene:
				u32ChnNum = 6;
				pstLayout = s_astPreviewLayout6;
				break;
			case SevenScene:
				u32ChnNum = 7;
				pstLayout = s_astPreviewLayout7;
				break;
			case EightScene:
				u32ChnNum = 8;
				pstLayout = s_astPreviewLayout8;
				break;
			case NineScene:
			case LinkNineScene:
				u32ChnNum = 9;
				pstLayout = s_astPreviewLayout9;
				break;
			case SixteenScene:
				u32ChnNum = 16;
				pstLayout = s_astPreviewLayout16;
				break;
			default:
				RET_FAILURE("Invalid Parameter: enPreviewMode");
		}

		for(i = 0; i < u32ChnNum && u32Index+i < Max_num; i++)
		{
			if(VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][(u32Index+i) % Max_num])
				break;
		}
		if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
			i = VoChn;
		
	}

    if((enPreviewMode == NineScene || enPreviewMode == LinkNineScene || enPreviewMode == ThreeScene || enPreviewMode == FiveScene
		|| enPreviewMode == SevenScene)&& s_astVoDevStatDflt[VoDev].bIsAlarm!=1 && s_astVoDevStatDflt[VoDev].bIsSingle!=1)
    {
	//解决9画面预览时相邻两列画面之间存在缝隙的问题
		while(w%6 != 0)
			w++;
		while(h%6 != 0)
			h++;
	}
	//printf("VoChn: %d, i: %d, u32ChnNum: %d\n", VoChn, i, u32ChnNum);
	//CHECK(HI_MPI_VO_ClearChnBuffer(VoDev, VoChn, HI_TRUE));
	CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
	if(pstLayout != NULL && i < u32ChnNum)
	{
		stVoChnAttr.stRect.s32X 	 = (w * pstLayout[i].s32X) / PRV_PREVIEW_LAYOUT_DIV;
		stVoChnAttr.stRect.s32Y 	 = (h * pstLayout[i].s32Y) / PRV_PREVIEW_LAYOUT_DIV;
		stVoChnAttr.stRect.u32Width  = (w * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
		stVoChnAttr.stRect.u32Height = (h * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;
		if(enPreviewMode == NineScene || enPreviewMode == LinkNineScene)
		{ 
			if((i + 1) % 3 == 0)//最后一列
				stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			if(i > 5 && i < 9)//最后一行
				stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		}
		if( enPreviewMode == ThreeScene )
		{ 
			if( i == 2)//最后一列
				stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			//最后一行
				stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		}
		if( enPreviewMode == FiveScene )
		{ 
			if( i > 1 )//最后一列
				stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			if(i==0 || i==1 || i==4)//最后一行
				stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		}
		if(enPreviewMode == SevenScene)
		{ 
			if(i==2 || i==4 || i==6)//最后一列
				stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			if(i==0 || i==5 || i==6)//最后一行
				stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		}
		stSrcRect.s32X		 = stVoChnAttr.stRect.s32X;
		stSrcRect.s32Y		 = stVoChnAttr.stRect.s32Y;
		stSrcRect.u32Width	 = stVoChnAttr.stRect.u32Width;
		stSrcRect.u32Height  = stVoChnAttr.stRect.u32Height;
		
		stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
		stVoChnAttr.stRect.s32X 	 = stDestRect.s32X 		& (~0x01);
		stVoChnAttr.stRect.s32Y 	 = stDestRect.s32Y 		& (~0x01);
		stVoChnAttr.stRect.u32Width  = stDestRect.u32Width  & (~0x01);
		stVoChnAttr.stRect.u32Height = stDestRect.u32Height & (~0x01);
		
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------stVoChnAttr.stRect.s32X:%d, stVoChnAttr.stRect.s32Y:%d, stVoChnAttr.stRect.u32Width: %d, stVoChnAttr.stRect.u32Height: %d\n",
		//									stVoChnAttr.stRect.s32X, stVoChnAttr.stRect.s32Y, stVoChnAttr.stRect.u32Width, stVoChnAttr.stRect.u32Height);
	}
	CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stVoChnAttr));
#if defined(SN9234H1)
	CHECK(HI_MPI_VO_SetChnFrameRate(VoDev, VoChn, 30));
	CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, VoDev, VoChn));
#else
	CHECK(HI_MPI_VO_SetChnFrameRate(VoDev, VoChn, 25));
	//vpss只要绑定一次
	if(VochnInfo[index].IsBindVdec[DHD0] != 1/* && VochnInfo[index].IsBindVdec[DSD0] != 1*/)
	{
		PRV_VPSS_ResetWH(VoChn,VochnInfo[index].VdecChn,VochnInfo[index].VideoInfo.width,VochnInfo[index].VideoInfo.height);
		CHECK(PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VoChn));
	}
#endif	
	VochnInfo[index].IsBindVdec[VoDev] = 1;
	return HI_SUCCESS;
}
/*************************************************
Function: //PRV_BindVoChnInSlave
Description: //  绑定VO输出通道与VI(0,0)显示从片上输出的视频
Calls: 
Called By: //
Input://VoDev输出设备
	//VoChn输出通道号
	//u32Index输出通道VoChn的索引号
	//enPreviewMode预览模式
Output: // 无
Return: //文档错误码
Others: //从PRV_PreviewVoDevInMode接口中分离出来，在新通道建立连接后，从片成功创建解码通道返回后，
	    //不需要调用PRV_PreviewVoDevInMode刷新所有通道，只需调用此接口刷新新连接的通道
************************************************************************/

HI_S32 PRV_BindVoChnInSlave(VO_DEV VoDev, VO_CHN VoChn, int u32Index, PRV_PREVIEW_MODE_E enPreviewMode)
{
	if(PRV_STAT_PB == s_astVoDevStatDflt[VoDev].enPreviewStat || PRV_STAT_PIC == s_astVoDevStatDflt[VoDev].enPreviewStat)
		RET_FAILURE("In PB or Pic Stat!");
	
	int i = 0, u32ChnNum = 0, w = 0, h = 0;
	int index = PRV_GetVoChnIndex(VoChn);
	if(index < 0)
		RET_FAILURE("------ERR: Invalid Index!");
	VO_ZOOM_ATTR_S stVoZoomAttr;
	VO_CHN_ATTR_S stVoChnAttr;
	RECT_S *pstLayout = NULL;
	RECT_S stSrcRect,stDestRect;
	HI_U32 Max_num = 0;
	
	w = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
	h = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
	
	if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL && VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn)
	{
		//画中画显示设置
		if(s_astVoDevStatDflt[VoDev].enCtrlFlag == PRV_CTRL_REGION_SEL && IsDispInPic == 1//"显示管理"画中画时，连接上IPC
			&& VoDev == s_VoDevCtrlDflt)//主设备才需要显示画中画，辅设备不需要
		{
			CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
			stVoChnAttr.stRect.s32X 	 = s_astVoDevStatDflt[VoDev].Pip_rect.s32X * w/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
			stVoChnAttr.stRect.s32Y 	 = s_astVoDevStatDflt[VoDev].Pip_rect.s32Y * h/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
			stVoChnAttr.stRect.u32Width  = 3 + s_astVoDevStatDflt[VoDev].Pip_rect.u32Width * w/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
			stVoChnAttr.stRect.u32Height = 4 + s_astVoDevStatDflt[VoDev].Pip_rect.u32Height * h/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
			//stVoChnAttr.u32Priority = 4;
	
			stVoChnAttr.stRect.s32X      &= (~0x1);
			stVoChnAttr.stRect.s32Y      &= (~0x1);
			stVoChnAttr.stRect.u32Width  &= (~0x1);
			stVoChnAttr.stRect.u32Height &= (~0x1);
			
			CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, PRV_CTRL_VOCHN, &stVoChnAttr));
		}
		//电子放大设置
		else if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_ZOOM_IN && VoDev == s_VoDevCtrlDflt)//电子放大时，连接的IPC断开，设置小窗口区域显示"无网络图片"
		{
			CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
			stVoChnAttr.stRect.s32X      = w*3/4;
			stVoChnAttr.stRect.s32Y      = h*3/4;
			stVoChnAttr.stRect.u32Width  = w*1/4;
			stVoChnAttr.stRect.u32Height = h*1/4;

			stSrcRect.s32X		 = stVoChnAttr.stRect.s32X;
			stSrcRect.s32Y		 = stVoChnAttr.stRect.s32Y;
			stSrcRect.u32Width	 = stVoChnAttr.stRect.u32Width;
			stSrcRect.u32Height  = stVoChnAttr.stRect.u32Height;
	
			stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);

			stVoChnAttr.stRect.s32X 	 = stDestRect.s32X		& (~0x01);
			stVoChnAttr.stRect.s32Y 	 = stDestRect.s32Y		& (~0x01);
			stVoChnAttr.stRect.u32Width  = stDestRect.u32Width	& (~0x01);
			stVoChnAttr.stRect.u32Height = stDestRect.u32Height & (~0x01);

			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "x: %d, y: %d, w: %d, h: %d\n", stVoChnAttr.stRect.s32X, stVoChnAttr.stRect.s32Y, stVoChnAttr.stRect.u32Width, stVoChnAttr.stRect.u32Height);
			CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, PRV_CTRL_VOCHN, &stVoChnAttr));
		}
	}
	
	//获取当前预览模式下最大通道数
	CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[VoDev].enPreviewMode, &Max_num));
	if((s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL && VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn)
		|| (s_astVoDevStatDflt[VoDev].bIsAlarm && VoChn == s_astVoDevStatDflt[VoDev].s32AlarmChn))
	{
		i = 0;
		u32ChnNum = 1;
		pstLayout = s_astPreviewLayout1;

	}
	else if(s_astVoDevStatDflt[VoDev].bIsSingle )
	{
		if((enPreviewMode != SingleScene) && s_astVoDevStatDflt[VoDev].s32DoubleIndex == 0)
			enPreviewMode = SingleScene;

		if(VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][s_astVoDevStatDflt[VoDev].s32SingleIndex])
		{
			u32ChnNum = 1;
			pstLayout = s_astPreviewLayout1;
			i = 0;
		}		
		if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
		{
			u32ChnNum = 1;
			pstLayout = s_astPreviewLayout1;
			i = 0;
		}
	}
	else if(LayoutToSingleChn == VoChn)
	{
		u32ChnNum = 1;
		pstLayout = s_astPreviewLayout1;
		i = 0;
	}
	else if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_NORM
		|| pstLayout == NULL)
	{	
	    switch(enPreviewMode)
		{
			case SingleScene:
				u32ChnNum = 1;
				pstLayout = s_astPreviewLayout1;
				break;
			case TwoScene:
				u32ChnNum = 2;
				pstLayout = s_astPreviewLayout2;
				break;
			case ThreeScene:
				u32ChnNum = 3;
				pstLayout = s_astPreviewLayout3;
				break;
			case FourScene:
			case LinkFourScene:
				u32ChnNum = 4;
				pstLayout = s_astPreviewLayout4;
				break;
			case FiveScene:
				u32ChnNum = 5;
				pstLayout = s_astPreviewLayout5;
				break;
			case SixScene:
				u32ChnNum = 6;
				pstLayout = s_astPreviewLayout6;
				break;
			case SevenScene:
				u32ChnNum = 7;
				pstLayout = s_astPreviewLayout7;
				break;
			case EightScene:
				u32ChnNum = 8;
				pstLayout = s_astPreviewLayout8;
				break;
			case NineScene:
			case LinkNineScene:
				u32ChnNum = 9;
				pstLayout = s_astPreviewLayout9;
				break;
			case SixteenScene:
				u32ChnNum = 16;
				pstLayout = s_astPreviewLayout16;
				break;
			default:
				RET_FAILURE("Invalid Parameter: enPreviewMode");
		}

		for(i = 0; i < u32ChnNum && u32Index+i < Max_num; i++)
		{
			if(VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][(u32Index+i) % Max_num])
				break;
		}
		if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
			i = VoChn;
	}
	//CHECK(HI_MPI_VO_ClearChnBuffer(VoDev, VoChn, HI_TRUE));
	if((enPreviewMode == NineScene || enPreviewMode == LinkNineScene || enPreviewMode == ThreeScene || enPreviewMode == FiveScene
		|| enPreviewMode == SevenScene) && s_astVoDevStatDflt[VoDev].bIsAlarm!=1 && s_astVoDevStatDflt[VoDev].bIsSingle!=1)
	{
	//解决9画面预览时相邻两列画面之间存在缝隙的问题
		while(w%6 != 0)
			w++;
		while(h%6 != 0)
			h++;
	}
	CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
	if(pstLayout != NULL && i < u32ChnNum)
	{
		stVoChnAttr.stRect.s32X 	 = (w * pstLayout[i].s32X) / PRV_PREVIEW_LAYOUT_DIV;
		stVoChnAttr.stRect.s32Y 	 = (h * pstLayout[i].s32Y) / PRV_PREVIEW_LAYOUT_DIV;
		stVoChnAttr.stRect.u32Width  = (w * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
		stVoChnAttr.stRect.u32Height = (h * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;
		if(enPreviewMode == NineScene || enPreviewMode == LinkNineScene)
		{ 
			if((i + 1) % 3 == 0)//最后一列
				stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			if(i > 5 && i < 9)//最后一行
				stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		}

	    if( enPreviewMode == ThreeScene )
		{ 
		    if( i == 2)//最后一列
			    stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
					//最后一行
				stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		}
		if( enPreviewMode == FiveScene )
		{ 
		    if( i > 1 )//最后一列
			    stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			if(i==0 || i==1 || i==4)//最后一行
			    stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		}
		if(enPreviewMode == SevenScene)
		{ 
		    if(i==2 || i==4 || i==6)//最后一列
			    stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			if(i==0 || i==5 || i==6)//最后一行
			    stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		}
		
		stSrcRect.s32X		 = stVoChnAttr.stRect.s32X;
		stSrcRect.s32Y		 = stVoChnAttr.stRect.s32Y;
		stSrcRect.u32Width	 = stVoChnAttr.stRect.u32Width;
		stSrcRect.u32Height  = stVoChnAttr.stRect.u32Height;
		
		stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
		stVoChnAttr.stRect.s32X 	 = stDestRect.s32X 		& (~0x01);
		stVoChnAttr.stRect.s32Y 	 = stDestRect.s32Y 		& (~0x01);
		stVoChnAttr.stRect.u32Width  = stDestRect.u32Width  & (~0x01);
		stVoChnAttr.stRect.u32Height = stDestRect.u32Height & (~0x01);
		
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------stVoChnAttr.stRect.s32X:%d, stVoChnAttr.stRect.s32Y:%d, stVoChnAttr.stRect.u32Width: %d, stVoChnAttr.stRect.u32Height: %d\n",
		//									stVoChnAttr.stRect.s32X, stVoChnAttr.stRect.s32Y, stVoChnAttr.stRect.u32Width, stVoChnAttr.stRect.u32Height);
	}
	CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stVoChnAttr));
	
	//if(VochnInfo[index].IsBindVdec[VoDev] == -1)
	if(pstLayout != NULL && i < u32ChnNum)
	{
		#if 0
		if(s_astVoDevStatDflt[VoDev].bIsSingle || s_astVoDevStatDflt[VoDev].enPreviewStat != PRV_STAT_NORM)
		{	
			w = PRV_SINGLE_SCREEN_W;
			h = PRV_SINGLE_SCREEN_H;
		}
		else
		
		{
			w = PRV_BT1120_SIZE_W;
			h = PRV_BT1120_SIZE_H;
		}
		#endif
		stSrcRect.s32X		= (w * pstLayout[i].s32X) / PRV_PREVIEW_LAYOUT_DIV;
		stSrcRect.s32Y		= (h * pstLayout[i].s32Y) / PRV_PREVIEW_LAYOUT_DIV;
		stSrcRect.u32Width	= (w * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
		stSrcRect.u32Height = (h * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;
		
		stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
		
		stVoZoomAttr.stZoomRect.s32X		= stDestRect.s32X	   & (~0x01);
		stVoZoomAttr.stZoomRect.s32Y		= stDestRect.s32Y	   & (~0x01);
		stVoZoomAttr.stZoomRect.u32Width	= stDestRect.u32Width  & (~0x01);
		stVoZoomAttr.stZoomRect.u32Height	= stDestRect.u32Height & (~0x01);
		
		//stVoZoomAttr.enField = VIDEO_FIELD_INTERLACED;
#if defined(SN9234H1)		
		stVoZoomAttr.enField = VIDEO_FIELD_FRAME;
#endif	
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------x:%d, y:%d, width: %d, height: %d\n", stVoZoomAttr.stZoomRect.s32X, stVoZoomAttr.stZoomRect.s32Y,stVoZoomAttr.stZoomRect.u32Width, stVoZoomAttr.stZoomRect.u32Height);
		CHECK_RET(HI_MPI_VO_SetZoomInWindow(VoDev, VoChn, &stVoZoomAttr));
	}
	
	CHECK(HI_MPI_VO_SetChnFrameRate(VoDev, VoChn, 30));
#if defined(SN9234H1)	
	CHECK(HI_MPI_VI_BindOutput(PRV_HD_DEV, 0, VoDev, VoChn));
#endif
	//if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_NORM)
	{
		VochnInfo[index].IsBindVdec[VoDev] = 1;
	}
	if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL && VoDev == s_VoDevCtrlDflt && VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn
		&& (IsDispInPic == 1 || s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_ZOOM_IN ))//电子放大或画中画
	{
		CHECK(HI_MPI_VO_SetChnFrameRate(VoDev, PRV_CTRL_VOCHN, 30));
#if defined(SN9234H1)		
		CHECK(HI_MPI_VI_BindOutput(PRV_HD_DEV, 0, VoDev, PRV_CTRL_VOCHN));
#endif
	}
		
			
	return HI_SUCCESS;
}

/*************************************************
Function: //PRV_PreviewVoDevSingle
Description: 根据指定VO状态显示VO设备上的视频层的单画面。
Calls: 
Called By: //
Input: //VoDev:设备号
		enPreviewMode:需要显示预览模式
		u32Index:多画面通道索引号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/

STATIC HI_S32 PRV_PreviewVoDevSingle(VO_DEV VoDev, HI_U32 u32chn)
{	
	//sem_post(&sem_SendNoVideoPic);
    HI_U32 u32Width = 0, u32Height = 0, u32ChnNum = 0;
	HI_S32 i = 0, index = 0;
	VO_CHN VoChn = 0;
	VO_CHN_ATTR_S stVoChnAttr;
#if defined(SN9234H1)	
	VO_ZOOM_ATTR_S stVoZoomAttr;
#else
	int s32Ret = 0;
#endif
	RECT_S *pstLayout = NULL;
	unsigned char order_buf[PRV_VO_CHN_NUM];
	VoChn = u32chn;
	RECT_S stSrcRect, stDestRect;
	
	/*确保参数的合法性*/
#if defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV || VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}

	if ( (VoDev < 0 || VoDev >= PRV_VO_MAX_DEV))
	{
		RET_FAILURE("Invalid Parameter: VoDev or u32Index");
	}


#else	

	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
	{
		RET_FAILURE("Invalid Parameter: VoDev or u32Index");
	}
	if(VoDev == DHD1)
		VoDev = DSD0;
#endif	
	if (VoChn < 0 || VoChn >= g_Max_Vo_Num)//PRV_VO_CHN_NUM)
	{
		RET_FAILURE("Invalid Parameter: VoChn ");
	}
	
	index = PRV_GetVoChnIndex(VoChn);
	if(index < 0)
		RET_FAILURE("Valid index!!");
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "#############PRV_PreviewVoDevSingle s_s32NPFlagDflt = %d######################\n",s_s32NPFlagDflt);

	//printf("#############PRV_PreviewVoDevSingle s_s32NPFlagDflt = %d ,stImageSize h  =%d######################\n",s_s32NPFlagDflt,u32Height);

	u32ChnNum = 1;
	pstLayout = s_astPreviewLayout1;
#if defined(SN9234H1)
	//PRV_HideAllVoChn(VoDev);
	PRV_ViUnBindAllVoChn(VoDev);
	PRV_VdecUnBindAllVoChn1(VoDev);
	//PRV_ClearAllVoChnBuf(VoDev);
	PRV_ResetVideoLayer(VoDev);
#else
	PRV_HideAllVoChn(VoDev);
	PRV_VdecUnBindAllVpss(VoDev);
	//PRV_ClearAllVoChnBuf(VoDev);
	for(i = 0; i < PRV_VO_CHN_NUM; i++)
	{
#if defined(Hi3535)
		PRV_VO_UnBindVpss(VoDev, i, i, VPSS_BSTR_CHN);
#else
		PRV_VO_UnBindVpss(VoDev, i, i, VPSS_PRE0_CHN);
#endif	
	}
	PRV_ResetVideoLayer(VoDev);

	for(i = 0; i < PRV_VO_CHN_NUM;i++)
	{
		if(VoDev == DHD0)
		{
#if defined(Hi3535)
			s32Ret = PRV_VO_BindVpss(VoDev, i, i,VPSS_BSTR_CHN);
		    if (HI_SUCCESS != s32Ret)
		    {
		        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_VO_BindVpss VPSS_PRE0_CHN failed!\n");
		        return -1;
		    }
#else
			s32Ret = PRV_VO_BindVpss(VoDev, i, i,VPSS_PRE0_CHN);
		    if (HI_SUCCESS != s32Ret)
		    {
		        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_VO_BindVpss VPSS_PRE0_CHN failed!\n");
		        return -1;
		    }
#endif
		}
		#if 0
		else
		{
			s32Ret = PRV_VO_BindVpss(VoDev, i, i,VPSS_BYPASS_CHN);
		    if (HI_SUCCESS != s32Ret)
		    {
		        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"PRV_VO_BindVpss VPSS_BYPASS_CHN failed!\n");
		        return -1;
		    }
		}
		#endif
	}
#endif	
	u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
	u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
	PRV_HideAllVoChn(VoDev);
	i = 0;
    {
		if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
			VoChn = u32chn;
		
		order_buf[i] = VoChn;
		//被动解码下，没有"无网络"选项
		if(PRV_CurDecodeMode == PassiveDecode && VochnInfo[index].VdecChn == DetVLoss_VdecChn)
		{
			VochnInfo[index].VdecChn = NoConfig_VdecChn;	
		}
		//报警弹出时，即使对应通道未启用，也会连接，不需要限定为"未配置"
		if(PRV_CurDecodeMode == SwitchDecode && s_astVoDevStatDflt[VoDev].bIsAlarm != HI_TRUE)
		{
			if(ScmGetListCtlState() == 0 && SCM_ChnConfigState(VoChn) == 0 && PRV_GetDoubleIndex() == 0)//非点位控制下，通道未启用时黑屏
			{
				VochnInfo[index].VdecChn = NoConfig_VdecChn;	
			}
		}
		/* 判断VoChn是否有效 */
		

		CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
    	stVoChnAttr.stRect.s32X		 = (u32Width * pstLayout[i].s32X) / PRV_PREVIEW_LAYOUT_DIV;
    	stVoChnAttr.stRect.s32Y		 = (u32Height * pstLayout[i].s32Y) / PRV_PREVIEW_LAYOUT_DIV;
		stVoChnAttr.stRect.u32Width	 = (u32Width * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
		stVoChnAttr.stRect.u32Height = (u32Height * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;

		//有三种情况调用此接口:报警弹出、进入参数界面以及电子放大
		//进入参数界面时，由于默认设备的单画面的"无网络"会被覆盖，不重设区域以及绑定解码通道显示效果更好，
		//								第二设备只显示单画面，需要重设
		//"无网络"时电子放大禁用
		//所以只有报警弹出时需要重设显示区域以及绑定解码通道
#if defined(SN9234H1)
		if((VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn) 
			&& (s_astVoDevStatDflt[VoDev].bIsAlarm == HI_TRUE || VoDev == (s_VoDevCtrlDflt == HD ? s_VoSecondDev : HD)))
#else
		if((VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn) 
			&& (s_astVoDevStatDflt[VoDev].bIsAlarm == HI_TRUE || VoDev == (s_VoDevCtrlDflt == DHD0 ? s_VoSecondDev : DHD0)))
#endif			
		{
			//stVoChnAttr.stRect.s32X 	 = stVoChnAttr.stRect.s32X + (stVoChnAttr.stRect.u32Width - (NOVIDEO_IMAGWIDTH * pstLayout[i].u32Width / PRV_PREVIEW_LAYOUT_DIV)) / 2;
			//stVoChnAttr.stRect.s32Y 	 = stVoChnAttr.stRect.s32Y + (stVoChnAttr.stRect.u32Height - (NOVIDEO_IMAGHEIGHT * pstLayout[i].u32Height / PRV_PREVIEW_LAYOUT_DIV))/ 2;
			//stVoChnAttr.stRect.u32Width  = (NOVIDEO_IMAGWIDTH * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
			//stVoChnAttr.stRect.u32Height = (NOVIDEO_IMAGHEIGHT * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;
			
		}

		if(VochnInfo[index].VdecChn >= 0 && VochnInfo[index].VdecChn != DetVLoss_VdecChn && VochnInfo[index].VdecChn != NoConfig_VdecChn)
		{
			stSrcRect.s32X		 = stVoChnAttr.stRect.s32X;
			stSrcRect.s32Y		 = stVoChnAttr.stRect.s32Y;
			stSrcRect.u32Width	 = stVoChnAttr.stRect.u32Width;
			stSrcRect.u32Height  = stVoChnAttr.stRect.u32Height;
			
			stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
			stVoChnAttr.stRect.s32X 	 = stDestRect.s32X;
			stVoChnAttr.stRect.s32Y 	 = stDestRect.s32Y;
			stVoChnAttr.stRect.u32Width  = stDestRect.u32Width;
			stVoChnAttr.stRect.u32Height = stDestRect.u32Height;

		}
		stVoChnAttr.stRect.s32X 	 &= (~0x01);
		stVoChnAttr.stRect.s32Y 	 &= (~0x01);
		stVoChnAttr.stRect.u32Width  &= (~0x01);
		stVoChnAttr.stRect.u32Height &= (~0x01);
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "i: %d,X: %d, Y: %d, W: %d, H: %d\n", i, stVoChnAttr.stRect.s32X, stVoChnAttr.stRect.s32Y, stVoChnAttr.stRect.u32Width, stVoChnAttr.stRect.u32Height);
		CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stVoChnAttr));
#if defined(SN9234H1)
		if(VoChn < (LOCALVEDIONUM < PRV_CHAN_NUM ? LOCALVEDIONUM : PRV_CHAN_NUM))
		{
			int videv = 0;
			if(VoChn >= PRV_VI_CHN_NUM)
			{
			//如果通道为5到8，那么对应采集设备2
				videv = PRV_656_DEV;
			}
			else
			{
				videv = PRV_656_DEV_1;
			}

			CHECK_RET(HI_MPI_VI_BindOutput(videv, VoChn%PRV_VI_CHN_NUM, VoDev, VoChn));

			//printf("###########VoChn = %d ,VoDev = %d, videv = %d,vicha = %d ######################\n",VoChn,VoDev,videv,VoChn%PRV_VI_CHN_NUM);
		}
		else 
#endif
		{	
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VochnInfo[index].VdecChn: %d, VochnInfo[index].VoChn: %d, VochnInfo[index].SlaveId: %d\n", VochnInfo[index].VdecChn, VochnInfo[index].VoChn, VochnInfo[index].SlaveId);
#if defined(SN9234H1)
			if((VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn)
				&& (s_astVoDevStatDflt[VoDev].bIsAlarm == HI_TRUE|| VoDev == (s_VoDevCtrlDflt == HD ? s_VoSecondDev : HD))
				&& VochnInfo[index].IsBindVdec[VoDev] == -1)
#else			
			if((VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn)
				&& (s_astVoDevStatDflt[VoDev].bIsAlarm == HI_TRUE|| VoDev == (s_VoDevCtrlDflt == DHD0 ? s_VoSecondDev : DHD0))
				&& VochnInfo[index].IsBindVdec[VoDev] == -1)
#endif				
			{
#if defined(SN9234H1)
				CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, VoDev, VochnInfo[index].VoChn));
#else
				PRV_VPSS_ResetWH(VochnInfo[index].VoChn,VochnInfo[index].VdecChn,704,576);
				CHECK(PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VochnInfo[index].VoChn));
#endif
				VochnInfo[index].IsBindVdec[VoDev] =  0;

			}
			//之前已经绑定好解码通道，在此需要解绑定
			if((VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn)
				&& s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL
				&& VoDev == s_VoDevCtrlDflt
				&& VochnInfo[index].IsBindVdec[VoDev] == 0)
			{
#if defined(SN9234H1)
				(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[index].VdecChn, VoDev, VochnInfo[index].VoChn));
#else
				CHECK(PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn, VochnInfo[index].VoChn));
#endif
				VochnInfo[index].IsBindVdec[VoDev] = -1;
			}
			//CHECK(HI_MPI_VO_ClearChnBuffer(VoDev, VochnInfo[index].VoChn, HI_TRUE));
			if((VochnInfo[index].VdecChn != DetVLoss_VdecChn && VochnInfo[index].VdecChn != NoConfig_VdecChn)
				&&VochnInfo[index].SlaveId == PRV_MASTER
				&& VochnInfo[index].IsBindVdec[VoDev] == -1)
			{	
#if defined(SN9234H1)
				(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, VoDev, VochnInfo[index].VoChn));
#else
				PRV_VPSS_ResetWH(VochnInfo[index].VoChn,VochnInfo[index].VdecChn,704,576);
				CHECK(PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VochnInfo[index].VoChn));
#endif
				VochnInfo[index].IsBindVdec[VoDev] = 1;
			}
#if defined(SN9234H1)
			//从片绑定到输入设备0上
			//else if(VoChn >= PRV_CHAN_NUM)
			else if(VochnInfo[index].SlaveId > PRV_MASTER && VochnInfo[index].IsBindVdec[VoDev] == -1)
			{
				int w = 0, h = 0;
#if 0
#if defined(SN8604M) || defined(SN8608M) || defined(SN8608M_LE) || defined(SN8616M_LE)|| defined(SN9234H1)
				w = PRV_BT1120_SIZE_W;
				h = PRV_BT1120_SIZE_H;
#else
				w = PRV_SINGLE_SCREEN_W;
				h = PRV_SINGLE_SCREEN_H;
#endif
#endif
				w = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
				h = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
				#if 1
				stSrcRect.s32X		= (w * pstLayout[i].s32X) / PRV_PREVIEW_LAYOUT_DIV;
				stSrcRect.s32Y		= (h * pstLayout[i].s32Y) / PRV_PREVIEW_LAYOUT_DIV;
				stSrcRect.u32Width	= (w * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
				stSrcRect.u32Height = (h * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;
				stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
				stVoZoomAttr.stZoomRect.s32X		= stDestRect.s32X	   & (~0x01);
				stVoZoomAttr.stZoomRect.s32Y		= stDestRect.s32Y	   & (~0x01);
				stVoZoomAttr.stZoomRect.u32Width	= stDestRect.u32Width  & (~0x01);
				stVoZoomAttr.stZoomRect.u32Height	= stDestRect.u32Height & (~0x01);
				#else
				stVoZoomAttr.stZoomRect.s32X		= ((w * pstLayout[i].s32X) / PRV_PREVIEW_LAYOUT_DIV) & (~0x01);
				stVoZoomAttr.stZoomRect.s32Y		= ((h * pstLayout[i].s32Y) / PRV_PREVIEW_LAYOUT_DIV) & (~0x01);
				stVoZoomAttr.stZoomRect.u32Width	= ((w * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV) & (~0x01);
				stVoZoomAttr.stZoomRect.u32Height	= ((h * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV) & (~0x01);
				#endif
				stVoZoomAttr.enField = VIDEO_FIELD_FRAME;
		
				CHECK_RET(HI_MPI_VO_SetZoomInWindow(VoDev, VoChn, &stVoZoomAttr));

				CHECK(HI_MPI_VI_BindOutput(PRV_HD_DEV, 0, VoDev, VoChn));

				VochnInfo[index].IsBindVdec[VoDev] = 1;								
			}
#endif
			
		}
		//VochnInfo[i].bIsStopGetVideoData = 0;
#if defined(Hi3535)
		CHECK(HI_MPI_VO_SetChnFrameRate(VoDev, VoChn, 25));
		CHECK_RET(HI_MPI_VO_ShowChn(VoDev,VoChn));
		CHECK_RET(HI_MPI_VO_EnableChn(VoDev,VoChn));
#else
		CHECK(HI_MPI_VO_SetChnFrameRate(VoDev, VoChn, 30));
		CHECK_RET(HI_MPI_VO_ChnShow(VoDev,VoChn));
		CHECK_RET(HI_MPI_VO_EnableChn(VoDev,VoChn));
#endif
   }
	//CHECK_RET(HI_MPI_VO_SetAttrEnd(VoDev));
#if defined(SN9234H1)
	PRV_VdecBindAllVoChn(VoDev);
	sem_post(&sem_SendNoVideoPic);
#else	
	//PRV_VdecBindAllVpss(VoDev);
	//sem_post(&sem_SendNoVideoPic);
#endif
	OSD_Get_Preview_param(VoDev,
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width,
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height,
		u32ChnNum,SingleScene,order_buf);

#if 0 /*2010-9-17 动态调整VI通道图像大小*/
	PRV_SetViChnAttrByPreviewMode();
#endif
	RET_SUCCESS("");
}
/*************************************************
Function: //PRV_PreviewVoDevInMode
Description: 根据指定VO状态显示VO设备上的视频层的画面排布。
Calls: 
Called By: //
Input: //VoDev:设备号
		enPreviewMode:需要显示预览模式
		u32Index:多画面通道索引号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_PreviewVoDevInMode(VO_DEV VoDev, PRV_PREVIEW_MODE_E enPreviewMode, HI_U32 u32Index, VO_CHN *pOrder)
{
	//sem_post(&sem_SendNoVideoPic);
    HI_U32 u32Width = 0, u32Height = 0, u32ChnNum = 0, Max_num = 0;
	//HI_U32 oldX = 0, oldY = 0, oldWidth = 0, oldHeight = 0, tmpX = 0, tmpY = 0, tmpWidth = 0, tmpHeight = 0;
	HI_S32 i = 0, index = 0;
	VO_CHN VoChn;
	VO_CHN_ATTR_S stVoChnAttr;
#if defined(SN9234H1)
	VO_ZOOM_ATTR_S stVoZoomAttr;
#else
	HI_S32 s32Ret = 0;
#endif
	RECT_S *pstLayout = NULL;
	RECT_S stSrcRect,stDestRect;
	unsigned char order_buf[PRV_VO_CHN_NUM];

#if defined(SN9234H1)
	//获取当前预览模式下最大通道数
	CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[HD].enPreviewMode, &Max_num));
	TRACE(SCI_TRACE_NORMAL,MOD_PRV,"-------------PRV_PreviewVoDevInMode s_s32NPFlagDflt = %d ,Max_num =%d ,u32Index =%d ,enPreviewMode= %d\n",s_s32NPFlagDflt,Max_num,u32Index,enPreviewMode);
	//printf("-------------PRV_PreviewVoDevInMode s_s32NPFlagDflt = %d ,Max_num =%d ,u32Index =%d ,enPreviewMode= %d\n",s_s32NPFlagDflt,Max_num,u32Index,enPreviewMode);

	/*确保参数的合法性*/
	if(VoDev == SPOT_VO_DEV || VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}

	if ( (VoDev < 0 || VoDev >= PRV_VO_MAX_DEV))
		//|| (u32Index < 0 || u32Index >= Max_num))
	{
		RET_FAILURE("Invalid Parameter: VoDev or u32Index");
	}
#else
	//获取当前预览模式下最大通道数
	CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[DHD0].enPreviewMode, &Max_num));
	TRACE(SCI_TRACE_NORMAL,MOD_PRV,"-------------PRV_PreviewVoDevInMode VoDev: %d, s_s32NPFlagDflt = %d ,Max_num =%d ,u32Index =%d ,enPreviewMode= %d\n", VoDev, s_s32NPFlagDflt,Max_num,u32Index,enPreviewMode);
	//printf("-------------PRV_PreviewVoDevInMode s_s32NPFlagDflt = %d ,Max_num =%d ,u32Index =%d ,enPreviewMode= %d\n",s_s32NPFlagDflt,Max_num,u32Index,enPreviewMode);

	/*确保参数的合法性*/

	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
		//|| (u32Index < 0 || u32Index >= Max_num))
	{
		RET_FAILURE("Invalid Parameter: VoDev or u32Index");
	}
	if(VoDev == DHD1)
		VoDev = DSD0;
#endif
    switch(enPreviewMode)
	{
		case SingleScene:
			u32ChnNum = 1;
			pstLayout = s_astPreviewLayout1;
			break;
		case TwoScene:
			u32ChnNum = 2;
			pstLayout = s_astPreviewLayout2;
			break;
		case ThreeScene:
			u32ChnNum = 3;
			pstLayout = s_astPreviewLayout3;
			break;
		case FourScene:
		case LinkFourScene:
			u32ChnNum = 4;
			pstLayout = s_astPreviewLayout4;
			break;
		case FiveScene:
			u32ChnNum = 5;
			pstLayout = s_astPreviewLayout5;
			break;
		case SixScene:
			u32ChnNum = 6;
			pstLayout = s_astPreviewLayout6;
			break;
		case SevenScene:
			u32ChnNum = 7;
			pstLayout = s_astPreviewLayout7;
			break;
		case EightScene:
			u32ChnNum = 8;
			pstLayout = s_astPreviewLayout8;
			break;
		case NineScene:
		case LinkNineScene:
			u32ChnNum = 9;
			pstLayout = s_astPreviewLayout9;
			break;
		case SixteenScene:
			u32ChnNum = 16;
			pstLayout = s_astPreviewLayout16;
			break;
		default:
			RET_FAILURE("Invalid Parameter: enPreviewMode");
	}
	PRV_HideAllVoChn(VoDev);
#if defined(SN9234H1)
	PRV_ViUnBindAllVoChn(VoDev);
	PRV_VdecUnBindAllVoChn1(VoDev);
	PRV_ResetVideoLayer(VoDev);
#else
	PRV_VdecUnBindAllVpss(VoDev);
	for(i = 0; i < PRV_VO_CHN_NUM; i++)
	{
#if defined(Hi3535)
		PRV_VO_UnBindVpss(VoDev, i, i, VPSS_BSTR_CHN);
#else
		PRV_VO_UnBindVpss(VoDev, i, i, VPSS_PRE0_CHN);
#endif			
	}
	PRV_ClearAllVoChnBuf(VoDev);
	PRV_ResetVideoLayer(VoDev);

	for(i = 0;i < PRV_VO_CHN_NUM; i++)
	{
		if(VoDev == DHD0)
		{			
			HI_MPI_VO_DisableChn(VoDev, i);
#if defined(Hi3535)
			s32Ret = PRV_VO_BindVpss(VoDev, i, i,VPSS_BSTR_CHN);
		    if (HI_SUCCESS != s32Ret)
		    {
		        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"PRV_VO_BindVpss VPSS_PRE0_CHN failed!\n");
		        return -1;
		    }
#else
			s32Ret = PRV_VO_BindVpss(VoDev, i, i,VPSS_PRE0_CHN);
		    if (HI_SUCCESS != s32Ret)
		    {
		        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"PRV_VO_BindVpss VPSS_PRE0_CHN failed!\n");
		        return -1;
		    }
#endif
		}
	}
#endif	
	//PRV_ClearAllVoChnBuf(VoDev);
	u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
	u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
	
	if(enPreviewMode == NineScene || enPreviewMode == LinkNineScene || enPreviewMode == ThreeScene || enPreviewMode == FiveScene
		|| enPreviewMode == SevenScene)
	{
	//解决9画面预览时相邻两列画面之间存在缝隙的问题
		while(u32Width%6 != 0)
			u32Width++;
		while(u32Height%6 != 0)
			u32Height++;
	}
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----u32Index:%d-----u32Width: %d, u32Height: %d\n", u32Index, u32Width, u32Height);
	PRV_HideAllVoChn(VoDev);
	//for(i = 0; i < Max_num; i++)
    for (i = 0; i < u32ChnNum && u32Index + i < Max_num; i++)
    {
		//点位控制和被动解码下下，无论通道是否隐藏，通道顺序如何，各个通道均显示
		if(PRV_CurDecodeMode == PassiveDecode)
		{
			VoChn = i + u32Index;
		}
		else if(enPreviewMode == SingleScene && LayoutToSingleChn >= 0)
		{
			VoChn = LayoutToSingleChn;
		}
		else
		{
			VoChn = pOrder[(u32Index + i) % Max_num];	
		}
		
		order_buf[i] = VoChn;
		//printf("i: %d, u32ChnNum: %d, PRV_CurDecodeMode: %d, Vochn: %d\n", i, u32ChnNum, PRV_CurDecodeMode, VoChn);
		/* 判断VoChn是否有效 */
		if (VoChn < 0 || VoChn >= g_Max_Vo_Num)
		{
			continue;//break;//
		}
		index = PRV_GetVoChnIndex(VoChn);
		//被动解码下，没有"未配置"选项
		if(PRV_CurDecodeMode == PassiveDecode && VochnInfo[index].VdecChn == DetVLoss_VdecChn && IsTest == 0)
		{
			VochnInfo[index].VdecChn = NoConfig_VdecChn;	
		}
		if(PRV_CurDecodeMode == SwitchDecode && IsTest == 0)//生产测试没有创建解码通道:NoConfig_VdecChn
		{
			if(ScmGetListCtlState() == 0 && SCM_ChnConfigState(VoChn) == 0  && PRV_GetDoubleIndex() == 0)//非点位控制下，通道未启用时黑屏
			{
				VochnInfo[index].VdecChn = NoConfig_VdecChn;
			}
			
		}
		{
			CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));

	        stVoChnAttr.stRect.s32X		 = (u32Width * pstLayout[i].s32X) / PRV_PREVIEW_LAYOUT_DIV;
	        stVoChnAttr.stRect.s32Y		 = (u32Height * pstLayout[i].s32Y) / PRV_PREVIEW_LAYOUT_DIV;
			stVoChnAttr.stRect.u32Width	 = (u32Width * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
			stVoChnAttr.stRect.u32Height = (u32Height * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;

			if(enPreviewMode == NineScene || enPreviewMode == LinkNineScene)
			{ 
				if((i + 1) % 3 == 0)//最后一列
					stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
				if(i > 5 && i < 9)//最后一行
					stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
			}
			
			if( enPreviewMode == ThreeScene )
			{ 
			    if( i == 2)//最后一列
				     stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
						//最后一行
					 stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
			}
			if( enPreviewMode == FiveScene )
			{ 
			    if( i > 1 )//最后一列
				     stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
				if(i==0 || i==1 || i==4)//最后一行
				     stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
			}
			if(enPreviewMode == SevenScene)
			{ 
			    if(i==2 || i==4 || i==6)//最后一列
				     stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
				if(i==0 || i==5 || i==6)//最后一行
				     stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
			}
			//"无网络视频"或"未配置"图片大小为352*80，因此只需在指定区域中截取一部分显示区域
			if(IsTest == 0//生产测试时，采用同一个解码通道道，不需要重设显示区域
				&& (VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn))
			{				
		        //stVoChnAttr.stRect.s32X		 = stVoChnAttr.stRect.s32X + (stVoChnAttr.stRect.u32Width - (NOVIDEO_IMAGWIDTH * pstLayout[i].u32Width / PRV_PREVIEW_LAYOUT_DIV)) / 2;
		        //stVoChnAttr.stRect.s32Y		 = stVoChnAttr.stRect.s32Y + (stVoChnAttr.stRect.u32Height - (NOVIDEO_IMAGHEIGHT * pstLayout[i].u32Height / PRV_PREVIEW_LAYOUT_DIV))/ 2;
				//stVoChnAttr.stRect.u32Width	 = (NOVIDEO_IMAGWIDTH * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
				//stVoChnAttr.stRect.u32Height = (NOVIDEO_IMAGHEIGHT * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;
#if defined(Hi3531)||defined(Hi3535)
				if(stVoChnAttr.stRect.u32Height < 32)
				{
					stVoChnAttr.stRect.s32Y = stVoChnAttr.stRect.s32Y - (32 - stVoChnAttr.stRect.u32Height)/2;
					stVoChnAttr.stRect.u32Height = 32;
				}
#endif				
			}
			else
			{
				stSrcRect.s32X		 = stVoChnAttr.stRect.s32X;
				stSrcRect.s32Y		 = stVoChnAttr.stRect.s32Y;
				stSrcRect.u32Width	 = stVoChnAttr.stRect.u32Width;
				stSrcRect.u32Height  = stVoChnAttr.stRect.u32Height;				
				stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
				stVoChnAttr.stRect.s32X 	 = stDestRect.s32X;
				stVoChnAttr.stRect.s32Y 	 = stDestRect.s32Y;
				stVoChnAttr.stRect.u32Width  = stDestRect.u32Width;
				stVoChnAttr.stRect.u32Height = stDestRect.u32Height;
			}
			
			stVoChnAttr.stRect.s32X 	 &= (~0x01);
			stVoChnAttr.stRect.s32Y 	 &= (~0x01);
			stVoChnAttr.stRect.u32Width  &= (~0x01);
			stVoChnAttr.stRect.u32Height &= (~0x01);
			//HostSendHostToSlaveStream(0,0,0,0);
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "i: %d,X: %d, Y: %d, W: %d, H: %d\n", i, stVoChnAttr.stRect.s32X, stVoChnAttr.stRect.s32Y, stVoChnAttr.stRect.u32Width, stVoChnAttr.stRect.u32Height);
			//printf("i: %d,X: %d, Y: %d, W: %d, H: %d, stVoChnAttr.u32Priority: %d\n", i, stVoChnAttr.stRect.s32X, stVoChnAttr.stRect.s32Y, stVoChnAttr.stRect.u32Width, stVoChnAttr.stRect.u32Height, stVoChnAttr.u32Priority);

			CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stVoChnAttr));

			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------VoChn: %d, x:%d, y:%d, width: %d, height: %d\n", VoChn, stVoChnAttr.stRect.s32X, stVoChnAttr.stRect.s32Y, stVoChnAttr.stRect.u32Width, stVoChnAttr.stRect.u32Height);
#if !defined(Hi3535)
			if((enPreviewMode == EightScene && 0 == i))
			{
				//8画面的第一个画面用both显示
				CHECK_RET(HI_MPI_VO_SetChnField(VoDev, VoChn, VO_FIELD_BOTH));
			}
			else if(enPreviewMode < EightScene)
			{
				CHECK_RET(HI_MPI_VO_SetChnField(VoDev, VoChn, VO_FIELD_BOTH));
			}
			else
			{
				CHECK_RET(HI_MPI_VO_SetChnField(VoDev, VoChn, VO_FIELD_BOTTOM));
			}
#endif
		}
#if defined(SN9234H1)
		if(VoChn < (LOCALVEDIONUM < PRV_CHAN_NUM ? LOCALVEDIONUM : PRV_CHAN_NUM))
		{
			int videv = 0;
			if(VoChn >= PRV_VI_CHN_NUM)
			{
			//如果通道为5到8，那么对应采集设备2
				videv = PRV_656_DEV;
			}
			else
			{
				videv = PRV_656_DEV_1;
			}
			CHECK_RET(HI_MPI_VI_BindOutput(videv, VoChn%PRV_VI_CHN_NUM, VoDev, VoChn));

			//printf("###########VoChn = %d ,VoDev = %d, videv = %d,vichn = %d ######################\n",VoChn,VoDev,videv,VoChn%PRV_VI_CHN_NUM);
		}
		else 
#endif
		{			
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----index: %d---VoDev: %d----Bind---SlaveId: %d, IsBindVdec: %d, VochnInfo[index].VdecChn: %d, VoChn: %d\n",index, VoDev, VochnInfo[index].SlaveId, VochnInfo[index].IsBindVdec[VoDev], VochnInfo[index].VdecChn, VoChn);
			if(VochnInfo[index].VdecChn >= 0 && -1 == VochnInfo[index].IsBindVdec[VoDev])
			{
				if(VochnInfo[index].SlaveId == PRV_MASTER)
				{
#if defined(SN9234H1)
					CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, VoDev, VoChn));
#else
					PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn, VoChn);
					PRV_VPSS_ResetWH(VoChn,VochnInfo[index].VdecChn,VochnInfo[index].VideoInfo.width,VochnInfo[index].VideoInfo.height);
					CHECK(PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VoChn));
#endif					
					VochnInfo[index].IsBindVdec[VoDev] = ((VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn) ? 0 : 1);
				}
#if defined(SN9234H1)
				else if(VochnInfo[index].SlaveId > PRV_MASTER/* && VochnInfo[index].IsHaveVdec*/)//从片创建好解码通道已经开始接收数据
				{//从片绑定到输入设备0上
					int w = 0, h = 0;

					w = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
					h = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
					stSrcRect.s32X 	    = (w * pstLayout[i].s32X) / PRV_PREVIEW_LAYOUT_DIV;
					stSrcRect.s32Y 	    = (h * pstLayout[i].s32Y) / PRV_PREVIEW_LAYOUT_DIV;
					stSrcRect.u32Width  = (w * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
					stSrcRect.u32Height = (h * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;

					stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
					
					stVoZoomAttr.stZoomRect.s32X		= stDestRect.s32X      & (~0x01);
					stVoZoomAttr.stZoomRect.s32Y		= stDestRect.s32Y      & (~0x01);
					stVoZoomAttr.stZoomRect.u32Width	= stDestRect.u32Width  & (~0x01);
					stVoZoomAttr.stZoomRect.u32Height	= stDestRect.u32Height & (~0x01);
					//stVoZoomAttr.enField = VIDEO_FIELD_INTERLACED;
					stVoZoomAttr.enField = VIDEO_FIELD_FRAME;
					
					//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------x:%d, y:%d, width: %d, height: %d\n", stVoZoomAttr.stZoomRect.s32X, stVoZoomAttr.stZoomRect.s32Y,stVoZoomAttr.stZoomRect.u32Width, stVoZoomAttr.stZoomRect.u32Height);
					//printf("-------x:%d, y:%d, width: %d, height: %d\n", stVoZoomAttr.stZoomRect.s32X, stVoZoomAttr.stZoomRect.s32Y,stVoZoomAttr.stZoomRect.u32Width, stVoZoomAttr.stZoomRect.u32Height);
					CHECK_RET(HI_MPI_VO_SetZoomInWindow(VoDev, VoChn, &stVoZoomAttr));
					CHECK(HI_MPI_VI_BindOutput(PRV_HD_DEV, 0, VoDev, VoChn));
					VochnInfo[index].IsBindVdec[VoDev] = 1;
				}
#endif
			}			
		}
#if defined(Hi3535)
		CHECK_RET(HI_MPI_VO_ShowChn(VoDev, VoChn));
#else		
		CHECK_RET(HI_MPI_VO_ChnShow(VoDev, VoChn));
#endif
		CHECK_RET(HI_MPI_VO_EnableChn(VoDev, VoChn));		
	}
#if defined(SN9234H1)
	PRV_VdecBindAllVoChn(VoDev);
#else
	//PRV_VdecBindAllVpss(VoDev);
#endif
	sem_post(&sem_SendNoVideoPic);
	OSD_Get_Preview_param(VoDev,
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width,
		s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height,
		i, enPreviewMode,order_buf);

#if 0 /*2010-9-17 动态调整VI通道图像大小*/
	PRV_SetViChnAttrByPreviewMode();
#endif

	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_RefreshVoDevScreen
Description: 根据VO状态刷新VO设备的显示
Calls: 
Called By: //
Input: //VoDev:设备号
		Is_Double: 是否双屏显示
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_RefreshVoDevScreen(VO_DEV VoDev, HI_U32 Is_Double, VO_CHN *pOrder)
{
	VO_DEV VoDev2 = VoDev;
#if defined(SN9234H1)
	sem_post(&sem_SendNoVideoPic);
again:
	if(VoDev != HD)
#else
	Is_Double = DISP_NOT_DOUBLE_DISP;
again:
	if(VoDev != DHD0)
#endif

	{
		RET_SUCCESS("");
	}	
	//1、判断状态：enPreviewStat
	switch (s_astVoDevStatDflt[VoDev].enPreviewStat)
	{
		case PRV_STAT_NORM:
			{				
				//2、是否报警bIsAlarm
				if (s_astVoDevStatDflt[VoDev].bIsAlarm)
				{
					//切到单画面s32AlarmChn
					//HI_S32 s32Index;
					//CHECK_RET(PRV_Chn2Index(VoDev, s_astVoDevStatDflt[VoDev].s32AlarmChn, &s32Index,pOrder));
					PRV_PreviewVoDevSingle(VoDev, s_astVoDevStatDflt[VoDev].s32AlarmChn);
				}
				else if (s_astVoDevStatDflt[VoDev].bIsSingle)
				{
					//切到单画面s32SingleIndex
					if(PRV_CurDecodeMode == PassiveDecode)
					{
						PRV_PreviewVoDevInMode(VoDev, SingleScene, DoubleToSingleIndex, pOrder);
					}
					else
					{
						PRV_PreviewVoDevInMode(VoDev, SingleScene, s_astVoDevStatDflt[VoDev].s32SingleIndex, pOrder);
					}
				}
				else
				{
					//切到多画面s32PreviewIndex
					PRV_PreviewVoDevInMode(VoDev, s_astVoDevStatDflt[VoDev].enPreviewMode, s_astVoDevStatDflt[VoDev].s32PreviewIndex, pOrder);
				}
			}
			break;
		case PRV_STAT_PB:
		case PRV_STAT_PIC:	
			{
				//解绑VoDev的所有VoChn
//				int i = 0;
#if defined(SN9234H1)
				PRV_ViUnBindAllVoChn(VoDev);				
				PRV_VdecUnBindAllVoChn1(VoDev);
				PRV_ClearAllVoChnBuf(VoDev);
				PRV_DisableAllVoChn(VoDev);
				CHECK_RET(HI_MPI_VO_DisableVideoLayer(VoDev));
				s_astVoDevStatDflt[VoDev].stVideoLayerAttr.s32PiPChn = VO_DEFAULT_CHN;
				CHECK_RET(HI_MPI_VO_SetVideoLayerAttr(VoDev, &s_astVoDevStatDflt[VoDev].stVideoLayerAttr));
				CHECK_RET(HI_MPI_VO_EnableVideoLayer(VoDev));
#else
				//PRV_ViUnBindAllVoChn(VoDev);	
				PRV_HideAllVoChn(VoDev);
				PRV_VdecUnBindAllVpss(VoDev);
				PRV_VoUnBindAllVpss(VoDev);
				PRV_ClearAllVoChnBuf(VoDev);
				PRV_DisableAllVoChn(VoDev);
				//CHECK_RET(HI_MPI_VO_DisableVideoLayer(VoDev));
				PRV_DisableVideoLayer(VoDev);
				//s_astVoDevStatDflt[VoDev].stVideoLayerAttr.s32PiPChn = VO_DEFAULT_CHN;
				CHECK_RET(HI_MPI_VO_SetVideoLayerAttr(VoDev, &s_astVoDevStatDflt[VoDev].stVideoLayerAttr));
				CHECK_RET(HI_MPI_VO_EnableVideoLayer(VoDev));
				CHECK_RET(PRV_EnablePipLayer(VoDev));
				CHECK_RET(HI_MPI_VO_SetPlayToleration (VoDev, 200));
#endif			
				PRV_SetPreviewVoDevInMode(1);
			}
			break;
		case PRV_STAT_CTRL:
			{
				//3、判断控制状态类型
				switch (s_astVoDevStatDflt[VoDev].enCtrlFlag)
				{
					case PRV_CTRL_REGION_SEL:
					case PRV_CTRL_ZOOM_IN:
					case PRV_CTRL_PTZ:
						{
							//切换到单画面s32CtrlChn
						//	HI_S32 s32Index,ret=0;


							//CHECK_RET(PRV_Chn2Index(VoDev, s_astVoDevStatDflt[VoDev].s32CtrlChn, &s32Index));
						//	ret = PRV_Chn2Index(VoDev, s_astVoDevStatDflt[VoDev].s32CtrlChn,&s32Index,pOrder);
							//printf("2222222222222s_astVoDevStatDflt[VoDev].s32CtrlCh = %d ,s32Index =%d2222222222222\n",s_astVoDevStatDflt[VoDev].s32CtrlChn,s32Index);
							//if(ret == HI_FAILURE)
							{//如果当前通道被隐藏，但是在配置时还是需要显示
								PRV_PreviewVoDevSingle(VoDev, s_astVoDevStatDflt[VoDev].s32CtrlChn);
							}
							//else
							{
							//	PRV_PreviewVoDevInMode(VoDev, SingleScene, s32Index,pOrder);
							}
						}
						break;
					default:
						RET_FAILURE("Invalid Parameter: enCtrlFlag");
				}
			}
			break;
		default:
			RET_FAILURE("Invalid Parameter: enPreviewStat");
	}
#if defined(SN9234H1)
	if(VoDev == HD)
	{
		PRV_Check_LinkageGroup(VoDev,s_astVoDevStatDflt[VoDev].enPreviewMode);
	}
#else
	if(VoDev == DHD0)
	{
		PRV_Check_LinkageGroup(VoDev,s_astVoDevStatDflt[VoDev].enPreviewMode);
	}
#endif	
//	printf("s_State_Info.bIsOsd_Init=%d,s_State_Info.bIsRe_Init=%d\n",s_State_Info.bIsOsd_Init,s_State_Info.bIsRe_Init);
	if((s_State_Info.bIsOsd_Init && s_State_Info.bIsRe_Init) && (PRV_STAT_PB != s_astVoDevStatDflt[VoDev].enPreviewStat && PRV_STAT_PIC != s_astVoDevStatDflt[VoDev].enPreviewStat))
	{
#if defined(SN9234H1)
		if(VoDev == s_VoSecondDev)
		{
			PRV_Check_LinkageGroup(AD,s_astVoDevStatDflt[VoDev].enPreviewMode);
			Prv_Disp_OSD(AD);
		}
#else
		if(VoDev == s_VoSecondDev || VoDev == DHD1)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VoDev == s_VoSecondDev\n");
			PRV_Check_LinkageGroup(DSD0,s_astVoDevStatDflt[VoDev].enPreviewMode);
			Prv_Disp_OSD(DSD0);
		}
#endif		
		else
		{
			Prv_Disp_OSD(VoDev);
		}
	}
#if 1 /*2010-9-19 双屏！*/
	//printf("##########s_VoSecondDev = %d######################\n",s_VoSecondDev);
	if(Is_Double == DISP_DOUBLE_DISP)
	{
		if (VoDev2 == VoDev)
		{
#if defined(SN9234H1)
			switch(VoDev)
			{
				case HD:
					{
						VO_PUB_ATTR_S stVoPubAttr = s_astVoDevStatDflt[s_VoSecondDev].stVoPubAttr;
						VO_VIDEO_LAYER_ATTR_S stVideoLayerAttr = s_astVoDevStatDflt[s_VoSecondDev].stVideoLayerAttr;

						s_astVoDevStatDflt[s_VoSecondDev] = s_astVoDevStatDflt[HD];
						s_astVoDevStatDflt[s_VoSecondDev].stVoPubAttr = stVoPubAttr;
						s_astVoDevStatDflt[s_VoSecondDev].stVideoLayerAttr = stVideoLayerAttr;

						VoDev = s_VoSecondDev;
						goto again;
					}
					break;
				//case s_VoSecondDev:
				case AD:
					{
						VO_PUB_ATTR_S stVoPubAttr = s_astVoDevStatDflt[HD].stVoPubAttr;
						VO_VIDEO_LAYER_ATTR_S stVideoLayerAttr = s_astVoDevStatDflt[HD].stVideoLayerAttr;
						
						s_astVoDevStatDflt[HD] = s_astVoDevStatDflt[AD];
						s_astVoDevStatDflt[HD].stVoPubAttr = stVoPubAttr;
						s_astVoDevStatDflt[HD].stVideoLayerAttr = stVideoLayerAttr;
						
						VoDev = HD;
						goto again;
					}
					break;
				case SD:
					{
						VO_PUB_ATTR_S stVoPubAttr = s_astVoDevStatDflt[HD].stVoPubAttr;
						VO_VIDEO_LAYER_ATTR_S stVideoLayerAttr = s_astVoDevStatDflt[HD].stVideoLayerAttr;
						
						s_astVoDevStatDflt[HD] = s_astVoDevStatDflt[SD];
						s_astVoDevStatDflt[HD].stVoPubAttr = stVoPubAttr;
						s_astVoDevStatDflt[HD].stVideoLayerAttr = stVideoLayerAttr;
						
						VoDev = HD;
						goto again;
					}
					break;
				default:
					break;
			}
#else
			switch(VoDev)
			{
				case DHD0:
					{
						VO_PUB_ATTR_S stVoPubAttr = s_astVoDevStatDflt[s_VoSecondDev].stVoPubAttr;
						VO_VIDEO_LAYER_ATTR_S stVideoLayerAttr = s_astVoDevStatDflt[s_VoSecondDev].stVideoLayerAttr;

						s_astVoDevStatDflt[s_VoSecondDev] = s_astVoDevStatDflt[DHD0];
						s_astVoDevStatDflt[s_VoSecondDev].stVoPubAttr = stVoPubAttr;
						s_astVoDevStatDflt[s_VoSecondDev].stVideoLayerAttr = stVideoLayerAttr;

						VoDev = s_VoSecondDev;
						goto again;
					}
					break;
				//case s_VoSecondDev:
				case DHD1:
				case DSD0:
					{
						VO_PUB_ATTR_S stVoPubAttr = s_astVoDevStatDflt[DHD0].stVoPubAttr;
						VO_VIDEO_LAYER_ATTR_S stVideoLayerAttr = s_astVoDevStatDflt[DHD0].stVideoLayerAttr;
						
						s_astVoDevStatDflt[DHD0] = s_astVoDevStatDflt[DSD0];
						s_astVoDevStatDflt[DHD0].stVoPubAttr = stVoPubAttr;
						s_astVoDevStatDflt[DHD0].stVideoLayerAttr = stVideoLayerAttr;
						
						VoDev = DHD0;
						goto again;
					}
					break;
				default:
					break;
			}
#endif			
		}
	}
#endif
	//播放音频
	PRV_PlayAudio(VoDev);	
	RET_SUCCESS("");
}
/*************************************************
Function: //PRV_ResetVo
Description: 重启指定的VO设备。包括其上的视频层及通道。
Calls: 
Called By: //
Input: //VoDev:设备号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_ResetVo(VO_DEV VoDev)
{
//	int flag = 0;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV,"##########s_VoDevCtrlDflt = %d,VoDev = %d################\n",s_VoDevCtrlDflt,VoDev);
	pthread_mutex_lock(&s_Reset_vo_mutex);
#if defined(Hi3531)||defined(Hi3535)	
	PRV_HideAllVoChn(VoDev);
	PRV_VoUnBindAllVpss(VoDev);
#endif	
	PRV_DisableAllVoChn(VoDev);
	usleep(200000);//延时200毫秒，让通道资源彻底释放
	if(PRV_DisableVideoLayer(VoDev) == HI_FAILURE)
	{//再重试一次
		PRV_DisableAllVoChn(VoDev);
		usleep(100000);//延时100毫秒，让通道资源彻底释放
		PRV_DisableVideoLayer(VoDev);
	}
	PRV_DisableVoDev(VoDev);

	if(PRV_EnableVoDev(VoDev) == HI_FAILURE)
	{
		pthread_mutex_unlock(&s_Reset_vo_mutex);
		RET_FAILURE("PRV_EnableVoDev(VoDev)");;
	}
	if(PRV_EnableVideoLayer(VoDev) == HI_FAILURE)
	{
		pthread_mutex_unlock(&s_Reset_vo_mutex);
		RET_FAILURE("PRV_EnableVideoLayer(VoDev)");;
	}
	if(PRV_EnableAllVoChn(VoDev) == HI_FAILURE)
	{
		pthread_mutex_unlock(&s_Reset_vo_mutex);
		RET_FAILURE("PRV_EnableAllVoChn(VoDev)");;
	}
#if defined(SN9234H1)
	//PRV_BindAllVoChn(VoDev);
	//if(s_State_Info.bIsInit)	
	{
		PRV_RefreshVoDevScreen(VoDev, (SD == VoDev) ? DISP_NOT_DOUBLE_DISP : DISP_DOUBLE_DISP,s_astVoDevStatDflt[VoDev].as32ChnOrder[s_astVoDevStatDflt[VoDev].enPreviewMode]);
	}
#else
	PRV_VoBindAllVpss(VoDev);
	//if(s_State_Info.bIsInit)	
	{
		PRV_RefreshVoDevScreen(VoDev, (DHD0 == VoDev) ? DISP_NOT_DOUBLE_DISP : DISP_DOUBLE_DISP, s_astVoDevStatDflt[VoDev].as32ChnOrder[s_astVoDevStatDflt[VoDev].enPreviewMode]);
	}
#endif	
	pthread_mutex_unlock(&s_Reset_vo_mutex);
	
	RET_SUCCESS("");
}


/*************************************************
Function: //PRV_ResetVoDev
Description: 重启指定的VO设备。包括其上的视频层及通道。
Calls: 
Called By: //
Input: //VoDev:设备号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_ResetVoDev(VO_DEV VoDev)
{
	//HI_S32 i = LOCALVEDIONUM;
#if defined(SN9234H1)
	PRV_ViUnBindAllVoChn(VoDev);
#endif
	//for(;i < DEV_CHANNEL_NUM; i++)
	//	PRV_VdecUnBindAllVoChn(VoDev, i);
#if defined(SN9234H1)
	PRV_VdecUnBindAllVoChn1(VoDev);
#else
	PRV_VdecUnBindAllVpss(VoDev);
#endif
//	PRV_VoUnBindAllVpss(VoDev);
	PRV_ResetVo(VoDev);
	
    RET_SUCCESS("");
}
/*************************************************
Function: //PRV_OpenVoFb
Description: 打开指定的VO设备的GUI输出。
Calls: 
Called By: //
Input: //VoDev:设备号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_OpenVoFb(VO_DEV VoDev)
{
	int h, w;
	char fb_name[16];
	
#if defined(Hi3531)||defined(Hi3535)	
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
		RET_FAILURE("Invalid Parameter: VoDev");
#endif		
	pthread_mutex_lock(&s_Reset_vo_mutex);

	h = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
	w = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
	
	//printf("##########h = %d,w = %d,VoDev=%d################\n",h,w,VoDev);
#if defined(SN9234H1)
	if(VoDev == HD)
	{
		SN_SPRINTF(fb_name,16,"/dev/fb1");
	}
	else if(VoDev == s_VoSecondDev)
	{
		/*if(s_VoSecondDev == SD)
		{
			SN_SPRINTF(fb_name,16,"/dev/fb3");
		}
		else
		{*/
			SN_SPRINTF(fb_name,16,"/dev/fb4");
		//}
	}
	else
	{
		SN_SPRINTF(fb_name,16,"/dev/fb1");
	}
#else
#if defined(Hi3535)
	SN_SPRINTF(fb_name,16,"/dev/fb0");
#else
	SN_SPRINTF(fb_name,16,"/dev/fb4");
#endif
#endif
	MMIOpenFB(fb_name, w, h, 16);
		
	pthread_mutex_unlock(&s_Reset_vo_mutex);
	
    RET_SUCCESS("PRV_OpenVoFb");
}
/*************************************************
Function: //PRV_CloseVoFb
Description: 关闭指定的VO设备的GUI输出。
Calls: 
Called By: //
Input: //VoDev:设备号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_CloseVoFb(VO_DEV VoDev)
{
#if defined(USE_UI_OSD)
	MMI_OsdReset();
#endif
	//printf("##########s_VoDevCtrlDflt = %d,VoDev = %d################\n",s_VoDevCtrlDflt,VoDev);
	pthread_mutex_lock(&s_Reset_vo_mutex);
	Get_Fb_param_exit();
	MMICloseFB();
	pthread_mutex_unlock(&s_Reset_vo_mutex);
	
    RET_SUCCESS("PRV_CloseVoFb");
}

#if (IS_DECODER_DEVTYPE == 1)

#else

/*************************************************
Function: //PRV_NextScreen
Description:下一屏、上一屏
Calls: 
Called By: //
Input: // VoDev: 输出设备
   		s32Dir: 0-下一屏，1-上一屏
  		 bIsManual: 是否手动下/上一屏控制
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_NextScreen(VO_DEV VoDev, HI_S32 s32Dir, HI_BOOL bIsAuto/*是否自动下一屏，HI_TRUE:是, HI_FALSE:否*/)
{
	HI_U32 u32ChnNum;
	HI_U32 Max_num;
	unsigned char mode = s_astVoDevStatDflt[VoDev].enPreviewMode;	

	if(s_astVoDevStatDflt[VoDev].bIsSingle == HI_TRUE)
	{
		mode = SingleScene;
	}			
	//获取当前预览模式下最大通道数
	CHECK_RET(PRV_Get_Max_chnnum(mode,&Max_num));

	/*确保参数的合法性*/
#if defined(Hi3520)
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
#endif
	if( VoDev < 0 || VoDev >= PRV_VO_MAX_DEV )
	{
		RET_FAILURE("Invalid Parameter: VoDev");
	}

	if (PRV_STAT_NORM != s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat )
	{
		RET_FAILURE("NOT in preview stat!!!");
	}

	switch(mode)
	{
		case SingleScene:
			u32ChnNum = 1;
			break;
		case TwoScene:
			u32ChnNum = 2;
			break;
		case ThreeScene:
			u32ChnNum = 3;
			break;
		case FourScene:
		case LinkFourScene:
			u32ChnNum = 4;
			break;
		case FiveScene:
			u32ChnNum = 5;
			break;
		case SixScene:
			u32ChnNum = 6;
			break;
		case SevenScene:
			u32ChnNum = 7;
			break;
		case EightScene:
			u32ChnNum = 8;
			break;
		case NineScene:
		case LinkNineScene:
			u32ChnNum = 9;
			break;
		case SixteenScene:
			u32ChnNum = 16;
			break;
		default:
			RET_FAILURE("Invalid Preview Mode");
	}
	
	if (s_astVoDevStatDflt[VoDev].bIsSingle)
	{	/*单画面*/
		//计算下一个需要轮询的画面索引号
		CHECK_RET(PRV_GetValidChnIdx(VoDev, s_astVoDevStatDflt[VoDev].s32SingleIndex, &s_astVoDevStatDflt[VoDev].s32SingleIndex, s32Dir,s_astVoDevStatDflt[VoDev].as32ChnOrder[mode],s_astVoDevStatDflt[VoDev].as32ChnpollOrder[mode]));	
		s_astVoDevStatDflt[VoDev].s32DoubleIndex = 0;
	}
	else
	{	/*多画面*/
		HI_S32 s32Index;
		//如果当前画面模式为最大画面模式，那么不进行轮询
		
		if (PRV_VO_MAX_MOD == mode)
		{
			RET_SUCCESS("PRV_VO_MAX_MOD!!");
		}
		//如果当前为8通道，那么8画面也不需要轮询
		if((g_Max_Vo_Num == 8) && (EightScene == mode))
		{
			RET_SUCCESS("PRV_VO_MAX_MOD!!");
		}
		do 
		{
			if (0 == s32Dir)//0-下一屏;
			{
				s32Index = (s_astVoDevStatDflt[VoDev].s32PreviewIndex+u32ChnNum >= Max_num ) ? 0 : (s_astVoDevStatDflt[VoDev].s32PreviewIndex+u32ChnNum);
			}
			else if (1 == s32Dir)//1-上一屏;
			{
				s32Index = (s_astVoDevStatDflt[VoDev].s32PreviewIndex < u32ChnNum) 
					? ((s_astVoDevStatDflt[VoDev].s32PreviewIndex==0)?((Max_num-1)/u32ChnNum)*u32ChnNum:0) 
					: (s_astVoDevStatDflt[VoDev].s32PreviewIndex-u32ChnNum);
			}
			else
			{
				RET_FAILURE("Invalid redirection value!!!");
			}
			
			s_astVoDevStatDflt[VoDev].s32PreviewIndex = s32Index;
#if 1
			/*跳过空屏*/
			if (s_astVoDevStatDflt[VoDev].as32ChnOrder[mode][s32Index] >= 0
				&& s_astVoDevStatDflt[VoDev].as32ChnOrder[mode][s32Index] < g_Max_Vo_Num)
			{
				//printf("############s_astVoDevStatDflt[VoDev].as32ChnOrder[mode][s32Index] = %d,,,s32Index = %d###############\n",s_astVoDevStatDflt[VoDev].as32ChnOrder[mode][s32Index],s32Index);
				if (bIsAuto)
				{
					//如果当前画面需要轮询
					if(s_astVoDevStatDflt[VoDev].as32ChnpollOrder[mode][s32Index])
					{
						break;
					}
				}else
				{
					break;
				}
			}
			//计算下一个需要轮询的画面索引号
			CHECK_RET(PRV_GetValidChnIdx(VoDev, s_astVoDevStatDflt[VoDev].s32PreviewIndex, &s32Index, 0,s_astVoDevStatDflt[VoDev].as32ChnOrder[mode],s_astVoDevStatDflt[VoDev].as32ChnpollOrder[mode]));
			//判断当前索引号是否在通道号范围内
			if (s32Index > s_astVoDevStatDflt[VoDev].s32PreviewIndex
				&& s32Index < s_astVoDevStatDflt[VoDev].s32PreviewIndex+u32ChnNum)
			{
				break;
			}
#else
			break;
#endif
		} while (1);
		s_astVoDevStatDflt[VoDev].s32PreviewIndex %= Max_num;

		//s_astVoDevStatDflt[VoDev].s32PreviewIndex = s32Index%Max_num;
#if 0
		/*最后一屏不留空*/
		if ((s_astVoDevStatDflt[VoDev].s32PreviewIndex+u32ChnNum) > Max_num)
		{
			s_astVoDevStatDflt[VoDev].s32PreviewIndex = Max_num - u32ChnNum;
		}
#endif
	}
	
	//printf("#############PRV_PreviewVoDevInMode idx = %d ,mode =%d ,Max_num =%d,uidx=%d,ISSINGLE= %d######################\n",
	//		s_astVoDevStatDflt[VoDev].s32SingleIndex,s_astVoDevStatDflt[VoDev].enPreviewMode,Max_num,s_astVoDevStatDflt[VoDev].s32SingleIndex,s_astVoDevStatDflt[VoDev].bIsSingle);
	//刷新画面
	if (!s_astVoDevStatDflt[VoDev].bIsAlarm)
	{//处于非报警状态才刷新画面
		PRV_RefreshVoDevScreen(VoDev,DISP_DOUBLE_DISP,s_astVoDevStatDflt[VoDev].as32ChnOrder[mode]);
	}
	RET_SUCCESS("Next/Prev Screen!");
}
#endif
int PRV_IsAllowZoomIn(int VoChn)
{
	if(VochnInfo[VoChn].VdecChn == DetVLoss_VdecChn || VochnInfo[VoChn].VdecChn == NoConfig_VdecChn)
	{
		return HI_FAILURE;
	}
	return HI_SUCCESS;
}
//通道电子放大
STATIC HI_S32 PRV_ChnZoomIn(VO_CHN VoChn, HI_U32 u32Ratio, const Preview_Point *pstPoint)
{
#if defined(SN9234H1)
	VoChn = s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn;//暂时忽略参数VoChn
	int index = 0;
	index = PRV_GetVoChnIndex(VoChn);
	if(index < 0)
		RET_FAILURE("------ERR: Invalid Index!");
	if(VoChn < 0 || VoChn >= g_Max_Vo_Num
		|| u32Ratio < PRV_MIN_ZOOM_IN_RATIO || u32Ratio > PRV_MAX_ZOOM_IN_RATIO)
	{
		RET_FAILURE("Invalid Parameter: u32Ratio or VoChn");
	}
	
	if(NULL == pstPoint)
	{
		RET_FAILURE("NULL Pointer!!");
	}
	
	if (PRV_STAT_CTRL != s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat
		&& PRV_CTRL_ZOOM_IN != s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag)
	{
		RET_FAILURE("NOT in [zoom in ctrl] stat!!!");
	}
	
	if (VoChn != s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn)
	{
		RET_FAILURE("VoChn NOT match current VoChn!!!");
	}
	//printf("vochn=%d, u32ratio=%d, pstPoint->x= %d, pstPoint->y=%d===========================\n",VoChn,u32Ratio,pstPoint->x, pstPoint->y);

//M系列产品支持高清(1080P/720P)，如果显示高清的输出通道在主片，信号源分辨率太大，进行缩放后，
//也大于VO_ZOOM_ATTR_S允许的范围，调用HI_MPI_VO_SetZoomInWindow会返回失败；
//如果在从片，因为信号源已经经过级联进行了缩放，在主片上显示时，在VO_ZOOM_ATTR_S范围内
//所以在支持高清的型号，且在主片显示高清时采用HI_MPI_VO_SetZoomInRatio进行电子放大

//在主片上显示的高清(大于D1)通道采用HI_MPI_VO_SetZoomInRatio进行电子放大
	if(PRV_MASTER == VochnInfo[index].SlaveId
		&& VochnInfo[index].VdecChn >= 0 )		
	{
		VO_ZOOM_RATIO_S	stZoomRatio;

		if (u32Ratio <= 1)
		{
			stZoomRatio.u32XRatio = 0;
			stZoomRatio.u32YRatio = 0;
			stZoomRatio.u32WRatio = 0;
			stZoomRatio.u32HRatio = 0;
		}
		else
		{
#if 0
			stZoomRatio.u32WRatio = 1000/u32Ratio;
			stZoomRatio.u32HRatio = 1000/u32Ratio;
			stZoomRatio.u32XRatio = ((pstPoint->x * 1000)/s_u32GuiWidthDflt + stZoomRatio.u32WRatio > 1000)
				? 1000 - stZoomRatio.u32WRatio
				: (pstPoint->x * 1000)/s_u32GuiWidthDflt;
			stZoomRatio.u32YRatio = ((pstPoint->y * 1000)/s_u32GuiHeightDflt + stZoomRatio.u32HRatio > 1000)
				? 1000 - stZoomRatio.u32HRatio
				: (pstPoint->y * 1000)/s_u32GuiHeightDflt;

#else /*将1到16倍放大转为1到4倍放大：y = (x - 1)/5 + 1*/

			u32Ratio += 4;
				
			stZoomRatio.u32WRatio = 5000/u32Ratio;
			stZoomRatio.u32HRatio = 5000/u32Ratio;
			stZoomRatio.u32XRatio = ((pstPoint->x * 1000)/s_u32GuiWidthDflt + stZoomRatio.u32WRatio > 1000)
				? 1000 - stZoomRatio.u32WRatio
				: (pstPoint->x * 1000)/s_u32GuiWidthDflt;
			stZoomRatio.u32YRatio = ((pstPoint->y * 1000)/s_u32GuiHeightDflt + stZoomRatio.u32HRatio > 1000)
				? 1000 - stZoomRatio.u32HRatio
				: (pstPoint->y * 1000)/s_u32GuiHeightDflt;
#endif
		}
//	printf("==================stZoomRatio.u32XRatio = %d; stZoomRatio.u32YRatio = %d; stZoomRatio.u32WRatio =%d; stZoomRatio.u32HRatio = %d;\n",	
//		stZoomRatio.u32XRatio ,stZoomRatio.u32YRatio,stZoomRatio.u32WRatio,stZoomRatio.u32HRatio);
#if 0 /*2010-8-31 优化：电子放大按中心放大方式进行放大*/
		stZoomRatio.u32XRatio = (stZoomRatio.u32XRatio < stZoomRatio.u32WRatio/2)?0:stZoomRatio.u32XRatio - stZoomRatio.u32WRatio/2;
		stZoomRatio.u32YRatio = (stZoomRatio.u32YRatio < stZoomRatio.u32HRatio/2)?0:stZoomRatio.u32YRatio - stZoomRatio.u32HRatio/2;
#endif
#if 1
		CHECK_RET(HI_MPI_VO_SetZoomInRatio(s_VoDevCtrlDflt, VoChn, &stZoomRatio));
#else /*2010-9-19 双屏！*/
		if(s_State_Info.g_zoom_first_in == HI_FALSE)
		{
			//CHECK_RET(HI_MPI_VO_GetZoomInRatio(HD,VoChn,&s_astZoomAttrDflt[HD]));
			CHECK_RET(HI_MPI_VO_GetZoomInRatio(s_VoDevCtrlDflt,VoChn,&s_astZoomAttrDflt[s_VoDevCtrlDflt]));
			s_State_Info.g_zoom_first_in = HI_TRUE;
		}
		//CHECK_RET(HI_MPI_VO_SetZoomInRatio(HD, VoChn, &stZoomRatio));
		CHECK_RET(HI_MPI_VO_SetZoomInRatio(s_VoDevCtrlDflt, VoChn, &stZoomRatio));
#endif
	}
	else
	//D系列的产品不支持高清，最大支持D1(704*576),均采用此种方法电子放大
	//M系列在从片上(SN8616M_LE)显示的通道以及主片显示的D1通道采用此种方法
	{
		VO_ZOOM_ATTR_S stVoZoomAttr;
		int w = 0, h = 0, x = 0, y = 0;
		HI_U32 u32Width = 0, u32Height = 0;
#if defined(SN8604M) || defined(SN8608M) || defined(SN8608M_LE) || defined(SN8616M_LE)|| defined(SN9234H1)
		u32Width = PRV_BT1120_SIZE_W;
		u32Height = PRV_BT1120_SIZE_H;
#else
		u32Width = PRV_SINGLE_SCREEN_W;
		u32Height = PRV_SINGLE_SCREEN_H;
#endif
		u32Width = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Width;
		u32Height = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Height;
		x = u32Width * pstPoint->x / s_u32GuiWidthDflt; //对应D1屏幕坐标X
		y = u32Height * pstPoint->y / s_u32GuiHeightDflt; //对应D1屏幕坐标Y
		w = u32Width * 5/(u32Ratio+4);					//放大矩形框宽度
		h = u32Height * 5/(u32Ratio+4);					//放大矩形框高度
		stVoZoomAttr.stZoomRect.s32X		= (((x + w) > u32Width) ? (u32Width -w) : x) & (~0x01);;	//调整x位置，超过D1宽度要退步,2像素对其
		stVoZoomAttr.stZoomRect.s32Y		= (((y + h) > u32Height) ? (u32Height -h) : y) & (~0x01);		//调整y位置，超过D1高度要退步,2像素对其
		stVoZoomAttr.stZoomRect.u32Width	= w & (~0x01);
		stVoZoomAttr.stZoomRect.u32Height	= h & (~0x01);
		stVoZoomAttr.enField = VIDEO_FIELD_FRAME;		
		
		CHECK_RET(HI_MPI_VO_SetZoomInWindow(s_VoDevCtrlDflt, VoChn, &stVoZoomAttr));

	}
	
#else	


	//VoChn = s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn;//暂时忽略参数VoChn
	int index = 0;
	VoChn = 0;
	//index = PRV_GetVoChnIndex(VoChn);
	index = s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn;
	if(index < 0)
		RET_FAILURE("------ERR: Invalid Index!");
	if(VoChn < 0 || VoChn >= PRV_VO_CHN_NUM
		|| u32Ratio < PRV_MIN_ZOOM_IN_RATIO || u32Ratio > PRV_MAX_ZOOM_IN_RATIO)
	{
		RET_FAILURE("Invalid Parameter: u32Ratio or VoChn");
	}
	
	if(NULL == pstPoint)
	{
		RET_FAILURE("NULL Pointer!!");
	}
	
	if (PRV_STAT_CTRL != s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat
		&& PRV_CTRL_ZOOM_IN != s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag)
	{
		RET_FAILURE("NOT in [zoom in ctrl] stat!!!");
	}
//	if (VoChn != s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn)
	{
//		RET_FAILURE("VoChn NOT match current VoChn!!!");
	}
	//printf("vochn=%d, u32ratio=%f, pstPoint->x= %d, pstPoint->y=%d===========================\n",VoChn,u32Ratio,pstPoint->x, pstPoint->y);

	VoChn = s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn;
	VPSS_GRP VpssGrp = VoChn;
    VPSS_CROP_INFO_S stVpssCropInfo;
	HI_S32 w = 0, h = 0, x = 0, y = 0, s32X = 0, s32Y = 0;
	HI_U32 u32Width = 0, u32Height = 0, u32W = 0, u32H = 0;
	u32Width = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Width;
	u32Height = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Height;
	w = u32Width/sqrt(u32Ratio);
	h = u32Height/sqrt(u32Ratio);
    stVpssCropInfo.bEnable = HI_TRUE;

	VO_CHN_ATTR_S stVoChnAttr;	
	CHECK_RET(HI_MPI_VO_GetChnAttr(s_VoDevCtrlDflt, VoChn, &stVoChnAttr));
	s32X = stVoChnAttr.stRect.s32X;
	s32Y = stVoChnAttr.stRect.s32Y;
	u32W = stVoChnAttr.stRect.u32Width;
	u32H = stVoChnAttr.stRect.u32Height;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VoChn: %d, s32X=%d, s32Y=%d, u32W=%d, u32H=%d\n", VoChn, s32X, s32Y, u32W, u32H);
	
	#if 1
	//x = u32Width * pstPoint->x / s_u32GuiWidthDflt; //对应D1屏幕坐标X
	//y = u32Height * pstPoint->y / s_u32GuiHeightDflt; //对应D1屏幕坐标Y
	//w = u32Width * 5/(u32Ratio+4);					//放大矩形框宽度
	//h = u32Height * 5/(u32Ratio+4); 				//放大矩形框高度
	//x = pstPoint->x;
	//y = pstPoint->y;
	//w = u32Width/sqrt(u32Ratio);					//放大矩形框宽度
	//h = u32Height/sqrt(u32Ratio); 				//放大矩形框高度
	//走廊模式下电子放大
	if(u32H == u32Height && u32W != u32Width)
	{
		if(pstPoint->x < s32X)
		{
			if(pstPoint->x + w <= s32X)
			{
				CHECK(PRV_VPSS_Stop(VpssGrp));
				CHECK(PRV_VPSS_Start(VpssGrp));
				return 0;
			}
			else if(pstPoint->x + w <= s32X + u32W)
			{
				x = 0;
				y = pstPoint->y * VochnInfo[index].VideoInfo.height/u32Height;
				w = (w - (s32X - pstPoint->x)) * VochnInfo[index].VideoInfo.width/u32W;
				h = VochnInfo[index].VideoInfo.height/sqrt(u32Ratio);
			}
			else
			{
				x = 0;
				y = pstPoint->y * VochnInfo[index].VideoInfo.height/u32Height;
				w = u32W * VochnInfo[index].VideoInfo.width/u32W;
				h = VochnInfo[index].VideoInfo.height/sqrt(u32Ratio);
			}
		}
		else if(pstPoint->x >= s32X && pstPoint->x <= s32X + u32W)
		{
			if(pstPoint->x + w <= s32X + u32W)
			{
				x = (pstPoint->x - s32X) * VochnInfo[index].VideoInfo.width/u32W;
				y = pstPoint->y * VochnInfo[index].VideoInfo.height/u32Height;
				w = VochnInfo[index].VideoInfo.width * w/u32W;
				h = VochnInfo[index].VideoInfo.height/sqrt(u32Ratio);

			}
			else
			{
				x = (pstPoint->x - s32X) * VochnInfo[index].VideoInfo.width/u32W;
				y = pstPoint->y * VochnInfo[index].VideoInfo.height/u32Height;
				w = (u32W - (pstPoint->x - s32X)) * VochnInfo[index].VideoInfo.width/u32W;
				h = VochnInfo[index].VideoInfo.height/sqrt(u32Ratio);
			}
		}
		else
		{
			CHECK(PRV_VPSS_Stop(VpssGrp));
			CHECK(PRV_VPSS_Start(VpssGrp));
			return 0;
		}
		
	}
	else if(u32W == u32Width && u32H != u32Height)
	{
		if(pstPoint->y < s32Y)
		{
			if(pstPoint->y + h <= s32Y)
			{
				CHECK(PRV_VPSS_Stop(VpssGrp));
				CHECK(PRV_VPSS_Start(VpssGrp));
				return 0;
			}
			else if(pstPoint->y + h <= s32Y + u32H)
			{
				x = pstPoint->x * VochnInfo[index].VideoInfo.width/u32Width;
				y = 0;
				w = VochnInfo[index].VideoInfo.width/sqrt(u32Ratio);
				h = (h - (s32Y - pstPoint->y)) * VochnInfo[index].VideoInfo.height/u32Height;
			}
			else
			{
				x = pstPoint->x * 1000 /u32Width;
				y = 0;
				w = VochnInfo[index].VideoInfo.width/sqrt(u32Ratio);
				h = u32H * VochnInfo[index].VideoInfo.height/u32H;
			}

		}
		else if(pstPoint->y >= s32Y && pstPoint->y <= s32Y + u32H)
		{
			if(pstPoint->y + h <= s32Y + u32H)
			{
				x = pstPoint->x * VochnInfo[index].VideoInfo.width/u32Width;
				y = (pstPoint->y - s32Y) * VochnInfo[index].VideoInfo.height/u32H;
				w = VochnInfo[index].VideoInfo.width * h/u32H;
				h = VochnInfo[index].VideoInfo.height/sqrt(u32Ratio);
			}
			else
			{
				x = pstPoint->x * VochnInfo[index].VideoInfo.width/u32Width;
				y = (pstPoint->y - s32Y) * VochnInfo[index].VideoInfo.height/u32H;
				w = VochnInfo[index].VideoInfo.width/sqrt(u32Ratio);
				h = (u32H - (pstPoint->y - s32Y)) * VochnInfo[index].VideoInfo.height/u32H;
			}
		}
		else
		{
			CHECK(PRV_VPSS_Stop(VpssGrp));
			CHECK(PRV_VPSS_Start(VpssGrp));
			return 0;
		}
	}
	else
	{
		x = pstPoint->x * VochnInfo[index].VideoInfo.width/u32Width;
		y = pstPoint->y * VochnInfo[index].VideoInfo.height/u32Height;
		w = VochnInfo[index].VideoInfo.width/sqrt(u32Ratio);
		h = VochnInfo[index].VideoInfo.height/sqrt(u32Ratio);
	}
	x = ((x + w) > VochnInfo[index].VideoInfo.width) ? (VochnInfo[index].VideoInfo.width - w) : x;
	y = ((y + h) > VochnInfo[index].VideoInfo.height) ? (VochnInfo[index].VideoInfo.height - h) : y;
	//printf("pstPoint->x: %d, pstPoint->y: %d\n", pstPoint->x, pstPoint->y);
	x = ((x + w) > VochnInfo[index].VideoInfo.width) ? (VochnInfo[index].VideoInfo.width - w) : x;
	y = ((y + h) > VochnInfo[index].VideoInfo.height) ? (VochnInfo[index].VideoInfo.height - h) : y;
	w = w >= 32 ? w : 32;
	h = h >= 32 ? h : 32;
	x = ALIGN_BACK(x, 4);//起始点为4的整数倍，宽高为16的整数倍
	y = ALIGN_BACK(y, 4);
	w = ALIGN_BACK(w, 16);
	h = ALIGN_BACK(h, 16);
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "11111111u32Ratio: %u, x: %d, y: %d, w: %d, h: %d===width: %d, height: %d\n", u32Ratio, x, y, w, h, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height);
    stVpssCropInfo.enCropCoordinate = VPSS_CROP_ABS_COOR;
    stVpssCropInfo.stCropRect.s32X = x;
    stVpssCropInfo.stCropRect.s32Y = y;
    stVpssCropInfo.stCropRect.u32Width = w;
    stVpssCropInfo.stCropRect.u32Height = h;
	#else
	#if 0
	if(u32H == u32Height && u32W != u32Width)
	{
		if(pstPoint->x < s32X)
		{
			if(pstPoint->x + w <= s32X)
			{
				CHECK(PRV_VPSS_Stop(VpssGrp));
				CHECK(PRV_VPSS_Start(VpssGrp));
				return 0;
			}
			else if(pstPoint->x + w <= s32X + u32W)
			{
				x = s32X * u32W/u32Width;
				y = pstPoint->y;
				w = w - (s32X - pstPoint->x);
			}
			else
			{
				x = s32X;
				y = pstPoint->y;
				w = u32W;
			}
		}
		else if(pstPoint->x >= s32X && pstPoint->x <= s32X + u32W)
		{
			if(pstPoint->x + w <= s32X + u32W)
			{
				x = (pstPoint->x - s32X);
				y = pstPoint->y;
				w = u32W/sqrt(u32Ratio);
				h = u32H/sqrt(u32Ratio);
			}
			else
			{
				x = (pstPoint->x - s32X);
				y = pstPoint->y;
				w = u32W - (pstPoint->x - s32X);
			}
		}
		else
		{
			CHECK(PRV_VPSS_Stop(VpssGrp));
			CHECK(PRV_VPSS_Start(VpssGrp));
			return 0;
		}
	}
	else if(u32W == u32Width && u32H != u32Height)
	{
		if(pstPoint->y < s32Y)
		{
			if(pstPoint->y + h <= s32Y)
			{
				CHECK(PRV_VPSS_Stop(VpssGrp));
				CHECK(PRV_VPSS_Start(VpssGrp));
				return 0;
			}
			else if(pstPoint->y + h <= s32Y + u32H)
			{
				y = s32Y * 1000/u32Height;
				x = pstPoint->x * 1000 /u32Width;
				h = h - (s32Y - pstPoint->y);
			}
			else
			{
				y = s32Y * 1000/u32Height;
				x = pstPoint->x * 1000 /u32Width;
				h = u32H;
			}

		}
		else if(pstPoint->y >= s32Y && pstPoint->y <= s32Y + u32H)
		{
			if(pstPoint->y + h <= s32Y + u32H)
			{
				x = pstPoint->x * 1000/u32Width;
				y = pstPoint->y * 1000 /u32Height;
				w = u32W/sqrt(u32Ratio);
				h = u32H/sqrt(u32Ratio);
			}
			else
			{
				x = pstPoint->y * 1000/u32Width;
				y = pstPoint->y * 1000 /u32Height;
				h = u32H - (pstPoint->y - s32Y);
			}

		}
		else
		{
			CHECK(PRV_VPSS_Stop(VpssGrp));
			CHECK(PRV_VPSS_Start(VpssGrp));
			return 0;
		}
	}
	else
	#endif
	{
		x = pstPoint->y * 1000 /u32Width;
		y = pstPoint->y * 1000 /u32Height;
		w = u32Width/sqrt(u32Ratio);
		h = u32Height/sqrt(u32Ratio);
	}
	//printf("11111111u32Ratio: %f, x: %d, y: %d, w: %d, h: %d\n", u32Ratio, x, y, w, h);
	
	x = x > 999 ? 999 : x;
	y = y > 999 ? 999 : y;
	x = ALIGN_BACK(x, 4);//起始点为4的整数倍，宽高为16的整数倍
	y = ALIGN_BACK(y, 4);
	w = ALIGN_BACK(w, 16);
	h = ALIGN_BACK(h, 16);
    stVpssCropInfo.enCropCoordinate = VPSS_CROP_RITIO_COOR;
    stVpssCropInfo.stCropRect.s32X = x;
    stVpssCropInfo.stCropRect.s32Y = y;
    stVpssCropInfo.stCropRect.u32Width = w;
    stVpssCropInfo.stCropRect.u32Height = h;
	#endif
#if defined(Hi3535)
	CHECK_RET(HI_MPI_VPSS_SetGrpCrop(VpssGrp, &stVpssCropInfo));
	CHECK_RET(HI_MPI_VO_RefreshChn(VHD0, VoChn));
#else
    stVpssCropInfo.enCapSel = VPSS_CAPSEL_BOTH;
    CHECK_RET(HI_MPI_VPSS_SetCropCfg(VpssGrp, &stVpssCropInfo));
    CHECK_RET(HI_MPI_VO_ChnRefresh(0, VoChn));
#endif
#endif	
	RET_SUCCESS("Chn Zoom in!");
}

/*************************************************
Function: //PRV_AlarmChn
Description://报警弹出
Calls: 
Called By: //
Input: // VoDev: 输出设备
   		VoChn: 通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_AlarmChn(VO_DEV VoDev, VO_CHN VoChn)
{
//	HI_U32 u32Index;

#if defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}

	if(VoDev < 0 || VoDev >= PRV_VO_MAX_DEV 
		|| VoChn < 0 || VoChn >= g_Max_Vo_Num)
	{
		RET_FAILURE("Invalid Parameter: VoChn or VoDev");
	}
	
#else

	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/
		|| VoChn < 0 || VoChn >= g_Max_Vo_Num)
	{
		RET_FAILURE("Invalid Parameter: VoChn or VoDev");
	}
	if(DHD1 == VoDev)
		VoDev = DSD0;
#endif	
	if (s_astVoDevStatDflt[VoDev].s32AlarmChn == VoChn
		&& s_astVoDevStatDflt[VoDev].bIsAlarm == HI_TRUE)
	{
		RET_SUCCESS("alarm chn already in display");/*重复弹出的报警画面不做再次刷新！*/
	}
	//CHECK_RET(PRV_Chn2Index(VoDev, VoChn, &u32Index,s_astVoDevStatDflt[VoDev].as32ChnOrder[s_astVoDevStatDflt[VoDev].enPreviewMode]));/*check if this chn has been hiden*/
	s_astVoDevStatDflt[VoDev].s32AlarmChn = VoChn;
	s_astVoDevStatDflt[VoDev].bIsAlarm = HI_TRUE;
	//s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsDouble = 2;
	//printf("######PRV_AlarmChn   :s_astVoDevStatDflt[VoDev].enPreviewStat = %d###################\n",s_astVoDevStatDflt[VoDev].enPreviewStat);
	/*必要时才刷新输出画面*/
	
	if (PRV_STAT_NORM == s_astVoDevStatDflt[VoDev].enPreviewStat)
	{
		PRV_RefreshVoDevScreen(VoDev,DISP_DOUBLE_DISP,s_astVoDevStatDflt[VoDev].as32ChnOrder[SingleScene]);
	}
	
	RET_SUCCESS("Alarm Chn!");
}

/*************************************************
Function: //PRV_AlarmOff
Description://报警撤消
Calls: 
Called By: //
Input: // VoDev: 输出设备
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_AlarmOff(VO_DEV VoDev)
{
	if (s_astVoDevStatDflt[VoDev].bIsAlarm)
	{
		
		s_astVoDevStatDflt[VoDev].bIsAlarm = HI_FALSE;
		//s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsDouble = 0;
		/*必要时才刷新输出画面*/
		if (PRV_STAT_NORM == s_astVoDevStatDflt[VoDev].enPreviewStat)
		{
#if defined(SN9234H1)
			PRV_RefreshVoDevScreen(VoDev,DISP_DOUBLE_DISP,s_astVoDevStatDflt[VoDev].as32ChnOrder[s_astVoDevStatDflt[VoDev].enPreviewMode]);
#else
			PRV_RefreshVoDevScreen(VoDev,DISP_NOT_DOUBLE_DISP,s_astVoDevStatDflt[VoDev].as32ChnOrder[s_astVoDevStatDflt[VoDev].enPreviewMode]);
#endif
			/*if(s_astVoDevStatDflt[VoDev].bIsDouble == 1)
			{
				s_astVoDevStatDflt[VoDev].bIsDouble ++;
			}*/
		}
	}
	
	RET_SUCCESS("Alarm Off!");
}

/*************************************************
Function: //PRV_SingleChn
Description://切换到单画面
Calls: 
Called By: //
Input: // VoDev: 输出设备
		VoChn: 通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_SingleChn(VO_DEV VoDev, VO_CHN VoChn)
{
#if defined(SN9234H1)
	HI_S32 s32Index = 0;

	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if( VoDev < 0 || VoDev >= PRV_VO_MAX_DEV
		|| VoChn < 0 || VoChn >= g_Max_Vo_Num)
	{
		RET_FAILURE("Invalid Parameter: VoDev or VoChn");
	}
	
#else
	HI_U32 s32Index = 0;
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/
		|| VoChn < 0 || VoChn >= g_Max_Vo_Num)
	{
		RET_FAILURE("Invalid Parameter: VoDev or VoChn");
	}
	if(DHD1 == VoDev)
		VoDev = DSD0;
#endif	
	if (PRV_STAT_NORM != s_astVoDevStatDflt[VoDev].enPreviewStat)
	{
		if(PRV_CurDecodeMode == PassiveDecode)
		{
			s_astVoDevStatDflt[VoDev].enPreviewMode = SingleScene;
			RET_SUCCESS("");
		}
		RET_FAILURE("NOT in preview stat!!!");
	}	
		
#if (DEV_TYPE != DEV_SN_9234_H_1)
	/*海康方案*/
	if (HI_FAILURE == PRV_Chn2Index(VoDev, VoChn, &s32Index,s_astVoDevStatDflt[VoDev].as32ChnOrder[SingleScene]))
	{
		CHECK_RET(PRV_Chn2Index(VoDev, -VoChn-1, &s32Index,s_astVoDevStatDflt[VoDev].as32ChnOrder[SingleScene]));/*VoChn负数(-1~-16)表示隐藏的通道*/
	}
#endif
	s_astVoDevStatDflt[VoDev].enPreviewMode = SingleScene;
	s_astVoDevStatDflt[VoDev].bIsSingle = HI_FALSE;	
	s_astVoDevStatDflt[VoDev].bIsAlarm = HI_FALSE;//切换后，取消报警状态
	s_astVoDevStatDflt[VoDev].s32SingleIndex = s32Index;
	s_astVoDevStatDflt[VoDev].s32DoubleIndex = 0;
	//printf("---------------s_astVoDevStatDflt[VoDev].s32SingleIndex: %d\n", s_astVoDevStatDflt[VoDev].s32SingleIndex);
	PRV_RefreshVoDevScreen(VoDev, DISP_DOUBLE_DISP, s_astVoDevStatDflt[VoDev].as32ChnOrder[SingleScene]);

	RET_SUCCESS("Set Single Chn!");
}

/*************************************************
Function: //PRV_MultiChn
Description://切换到多画面
Calls: 
Called By: //
Input: // VoDev: 输出设备
		enPreviewMode: 多画面预览模式
		s32Index: 多画面开始索引号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_MultiChn(VO_DEV VoDev, PRV_PREVIEW_MODE_E enPreviewMode, HI_S32 s32Index)
{
#if defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if( VoDev < 0 || VoDev >= PRV_VO_MAX_DEV
		|| s32Index < 0 || s32Index >= SIXINDEX)
	{
		RET_FAILURE("Invalid Parameter: VoDev or s32Index");
	}
	
	LayoutToSingleChn = -1;

	if (SD == VoDev)
	{
	
		//PRV_RefreshVoDevScreen(VoDev, DISP_NOT_DOUBLE_DISP, s_astVoDevStatDflt[VoDev].as32ChnOrder[s_astVoDevStatDflt[VoDev].enPreviewMode]);
		RET_SUCCESS("spot out single chn!");
	}
#else
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/
		|| s32Index < 0 || s32Index >= SIXINDEX)
	{
		RET_FAILURE("Invalid Parameter: VoDev or s32Index");
	}
	
	LayoutToSingleChn = -1;

	if (DHD1 == VoDev)
	{
		VoDev = DSD0;	
	}
	
#endif	

	if (PRV_STAT_NORM != s_astVoDevStatDflt[VoDev].enPreviewStat)
	{
		//解决被动下在通道控制时，预览模式切换，
		//退出通道控制后，还是在原来预览模式下
		if(PRV_CurDecodeMode == PassiveDecode)
		{
			s_astVoDevStatDflt[VoDev].enPreviewMode = enPreviewMode;
			RET_SUCCESS("");			
		}
			
		RET_FAILURE("NOT in preview state!!!");
	}
	
	s_astVoDevStatDflt[VoDev].enPreviewMode = enPreviewMode;
	s_astVoDevStatDflt[VoDev].s32PreviewIndex = s32Index;
	s_astVoDevStatDflt[VoDev].bIsSingle = HI_FALSE;
	s_astVoDevStatDflt[VoDev].bIsAlarm = HI_FALSE;//切换后，取消报警状态
	//printf("------s32Index: %d\n", s32Index);
	if(enPreviewMode == LinkFourScene||enPreviewMode==LinkNineScene)
	{
		LinkAgeGroup_ChnState LinkageGroup;
		int i = 0;
		SN_MEMSET(&LinkageGroup,0,sizeof(LinkAgeGroup_ChnState));
		Scm_GetLinkGroup(&LinkageGroup);
		if(enPreviewMode == LinkFourScene)
		{
			for(i = 0; i < 4; i++)
			{
				if(LinkageGroup.DevGroupChn[i].DevChn==0)
				{
					s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][i] = -1;
				}
				else
				{
					s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][i] = LinkageGroup.DevGroupChn[i].DevChn-1;
				}
			//	printf("enPreviewMode:%d,i:%d,%d\n",enPreviewMode,i,s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][i]);
			}
		}
		else
		{
			for(i = 0; i < 9; i++)
			{
				if(i<3)
				{
					s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][i] = -1;
					if(LinkageGroup.DevGroupChn[i].DevChn==0)
					{
						s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][i+3] = -1;
					}
					else
					{
						s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][i+3] = LinkageGroup.DevGroupChn[i].DevChn-1;
					}
				}
				if(i>5)
				{
					s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][i] = -1;
				}
				//printf("enPreviewMode:%d,i:%d,%d\n",enPreviewMode,i,s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][i]);
			}
		}
	}
	PRV_RefreshVoDevScreen(VoDev, DISP_DOUBLE_DISP, s_astVoDevStatDflt[VoDev].as32ChnOrder[s_astVoDevStatDflt[VoDev].enPreviewMode]);
	
	RET_SUCCESS("Set Multi Chn!");
}

/*************************************************
Function: //PRV_ZoomInPic
Description://画中画,电子放大状态时专用
Calls: 
Called By: //
Input: // bIsShow:是否显示
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
***********************************************************************/
STATIC HI_S32 PRV_ZoomInPic(HI_BOOL bIsShow)
{
	VO_DEV VoDev = s_VoDevCtrlDflt;
	VO_CHN VoChn = PRV_CTRL_VOCHN;
#if defined(SN9234H1)	
	VI_DEV ViDev = -1;
	VI_CHN ViChn = -1;
#endif
	VO_CHN_ATTR_S stVoChnAttr;
	int index = 0;
	RECT_S stSrcRect, stDestRect;

#if defined(SN9234H1)

 /*2010-9-19 双屏！*/
//VoDev = HD;
//again:
//获取s32CtrlChn对应的VI
	if(OldCtrlChn >= 0)
	{
		if(OldCtrlChn < (LOCALVEDIONUM < PRV_CHAN_NUM ? LOCALVEDIONUM : PRV_CHAN_NUM))
		{
			if(OldCtrlChn < PRV_VI_CHN_NUM)
			{
				ViDev = PRV_656_DEV_1;
			}
			else
			{
				ViDev = PRV_656_DEV;
			}
			ViChn = OldCtrlChn%PRV_VI_CHN_NUM;
		}
		//在从片
		else if(OldCtrlChn >= PRV_CHAN_NUM && OldCtrlChn < LOCALVEDIONUM)
		{
			ViDev = PRV_HD_DEV;
		}
	}
#endif

	if (s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat != PRV_STAT_CTRL
		|| s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag != PRV_CTRL_ZOOM_IN)
	{
		RET_FAILURE("Warning!!! not int zoom in stat now!!!");
	}

	//1.解绑VO通道
	if(OldCtrlChn >= 0)
	{	
#if defined(SN9234H1)
		if(ViDev != -1)//模拟视频通道
			CHECK(HI_MPI_VI_UnBindOutput(ViDev, ViChn, VoDev, VoChn));
		else
		{
			index = PRV_GetVoChnIndex(OldCtrlChn);
			if(index < 0)
				RET_FAILURE("-----------Invalid Index!");
			if(VochnInfo[index].SlaveId > PRV_MASTER)
				(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, VoDev, VoChn));
			else
			{
				(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[index].VdecChn, VoDev, VoChn));
				VochnInfo[index].IsBindVdec[VoDev] = -1;
			}
			
		}
#else
		index = PRV_GetVoChnIndex(OldCtrlChn);
		if(index < 0)
			RET_FAILURE("-----------Invalid Index!");
#if defined(Hi3535)
		CHECK(HI_MPI_VO_HideChn(VoDev, VoChn));
#else
		CHECK(HI_MPI_VO_ChnHide(VoDev, VoChn));
#endif
 		CHECK(PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn, VoChn));
		VochnInfo[index].IsBindVdec[VoDev] = -1;
#if defined(Hi3535)
		CHECK(PRV_VO_UnBindVpss(PIP,VoChn,VoChn,VPSS_BSTR_CHN));
#else
 		CHECK(PRV_VO_UnBindVpss(VoDev, VoChn, VoChn, VPSS_PRE0_CHN));
#endif
#endif		
	}
	//2.关闭VO通道
#if defined(Hi3535)
	CHECK(HI_MPI_VO_DisableChn(PIP ,VoChn));
#else	
	CHECK(HI_MPI_VO_DisableChn(VoDev ,VoChn));
#endif

	if (bIsShow)
	{		
#if defined(SN9234H1)
		if(s_astVoDevStatDflt[VoDev].s32CtrlChn < (LOCALVEDIONUM < PRV_CHAN_NUM ? LOCALVEDIONUM : PRV_CHAN_NUM))
		{
			if(s_astVoDevStatDflt[VoDev].s32CtrlChn < PRV_VI_CHN_NUM)
			{
				ViDev = PRV_656_DEV_1;
			}
			else
			{
				ViDev = PRV_656_DEV;
			}
			ViChn = s_astVoDevStatDflt[VoDev].s32CtrlChn %PRV_VI_CHN_NUM;
		}
		else if(s_astVoDevStatDflt[VoDev].s32CtrlChn >= PRV_CHAN_NUM && s_astVoDevStatDflt[VoDev].s32CtrlChn  < LOCALVEDIONUM)
		{
			ViDev = PRV_HD_DEV;
		}

#endif
		//3.设置VO通道		
		index = PRV_GetVoChnIndex(s_astVoDevStatDflt[VoDev].s32CtrlChn);
		if(index < 0)
			RET_FAILURE("------ERR: Invalid Index!");
#if defined(Hi3535)
		CHECK_RET(HI_MPI_VO_GetChnAttr(PIP, s_astVoDevStatDflt[VoDev].s32CtrlChn, &stVoChnAttr));
#else
		CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, s_astVoDevStatDflt[VoDev].s32CtrlChn, &stVoChnAttr));
#endif
		stVoChnAttr.stRect.s32X      = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width*3/4;
		stVoChnAttr.stRect.s32Y      = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height*3/4;
		stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height*1/4;
		stVoChnAttr.stRect.u32Width  = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width*1/4;
		stSrcRect.s32X		 = stVoChnAttr.stRect.s32X;
		stSrcRect.s32Y		 = stVoChnAttr.stRect.s32Y;
		stSrcRect.u32Width	 = stVoChnAttr.stRect.u32Width;
		stSrcRect.u32Height  = stVoChnAttr.stRect.u32Height;
		
		stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
		stVoChnAttr.stRect.s32X 	 = stDestRect.s32X 		& (~0x01);
		stVoChnAttr.stRect.s32Y 	 = stDestRect.s32Y 		& (~0x01);
		stVoChnAttr.stRect.u32Width  = stDestRect.u32Width  & (~0x01);
		stVoChnAttr.stRect.u32Height = stDestRect.u32Height & (~0x01);
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----------w=%d, h=%d, d_w=%d, d_h=%d, x=%d, y=%d, s_w=%d, s_h=%d\n", s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width, s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height,
		//	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width, s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height,
		//	stVoChnAttr.stRect.s32X ,stVoChnAttr.stRect.s32Y,stVoChnAttr.stRect.u32Width,stVoChnAttr.stRect.u32Height);
#if defined(Hi3535)
		stVoChnAttr.u32Priority = 1;
		CHECK_RET(HI_MPI_VO_SetChnAttr(PIP, VoChn, &stVoChnAttr));
		CHECK_RET(PRV_VO_BindVpss(PIP,VoChn,VoChn,VPSS_BSTR_CHN));
#elif defined(Hi3531)		
		stVoChnAttr.u32Priority = 1;
		CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stVoChnAttr));
		CHECK_RET(PRV_VO_BindVpss(VoDev,VoChn,VoChn,VPSS_PRE0_CHN));
#else
		CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stVoChnAttr));
#endif
		//stVoZoomAttr.stZoomRect = stVoChnAttr.stRect;
		//stVoZoomAttr.enField = VIDEO_FIELD_INTERLACED;
		//CHECK_RET(HI_MPI_VO_SetZoomInWindow(VoDev, VoChn, &stVoZoomAttr));

#if defined(SN9234H1)
		//4.绑定VO通道
		if(-1 == ViDev)
		{			
			if(VochnInfo[index].VdecChn >= 0)
			{			
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----index: %d---SlaveId: %d, VoDev: %d----Bind---VochnInfo[index].VdecChn: %d, VoChn: %d\n",index, VochnInfo[index].SlaveId, VoDev, VochnInfo[index].VdecChn, VoChn);
				if(VochnInfo[index].SlaveId == PRV_MASTER )
				{
					CHECK_RET(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, VoDev, VoChn)); 
				}				
				else if(VochnInfo[index].SlaveId > PRV_MASTER)
				{
					ViDev = PRV_HD_DEV;
					ViChn = 0;
				}
			}
		}
		
#if defined(SN_SLAVE_ON)
		if(ViDev == PRV_HD_DEV)
		{			
			VO_ZOOM_ATTR_S stVoZoomAttr;
			HI_U32 u32Width = 0, u32Height = 0;
#if defined(SN8604M) || defined(SN8608M) || defined(SN8608M_LE) || defined(SN8616M_LE)|| defined(SN9234H1)
			//u32Width = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Width;
			//u32Height = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Height;
			u32Width = PRV_BT1120_SIZE_W;
			u32Width = PRV_BT1120_SIZE_H;
#else
			u32Width = PRV_SINGLE_SCREEN_W;
			u32Height = PRV_SINGLE_SCREEN_H;
#endif				
			u32Width = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Width;
			u32Height = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Height;
			stSrcRect.s32X		= 0;
			stSrcRect.s32Y		= 0;
			stSrcRect.u32Width	= u32Width;
			stSrcRect.u32Height = u32Height;
			
			stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
			
			stVoZoomAttr.stZoomRect.s32X		= stDestRect.s32X	   & (~0x01);
			stVoZoomAttr.stZoomRect.s32Y		= stDestRect.s32Y	   & (~0x01);
			stVoZoomAttr.stZoomRect.u32Width	= stDestRect.u32Width  & (~0x01);
			stVoZoomAttr.stZoomRect.u32Height	= stDestRect.u32Height & (~0x01);

			//stVoZoomAttr.enField = VIDEO_FIELD_INTERLACED;
			stVoZoomAttr.enField = VIDEO_FIELD_FRAME;
#if defined(Hi3535)
			CHECK_RET(HI_MPI_VO_SetZoomInWindow(PIP, VoChn, &stVoZoomAttr));
#else
			CHECK_RET(HI_MPI_VO_SetZoomInWindow(VoDev, VoChn, &stVoZoomAttr));
#endif
		}
#endif
		if(-1 != ViDev)
		{
			//printf("--------Bind ViDev: %d, ViChn: %d\n", ViDev, ViChn);
			CHECK_RET(HI_MPI_VI_BindOutput(ViDev, ViChn, VoDev, VoChn));
		}

#else
		//4.绑定VO通道
		if(VochnInfo[index].VdecChn >= 0)
		{			
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----index: %d---SlaveId: %d, VoDev: %d----Bind---VochnInfo[index].VdecChn: %d, VoChn: %d\n",index, VochnInfo[index].SlaveId, VoDev, VochnInfo[index].VdecChn, VoChn);
			if(VochnInfo[index].SlaveId == PRV_MASTER )
			{
				PRV_VPSS_ResetWH(VoChn,VochnInfo[index].VdecChn,VochnInfo[index].VideoInfo.width,VochnInfo[index].VideoInfo.height);
				CHECK_RET(PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VoChn)); 
			}				
		}		
#endif
		VochnInfo[index].IsBindVdec[VoDev] = 1;
		//5.开启VO通道
#if defined(Hi3535)
		CHECK_RET(HI_MPI_VO_EnableChn(PIP, VoChn));
#else		
		CHECK_RET(HI_MPI_VO_EnableChn(VoDev, VoChn));
#endif
		OldCtrlChn = s_astVoDevStatDflt[VoDev].s32CtrlChn;
		sem_post(&sem_SendNoVideoPic);		
	}
	else
		OldCtrlChn = -1;
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_Disp_ParamInPic
Description://画中画,视频显示参数配置时专用
Calls: 
Called By: //
Input: // bIsShow:是否显示
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
***********************************************************************/
STATIC HI_S32 PRV_Disp_ParamInPic(HI_BOOL bIsShow)
{
	VO_DEV VoDev = s_VoDevCtrlDflt;
	VO_CHN VoChn = PRV_CTRL_VOCHN;
	VO_CHN_ATTR_S stVoChnAttr;
	int index = 0;
	
#if defined(SN9234H1)
	VI_DEV ViDev = -1;
	VI_CHN ViChn = -1;
	RECT_S stSrcRect, stDestRect;

/*2010-9-19 双屏！*/
//VoDev = HD;
//again:
	if(OldCtrlChn >= 0)
	{
		if(OldCtrlChn < (LOCALVEDIONUM < PRV_CHAN_NUM ? LOCALVEDIONUM : PRV_CHAN_NUM))
		{
			if(OldCtrlChn < PRV_VI_CHN_NUM)
			{
				ViDev = PRV_656_DEV_1;
			}
			else
			{
				ViDev = PRV_656_DEV;
			}
			ViChn = OldCtrlChn%PRV_VI_CHN_NUM;
		}
		//在从片
		else if(OldCtrlChn  >= PRV_CHAN_NUM && OldCtrlChn  < LOCALVEDIONUM)
		{
			ViDev = PRV_HD_DEV;
		}
		
	}

#endif

	if (s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat != PRV_STAT_CTRL)
		//|| s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag != PRV_CTRL_REGION_SEL)
	{
		RET_FAILURE("Warning!!! not int zoom in stat now!!!");
	}

	//1.解绑与PRV_CTRL_VOCHN绑定的VDEC或VI通道
	if(OldCtrlChn >= 0)
	{
#if defined(SN9234H1)
		if(ViDev != -1)//模拟视频通道
		{
			CHECK(HI_MPI_VI_UnBindOutput(ViDev, ViChn, VoDev, VoChn));
		}
		else
		{
			index = PRV_GetVoChnIndex(OldCtrlChn);
			if(index < 0)
				RET_FAILURE("------ERR: Invalid Index!");
			if(VochnInfo[index].SlaveId > PRV_MASTER)
			{
				(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, VoDev, VoChn));
			}
			else
			{
				(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[index].VdecChn, VoDev, VoChn));
				VochnInfo[index].IsBindVdec[VoDev] = -1;
			}
		}
#else
		index = PRV_GetVoChnIndex(OldCtrlChn);
		if(index < 0)
			RET_FAILURE("------ERR: Invalid Index!");
		CHECK(PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn, VoChn));
		VochnInfo[index].IsBindVdec[VoDev] = -1;
#endif		
	}
	
	//2.关闭VO通道	
#if defined(Hi3535)
	CHECK(HI_MPI_VO_HideChn(PIP, VoChn));
	CHECK_RET(PRV_VO_UnBindVpss(PIP,VoChn,VoChn,VPSS_BSTR_CHN));
	CHECK(HI_MPI_VO_DisableChn(PIP ,VoChn));
#elif defined(Hi3531)
	CHECK(HI_MPI_VO_ChnHide(VoDev, VoChn));
	CHECK_RET(PRV_VO_UnBindVpss(VoDev, VoChn, VoChn, VPSS_PRE0_CHN));
	CHECK(HI_MPI_VO_DisableChn(VoDev, VoChn));
#else
	CHECK(HI_MPI_VO_DisableChn(VoDev, VoChn));
#endif

	if (bIsShow)
	{
#if defined(SN9234H1)
//		HI_S32 s32Index;
		//判断当前的通道是否禁用
		//PRV_Chn2Index(VoDev,s_astVoDevStatDflt[VoDev].s32CtrlChn,&s32Index);
		//3.设置VO通道
		/*各分辨率下的范围值
		800*600:  121\113\328\264
		1024*768: 173\151\256\206
		1280*1024: 210\185\205\154
		1366*768: 220\151\192\206
		1440*900: 227\171\182\176
		*/
		//获取新的s32CtrlChn对应的VI
		if(s_astVoDevStatDflt[VoDev].s32CtrlChn < (LOCALVEDIONUM < PRV_CHAN_NUM ? LOCALVEDIONUM : PRV_CHAN_NUM))
		{
			if(s_astVoDevStatDflt[VoDev].s32CtrlChn < PRV_VI_CHN_NUM)
			{
				ViDev = PRV_656_DEV_1;
			}
			else
			{
				ViDev = PRV_656_DEV;
			}
			ViChn = s_astVoDevStatDflt[VoDev].s32CtrlChn %PRV_VI_CHN_NUM;
		}
		else if(s_astVoDevStatDflt[VoDev].s32CtrlChn >= PRV_CHAN_NUM && s_astVoDevStatDflt[VoDev].s32CtrlChn  < LOCALVEDIONUM)
		{
			ViDev = PRV_HD_DEV;
		}
#endif
		index = PRV_GetVoChnIndex(s_astVoDevStatDflt[VoDev].s32CtrlChn);
		if(index < 0)
			RET_FAILURE("------ERR: Invalid Index!");
//		HI_U32 u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
//		HI_U32 u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----------s32X=%d, s32Y=%d, u32Width=%d, u32Height=%d\n",
			s_astVoDevStatDflt[s_VoDevCtrlDflt].Pip_rect.s32X,s_astVoDevStatDflt[s_VoDevCtrlDflt].Pip_rect.s32Y,
			s_astVoDevStatDflt[s_VoDevCtrlDflt].Pip_rect.u32Width, s_astVoDevStatDflt[s_VoDevCtrlDflt].Pip_rect.u32Height);
#if defined(Hi3535)
		CHECK_RET(HI_MPI_VO_GetChnAttr(PIP, s_astVoDevStatDflt[VoDev].s32CtrlChn, &stVoChnAttr));
#else
		CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, s_astVoDevStatDflt[VoDev].s32CtrlChn, &stVoChnAttr));
#endif
		stVoChnAttr.stRect.s32X      = s_astVoDevStatDflt[s_VoDevCtrlDflt].Pip_rect.s32X * s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
		stVoChnAttr.stRect.s32Y      = s_astVoDevStatDflt[s_VoDevCtrlDflt].Pip_rect.s32Y * s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
		stVoChnAttr.stRect.u32Width  = 3 + s_astVoDevStatDflt[s_VoDevCtrlDflt].Pip_rect.u32Width * s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
		stVoChnAttr.stRect.u32Height = 4 + s_astVoDevStatDflt[s_VoDevCtrlDflt].Pip_rect.u32Height * s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height/s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
#if defined(SN9234H1)
		stVoChnAttr.u32Priority = 4;
#else
		stVoChnAttr.u32Priority = 1;
#endif
		if(VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn)
		{
			//printf("VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecCh\n");
			//stVoChnAttr.stRect.s32X 	 = stVoChnAttr.stRect.s32X + (stVoChnAttr.stRect.u32Width - (NOVIDEO_IMAGWIDTH * stVoChnAttr.stRect.u32Width / u32Width)) / 2;
			//stVoChnAttr.stRect.s32Y 	 = stVoChnAttr.stRect.s32Y + (stVoChnAttr.stRect.u32Height - (NOVIDEO_IMAGHEIGHT * stVoChnAttr.stRect.u32Height/ u32Height))/ 2;
			//stVoChnAttr.stRect.u32Width  = (NOVIDEO_IMAGWIDTH * stVoChnAttr.stRect.u32Width / u32Width);
			//stVoChnAttr.stRect.u32Height = (NOVIDEO_IMAGHEIGHT * stVoChnAttr.stRect.u32Height / u32Height);
			
		}
#if defined(Hi3531)||defined(Hi3535)		
		if(stVoChnAttr.stRect.u32Height < 32)
		{
			stVoChnAttr.stRect.s32Y = stVoChnAttr.stRect.s32Y - (32 - stVoChnAttr.stRect.u32Height)/2;
			stVoChnAttr.stRect.u32Height = 32;
		}
#endif		
		stVoChnAttr.stRect.s32X 	 &= (~0x1);
		stVoChnAttr.stRect.s32Y 	 &= (~0x1);
		stVoChnAttr.stRect.u32Width  &= (~0x1);
		stVoChnAttr.stRect.u32Height &= (~0x1);
		
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----------w=%d, h=%d, d_w=%d, d_h=%d, x=%d, y=%d, s_w=%d, s_h=%d\n", s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width, s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height,
			s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width, s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height,
			stVoChnAttr.stRect.s32X ,stVoChnAttr.stRect.s32Y,stVoChnAttr.stRect.u32Width,stVoChnAttr.stRect.u32Height);
#if defined(Hi3535)
		CHECK_RET(HI_MPI_VO_SetChnAttr(PIP, VoChn, &stVoChnAttr));
		CHECK_RET(PRV_VO_BindVpss(PIP, VoChn, VoChn, VPSS_BSTR_CHN));
#elif defined(Hi3531)
		CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stVoChnAttr));
		CHECK_RET(PRV_VO_BindVpss(VoDev, VoChn, VoChn, VPSS_PRE0_CHN));
#else
		CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stVoChnAttr));
#endif
		//stVoZoomAttr.stZoomRect = stVoChnAttr.stRect;
		//stVoZoomAttr.enField = VIDEO_FIELD_INTERLACED;
		//CHECK_RET(HI_MPI_VO_SetZoomInWindow(VoDev, VoChn, &stVoZoomAttr));
#if defined(SN9234H1)
		//4.绑定VO通道
		if(-1 == ViDev)
		{			
			if(VochnInfo[index].VdecChn >= 0 /*&& VochnInfo[index].IsBindVdec[VoDev] == -1*/)
			{				
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----index: %d---SlaveId: %d, VoDev: %d----Bind---VochnInfo[index].VdecChn: %d, VoChn: %d\n",index, VochnInfo[index].SlaveId, VoDev, VochnInfo[index].VdecChn, VoChn);
				if(VochnInfo[index].SlaveId == PRV_MASTER )
				{
					CHECK_RET(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, VoDev, VoChn));
					if(VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn )						
						sem_post(&sem_SendNoVideoPic);				
				}				
				else if(VochnInfo[index].SlaveId > PRV_MASTER)
				{
					ViDev = PRV_HD_DEV;
					ViChn = 0;
				}
			}
		}
		
#if defined(SN_SLAVE_ON)
		if(ViDev == PRV_HD_DEV)
		{			
			VO_ZOOM_ATTR_S stVoZoomAttr;
			int w = 0, h = 0;

			w = PRV_BT1120_SIZE_W;
			h = PRV_BT1120_SIZE_H;			
			w = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
			h = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;

			stSrcRect.s32X		= 0;
			stSrcRect.s32Y		= 0;
			stSrcRect.u32Width	= w;
			stSrcRect.u32Height = h;
			
			stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
			
			stVoZoomAttr.stZoomRect.s32X		= stDestRect.s32X	   & (~0x01);
			stVoZoomAttr.stZoomRect.s32Y		= stDestRect.s32Y	   & (~0x01);
			stVoZoomAttr.stZoomRect.u32Width	= stDestRect.u32Width  & (~0x01);
			stVoZoomAttr.stZoomRect.u32Height	= stDestRect.u32Height & (~0x01);

			stVoZoomAttr.enField = VIDEO_FIELD_FRAME;
			CHECK_RET(HI_MPI_VO_SetZoomInWindow(VoDev, VoChn, &stVoZoomAttr));
		}
#endif
		if(-1 != ViDev)
		{
			//printf("--------Bind ViDev: %d, ViChn: %d\n", ViDev, ViChn);
			CHECK_RET(HI_MPI_VI_BindOutput(ViDev, ViChn, VoDev, VoChn));
		}
#else
		//4.绑定VO通道
		if(VochnInfo[index].VdecChn >= 0 /*&& VochnInfo[index].IsBindVdec[VoDev] == -1*/)
		{				
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----index: %d---SlaveId: %d, VoDev: %d----Bind---VochnInfo[index].VdecChn: %d, VoChn: %d\n",index, VochnInfo[index].SlaveId, VoDev, VochnInfo[index].VdecChn, VoChn);
			if(VochnInfo[index].SlaveId == PRV_MASTER )
			{	
				if(VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn )		
				{
#if defined(Hi3535)
					CHECK(HI_MPI_VO_HideChn(VoDev, VochnInfo[index].VoChn));
#else
					CHECK(HI_MPI_VO_ChnHide(VoDev, VochnInfo[index].VoChn));
#endif
					PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn, VochnInfo[index].VoChn);
					VochnInfo[index].IsBindVdec[VoDev] = -1;
				}				
				PRV_VPSS_ResetWH(VoChn,VochnInfo[index].VdecChn,VochnInfo[index].VideoInfo.width,VochnInfo[index].VideoInfo.height);
				CHECK_RET(PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VoChn));
				VochnInfo[index].IsBindVdec[VoDev] = 0;
				if(VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn )						
					sem_post(&sem_SendNoVideoPic);				
			}				
		}
#endif		
		//5.开启VO通道
#if defined(Hi3535)
		CHECK_RET(HI_MPI_VO_EnableChn(PIP, VoChn));
#else
		CHECK_RET(HI_MPI_VO_EnableChn(VoDev, VoChn));
#endif
		
		OldCtrlChn = s_astVoDevStatDflt[VoDev].s32CtrlChn;
		
	}
	else		
		OldCtrlChn = -1;
	
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_EnterChnCtrl
Description://进入通道控制状态
Calls: 
Called By: //
Input: // VoDev: 输出设备
		VoChn: 通道号
		s32Flag: 通道状态
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_EnterChnCtrl(VO_DEV VoDev, VO_CHN VoChn, HI_S32 s32Flag)
{
	unsigned int is_double = DISP_DOUBLE_DISP;
#if defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if( VoDev < 0 || VoDev >= PRV_VO_MAX_DEV
		|| VoChn < 0 || VoChn >= g_Max_Vo_Num)
#else
	if(VoDev > DHD0)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/
		|| VoChn < 0 || VoChn >= g_Max_Vo_Num)
#endif		
	{
		RET_FAILURE("Invalid Parameter: VoDev or VoChn");
	}
	//printf("------------s_astVoDevStatDflt[VoDev].enPreviewStat: %d\n", s_astVoDevStatDflt[VoDev].enPreviewStat);
	if (PRV_STAT_NORM != s_astVoDevStatDflt[VoDev].enPreviewStat)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "WARNING: NOT in preview stat currently: %d!!,operation continues.", s_astVoDevStatDflt[VoDev].enPreviewStat);
		//RET_FAILURE("NOT in preview stat!!!");
	}
	
	//printf("######PRV_EnterChnCtrl s32Flag = %d  ,s_astVoDevStatDflt[VoDev].enPreviewStat = %d,vochn:%d###################\n",s32Flag,s_astVoDevStatDflt[VoDev].enPreviewStat,VoChn);
	//sem_post(&sem_SendNoVideoPic);

	switch (s32Flag)
	{
		case 8://1-进入区域选择,隐藏OSD,OSD位置选择进入OSD位置选择
 /*2010-9-2 修正：进入区域选择时关闭预览OSD*/
			if(s_State_Info.bIsOsd_Init && s_State_Info.bIsRe_Init)
			{
#if defined(SN9234H1)
				//Prv_OSD_Show(VoDev, HI_FALSE);
				Prv_OSD_Show(HD, HI_FALSE);
				Prv_OSD_Show(AD, HI_FALSE);
#else
				//Prv_OSD_Show(VoDev, HI_FALSE);
				Prv_OSD_Show(DHD0, HI_FALSE);
				//Prv_OSD_Show(DSD0, HI_FALSE);
#endif
			}

		case 1://1-进入区域选择，视频参数配置等
			s_astVoDevStatDflt[VoDev].enCtrlFlag = PRV_CTRL_REGION_SEL;
			s_astVoDevStatDflt[VoDev].enPreviewStat = PRV_STAT_CTRL;
			s_astVoDevStatDflt[VoDev].s32CtrlChn = VoChn;
			//is_double = DISP_NOT_DOUBLE_DISP;
			break;
		case 2://2-电子放大
			s_astVoDevStatDflt[VoDev].enCtrlFlag = PRV_CTRL_ZOOM_IN;
			s_astVoDevStatDflt[VoDev].enPreviewStat = PRV_STAT_CTRL;
			s_astVoDevStatDflt[VoDev].s32CtrlChn = VoChn;
			//is_double = DISP_NOT_DOUBLE_DISP;
			break;
		case 3://3-云台控制
			s_astVoDevStatDflt[VoDev].enCtrlFlag = PRV_CTRL_PTZ;
			s_astVoDevStatDflt[VoDev].enPreviewStat = PRV_STAT_CTRL;
			s_astVoDevStatDflt[VoDev].s32CtrlChn = VoChn;
			break;
		case 4://4-切换到回放
		case SLC_CTL_FLAG:
			s_astVoDevStatDflt[VoDev].enCtrlFlag = PRV_CTRL_PB;
			s_astVoDevStatDflt[VoDev].enPreviewStat = PRV_STAT_PB;
			
			//is_double = DISP_NOT_DOUBLE_DISP;
			break;
		case PIC_CTL_FLAG:	
			s_astVoDevStatDflt[VoDev].enCtrlFlag = PRV_CTRL_PB;
			s_astVoDevStatDflt[VoDev].enPreviewStat = PRV_STAT_PIC;
			//is_double = DISP_NOT_DOUBLE_DISP;
			break;	
		case 7:	//遮盖
#if 0 /*2010-9-2 修正：进入区域选择时关闭预览OSD*/
			if(s_State_Info.bIsOsd_Init)
			{
				Prv_OSD_Show(VoDev, HI_FALSE);
			}
#endif
			//进入遮盖时，需要关闭遮盖区域
			//OSD_Mask_disp_Ctl(VoChn,0);
			IsDispInPic = 0;
			PRV_Disp_ParamInPic(HI_FALSE);//关闭画中画
			//s_astVoDevStatDflt[VoDev].enCtrlFlag = PRV_CTRL_REGION_SEL;
			//s_astVoDevStatDflt[VoDev].enPreviewStat = PRV_STAT_CTRL;
			//s_astVoDevStatDflt[VoDev].s32CtrlChn = VoChn;
			//is_double = DISP_NOT_DOUBLE_DISP;
			//break;
			RET_SUCCESS("Enter Chn Ctrl!");
		case 9://进入视频显示参数配置界面
			s_astVoDevStatDflt[VoDev].enCtrlFlag = PRV_CTRL_REGION_SEL;
			s_astVoDevStatDflt[VoDev].enPreviewStat = PRV_STAT_CTRL;
			s_astVoDevStatDflt[VoDev].s32CtrlChn = VoChn;
			IsDispInPic = 1;
#if defined(SN9234H1)			
			sem_post(&sem_SendNoVideoPic);
#endif			
			if(s_State_Info.bIsOsd_Init && s_State_Info.bIsRe_Init)
			{
				if(s_VoDevCtrlDflt == s_VoSecondDev)
				{
#if defined(SN9234H1)					
					Prv_OSD_Show(AD, HI_FALSE);
#endif					
				}
				else
				{
					Prv_OSD_Show(s_VoDevCtrlDflt, HI_FALSE);
				}
			}
			//9-进入视频参数配置界面，显示画中画
			//PRV_Disp_ParamInPic(HI_TRUE);
			//is_double = DISP_NOT_DOUBLE_DISP;
			break;
			//RET_SUCCESS("Enter Chn Ctrl!");
		default:
			RET_FAILURE("Invalid CTRL FLAG!!!");
	}
	//printf("111111111######PRV_EnterChnCtrl s32Flag = %d  ,s_astVoDevStatDflt[VoDev].enPreviewStat = %d###################\n",s32Flag,s_astVoDevStatDflt[VoDev].enPreviewStat);
	PRV_RefreshVoDevScreen(VoDev, is_double, s_astVoDevStatDflt[VoDev].as32ChnOrder[s_astVoDevStatDflt[VoDev].enPreviewMode]);

	if (s32Flag == 2)//2-进入电子放大，显示画中画
	{
		PRV_ZoomInPic(HI_TRUE);
	}
	else if(s32Flag == 9)
	{
		//9-进入视频参数配置界面，显示画中画
		PRV_Disp_ParamInPic(HI_TRUE);
	}
	
	RET_SUCCESS("Enter Chn Ctrl!");
}

/*************************************************
Function: //PRV_ExitChnCtrl
Description://退出通道控制状态
Calls: 
Called By: //
Input: // s32Flag: 通道状态
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_ExitChnCtrl(HI_S32 s32Flag)
{
	PRV_CTRL_FLAG_E enCtrlFlag=0;
	unsigned int is_double=DISP_DOUBLE_DISP;

	if (PRV_STAT_CTRL != s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat
		&& PRV_STAT_PB != s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat
		&& PRV_STAT_PIC	!= s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat)
	{
		RET_FAILURE("NOT in ctrl or pb stat or pic stat!!!");
	}
	//printf("######s32Flag = %d###################\n",s32Flag);
	switch (s32Flag)
	{
		case 8://1-区域选择,打开预览OSD
#if 1 /*2010-9-2 修正：退出区域选择时打开预览OSD*/
			if(s_State_Info.bIsOsd_Init && s_State_Info.bIsRe_Init)
			{
#if defined(SN9234H1)
				//Prv_OSD_Show(s_VoDevCtrlDflt, HI_TRUE);
				Prv_OSD_Show(HD, HI_TRUE);
				Prv_OSD_Show(AD, HI_TRUE);
#else
				//Prv_OSD_Show(s_VoDevCtrlDflt, HI_TRUE);
				Prv_OSD_Show(DHD0, HI_TRUE);
				//Prv_OSD_Show(DSD0, HI_TRUE);
#endif				
			}
#endif
		case 1://1-区域选择
			enCtrlFlag = PRV_CTRL_REGION_SEL;
			is_double = DISP_NOT_DOUBLE_DISP;
			break;
		case 2://2-电子放大
			{
#if defined(SN9234H1)
				VO_ZOOM_RATIO_S stZoomRatio = {0};
#if defined(SN6108) || defined(SN8608D) || defined(SN8608M) || defined(SN6104) || defined(SN8604D) || defined(SN8604M)
				HI_MPI_VO_SetZoomInRatio(s_VoDevCtrlDflt, s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn, &stZoomRatio);
#else
				HI_MPI_VO_SetZoomInRatio(HD, s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn, &stZoomRatio);
				//HI_MPI_VO_SetZoomInRatio(AD, s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn, &stZoomRatio);
#endif
				s_State_Info.g_zoom_first_in = HI_FALSE;
#else
				int index = s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn;
				VPSS_GRP VpssGrp = VochnInfo[index].VoChn;
				//重启VPSS GROP，以清除电子放大时设置的属性
				
#if defined(Hi3535)
				CHECK(HI_MPI_VO_HideChn(s_VoDevCtrlDflt, VochnInfo[index].VoChn));
				CHECK(PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn, VpssGrp));
				CHECK(PRV_VO_UnBindVpss(s_VoDevCtrlDflt, VochnInfo[index].VoChn, VpssGrp, VPSS_BSTR_CHN));
#else
				CHECK(HI_MPI_VO_ChnHide(s_VoDevCtrlDflt, VochnInfo[index].VoChn));
				CHECK(PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn, VpssGrp));
				CHECK(PRV_VO_UnBindVpss(s_VoDevCtrlDflt, VochnInfo[index].VoChn, VpssGrp, VPSS_PRE0_CHN));
#endif
				CHECK(PRV_VPSS_Stop(VpssGrp));
				CHECK(PRV_VPSS_Start(VpssGrp));
#if defined(Hi3535)
				CHECK(PRV_VO_BindVpss(s_VoDevCtrlDflt, VochnInfo[index].VoChn, VpssGrp, VPSS_BSTR_CHN));
#else
				CHECK(PRV_VO_BindVpss(s_VoDevCtrlDflt, VochnInfo[index].VoChn, VpssGrp, VPSS_PRE0_CHN));
#endif
				PRV_VPSS_ResetWH(VpssGrp,VochnInfo[index].VdecChn,VochnInfo[index].VideoInfo.width,VochnInfo[index].VideoInfo.height);
				CHECK(PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VpssGrp));
				s_State_Info.g_zoom_first_in = HI_FALSE;
#endif				
			}
			enCtrlFlag = PRV_CTRL_ZOOM_IN;
			break;
		case 3://3-云台控制
			enCtrlFlag = PRV_CTRL_PTZ;
			break;
		case 4://4-切换到回放
		case SLC_CTL_FLAG:
		case PIC_CTL_FLAG:
			enCtrlFlag = PRV_CTRL_PB;
			//is_double = DISP_NOT_DOUBLE_DISP;
			break;
		case 7:
#if 0 /*2010-9-2 修正：退出区域选择时打开预览OSD*/
			if(s_State_Info.bIsOsd_Init)
			{
				Prv_OSD_Show(s_VoDevCtrlDflt, HI_TRUE);
			}
#endif
			//退出遮盖时，需要开启遮盖区域
			//OSD_Mask_disp_Ctl(s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn,1);
			PRV_Disp_ParamInPic(HI_TRUE);	//开启画中画
			//enCtrlFlag = PRV_CTRL_REGION_SEL;
			//is_double = DISP_NOT_DOUBLE_DISP;
			//break;
			RET_SUCCESS("Enter Chn Ctrl!");
		case 9://9-进入视频显示参数配置界面
			enCtrlFlag = PRV_CTRL_REGION_SEL;
			if(s_State_Info.bIsOsd_Init && s_State_Info.bIsRe_Init)
			{
				if(s_VoDevCtrlDflt == s_VoSecondDev)
				{
#if defined(SN9234H1)					
					Prv_OSD_Show(AD, HI_TRUE);
#endif					
				}
				else
				{
					Prv_OSD_Show(s_VoDevCtrlDflt, HI_TRUE);
				}
			}
			IsDispInPic = 0;
			PRV_Disp_ParamInPic(HI_FALSE);
			//s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat = PRV_STAT_NORM;
			//is_double = DISP_NOT_DOUBLE_DISP;
			break;
			//RET_SUCCESS("Exit Chn Ctrl!");
		default:
			RET_FAILURE("Invalid CTRL FLAG!!!");
	}

	if (s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag != enCtrlFlag)
	{
		RET_FAILURE("Ctrl Flag NOT match!");
	}
	if (s32Flag == 2)//2-退出电子放大，取消画中画
	{
		PRV_ZoomInPic(HI_FALSE);
		//is_double = DISP_NOT_DOUBLE_DISP;
	}
	s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat = PRV_STAT_NORM;
#if defined(Hi3535)
	int i = 0;
	for(i=0;i<PRV_VO_CHN_NUM;i++)
	{
		PRV_VO_UnBindVpss(DHD0,i,i,VPSS_BSTR_CHN);
	}
#endif

	PRV_RefreshVoDevScreen(s_VoDevCtrlDflt,is_double,s_astVoDevStatDflt[s_VoDevCtrlDflt].as32ChnOrder[s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode]);
	//if(s32Flag == 7)
	//{//如果在遮盖区域功能，那么状态还是控制状态，否则画中画无法退出
	//	s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat = PRV_STAT_CTRL;
	//}
	RET_SUCCESS("Enter Chn Ctrl!");

}

/*************************************************
Function: //PRV_DoubleClick
Description://鼠标双击处理状态切换
Calls: 
Called By: //
Input: // vochn: 通道号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_DoubleClick(const Preview_Point *pstPoint)
{
	HI_U32 u32Index;
	unsigned char mode =s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode;
	if (NULL == pstPoint)
	{
		RET_FAILURE("NULL pointer");
	}

	if (PRV_STAT_NORM != s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat)
	{
		RET_FAILURE("NOT in preview stat!!!");
	}
	LayoutToSingleChn = -1;
#if 0
	if (s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsAlarm)
	{
		s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsAlarm = HI_FALSE;
	}
	else if (s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle)
	{
		s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle = HI_FALSE;
	}
	else
	{
		CHECK_RET(PRV_Point2Index(pstPoint, &u32Index));
		s_astVoDevStatDflt[s_VoDevCtrlDflt].s32SingleIndex = u32Index;
		s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle = HI_TRUE;
	}
#else 
	/* 2010-8-23 修改预览配置的参数，退回到预览画面导致鼠标双击失效。 
	前置条件：
	进入预览配置菜单，修改通道和画面顺序，（或者做其他参数设置）退回到预览画面。双击通道1。
	BUG描述：
	此时时间条呈现黑色底，双击后，不会回到多画面显示模式。
	补充信息：
	当预览配置设置预览模式为单画面，开机时双击单画面同样有如上问题。  */
	
	if (s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsAlarm)
	{
		//RET_FAILURE("In Alarm stat!!!");
		s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsAlarm = HI_FALSE;
		//单画面预览触发报警时，双击回到单画面
		if (SingleScene == mode)
		{
			s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode = PRV_VO_MAX_MOD;
			mode = PRV_VO_MAX_MOD;
		}
		s_astVoDevStatDflt[s_VoDevCtrlDflt].s32DoubleIndex = 0;
	}
	else if (s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle)
	{
		s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle = HI_FALSE;
#if(IS_DECODER_DEVTYPE == 0)
		//单画面预览时，双击无效
		if (SingleScene == mode)
		{
			s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode = PRV_VO_MAX_MOD;
			s_astVoDevStatDflt[s_VoDevCtrlDflt].s32PreviewIndex = 0;
			mode = PRV_VO_MAX_MOD;
		}
#endif
		
		s_astVoDevStatDflt[s_VoDevCtrlDflt].s32DoubleIndex = 0;
		DoubleToSingleIndex = -1;
	}
	else if (SingleScene == mode)
	{
	//解码器中单画面双击无效
#if(IS_DECODER_DEVTYPE == 0)
		s_astVoDevStatDflt[s_VoDevCtrlDflt].s32PreviewIndex = 0;
		s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode = PRV_VO_MAX_MOD;
		mode = PRV_VO_MAX_MOD;
#endif
		s_astVoDevStatDflt[s_VoDevCtrlDflt].s32DoubleIndex = 0;
	}
	else
	{
		CHECK_RET(PRV_Point2Index(pstPoint, &u32Index, s_astVoDevStatDflt[s_VoDevCtrlDflt].as32ChnOrder[mode]));
		s_astVoDevStatDflt[s_VoDevCtrlDflt].s32SingleIndex = u32Index;
		s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle = HI_TRUE;
		s_astVoDevStatDflt[s_VoDevCtrlDflt].s32DoubleIndex = 1;
		DoubleToSingleIndex = u32Index;
		VochnInfo[u32Index].bIsDouble = 0;

		g_ChnPlayStateInfo stPlayStateInfo;
		g_PlayInfo	stPlayInfo;
		PRV_GetPlayInfo(&stPlayInfo);
		PRV_GetVoChnPlayStateInfo(stPlayInfo.InstantPbChn, &stPlayStateInfo);
		
		if(stPlayInfo.PlayBackState == PLAY_INSTANT && stPlayStateInfo.CurPlayState == DEC_STATE_NORMALPAUSE)
		{
#if defined(Hi3535)
			HI_MPI_VO_ResumeChn(0, stPlayInfo.InstantPbChn);
#else
			HI_MPI_VO_ChnResume(0, stPlayInfo.InstantPbChn);
#endif
			if(Achn == stPlayInfo.InstantPbChn)
			{
#if defined(SN9234H1)
				HI_MPI_AO_ResumeChn(0, AOCHN);
#else
				HI_MPI_AO_ResumeChn(4, AOCHN);
#endif
			}
			sem_post(&sem_VoPtsQuery);
		}
		
	}
#endif
	PRV_RefreshVoDevScreen(s_VoDevCtrlDflt, DISP_DOUBLE_DISP, s_astVoDevStatDflt[s_VoDevCtrlDflt].as32ChnOrder[mode]);
	PRV_PlayAudio(s_VoDevCtrlDflt);
	RET_SUCCESS("Double Click");
}

/*************************************************
Function: //PRV_GetVoPrvMode
Description://获取指定输出设备上的预览模式。
Calls: 
Called By: //
Input: // VoDev: 设备号
Output: // pePreviewMode:预览模式
Return: //成功返回HI_SUCCESS
		失败返回HI_FAILURE
Others: // 其它说明
************************************************************************/
HI_S32 PRV_GetVoPrvMode(VO_DEV VoDev, PRV_PREVIEW_MODE_E *pePreviewMode)
{
	if ( NULL == pePreviewMode)
	{
		RET_FAILURE("NULL Poniter parameter!");
	}
#if defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}

	if (VoDev<0 || VoDev>=PRV_VO_MAX_DEV)
	{
		RET_FAILURE("bad parameter: VoDev!!!");
	}
#else
	if(VoDev > DHD0)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
	{
		RET_FAILURE("bad parameter: VoDev!!!");
	}
#endif
	switch(s_astVoDevStatDflt[VoDev].enPreviewStat)
	{
		case PRV_STAT_NORM:
			{
				if (s_astVoDevStatDflt[VoDev].bIsAlarm || s_astVoDevStatDflt[VoDev].bIsSingle)
				{
					*pePreviewMode = SingleScene;
				}
				else
				{
					*pePreviewMode = s_astVoDevStatDflt[VoDev].enPreviewMode;
				}
			}
			break;
		case PRV_STAT_CTRL:
			*pePreviewMode = SingleScene;
			break;
		case PRV_STAT_PB:
			RET_FAILURE("current in playback mode now!");
		case PRV_STAT_PIC:
			break;
		default :
			RET_FAILURE("I'm in PRV_STAT_???... (°ο°)~@ ..");
	}
	RET_SUCCESS("");
}


int PRV_GetPlaybackState(UINT8 *PlaybackState)
{
	g_ChnPlayStateInfo stPlayStateInfo;
	g_PlayInfo	stPlayInfo;
	PRV_GetPlayInfo(&stPlayInfo);

	PRV_GetVoChnPlayStateInfo(0, &stPlayStateInfo);

	//printf("333333333333stPlayStateInfo.CurPlayState=%d\n", stPlayStateInfo.CurPlayState);
	//printf("444444444444stPlayInfo.PlayBackState=%d\n", stPlayInfo.PlayBackState);

	if(stPlayInfo.PlayBackState >= PLAY_INSTANT)
	{
		switch(stPlayStateInfo.CurPlayState)
		{
			case DEC_STATE_NORMAL:
				*PlaybackState = PLAYBACK_STATE_NORMAL;
				break;
			case DEC_STATE_NORMALPAUSE:
				*PlaybackState = PLAYBACK_STATE_NORMALPAUSE;
				break;
			case DEC_STATE_STOP:
				*PlaybackState = PLAYBACK_STATE_STOP;
				break;
			case DEC_STATE_EXIT:
				*PlaybackState = PLAYBACK_STATE_STOP;
				break;
			default:
				*PlaybackState = PLAYBACK_STATE_STOP;
				break;
		}
		
	}
	else
	{
		*PlaybackState = PLAYBACK_STATE_EXIT;
	}

	return OK;
}

int PRV_GetPrvStat()
{
	return s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat;
}

/*************************************************
Function: //PRV_GetPrvMode
Description:// 获取当前GUI所在输入设备的预览模式。
Calls: 
Called By: //
Input: //
Output: // pePreviewMode:预览模式
Return: //成功返回HI_SUCCESS
		失败返回HI_FAILURE
Others: // 其它说明
************************************************************************/
int PRV_GetPrvMode(enum PreviewMode_enum *pePreviewMode)
{
	return PRV_GetVoPrvMode(s_VoDevCtrlDflt, pePreviewMode);
}

void PRV_GetPrvMode_EX(enum PreviewMode_enum *pePreviewMode)
{
	*pePreviewMode = s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode;
}

/************************************************************************/
/* 获取VO设备输入画面的显示大小和位置。
                                                                     */
/************************************************************************/
HI_S32 PRV_GetVoDspRect(VO_DEV VoDev, PRV_RECT_S *pstDspRect)
{
	VO_DEV vodevtmp = VoDev;
	//printf("11111111VoDev = %d,PRV_VO_DEV_NUM=%d1111111111111111111\n",VoDev,PRV_VO_DEV_NUM);
	if (NULL == pstDspRect)
	{
		RET_FAILURE("NULL pointer!!!!");
	}
#if defined(SN9234H1)	
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (VoDev<0 || VoDev>=PRV_VO_MAX_DEV)
	{
		RET_FAILURE("bad parameter: VoDev!!!");
	}
	vodevtmp = (VoDev == HD)?HD:s_VoSecondDev;
#else
	if(VoDev > DHD0)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
	{
		RET_FAILURE("bad parameter: VoDev!!!");
	}
	vodevtmp = (VoDev == DHD0) ? DHD0 : s_VoSecondDev;
#endif	
	pstDspRect->s32X = s_astVoDevStatDflt[vodevtmp].stVideoLayerAttr.stDispRect.s32X;
	pstDspRect->s32Y = s_astVoDevStatDflt[vodevtmp].stVideoLayerAttr.stDispRect.s32Y;
	pstDspRect->u32Height = s_astVoDevStatDflt[vodevtmp].stVideoLayerAttr.stDispRect.u32Height;
	pstDspRect->u32Width = s_astVoDevStatDflt[vodevtmp].stVideoLayerAttr.stDispRect.u32Width;
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_GetVoDevImgSize
Description://  获取VO通道的大小位置（数值相对所在VO设备的iMAGE大小）。
			末在显示状态的VO通道返回HI_FAILURE。
Calls: 
Called By: //
Input: //VoDev:设备号
		VoChn:通道号
		pstRect:保存返回的通道位置
Output: // pstSize: 返回图像大小
Return: //成功返回HI_SUCCESS
		失败返回HI_FAILURE
Others: // 其它说明
************************************************************************/

HI_S32 PRV_GetVoChnRect(VO_DEV VoDev, VO_CHN VoChn, RECT_S *pstRect)
{
//	int chn, index, i, chnnum;
//	int num[16] = {1,2,4,6,8,9,16};
	VO_CHN_ATTR_S stVoChnAttr;
	HI_S32 index = 0;
//	HI_U32 Max_num;
//	PRV_PREVIEW_MODE_E mode = s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode;
	if (NULL == pstRect)
	{
		RET_FAILURE("NULL pointer!");
	}
#if defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (VoChn<0 || VoChn>=g_Max_Vo_Num || VoDev<0 || VoDev>=PRV_VO_MAX_DEV)
	{
		RET_FAILURE("bad parameter: VoDev or VoChn");
	}
#else
	if(VoDev > DHD0)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (VoChn<0 || VoChn>=g_Max_Vo_Num ||VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
	{
		RET_FAILURE("bad parameter: VoDev or VoChn");
	}
#endif

	if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_PB
		|| s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_PIC)
	{
		RET_FAILURE("current in pb or pic stat!");
	}
	
	index = PRV_GetVoChnIndex(VoChn);
	if(index < 0)
		RET_FAILURE("------ERR: Invalid Index!");

#if defined(SN9234H1)
	if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_NORM
		&& DEV_SPOT_NUM > 0 && SPOT_VO_DEV == VoDev)
	{
		VO_VIDEO_LAYER_ATTR_S stLayerAttr;
		CHECK_RET(HI_MPI_VO_GetVideoLayerAttr(VoDev, &stLayerAttr));
		pstRect->u32Height = stLayerAttr.stImageSize.u32Height;
		pstRect->u32Width = stLayerAttr.stImageSize.u32Width;
		pstRect->s32X = 0;
		pstRect->s32Y = 0;
		RET_SUCCESS("");

	}
#endif
	
	#if 0
	if(VochnInfo[index].VdecChn != DetVLoss_VdecChn && VochnInfo[index].VdecChn != NoConfig_VdecChn)
	{
		switch (s_astVoDevStatDflt[VoDev].enPreviewStat)
		{
			case PRV_STAT_NORM:
				{
	//#ifdef SN_SLAVE_ON
	                if (DEV_SPOT_NUM > 0 && SPOT_VO_DEV == VoDev) /*SPOT: PCIV !!!*/
	                {
	                    VO_VIDEO_LAYER_ATTR_S stLayerAttr;
	                    CHECK_RET(HI_MPI_VO_GetVideoLayerAttr(VoDev, &stLayerAttr));
	                    pstRect->u32Height = stLayerAttr.stImageSize.u32Height;
	                    pstRect->u32Width = stLayerAttr.stImageSize.u32Width;
	                    pstRect->s32X = 0;
	                    pstRect->s32Y = 0;
	                    RET_SUCCESS("");
	                }
	//#endif                
					if (s_astVoDevStatDflt[VoDev].bIsAlarm && VoChn == s_astVoDevStatDflt[VoDev].s32AlarmChn)
					{
						if(VochnInfo[index].VdecChn >= 0)
						{
							CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
							*pstRect = stVoChnAttr.stRect;
							RET_SUCCESS("");
						}
					}
					else if(s_astVoDevStatDflt[VoDev].bIsSingle)
					{
						if(mode !=SingleScene && s_astVoDevStatDflt[VoDev].s32DoubleIndex == 0 )
							mode = SingleScene;
						if(PRV_CurDecodeMode == PassiveDecode || VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[mode][s_astVoDevStatDflt[VoDev].s32SingleIndex])
						{
							CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
							*pstRect = stVoChnAttr.stRect;
							RET_SUCCESS("");
						}
					}
					
					else
					{					
						CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
						*pstRect = stVoChnAttr.stRect;
						RET_SUCCESS("");
					
					}
					
				}
				break;
			case PRV_STAT_CTRL:
				if (VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn)
				{			
					CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
					*pstRect = stVoChnAttr.stRect;
					RET_SUCCESS("");
				}
				else
				{
					RET_FAILURE("not the ctrl chn!");
				}
				break;
			case PRV_STAT_PB:
				RET_FAILURE("current in pb stat!");
				break;
			case PRV_STAT_PIC:
				RET_FAILURE("current in pic stat!");
				break;
			default:
				RET_FAILURE("unknown preview stat!");
		}
	}
	//由于设置的"无网络视频"或"未配置"图片的显示区域为指定区域的一部分，这里需要
	//获取指定区域大小而不是放图片的区域大小。
	else
	#endif
	{
		HI_S32  i = 0, u32Index = 0;	
		HI_U32 u32ChnNum = 0, u32Width = 0, u32Height = 0;
		RECT_S *pstLayout = NULL;
		PRV_PREVIEW_MODE_E	enPreviewMode;
		HI_U32 Max_num;
		enPreviewMode = s_astVoDevStatDflt[VoDev].enPreviewMode;	
#if defined(SN9234H1)
		u32Index = s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle ? s_astVoDevStatDflt[s_VoDevCtrlDflt].s32SingleIndex : s_astVoDevStatDflt[HD].s32PreviewIndex;
#else
		u32Index = s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle ? s_astVoDevStatDflt[s_VoDevCtrlDflt].s32SingleIndex : s_astVoDevStatDflt[DHD0].s32PreviewIndex;
#endif
		//获取当前预览模式下最大通道数
		CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[VoDev].enPreviewMode, &Max_num));
		
		if((s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL && VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn)
			|| (s_astVoDevStatDflt[VoDev].bIsAlarm && VoChn == s_astVoDevStatDflt[VoDev].s32AlarmChn))
		{
			u32ChnNum = 1;
			pstLayout = s_astPreviewLayout1;
			i = 0;
		}
		else if(s_astVoDevStatDflt[VoDev].bIsSingle )
		{
			if(enPreviewMode != SingleScene && s_astVoDevStatDflt[VoDev].s32DoubleIndex == 0)
				enPreviewMode = SingleScene;
			//printf("enPreviewMode: %d, VoChn: %d, s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][s_astVoDevStatDflt[VoDev].s32SingleIndex]: %d\n", enPreviewMode, VoChn, s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][s_astVoDevStatDflt[VoDev].s32SingleIndex]);
			if(VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][s_astVoDevStatDflt[VoDev].s32SingleIndex])
			{
				u32ChnNum = 1;
				pstLayout = s_astPreviewLayout1;
				i = 0;
			}		
		}
		else if(LayoutToSingleChn != -1)
		{
			u32ChnNum = 1;
			pstLayout = s_astPreviewLayout1;
			i = 0;
		}
		else if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_NORM
				|| pstLayout == NULL)
		{
			switch(enPreviewMode)
			{
				case SingleScene:
					u32ChnNum = 1;
					pstLayout = s_astPreviewLayout1;
					break;
				case TwoScene:
					u32ChnNum = 2;
					pstLayout = s_astPreviewLayout2;
					break;
				case ThreeScene:
					u32ChnNum = 3;
					pstLayout = s_astPreviewLayout3;
					break;
				case FourScene:
				case LinkFourScene:
					u32ChnNum = 4;
					pstLayout = s_astPreviewLayout4;
					break;
				case FiveScene:
					u32ChnNum = 5;
					pstLayout = s_astPreviewLayout5;
					break;
				case SixScene:
					u32ChnNum = 6;
					pstLayout = s_astPreviewLayout6;
					break;
				case SevenScene:
					u32ChnNum = 7;
					pstLayout = s_astPreviewLayout7;
					break;
				case EightScene:
					u32ChnNum = 8;
					pstLayout = s_astPreviewLayout8;
					break;
				case NineScene:
				case LinkNineScene:
					u32ChnNum = 9;
					pstLayout = s_astPreviewLayout9;
					break;
				case SixteenScene:
					u32ChnNum = 16;
					pstLayout = s_astPreviewLayout16;
					break;
				default:
					RET_FAILURE("Invalid Parameter: enPreviewMode");
			}
			if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
			{
				i = VoChn;
			}
			else
			{
				for(i = 0; i < u32ChnNum && u32Index+i < Max_num; i++)
				{
					if(VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][(i + u32Index)%Max_num])
						break;
				}
			}
		
		}
		u32Width  = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
		u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
		if((enPreviewMode == NineScene || enPreviewMode == LinkNineScene || enPreviewMode == ThreeScene || enPreviewMode == FiveScene
		|| enPreviewMode == SevenScene) && s_astVoDevStatDflt[VoDev].bIsAlarm!=1 && s_astVoDevStatDflt[VoDev].bIsSingle!=1)
		{
		//解决9画面预览时相邻两列画面之间存在缝隙的问题
			while(u32Width%6 != 0)
				u32Width++;
			while(u32Height%6 != 0)
				u32Height++;
		}
		if(pstLayout != NULL && i < u32ChnNum)
		{
			stVoChnAttr.stRect.s32X 	 = (u32Width  * pstLayout[i].s32X) / PRV_PREVIEW_LAYOUT_DIV;
			stVoChnAttr.stRect.s32Y 	 = (u32Height * pstLayout[i].s32Y) / PRV_PREVIEW_LAYOUT_DIV;
			stVoChnAttr.stRect.u32Width  = (u32Width  * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
			stVoChnAttr.stRect.u32Height = (u32Height * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;
			if(enPreviewMode == NineScene || enPreviewMode == LinkNineScene)
			{ 
				if((i + 1) % 3 == 0)//最后一列
					stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
				if(i > 5 && i < 9)//最后一行
					stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
			}
		   if( enPreviewMode == ThreeScene )
		   { 
			   if( i == 2)//最后一列
				   stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			//最后一行
			       stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		   }
		   if( enPreviewMode == FiveScene )
		  { 
			 if( i > 1 )//最后一列
				   stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			 if(i==0 || i==1 || i==4)//最后一行
				   stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		  }
		  if(enPreviewMode == SevenScene)
		  { 
			if(i==2 || i==4 || i==6)//最后一列
				stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
			if(i==0 || i==5 || i==6)//最后一行
				stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
		 }
			stVoChnAttr.stRect.s32X 	 &= (~0x01);
			stVoChnAttr.stRect.s32Y 	 &= (~0x01);
			stVoChnAttr.stRect.u32Width  &= (~0x01);
			stVoChnAttr.stRect.u32Height &= (~0x01);
		}
		else
		{
			CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
		}
		
		*pstRect = stVoChnAttr.stRect;
		RET_SUCCESS("");
	}
	RET_FAILURE("????");
}
/*************************************************
Function: //PRV_GetVoChnRect_Forxy
Description://  获取VO通道的大小位置（数值相对所在VO设备的显示大小）。
			末在显示状态的VO通道返回HI_FAILURE。
Calls: 
Called By: //
Input: //VoDev:设备号
		VoChn:通道号
		pstRect:保存返回的通道位置
Output: // pstSize: 返回图像大小
Return: //成功返回HI_SUCCESS
		失败返回HI_FAILURE
Others: // 其它说明
************************************************************************/
static HI_S32 PRV_GetVoChnRect_Forxy(VO_DEV VoDev, VO_CHN VoChn, RECT_S *pstRect)
{
	VO_CHN_ATTR_S stVoChnAttr;
	HI_S32 index = 0;
	if (NULL == pstRect)
	{
		RET_FAILURE("NULL pointer!");
	}
#if defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (VoChn<0 || VoChn>=PRV_VO_CHN_NUM || VoDev<0 || VoDev>=PRV_VO_MAX_DEV)
	{
		RET_FAILURE("bad parameter: VoDev or VoChn");
	}
#else
	if(VoDev != DHD0)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (VoChn<0 || VoChn>=PRV_VO_CHN_NUM || VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
	{
		RET_FAILURE("bad parameter: VoDev or VoChn");
	}
#endif	
	index = PRV_GetVoChnIndex(VoChn);
	if(index < 0)
		RET_FAILURE("------ERR: Invalid Index!");
	#if 0
	if(VochnInfo[index].VdecChn != DetVLoss_VdecChn && VochnInfo[index].VdecChn != NoConfig_VdecChn)
	{
		CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
		*pstRect = stVoChnAttr.stRect;
	}
	else
	#endif
	{
		HI_S32  i = 0, u32Index = 0;	
		HI_U32 u32ChnNum = 0, u32Width = 0, u32Height = 0;
		RECT_S *pstLayout = NULL;
		PRV_PREVIEW_MODE_E	enPreviewMode;
		HI_U32 Max_num;
		enPreviewMode = s_astVoDevStatDflt[VoDev].enPreviewMode;	
#if defined(SN9234H1)
		u32Index = s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle ? s_astVoDevStatDflt[s_VoDevCtrlDflt].s32SingleIndex : s_astVoDevStatDflt[HD].s32PreviewIndex;
#else
		u32Index = s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle ? s_astVoDevStatDflt[s_VoDevCtrlDflt].s32SingleIndex : s_astVoDevStatDflt[DHD0].s32PreviewIndex;
#endif
		//获取当前预览模式下最大通道数
		CHECK_RET(PRV_Get_Max_chnnum(s_astVoDevStatDflt[VoDev].enPreviewMode, &Max_num));
		
		if((s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_CTRL && VoChn == s_astVoDevStatDflt[VoDev].s32CtrlChn)
			|| (s_astVoDevStatDflt[VoDev].bIsAlarm && VoChn == s_astVoDevStatDflt[VoDev].s32AlarmChn))
		{
			u32ChnNum = 1;
			pstLayout = s_astPreviewLayout1;
			i = 0;
		}

		else if(s_astVoDevStatDflt[VoDev].bIsSingle)
		{
			if((enPreviewMode != SingleScene) && s_astVoDevStatDflt[VoDev].s32DoubleIndex == 0 )
				enPreviewMode = SingleScene;
			if(VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][s_astVoDevStatDflt[VoDev].s32SingleIndex])
			{
				u32ChnNum = 1;
				pstLayout = s_astPreviewLayout1;
				i = 0;				
			}
			if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
			{
				u32ChnNum = 1;
				pstLayout = s_astPreviewLayout1;
				i = 0;
			}
		}
		else if(LayoutToSingleChn == VoChn)
		{
			u32ChnNum = 1;
			pstLayout = s_astPreviewLayout1;
			i = 0;

		}
		else if(s_astVoDevStatDflt[VoDev].enPreviewStat == PRV_STAT_NORM
			|| pstLayout == NULL)
		{
			switch(enPreviewMode)
			{
				case SingleScene:
					u32ChnNum = 1;
					pstLayout = s_astPreviewLayout1;
					break;
				case TwoScene:
					u32ChnNum = 2;
					pstLayout = s_astPreviewLayout2;
					break;
				case ThreeScene:
					u32ChnNum = 3;
					pstLayout = s_astPreviewLayout3;
					break;
				case FourScene:
				case LinkFourScene:
					u32ChnNum = 4;
					pstLayout = s_astPreviewLayout4;
					break;
				case FiveScene:
					u32ChnNum = 5;
					pstLayout = s_astPreviewLayout5;
					break;
				case SixScene:
					u32ChnNum = 6;
					pstLayout = s_astPreviewLayout6;
					break;
				case SevenScene:
					u32ChnNum = 7;
					pstLayout = s_astPreviewLayout7;
					break;
				case EightScene:
					u32ChnNum = 8;
					pstLayout = s_astPreviewLayout8;
					break;
				case NineScene:
				case LinkNineScene:
					u32ChnNum = 9;
					pstLayout = s_astPreviewLayout9;
					break;
				case SixteenScene:
					u32ChnNum = 16;
					pstLayout = s_astPreviewLayout16;
					break;
				default:
					RET_FAILURE("Invalid Parameter: enPreviewMode");
			}
			for(i = 0; i < u32ChnNum && u32Index+i < Max_num; i++)
			{
				if(VoChn == s_astVoDevStatDflt[VoDev].as32ChnOrder[enPreviewMode][(i + u32Index)%Max_num])
					break;
			}
			if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
			{
				i = VoChn;
			}

		}
		u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
		u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
		if((enPreviewMode == NineScene || enPreviewMode == LinkNineScene || enPreviewMode == ThreeScene || enPreviewMode == FiveScene
		|| enPreviewMode == SevenScene) && s_astVoDevStatDflt[VoDev].bIsAlarm!=1 && s_astVoDevStatDflt[VoDev].bIsSingle!=1)
		{
		//解决9画面预览时相邻两列画面之间存在缝隙的问题
			while(u32Width%6 != 0)
				u32Width++;
			while(u32Height%6 != 0)
				u32Height++;
		}
		if(pstLayout != NULL && i < u32ChnNum)
		{
			stVoChnAttr.stRect.s32X 	 = (u32Width * pstLayout[i].s32X) / PRV_PREVIEW_LAYOUT_DIV;
			stVoChnAttr.stRect.s32Y 	 = (u32Height * pstLayout[i].s32Y) / PRV_PREVIEW_LAYOUT_DIV;
			stVoChnAttr.stRect.u32Width  = (u32Width * pstLayout[i].u32Width) / PRV_PREVIEW_LAYOUT_DIV;
			stVoChnAttr.stRect.u32Height = (u32Height * pstLayout[i].u32Height) / PRV_PREVIEW_LAYOUT_DIV;
			if(enPreviewMode == NineScene || enPreviewMode == LinkNineScene)
			{ 
				if((i + 1) % 3 == 0)//最后一列
					stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
				if(i > 5 && i < 9)//最后一行
					stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
			}
			
			if( enPreviewMode == ThreeScene )
			{ 
			    if( i == 2)//最后一列
				     stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
						//最后一行
					 stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
			}
			if( enPreviewMode == FiveScene )
			{ 
			    if( i > 1 )//最后一列
				      stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
				if(i==0 || i==1 || i==4)//最后一行
				      stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
			}
			if(enPreviewMode == SevenScene)
			{ 
			    if(i==2 || i==4 || i==6)//最后一列
				      stVoChnAttr.stRect.u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width - stVoChnAttr.stRect.s32X;
				if(i==0 || i==5 || i==6)//最后一行
				      stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height - stVoChnAttr.stRect.s32Y;
			}
			stVoChnAttr.stRect.s32X &= (~0x01);
			stVoChnAttr.stRect.s32Y &= (~0x01);
			stVoChnAttr.stRect.u32Width &= (~0x01);
			stVoChnAttr.stRect.u32Height &= (~0x01);
		}
		else
		{
			CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stVoChnAttr));
		}
		*pstRect = stVoChnAttr.stRect;

	}
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_GetVoDevDispSize
Description://  获取VO设备显示大小。
Calls: 
Called By: //
Input: //VoDev:设备号
Output: // pstSize: 返回显示大小
Return: //成功返回HI_SUCCESS
		失败返回HI_FAILURE
Others: // 其它说明
************************************************************************/
HI_S32 PRV_GetVoDevDispSize(VO_DEV VoDev, SIZE_S *pstSize)
{
	if (NULL == pstSize)
	{
		RET_FAILURE("NULL pointer!");
	}
#if defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (VoDev<0 || VoDev>=PRV_VO_MAX_DEV)
	{
		RET_FAILURE("bad parameter: VoDev");
	}
#else
	if(VoDev > DHD0)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
	{
		RET_FAILURE("bad parameter: VoDev");
	}
#endif

	pstSize->u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height;
	pstSize->u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width;
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_GetVoDevImgSize
Description://  获取VO设备图像大小。
Calls: 
Called By: //
Input: //VoDev:设备号
Output: // pstSize: 返回图像大小
Return: //成功返回HI_SUCCESS
		失败返回HI_FAILURE
Others: // 其它说明
************************************************************************/
HI_S32 PRV_GetVoDevImgSize(VO_DEV VoDev, SIZE_S *pstSize)
{
	if (NULL == pstSize)
	{
		RET_FAILURE("NULL pointer!");
	}
#if defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV|| VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (VoDev<0 || VoDev>=PRV_VO_MAX_DEV)
	{
		RET_FAILURE("bad parameter: VoDev");
	}
#else
	if(VoDev > DHD0)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
	{
		RET_FAILURE("bad parameter: VoDev");
	}
#endif
	pstSize->u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
	pstSize->u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
	RET_SUCCESS("");
}
/*************************************************
Function: //PRV_GetVoChnDispRect
Description://  获取VO通道的大小位置（数值相对所在VO设备的分辨率大小）。
			末在显示状态的VO通道返回HI_FAILURE。
Calls: 
Called By: //
Input: //VoDev:设备号
		VoChn:通道号
		pstRect:保存返回的通道位置
Output: // pstSize: 返回图像大小
Return: //成功返回HI_SUCCESS
		失败返回HI_FAILURE
Others: // 其它说明
************************************************************************/

HI_S32 PRV_GetVoChnDispRect(VO_DEV VoDev, VO_CHN VoChn, RECT_S *pstRect)
{
	RECT_S rect;
	SIZE_S disp_size,img_size;
	
	CHECK_RET(PRV_GetVoChnRect(VoDev,VoChn,&rect));
	CHECK_RET(PRV_GetVoDevDispSize(VoDev,&disp_size));
	CHECK_RET(PRV_GetVoDevImgSize(VoDev,&img_size));
	
	pstRect->s32X = rect.s32X*disp_size.u32Width/img_size.u32Width;
	pstRect->s32Y = rect.s32Y*disp_size.u32Height/img_size.u32Height;
	pstRect->u32Height= rect.u32Height*disp_size.u32Height/img_size.u32Height;
	pstRect->u32Width= rect.u32Width*disp_size.u32Width/img_size.u32Width;
	
	RET_SUCCESS("");
}
/*************************************************
Function: //PRV_GetVoChnDispRect
Description://  获取VO通道的大小位置（数值相对所在VO设备的分辨率大小）。
			末在显示状态的VO通道返回HI_FAILURE。
Calls: 
Called By: //
Input: //VoDev:设备号
		VoChn:通道号
		pstRect:保存返回的通道位置
Output: // pstSize: 返回图像大小
Return: //成功返回HI_SUCCESS
		失败返回HI_FAILURE
Others: // 其它说明
************************************************************************/

HI_S32 PRV_GetVoChnDispRect_Forxy(VO_DEV VoDev, VO_CHN VoChn, RECT_S *pstRect)
{
	RECT_S rect;
	SIZE_S disp_size,img_size;	
	
	CHECK_RET(PRV_GetVoChnRect_Forxy(VoDev,VoChn,&rect));
	CHECK_RET(PRV_GetVoDevDispSize(VoDev,&disp_size));
	CHECK_RET(PRV_GetVoDevImgSize(VoDev,&img_size));
	
	pstRect->s32X = rect.s32X*disp_size.u32Width/img_size.u32Width;
	pstRect->s32Y = rect.s32Y*disp_size.u32Height/img_size.u32Height;
	pstRect->u32Height= rect.u32Height*disp_size.u32Height/img_size.u32Height;
	pstRect->u32Width= rect.u32Width*disp_size.u32Width/img_size.u32Width;
	RET_SUCCESS("");
}

/************************************************************************/
/* 进入回放。
                                                                     */
/************************************************************************/
STATIC HI_S32 PRV_EnterPB(VO_DEV dev, HI_S32 s32Flag)
{
	int flag = 0,ret = 0;
	int i = 0;
	//AUDIO_DEV AoDev = 0;
	//AO_CHN AoChn = 0;
#if defined(SN9234H1)
	if(dev == SPOT_VO_DEV|| dev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (dev < 0 || dev >= PRV_VO_MAX_DEV)
	{
		RET_FAILURE("invalid dev!!");
	}
#else
	if(dev > DHD0)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (dev < 0 || dev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
	{
		RET_FAILURE("invalid dev!!");
	}
#endif
	if (PRV_STAT_NORM != s_astVoDevStatDflt[dev].enPreviewStat)
	{
		RET_FAILURE("current stat already in ctrl or pb !!");
	}

	ret = PRV_TkOrPb(&flag);
	if(ret == HI_FAILURE)
	{
		RET_FAILURE("PONIT null!!");
	}
	if(s32Flag == PIC_CTL_FLAG)
	{
		s_astVoDevStatDflt[dev].enCtrlFlag = PRV_CTRL_PB;
		s_astVoDevStatDflt[dev].enPreviewStat = PRV_STAT_PIC;
	}
	else
	{
		if(flag != PRV_STATE)
		{//如果当前不是空闲状态。那么返回失败
			RET_FAILURE("current stat already in pb or voa stat!!");
		}
		s_astVoDevStatDflt[dev].enCtrlFlag = PRV_CTRL_PB;
		s_astVoDevStatDflt[dev].enPreviewStat = PRV_STAT_PB;

		PRV_TkPbSwitch(1);
		//PRV_DisableAudioPreview();		
		//HI_MPI_AO_UnBindAdec(AoDev, AoChn, DecAdec);
	}
	//PRV_VdecUnBindAllVoChn1(dev);


	for(i = LOCALVEDIONUM; i < DEV_CHANNEL_NUM; i++)
	{
#if defined(SN9234H1)
		if(VochnInfo[i].SlaveId == 0 && VochnInfo[i].VdecChn >= 0 && VochnInfo[i].VdecChn != DetVLoss_VdecChn)
		{
			if(HI_SUCCESS == PRV_WaitDestroyVdecChn(VochnInfo[i].VdecChn))	
			{
				PRV_VoChnStateInit(VochnInfo[i].CurChnIndex);
				PRV_PtsInfoInit(VochnInfo[i].CurChnIndex);		
			}
			PRV_InitVochnInfo(i);	
		}		
#else
		if(VochnInfo[i].SlaveId == 0 && VochnInfo[i].VdecChn >= 0
			&& VochnInfo[i].VdecChn != DetVLoss_VdecChn && VochnInfo[i].VdecChn != NoConfig_VdecChn)
		{
			if(HI_SUCCESS == PRV_WaitDestroyVdecChn(VochnInfo[i].VdecChn))	
			{
				VochnInfo[i].IsHaveVdec = 0;	
			}
			PRV_InitVochnInfo(i);
		}
#endif		
		BufferSet(i + PRV_VIDEOBUFFER, MAX_ARRAY_NODE); 			
		BufferSet(i + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
		PRV_VoChnStateInit(i);
		
	}
#if defined(Hi3531)||defined(Hi3535)	
	if(PRV_CurIndex > 0)
	{
		for(i = 0; i < PRV_CurIndex; i++)
		{
			NTRANS_FreeMediaData(PRV_OldVideoData[i]);
			PRV_OldVideoData[i] = NULL;
		}
		PRV_CurIndex = 0;
		PRV_SendDataLen = 0;
	}
#endif
	PRV_RefreshVoDevScreen(dev, DISP_DOUBLE_DISP, s_astVoDevStatDflt[dev].as32ChnOrder[s_astVoDevStatDflt[dev].enPreviewMode]);
	
	if(s_State_Info.bIsOsd_Init && s_State_Info.bIsRe_Init)
	{
#if defined(SN9234H1)
		Prv_OSD_Show(HD, HI_FALSE);
		Prv_OSD_Show(AD, HI_FALSE);
#else /*2010-9-19 双屏！*/
		Prv_OSD_Show(DHD0, HI_FALSE);
		//Prv_OSD_Show(DSD0, HI_FALSE);
#endif
	}
	PRV_DisableDigChnAudio();
	CurCap = 0;
	CurMasterCap = 0;
	CurSlaveCap = 0;
	RET_SUCCESS("");
}

/************************************************************************/
/* 退出回放。
                                                                     */
/************************************************************************/
STATIC HI_S32 PRV_ExitPB(VO_DEV dev,HI_S32 s32Flag)
{
	int i = 0;
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif
#if defined(SN9234H1)
	if(dev == SPOT_VO_DEV|| dev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (dev<0 || dev>=PRV_VO_MAX_DEV)
	{
		RET_FAILURE("invalid dev!!");
	}
#else
	if(dev > DHD0)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (dev < 0 || dev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
	{
		RET_FAILURE("invalid dev!!");
	}
#endif
	if (PRV_CTRL_PB != s_astVoDevStatDflt[dev].enCtrlFlag
		|| (PRV_STAT_PB != s_astVoDevStatDflt[dev].enPreviewStat
		&& PRV_STAT_PIC != s_astVoDevStatDflt[dev].enPreviewStat))
	{
		RET_FAILURE("Ctrl Flag NOT PB or Preview stat not in PB and not in pic stat!");
	}
#if defined(SN9234H1)
	s_astVoDevStatDflt[dev].enPreviewStat = PRV_STAT_NORM;

	PRV_EnableAllVoChn(HD);
	PRV_HideAllVoChn(HD);
	//PRV_BindAllVoChn(HD);

	//PRV_EnableAllVoChn(s_VoSecondDev);
	//PRV_HideAllVoChn(s_VoSecondDev);
	//PRV_BindAllVoChn(AD);
#else
	for(i = LOCALVEDIONUM; i < DEV_CHANNEL_NUM; i++)
	{
		if(VochnInfo[i].IsConnect == 1 && //未断开设备连接。在回放期间，有可能断开连接
			VochnInfo[i].SlaveId == PRV_MASTER && 
			VochnInfo[i].VdecChn >= 0
			&& VochnInfo[i].VdecChn != DetVLoss_VdecChn && VochnInfo[i].VdecChn != NoConfig_VdecChn)
		{		
			if(HI_SUCCESS == PRV_CreateVdecChn(VochnInfo[i].VideoInfo.vdoType, VochnInfo[i].VideoInfo.height, VochnInfo[i].VideoInfo.width, VochnInfo[i].u32RefFrameNum, VochnInfo[i].VdecChn))
			{
				VochnInfo[i].IsHaveVdec = 1;
			}
			else
			{				
				TRACE(SCI_TRACE_HIGH, MOD_PRV,"-------line: %d, Create Vdec: %d fail!!!\n", __LINE__, VochnInfo[i].VdecChn);
			}
		}
	}
	s_astVoDevStatDflt[dev].enPreviewStat = PRV_STAT_NORM;

	PRV_EnableAllVoChn(DHD0);
	PRV_HideAllVoChn(DHD0);
	//PRV_BindAllVoChn(HD);
	CHECK_RET(HI_MPI_VO_SetPlayToleration (DHD0, 200));
#if defined(Hi3531)
	PRV_EnableAllVoChn(s_VoSecondDev);
	PRV_HideAllVoChn(s_VoSecondDev);
#endif
	//PRV_BindAllVoChn(AD);
#endif
	if(s32Flag != PIC_CTL_FLAG)
	{
		PRV_TkPbSwitch(0);
		//PRV_EnableAudioPreview();
	}

	AIO_ATTR_S stmp;
#if defined(SN9234H1)	
	CHECK(HI_MPI_AO_GetPubAttr(AoDev, &stmp));
#else
	CHECK(HI_MPI_AO_GetPubAttr(AoDev, &stmp));
#endif
	PtNumPerFrm = stmp.u32PtNumPerFrm;
	for(i = 0; i < DEV_CHANNEL_NUM; i++)
	{
		PRV_PBStateInfoInit(i);
		PRV_PtsInfoInit(i);
		PRV_InitVochnInfo(i);
		PRV_VoChnStateInit(i);
	}
	if(PRV_CurIndex > 0)
	{
		for(i = 0; i < PRV_CurIndex; i++)
		{
			NTRANS_FreeMediaData(PRV_OldVideoData[i]);
			PRV_OldVideoData[i] = NULL;
		}
		PRV_CurIndex = 0;
		PRV_SendDataLen = 0;
	}
	
	PRV_RefreshVoDevScreen(dev, DISP_DOUBLE_DISP,s_astVoDevStatDflt[dev].as32ChnOrder[s_astVoDevStatDflt[dev].enPreviewMode]);


	if(s_State_Info.bIsOsd_Init && s_State_Info.bIsRe_Init)
	{
#if defined(SN9234H1)		
		Prv_OSD_Show(HD,HI_TRUE);
		Prv_OSD_Show(AD,HI_TRUE);
#else
		Prv_OSD_Show(DHD0,HI_TRUE);
#endif
	}
	PRV_PBPlayInfoInit();
	PRV_EnableDigChnAudio();
	CurCap = 0;
	CurMasterCap = 0;
	CurSlaveCap = 0;
	sem_post(&sem_PrvGetData); 		
	//sem_post(&sem_PrvSendData); 	

	RET_SUCCESS("");
}

#if defined(SN_SLAVE_ON)
/*************************************************
Function: //PRV_TimeOut_repeat
Description: //超时定时器重新设置程序
Calls: 
Called By: //
Input: // timer:定时器时间，重发间隔为原时间+2
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int PRV_TimeOut_repeat(int timer)
{
	switch(s_State_Info.TimeoutCnt)
	{
		case 0:
		{//启动从片定时，时间为100ms
			if(s_State_Info.f_timer_handle == -1)
			{
				TimeInfo timer_info;
				SN_MEMSET(&timer_info, 0, sizeof(TimeInfo));
				timer_info.type = 1;//定时器返回类型为消息返回
				timer_info.info.MESSAGE = MSG_ID_PRV_DISPLAY_TIMEOUT_IND;//消息返回值为MSG_ID_PRV_DISPLAY_TIMEOUT_IND
				
				s_State_Info.f_timer_handle = TimerAdd(MOD_PRV, timer, timer_info,timer, 0);
			}else
			{	//重置定时器
				TimerReset(s_State_Info.f_timer_handle,timer);
				TimerResume(s_State_Info.f_timer_handle,0);
			}
			s_State_Info.bIsTimerState = HI_TRUE;
			s_State_Info.TimerType = PRV_INIT;
		}
			break;
		case 1:
		{//重新设置定时器，时间为200ms
			TimerReset(s_State_Info.f_timer_handle,timer+2);
			TimerResume(s_State_Info.f_timer_handle,0);
			s_State_Info.bIsTimerState = HI_TRUE;
			s_State_Info.TimerType = PRV_INIT;
		}
			break;
		case 2:
		{//重新设置定时器，时间为300ms
			TimerReset(s_State_Info.f_timer_handle,timer+4);
			TimerResume(s_State_Info.f_timer_handle,0);
			s_State_Info.bIsTimerState = HI_TRUE;
			s_State_Info.TimerType = PRV_INIT;
		}
			break;
		case 3:
		{	//3次超时，那么重启从片
			s_State_Info.bslave_IsInit = HI_TRUE;//如果从片消息都返回了，那么置位从片初始化标志位
			s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
			s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
			s_State_Info.bIsReply = 0;			//回复状态退出
			s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
		}	
			return-1;
		default:
			return -1;
	}
	return 0;
}

/*************************************************
Function: //Prv_Set_Slave_chn_param
Description: //初始化从片参数
Calls: 
Called By: //
Input: //
Output: // 
Return: //
Others: // 其它说明
************************************************************************/
static int Prv_Set_Slave_chn_param(void)
{
//	HI_S32 i=0;
#if defined(SN9234H1)
	s_slaveVoStat.bIsSingle = s_astVoDevStatDflt[HD].bIsSingle;
	s_slaveVoStat.enPreviewMode = s_astVoDevStatDflt[HD].enPreviewMode;
	s_slaveVoStat.s32PreviewIndex = s_astVoDevStatDflt[HD].s32PreviewIndex;
	s_slaveVoStat.s32SingleIndex = s_astVoDevStatDflt[HD].s32SingleIndex;
#else
	s_slaveVoStat.bIsSingle = s_astVoDevStatDflt[DHD0].bIsSingle;
	s_slaveVoStat.enPreviewMode = s_astVoDevStatDflt[DHD0].enPreviewMode;
	s_slaveVoStat.s32PreviewIndex = s_astVoDevStatDflt[DHD0].s32PreviewIndex;
	s_slaveVoStat.s32SingleIndex = s_astVoDevStatDflt[DHD0].s32SingleIndex;
#endif	
	s_slaveVoStat.enVideoNorm = s_s32NPFlagDflt;
	return 0;
}
#endif

/*************************************************
Function: //PRV_Init_TimeOut
Description: //初始化超时处理
Calls: 
Called By: //
Input: // is_first:是否初次调用，0表示初次调用，1表示再次调用
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
#if defined(SN_SLAVE_ON)
static int PRV_Slave_Init(unsigned char is_first)
{
	int i,j;
	Prv_Slave_Init_Req slave_req;
	//发送从片初始化消息
	slave_req.DecodeMode= PRV_CurDecodeMode;	
	slave_req.VoOutPutMode = OutPutMode;
	slave_req.enPreviewMode= s_slaveVoStat.enPreviewMode;
	slave_req.bIsSingle= s_slaveVoStat.bIsSingle;
	slave_req.s32PreviewIndex= s_slaveVoStat.s32PreviewIndex;
	slave_req.s32SingleIndex= s_slaveVoStat.s32SingleIndex;
	slave_req.enVideoNorm = s_slaveVoStat.enVideoNorm;
	SN_MEMCPY(slave_req.slave_OSD_off_flag,PRV_CHAN_NUM, s_slaveVoStat.slave_OSD_off_flag,PRV_CHAN_NUM,PRV_CHAN_NUM);
	//printf("############slave_req.enPreviewMode = %d###########################\n",slave_req.enPreviewMode);
	for(i=0;i<NINEINDEX;i++)
	{
		slave_req.as32ChnOrder[SingleScene][i] = s_astVoDevStatDflt[HD].as32ChnOrder[SingleScene][i];
		slave_req.as32ChnpollOrder[SingleScene][i] = s_astVoDevStatDflt[HD].as32ChnpollOrder[SingleScene][i];
		slave_req.as32ChnOrder[ThreeScene][i] = s_astVoDevStatDflt[HD].as32ChnOrder[ThreeScene][i];
		slave_req.as32ChnpollOrder[ThreeScene][i] = s_astVoDevStatDflt[HD].as32ChnpollOrder[ThreeScene][i];
		slave_req.as32ChnOrder[FourScene][i] = s_astVoDevStatDflt[HD].as32ChnOrder[FourScene][i];
		slave_req.as32ChnpollOrder[FourScene][i] = s_astVoDevStatDflt[HD].as32ChnpollOrder[FourScene][i];
		slave_req.as32ChnOrder[FiveScene][i] = s_astVoDevStatDflt[HD].as32ChnOrder[FiveScene][i];
		slave_req.as32ChnpollOrder[FiveScene][i] = s_astVoDevStatDflt[HD].as32ChnpollOrder[FiveScene][i];
		slave_req.as32ChnOrder[SixScene][i] = s_astVoDevStatDflt[HD].as32ChnOrder[SixScene][i];
		slave_req.as32ChnpollOrder[SixScene][i] = s_astVoDevStatDflt[HD].as32ChnpollOrder[SixScene][i];
		slave_req.as32ChnOrder[SevenScene][i] = s_astVoDevStatDflt[HD].as32ChnOrder[SevenScene][i];
		slave_req.as32ChnpollOrder[SevenScene][i] = s_astVoDevStatDflt[HD].as32ChnpollOrder[SevenScene][i];
		slave_req.as32ChnOrder[EightScene][i] = s_astVoDevStatDflt[HD].as32ChnOrder[EightScene][i];
		slave_req.as32ChnpollOrder[EightScene][i] = s_astVoDevStatDflt[HD].as32ChnpollOrder[EightScene][i];
		slave_req.as32ChnOrder[NineScene][i] = s_astVoDevStatDflt[HD].as32ChnOrder[NineScene][i];
		slave_req.as32ChnpollOrder[NineScene][i] = s_astVoDevStatDflt[HD].as32ChnpollOrder[NineScene][i];
		slave_req.as32ChnOrder[SixteenScene][i] = s_astVoDevStatDflt[HD].as32ChnOrder[SixteenScene][i];
		slave_req.as32ChnpollOrder[SixteenScene][i] = s_astVoDevStatDflt[HD].as32ChnpollOrder[SixteenScene][i];
		
		if(i<PRV_CHAN_NUM)
		{
			for(j=0;j<REC_OSD_GROUP;j++)
			{
				slave_req.slave_Chn_Bmp_h[j][i] = s_slaveVoStat.slave_BmpData_name_w[j][i];
				slave_req.slave_Chn_Bmp_w[j][i] = s_slaveVoStat.slave_BmpData_name_h[j][i];
				slave_req.slave_Chn_Bmp_DSize[j][i] = s_slaveVoStat.slave_BmpData_name_size[j][i];	
				slave_req.f_rec_srceen_h[j][i] = s_slaveVoStat.f_rec_srceen_h[j][i];
				slave_req.f_rec_srceen_w[j][i] = s_slaveVoStat.f_rec_srceen_w[j][i];
			}
		}
	}
	for(i=0;i<THREEINDEX;i++)
	{
        slave_req.as32ChnOrder[ThreeScene][i] = s_astVoDevStatDflt[HD].as32ChnOrder[ThreeScene][i];
		slave_req.as32ChnpollOrder[ThreeScene][i] = s_astVoDevStatDflt[HD].as32ChnpollOrder[ThreeScene][i];
	}
	for(i=0;i<FIVEINDEX;i++)
	{
        slave_req.as32ChnOrder[FiveScene][i] = s_astVoDevStatDflt[HD].as32ChnOrder[FiveScene][i];
		slave_req.as32ChnpollOrder[FiveScene][i] = s_astVoDevStatDflt[HD].as32ChnpollOrder[FiveScene][i];
	}
	for(i=0;i<SEVENINDEX;i++)
	{
        slave_req.as32ChnOrder[SevenScene][i] = s_astVoDevStatDflt[HD].as32ChnOrder[SevenScene][i];
		slave_req.as32ChnpollOrder[SevenScene][i] = s_astVoDevStatDflt[HD].as32ChnpollOrder[SevenScene][i];
	}
	
	SN_MEMCPY(slave_req.cover_info,PRV_CHAN_NUM*sizeof(PRM_AREA_HIDE), s_slaveVoStat.cover_info,PRV_CHAN_NUM*sizeof(PRM_AREA_HIDE),PRV_CHAN_NUM*sizeof(PRM_AREA_HIDE));
	SN_MEMCPY(slave_req.slave_OSD_Rect,PRV_CHAN_NUM*REGION_NUM*sizeof(Preview_Point),s_slaveVoStat.slave_OSD_Rect,PRV_CHAN_NUM*REGION_NUM*sizeof(Preview_Point),PRV_CHAN_NUM*REGION_NUM*sizeof(Preview_Point));

	Slave_Get_Time_InitInfo(slave_req.slave_osd_time,MAX_TIME_STR_LEN);//获取时间图片初始化信息
	SN_SendMccMessageEx(PRV_SLAVE_1,SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_SLAVE_INIT_REQ, &slave_req, sizeof(Prv_Slave_Init_Req));
	s_State_Info.bIsSlaveConfig = HI_TRUE;	//置位配置标志位

	return 0;
}
#endif
static int PRV_Init_TimeOut(unsigned char is_first)
{
	
#if defined(SN_SLAVE_ON)	
	int ret=0;
	

	if(s_State_Info.bslave_IsInit == HI_FALSE)
	{
	//如果从片没有初始化，发送初始化信息
		if(is_first == 0)
		{ 
			//如果是初次初始化，那么需要读取配置信息
			Prv_Set_Slave_chn_param();
		}
		//printf("###############s  PRV_Init_TimeOut  !!!! s_State_Info.TimeoutCnt = %d##########################\n",s_State_Info.TimeoutCnt);
		ret = PRV_TimeOut_repeat(5);
		if(ret == -1)
		{//重启从片
			
		}
		else
		{
			//发送从片初始化消息
			PRV_Slave_Init(is_first);
		}
	}
#endif	
	return 0;
}


/*************************************************
Function: //PRV_MSG_Mcc_Init_Rsp
Description: // 收到从片返回初始化 消息处理
Calls: 
Called By: //
Input: // msg_req :收到的从片消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_MSG_Mcc_Init_Rsp(const SN_MSG *msg_req)
{
	Prv_Slave_Init_Rsp *rsp = (Prv_Slave_Init_Rsp *)msg_req->para;
				
	//如果从片返回，那么置位从片的返回标志位
	s_State_Info.bIsReply |= 1<<(rsp->slaveid-1);
	if(rsp->result == SN_RET_OK)
	{
		s_State_Info.g_slave_OK = 1<<(rsp->slaveid-1);
	}
	else
	{//如果从片返回错误消息。那么错误处理
		PRV_Init_TimeOut(1);
	}
	//如果返回值加1 等于1左移芯片个数的数值，那么表示所有从片都返回了
	if((s_State_Info.bIsReply+1) == 1<<(PRV_CHIP_NUM-1))	
	{
		s_State_Info.bslave_IsInit = HI_TRUE;//如果从片消息都返回了，那么置位从片初始化标志位
		s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
		s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
		TimerPause(s_State_Info.f_timer_handle); //暂停定时器
		s_State_Info.bIsReply = 0;			//回复状态退出
		s_State_Info.g_slave_OK = 0;
	}

	RET_SUCCESS("");
}

#if defined(SN9234H1)
/************************************************************************/
/* 设置通道显示与隐藏时，将通道顺序重排列。
                                                                     */
/************************************************************************/
STATIC HI_VOID PRV_SortChnOrder(VO_DEV VoDev)
{
#if 1
	/*海康方案*/
	return;
#else
	/*顺序重排列*/
	int i, j;
#if defined(SN8608D_LE) || defined(SN8608M_LE) || defined(SN8616D_LE) ||defined(SN8616M_LE) || defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
#endif

	if (VoDev<0 || VoDev>=PRV_VO_MAX_DEV)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, TEXT_COLOR_RED("%s:%d: bad devid: VoDev=%d\n"), __FUNCTION__, __LINE__, VoDev);
		return;
	}

	for (i=0, j=0; i<g_Max_Vo_Num; i++)
	{
		if (s_astVoDevStatDflt[VoDev].as32ChnOrder[i] < 0
			|| s_astVoDevStatDflt[VoDev].as32ChnOrder[i] >= g_Max_Vo_Num)
		{
			continue;
		}
		else
		{
			s_astVoDevStatDflt[VoDev].as32ChnOrder[j++] = s_astVoDevStatDflt[VoDev].as32ChnOrder[i];
		}
	}
	while (j<g_Max_Vo_Num)
	{
		s_astVoDevStatDflt[VoDev].as32ChnOrder[j++] = -1;
	}
#endif
}



HI_S32 PRV_RefreshSpotOsd(int chan)
{

	unsigned char order_buf[PRV_VO_CHN_NUM];
	order_buf[0] = chan;
	OSD_Get_Preview_param(SPOT_VO_DEV,
							s_astVoDevStatDflt[SPOT_VO_DEV].stVideoLayerAttr.stDispRect.u32Width,
							s_astVoDevStatDflt[SPOT_VO_DEV].stVideoLayerAttr.stDispRect.u32Height,
							1,SingleScene,order_buf);
	if((s_State_Info.bIsOsd_Init && s_State_Info.bIsRe_Init))
	{
		if(SPOT_VO_DEV == s_VoSecondDev)
		{
			Prv_Disp_OSD(AD);
		}
		else
		{
			Prv_Disp_OSD(SPOT_VO_DEV);
		}
	}
	return HI_SUCCESS;
}

HI_S32 PRV_HostStopPciv(PCIV_CHN PcivChn, int EventId)
{
	HI_S32  i;
	HI_U32 u32Count;
	PCIV_CHN  ch = PcivChn;
	PCIV_ATTR_S  stPciAttr;
	PCIV_BIND_OBJ_S astBindObj[PCIV_MAX_BINDOBJ];

	TRACE(SCI_TRACE_NORMAL, MOD_PRV,  "PRV_HostStopPciv: chan=%d\n", PcivChn);
	//printf("PRV_HostStopPciv: chan=%d\n", PcivChn);

	/* 1, stop */
	CHECK_RET(HI_MPI_PCIV_Stop(PcivChn));

	/* 2, unbind */
	CHECK_RET(HI_MPI_PCIV_EnumBindObj(PcivChn, astBindObj, &u32Count));
	for (i=0; i<u32Count; i++)
	{
		CHECK_RET(HI_MPI_PCIV_UnBind(PcivChn, &astBindObj[i]));
	}

	/* 3, free */
	CHECK_RET(HI_MPI_PCIV_GetAttr(PcivChn, &stPciAttr));
	CHECK_RET(HI_MPI_PCIV_Free(stPciAttr.u32Count, stPciAttr.u32PhyAddr));

	/* 4, destroy */
	CHECK_RET(HI_MPI_PCIV_Destroy(PcivChn));

	SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, EventId, &ch, sizeof(PCIV_CHN));
    	return HI_SUCCESS;
}




/*************************************************
Function: //PRV_InitSpotVo
Description: 初始化SPOT口的 VO。
Calls: 
Called By: //
Input: //
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_InitSpotVo(void)
{
	HI_U32 u32Width, u32Height;
	VO_CHN_ATTR_S stVoChnAttr;
	RECT_S *pstLayout = NULL;
	VO_DEV VoDev = SPOT_VO_DEV;


	u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
	u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
	pstLayout = s_astPreviewLayout1;

	PRV_HideAllVoChn(VoDev);
	PRV_ViUnBindAllVoChn(VoDev);
	PRV_VdecUnBindAllVoChn1(VoDev);

	CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, SPOT_VO_CHAN, &stVoChnAttr));

	//stVoChnAttr.u32Priority = PRV_DFLT_CHN_PRIORITY + 1;
	stVoChnAttr.stRect.s32X		= (u32Width * pstLayout->s32X) / PRV_PREVIEW_LAYOUT_DIV;
	stVoChnAttr.stRect.s32Y		= (u32Height * pstLayout->s32Y) / PRV_PREVIEW_LAYOUT_DIV;
	stVoChnAttr.stRect.u32Width	= (u32Width * pstLayout->u32Width) / PRV_PREVIEW_LAYOUT_DIV;
	stVoChnAttr.stRect.u32Height	= (u32Height * pstLayout->u32Height) / PRV_PREVIEW_LAYOUT_DIV;

	stVoChnAttr.stRect.s32X /= 2;
	stVoChnAttr.stRect.s32Y /= 2;
	stVoChnAttr.stRect.u32Width /= 2;
	stVoChnAttr.stRect.u32Height /= 2;
	stVoChnAttr.stRect.s32X *= 2;
	stVoChnAttr.stRect.s32Y *= 2;
	stVoChnAttr.stRect.u32Width *= 2;
	stVoChnAttr.stRect.u32Height *= 2;

	CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, SPOT_VO_CHAN, &stVoChnAttr));
	CHECK_RET(HI_MPI_VO_ChnShow(VoDev,SPOT_VO_CHAN));


	RET_SUCCESS("");
}


/*************************************************
Function: //PRV_PrevInitSpotVo
Description: 根据指定VO状态显示VO设备上的视频层的画面排布。
Calls: 
Called By: //
Input: //VoDev:设备号
		enPreviewMode:需要显示预览模式
		u32Index:多画面通道索引号
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_PrevInitSpotVo( HI_U32 u32Index)
{
	HI_U32 u32Width, u32Height;
	VO_CHN VoChn;
	VO_CHN_ATTR_S stVoChnAttr;
	RECT_S *pstLayout = NULL;
	VO_DEV VoDev = SPOT_VO_DEV;
	int index = 0;
	RECT_S stSrcRect, stDestRect;

	/*确保参数的合法性*/

	if ( (VoDev < 0 || VoDev >= PRV_VO_MAX_DEV))
	{
		RET_FAILURE("Invalid Parameter: VoDev or u32Index");
	}

	//TRACE(SCI_TRACE_NORMAL, MOD_PRV,  "PRV_PrevInitSpotVo = %d\n", u32Index);
	//printf("PRV_PrevInitSpotVo = %d\n", u32Index);
	u32Width = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width;
	u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height;
	pstLayout = s_astPreviewLayout1;

	PRV_HideAllVoChn(VoDev);
	PRV_ViUnBindAllVoChn(VoDev);
	//PRV_VdecUnBindAllVoChn(VoDev);
	
	VoChn = u32Index;
	index = PRV_GetVoChnIndex(VoChn);

	CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, SPOT_VO_CHAN, &stVoChnAttr));

	//stVoChnAttr.u32Priority = PRV_DFLT_CHN_PRIORITY + 1;
	stVoChnAttr.stRect.s32X		= (u32Width * pstLayout->s32X) / PRV_PREVIEW_LAYOUT_DIV;
	stVoChnAttr.stRect.s32Y		= (u32Height * pstLayout->s32Y) / PRV_PREVIEW_LAYOUT_DIV;
	stVoChnAttr.stRect.u32Width	= (u32Width * pstLayout->u32Width) / PRV_PREVIEW_LAYOUT_DIV;
	stVoChnAttr.stRect.u32Height	= (u32Height * pstLayout->u32Height) / PRV_PREVIEW_LAYOUT_DIV;

	stSrcRect.s32X		 = stVoChnAttr.stRect.s32X;
	stSrcRect.s32Y		 = stVoChnAttr.stRect.s32Y;
	stSrcRect.u32Width	 = stVoChnAttr.stRect.u32Width;
	stSrcRect.u32Height  = stVoChnAttr.stRect.u32Height;	
	stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
	stVoChnAttr.stRect.s32X 	 = stDestRect.s32X 		& (~0x01);
	stVoChnAttr.stRect.s32Y 	 = stDestRect.s32Y 		& (~0x01);
	stVoChnAttr.stRect.u32Width  = stDestRect.u32Width  & (~0x01);
	stVoChnAttr.stRect.u32Height = stDestRect.u32Height & (~0x01);


	CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, SPOT_VO_CHAN, &stVoChnAttr));

	//if(ViChn < PRV_CHAN_NUM)
	if(VoChn < LOCALVEDIONUM)
	{
		int videv = 0;
		if(VoChn >= PRV_VI_CHN_NUM)
		{
		//如果通道为5到8，那么对应采集设备2
			videv = PRV_656_DEV;
		}
		else
		{
			videv = PRV_656_DEV_1;
		}
		CHECK_RET(HI_MPI_VI_BindOutput(videv, VoChn%PRV_VI_CHN_NUM, VoDev, SPOT_VO_CHAN));
	}
	else if(VoChn >= LOCALVEDIONUM || VoChn < PRV_CHAN_NUM)
	{
		int index = PRV_GetVoChnIndex(VoChn);
		if(index < 0)
			return HI_FAILURE;
		if(VochnInfo[index].VdecChn >= 0 /*&& VochnInfo[index].IsBindVdec[VoDev] == 0*/)
		{
			if(VochnInfo[index].SlaveId == 0 )
			{
				//printf("-----index: %d---VoDev: %d----Bind---VochnInfo[index].VdecChn: %d, VoChn: %d\n",index, VoDev, VochnInfo[index].VdecChn, SPOT_VO_CHAN);
				if(HI_SUCCESS != HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, VoDev, SPOT_VO_CHAN))
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "------------Vdec Bind Vo fail\n");
				VochnInfo[index].IsBindVdec[VoDev] = (VochnInfo[index].VdecChn == DetVLoss_VdecChn || VochnInfo[index].VdecChn == NoConfig_VdecChn) ? 0 : 1;
			
			}
		}
	}	
	CHECK_RET(HI_MPI_VO_ChnShow(VoDev, SPOT_VO_CHAN));

	PRV_RefreshSpotOsd(VoChn);

	RET_SUCCESS("");
}


HI_S32 PRV_start_pciv(PCIV_CHN PcivChn)
{
	VO_DEV VoDev = SPOT_VO_DEV;    
	PCIV_ATTR_S stPcivAttr;      
	PCIV_BIND_OBJ_S stPcivBindObj;    

	CurrertPciv = PcivChn;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV,  "PRV_start_pciv: chan=%d\n", PcivChn);

	stPcivAttr.stPicAttr.u32Width = 704;
	//stPcivAttr.stPicAttr.u32Height = (s_s32NPFlagDflt ==0) ? PRV_IMAGE_SIZE_H_P : PRV_IMAGE_SIZE_H_N;
	
	stPcivAttr.stPicAttr.u32Height = 576;
	stPcivAttr.stPicAttr.u32Stride[2] = stPcivAttr.stPicAttr.u32Stride[1] = stPcivAttr.stPicAttr.u32Stride[0] = 704;
	//stPcivAttr.stPicAttr.u32Field  = VIDEO_FIELD_INTERLACED;	
	
	stPcivAttr.stPicAttr.u32Field  = VIDEO_FIELD_FRAME;	
	stPcivAttr.stPicAttr.enPixelFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420;

	/* config target pci id and pciv chn*/
	stPcivAttr.s32BufChip = 0;
	stPcivAttr.stRemoteObj.s32ChipId = PRV_SLAVE_1;
	stPcivAttr.stRemoteObj.pcivChn = PcivChn;

	/* 1) malloc phyaddr for receive pic (come from video buffer) */
	stPcivAttr.u32Count = 4;
	stPcivAttr.u32BlkSize = stPcivAttr.stPicAttr.u32Stride[0]*stPcivAttr.stPicAttr.u32Height*2;
	CHECK_RET(HI_MPI_PCIV_Malloc(stPcivAttr.u32BlkSize, stPcivAttr.u32Count, stPcivAttr.u32PhyAddr));

	/* 2) create pciv chn */
	CHECK_RET(HI_MPI_PCIV_Create(PcivChn, &stPcivAttr));

	/* 3) pciv chn bind vo chn (for display pic in host)*/
	stPcivBindObj.enType = PCIV_BIND_VO;
	stPcivBindObj.unAttachObj.voDevice.voDev = VoDev;
	stPcivBindObj.unAttachObj.voDevice.voChn = SPOT_VO_CHAN;
	CHECK_RET(HI_MPI_PCIV_Bind(PcivChn, &stPcivBindObj));

	/* 4) start pciv chn (now vo will display pic from slave chip) */
	CHECK_RET(HI_MPI_PCIV_Start(PcivChn));


	TRACE(SCI_TRACE_NORMAL, MOD_PRV,  "pciv chn%d start ok, remote(%d,%d);then send msg to slave chip !\n", PcivChn, stPcivAttr.stRemoteObj.s32ChipId,stPcivAttr.stRemoteObj.pcivChn);
	//printf("pciv chn: %d start ok, remote(%d,%d);then send msg to slave chip !\n", PcivChn, stPcivAttr.stRemoteObj.s32ChipId,stPcivAttr.stRemoteObj.pcivChn);


	SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_SPOT_PREVIEW_START_REQ, &stPcivAttr, sizeof(PCIV_ATTR_S));
	return HI_SUCCESS;
}


#endif


HI_S32 PRV_SetVoPreview(const SN_MSG *msg_req)
{
	HI_S32 i = 0, j = 0;
	UINT8 index = 255;
	PRM_PREVIEW_CFG_EX	stVoPreview;
	PRM_PREVIEW_CFG_EX_EXTEND stVoPreview_extend;
	if (ERROR == GetParameter(PRM_ID_PREVIEW_CFG_EX, NULL, &stVoPreview, sizeof(PRM_PREVIEW_CFG_EX), 0, SUPER_USER_ID, NULL))
	{
		TRACE(SCI_TRACE_NORMAL, MOD_SCM, "PRV_MSG_LayoutCtrl---PRM_ID_PREVIEW_CFG_EX Error!\n");
		return HI_FAILURE;
	}
	if (ERROR == GetParameter(PRM_ID_PREVIEW_CFG_EX_EXTEND, NULL, &stVoPreview_extend, sizeof(PRM_PREVIEW_CFG_EX_EXTEND), 0, SUPER_USER_ID, NULL))
	{
		TRACE(SCI_TRACE_NORMAL, MOD_SCM, "PRV_MSG_LayoutCtrl---PRM_ID_PREVIEW_CFG_EXTEND Error!\n");
		return HI_FAILURE;
	}
	for(i = 0; i < PRV_VO_MAX_DEV; i++)
	{
#if defined(SN9234H1)
		if(i != HD)
#else
		if(i > DHD0)
#endif			
			continue;

		if(ScmGetIsListCtrlStat() == 1)
		{
			s_astVoDevStatDflt[i].as32ChnOrder[SingleScene][0] = UCHAR2INIT32(j);
			for(j = 0; j < CHANNEL_NUM; j++)
			{
                if(j < 3)
				{
					s_astVoDevStatDflt[i].as32ChnOrder[ThreeScene][j] = UCHAR2INIT32(j);
				}
				if(j < 5)
				{
					s_astVoDevStatDflt[i].as32ChnOrder[FiveScene][j] = UCHAR2INIT32(j);
				}
				if(j < 7)
				{
					s_astVoDevStatDflt[i].as32ChnOrder[SevenScene][j] = UCHAR2INIT32(j);
				}
				
				if(j < 4)
				{
					s_astVoDevStatDflt[i].as32ChnOrder[FourScene][j] = UCHAR2INIT32(j);
					s_astVoDevStatDflt[i].as32ChnOrder[NineScene][j] = UCHAR2INIT32(j);
					s_astVoDevStatDflt[i].as32ChnOrder[SixteenScene][j] = UCHAR2INIT32(j);
				}
				else if(j < 9)
				{
					s_astVoDevStatDflt[i].as32ChnOrder[NineScene][j] = UCHAR2INIT32(j);
					s_astVoDevStatDflt[i].as32ChnOrder[SixteenScene][j] = UCHAR2INIT32(j);
				}
				else
				{
					s_astVoDevStatDflt[i].as32ChnOrder[SixteenScene][j] = UCHAR2INIT32(j);
				}
			}
			//4种预览模式下对应的音频通道
			for(j = 0; j < 4; j++)
			{
				index = stVoPreview.AudioChn[j];
				//保存音频通道号
				if(j == 0)
				{
					if(index == 0)
						s_astVoDevStatDflt[i].AudioChn[j] = UCHAR2INIT32(index);
					else
						s_astVoDevStatDflt[i].AudioChn[j] = -1;
				}
				else if(j == 1)
				{
					if(index < 4)
						s_astVoDevStatDflt[i].AudioChn[j] = UCHAR2INIT32(index);
					else
						s_astVoDevStatDflt[i].AudioChn[j] = -1;
				}
				else if(j == 2)
				{
					if(index < 9)
						s_astVoDevStatDflt[i].AudioChn[j] = UCHAR2INIT32(index);
					else
						s_astVoDevStatDflt[i].AudioChn[j] = -1;
				}
				else
				{
					if(index < 16)
						s_astVoDevStatDflt[i].AudioChn[j] = UCHAR2INIT32(index);
					else
						s_astVoDevStatDflt[i].AudioChn[j] = -1;
				}
			}
			for(j = 0; j < 3; j++)
			{
				index = stVoPreview_extend.AudioChn[j];
				//保存音频通道号
				if(j == 0)
				{
					if(index < 3)
						s_astVoDevStatDflt[i].AudioChn[j+4] = UCHAR2INIT32(index);
					else
						s_astVoDevStatDflt[i].AudioChn[j+4] = -1;
				}
				else if(j == 1)
				{
					if(index < 5)
						s_astVoDevStatDflt[i].AudioChn[j+4] = UCHAR2INIT32(index);
					else
						s_astVoDevStatDflt[i].AudioChn[j+4] = -1;
				}
				else if(j == 2)
				{
					if(index < 7)
						s_astVoDevStatDflt[i].AudioChn[j+4] = UCHAR2INIT32(index);
					else
						s_astVoDevStatDflt[i].AudioChn[j+4] = -1;
				}
			}
            
		}
		else
		{
			s_astVoDevStatDflt[i].as32ChnOrder[SingleScene][0] = UCHAR2INIT32(stVoPreview.SingleOrder);
			for(j = 0; j < CHANNEL_NUM; j++)
			{
                if(j < 3)
				{
					s_astVoDevStatDflt[i].as32ChnOrder[ThreeScene][j] = UCHAR2INIT32(stVoPreview_extend.ThreeOrder[j]);
				} 
				if(j < 5)
				{
					s_astVoDevStatDflt[i].as32ChnOrder[FiveScene][j] = UCHAR2INIT32(stVoPreview_extend.FiveOrder[j]);
				} 
				if(j < 7)
				{
					s_astVoDevStatDflt[i].as32ChnOrder[SevenScene][j] = UCHAR2INIT32(stVoPreview_extend.SevenOrder[j]);
				} 
				if(j < 4)
				{
					s_astVoDevStatDflt[i].as32ChnOrder[FourScene][j] = UCHAR2INIT32(stVoPreview.FourOrder[j]);
					s_astVoDevStatDflt[i].as32ChnOrder[NineScene][j] = UCHAR2INIT32(stVoPreview.NineOrder[j]);
					s_astVoDevStatDflt[i].as32ChnOrder[SixteenScene][j] = UCHAR2INIT32(stVoPreview.SixteenOrder[j]);
				}
				else if(j < 9)
				{
					s_astVoDevStatDflt[i].as32ChnOrder[NineScene][j] = UCHAR2INIT32(stVoPreview.NineOrder[j]);
					s_astVoDevStatDflt[i].as32ChnOrder[SixteenScene][j] = UCHAR2INIT32(stVoPreview.SixteenOrder[j]);
				}
				else
				{
					s_astVoDevStatDflt[i].as32ChnOrder[SixteenScene][j] = UCHAR2INIT32(stVoPreview.SixteenOrder[j]);
				}
			}
			
			//4种预览模式下对应的音频通道
			for(j = 0; j < 4; j++)
			{
				index = stVoPreview.AudioChn[j];
				//保存音频通道号
				if(j == 0)
				{
					if(index == 0)
						s_astVoDevStatDflt[i].AudioChn[j] = UCHAR2INIT32(stVoPreview.SingleOrder);
					else
						s_astVoDevStatDflt[i].AudioChn[j] = -1;
				}
				else if(j == 1)
				{
					if(index < 4)
						s_astVoDevStatDflt[i].AudioChn[j] = UCHAR2INIT32(stVoPreview.FourOrder[index]);
					else
						s_astVoDevStatDflt[i].AudioChn[j] = -1;
				}
				else if(j == 2)
				{
					if(index < 9)
						s_astVoDevStatDflt[i].AudioChn[j] = UCHAR2INIT32(stVoPreview.NineOrder[index]);
					else
						s_astVoDevStatDflt[i].AudioChn[j] = -1;
				}
				else
				{
					if(index < 16)
						s_astVoDevStatDflt[i].AudioChn[j] = UCHAR2INIT32(stVoPreview.SixteenOrder[index]);
					else
						s_astVoDevStatDflt[i].AudioChn[j] = -1;
				}
			}
			for(j = 0; j < 3; j++)
			{
				index = stVoPreview_extend.AudioChn[j];
				//保存音频通道号
				if(j == 0)
				{
					if(index <3)
						s_astVoDevStatDflt[i].AudioChn[4+j] = UCHAR2INIT32(stVoPreview_extend.ThreeOrder[index]);
					else
						s_astVoDevStatDflt[i].AudioChn[4+j] = -1;
				}
				else if(j == 1)
				{
					if(index < 5)
						s_astVoDevStatDflt[i].AudioChn[4+j] = UCHAR2INIT32(stVoPreview_extend.FiveOrder[index]);
					else
						s_astVoDevStatDflt[i].AudioChn[4+j] = -1;
				}
				else if(j == 2)
				{
					if(index < 7)
						s_astVoDevStatDflt[i].AudioChn[4+j] = UCHAR2INIT32(stVoPreview_extend.SevenOrder[index]);
					else
						s_astVoDevStatDflt[i].AudioChn[4+j] = -1;
				}
				
			}
		}
	}
#if defined(SN_SLAVE_ON)
	//发送消息给从片
	{
		Prv_Slave_Set_vo_preview_Req_EX	slave_req;
		int ret=0;
		
		slave_req.preview_info = stVoPreview;
		slave_req.preview_info_exn=stVoPreview_extend;
#if defined(SN9234H1)		
		slave_req.dev = HD;
#else
		slave_req.dev = DHD0;
#endif
		
		ret = SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_SET_VO_PREVIEW_REQ, &slave_req, sizeof(Prv_Slave_Set_vo_preview_Req_EX));
	}	
#endif
	return HI_SUCCESS;
}

void PRV_GetVoChnVideoInfo(unsigned char Chn, unsigned int *Width, unsigned int *Height)
{
	if(Chn > DEV_CHANNEL_NUM)
		return;
	*Width = VochnInfo[Chn].VideoInfo.width;	
	*Height = VochnInfo[Chn].VideoInfo.height;
}

void PRV_GetDecodeState(int VoChn, PRV_DecodeState *DecodeState)
{
#if !defined(USE_UI_OSD)
	pthread_mutex_lock(&send_data_mutex);
#endif
	if(VoChn > DEV_CHANNEL_NUM)
	{
		pthread_mutex_unlock(&send_data_mutex);
		return;
	}
	if(VochnInfo[VoChn].VdecChn >= 0
		&& VochnInfo[VoChn].VdecChn != DetVLoss_VdecChn 
		&& VochnInfo[VoChn].VdecChn != NoConfig_VdecChn)
		DecodeState->ConnectState = 1;
	else
		DecodeState->ConnectState = 0;
	
	DecodeState->Height = VochnInfo[VoChn].VideoInfo.height;
	DecodeState->Width = VochnInfo[VoChn].VideoInfo.width;
		
	DecodeState->DecodeVideoStreamFrames = VoChnState.VideoDataCount[VoChn];
	if(Achn == VoChn)
		DecodeState->DecodeAudioStreamFrames = VoChnState.SendAudioDataCount[VoChn];	
	else
		DecodeState->DecodeAudioStreamFrames = 0;
#if !defined(USE_UI_OSD)
	pthread_mutex_unlock(&send_data_mutex);
#endif

}

void PRV_UpdateDecodeStrategy(UINT8 DecodeStrategy)
{	
	int i = 0;
	pthread_mutex_lock(&send_data_mutex);
	g_PrvType = DecodeStrategy;
	for(i = 0; i < DEV_CHANNEL_NUM; i++)
		VochnInfo[i].PrvType = DecodeStrategy;
	pthread_mutex_unlock(&send_data_mutex);
}
/************************************************************************/
/* 对应各消息的处理函数。
   PRV_MSG_???();
                                                                  */
/************************************************************************/
#if (IS_DECODER_DEVTYPE == 1)

#else
/*************************************************
Function: //PRV_MSG_ScreenCtrl
Description: // 主片上一屏/下一屏切换消息 处理
Calls: 
Called By: //
Input: // msg_req:收到的消息结构体
		rsp : 回复给GUI的消息
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static HI_S32 PRV_MSG_ScreenCtrl(const SN_MSG *msg_req, Msg_id_prv_Rsp *rsp)
{
	Screen_ctrl_Req *param = (Screen_ctrl_Req *)msg_req->para;
	unsigned char vodev=param->dev;
	//主片回复GUI消息信息
	rsp->chn = 0;
	rsp->dev = param->dev;
	rsp->flag = 0;
	rsp->result = -1;
	if (NULL == param || (NULL == rsp))
	{
		RET_FAILURE("NULL pointer!!!");
	}
#if defined(Hi3520)
	if(param->dev == HD || param->dev == AD)
#else
	if(param->dev == DHD0)
#endif
	//if(param->dev == HD )
	{
		//VGA 和CVBS的轮询
		vodev = (param->dev == HD) ? HD : s_VoSecondDev;
		//printf("################PRV_MSG_ScreenCtrl  param->dev = %d  ,vodev =%d#################\n", param->dev,vodev);
#if defined(SN_SLAVE_ON)	
		{
			Prv_Slave_Screen_ctrl_Req slave_req;
			//把当前的消息信息传给从片，从片需要配置
			//printf("################PRV_MSG_ScreenCtrl  s32Dir = %d#################\n", param->dir);
			slave_req.dev = rsp->dev;
			slave_req.dir = param->dir;
            SN_MEMCPY(slave_req.reserve, sizeof(slave_req.reserve), param->reserve, sizeof(param->reserve), sizeof(slave_req.reserve));
			//发送下一屏请求给从片
			SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_SCREEN_CTRL_REQ, &slave_req, sizeof(Prv_Slave_Screen_ctrl_Req));

			TimerReset(s_State_Info.f_timer_handle, 15);
			TimerResume(s_State_Info.f_timer_handle, 0);
			
			PRV_Msg_Cpy(msg_req);		//保存当前消息，以便后面消息重发

			return SN_SLAVE_MSG;
		}
#else
		CHECK_RET(PRV_NextScreen(vodev, param->dir, param->reserve[0]));
		rsp->result = 0;
#endif
	}
	else
	{
		RET_SUCCESS("");
#if defined (SN6108HE) || (SN6108LE) || (SN6116HE) || (SN6116LE) || (SN8608D_LE) || (SN8608M_LE) || (SN8616D_LE) || (SN8616M_LE)|| defined(SN9234H1)
		{
			//SPOT口轮询
			int index1 = 0, index2 = 0;
			int bCurrentMaster = 0, bNextMaster = 0;
			int oldchan = s_astVoDevStatDflt[vodev].s32SingleIndex, next;
			vodev = SPOT_VO_DEV;
			s_astVoDevStatDflt[vodev].bIsSingle = HI_TRUE;//强制为单画面
			oldchan = s_astVoDevStatDflt[vodev].as32ChnOrder[SingleScene][s_astVoDevStatDflt[vodev].s32SingleIndex];
			if(oldchan < LOCALVEDIONUM)
			{
				bCurrentMaster = (oldchan >= PRV_CHAN_NUM) ? HI_FALSE : HI_TRUE;
			}
			else
			{
				index1 = PRV_GetVoChnIndex(oldchan);
				if(index1 < 0)
					RET_FAILURE("------ERR: Invalid Index!");
				bCurrentMaster = (VochnInfo[index1].SlaveId > 0) ? HI_FALSE : HI_TRUE;
			}
			CHECK_RET(PRV_GetValidChnIdx(vodev, s_astVoDevStatDflt[vodev].s32SingleIndex, &s_astVoDevStatDflt[vodev].s32SingleIndex, 
										param->dir, s_astVoDevStatDflt[vodev].as32ChnOrder[SingleScene], s_astVoDevStatDflt[vodev].as32ChnpollOrder[SingleScene]));
			
			next = s_astVoDevStatDflt[vodev].as32ChnOrder[SingleScene][s_astVoDevStatDflt[vodev].s32SingleIndex];
			if(next < LOCALVEDIONUM)
			{
				bCurrentMaster = (next >= PRV_CHAN_NUM) ? HI_FALSE : HI_TRUE;
			}
			else
			{
				index2 = PRV_GetVoChnIndex(next);
				if(index2 < 0)
					RET_FAILURE("------ERR: Invalid Index!");
				bNextMaster = (VochnInfo[index2].SlaveId > 0) ? HI_FALSE : HI_TRUE;
			}
			
			CHECK_RET(HI_MPI_VO_ClearChnBuffer(SPOT_VO_DEV, SPOT_VO_CHAN, 1)); /* 清除VO缓存 */
			//printf("SD : current =%d, next=%d, bCurrentMaster=%d, bNextMaster=%d\n", oldchan, next, bCurrentMaster, bNextMaster);
			if( bCurrentMaster == HI_TRUE && bNextMaster == HI_TRUE)
			{
				//都在主片，更换通道即可
				//printf("SD : bCurrentMaster =%d, bNextMaster=%d, line=%d\n", bCurrentMaster, bNextMaster, __LINE__);
				//PRV_RefreshVoDevScreen(vodev,DISP_NOT_DOUBLE_DISP,s_astVoDevStatDflt[vodev].as32ChnOrder[SingleScene]);
				if(VochnInfo[index1].IsBindVdec[SPOT_VO_DEV] != -1)
				{
					CHECK(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[index1].VdecChn, SPOT_VO_DEV, SPOT_VO_CHAN));				
					VochnInfo[index1].IsBindVdec[SPOT_VO_DEV] = -1;
				}
				PRV_PrevInitSpotVo(next);
				//printf("\n");
			}
			else if( bCurrentMaster == HI_TRUE && bNextMaster == HI_FALSE)
			{
				//当前在主片，下一个通道在从片，本地通道切换到PCI通道
				//printf("SD : bCurrentMaster =%d, bNextMaster=%d, line=%d\n", bCurrentMaster, bNextMaster, __LINE__);
				//int videv=(oldchan >= PRV_VI_CHN_NUM) ? PRV_656_DEV : PRV_656_DEV_1;
				//CHECK(HI_MPI_VI_UnBindOutput(videv, oldchan%PRV_VI_CHN_NUM, SPOT_VO_DEV, SPOT_VO_CHAN));				
				if(VochnInfo[index1].IsBindVdec[SPOT_VO_DEV] != -1)
				{
					CHECK(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[index1].VdecChn, SPOT_VO_DEV, SPOT_VO_CHAN));				
					VochnInfo[index1].IsBindVdec[SPOT_VO_DEV] = -1;

				}
				PRV_start_pciv(next);
				PRV_RefreshSpotOsd(next);
				//printf("\n");
			}
			else if( bCurrentMaster == HI_FALSE && bNextMaster == HI_TRUE)
			{
				
				//当前在从片，下一个通道在主片，通知从片解绑定
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV,  "SD : bCurrentMaster =%d, bNextMaster=%d, line=%d\n", bCurrentMaster, bNextMaster, __LINE__);

				PRV_HostStopPciv(CurrertPciv, MSG_ID_PRV_MCC_SPOT_PREVIEW_STOP_REQ);
				PRV_PrevInitSpotVo(next);
				//printf("\n");
			}
			else if( bCurrentMaster == HI_FALSE && bNextMaster == HI_FALSE)
			{
				Prv_Next_Pciv NextPciv;
				NextPciv.CurrentChan = oldchan;
				NextPciv.NextChan = next;
				#if 0
				//都在从片，通知从片切换PCI通道
				SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, 
						MSG_ID_PRV_MCC_SPOT_PREVIEW_NEXT_REQ, &NextPciv, sizeof(Prv_Next_Pciv));
				#else
				PRV_HostStopPciv(CurrertPciv, MSG_ID_PRV_MCC_SPOT_PREVIEW_NEXT_REQ);
				//PRV_start_pciv(s_astVoDevStatDflt[vodev].s32SingleIndex);
				
				//PRV_RefreshSpotOsd(s_astVoDevStatDflt[vodev].s32SingleIndex);
				#endif
				//printf("\n");
			
			}
		}
#endif
	}
	RET_SUCCESS("");
}
/*************************************************
Function: //PRV_MSG_Mcc_ScreenCtrl_Rsp
Description: // 收到从片返回后上一屏/下一屏切换 消息处理
Calls: 
Called By: //
Input: // slave_rsp :收到的从片消息结构体
		rsp : 回复给GUI的消息
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_MSG_Mcc_ScreenCtrl_Rsp(const Prv_Slave_Screen_ctrl_Rsp *slave_rsp,Msg_id_prv_Rsp *rsp)
{	
	if (NULL == rsp || NULL == slave_rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	//主片回复GUI消息信息
	rsp->chn = 0;
	rsp->dev = slave_rsp->dev;
	rsp->flag = 0;
	rsp->result = -1;
	 
	//如果从片返回，那么置位从片的返回标志位
	s_State_Info.bIsReply |= 1<<(slave_rsp->slaveid-1);
	
	if(slave_rsp->result == SN_RET_OK)
	{	//如果从片返回正确值
		s_State_Info.g_slave_OK = 1<<(slave_rsp->slaveid-1);
	}
	//如果返回值加1 等于1左移芯片个数的数值，那么表示所有从片都返回了
	if((s_State_Info.bIsReply+1) == 1<<(PRV_CHIP_NUM-1))	
	{
		TimerPause(s_State_Info.f_timer_handle); //暂停定时器
		s_State_Info.bIsReply = 0;//回复状态退出
		s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
		if((s_State_Info.g_slave_OK+1) == 1<<(PRV_CHIP_NUM-1))
		{//如果返回的消息中没有错误消息
			CHECK_RET(PRV_NextScreen(slave_rsp->dev, slave_rsp->dir, slave_rsp->reserve[0]));
			rsp->result = 0;
		}		
		s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
	}
	RET_SUCCESS("");
}
#endif
/*************************************************
Function: //PRV_MSG_LayoutCtrl
Description: // 主片切换画面布局消息处理
Calls: 
Called By: //
Input: // slave_rsp :收到的消息结构体
		rsp : 回复给GUI的消息
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_MSG_LayoutCtrl(const SN_MSG *msg_req , Msg_id_prv_Rsp *rsp)
{
	//printf("%s Line %d ---------> here\n",__func__,__LINE__);
	HI_S32 s32Index = 0, vochn = 0;
	Layout_crtl_Req *param = (Layout_crtl_Req *)msg_req->para;
	unsigned char vodev = param->dev;
	
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	
	//printf("##########ch=%d,dev=%d,mode=%d,flag=%d#############################\n",
									//param->chn,param->dev,param->mode,param->flag);
	rsp->chn = param->chn;	//都表示第几个画面
	rsp->dev = param->dev;
	rsp->flag = 0;
	rsp->result = -1;
#if defined(SN9234H1)
	vodev = (param->dev == HD) ? HD : s_VoSecondDev;
#else
	vodev = (param->dev == DHD0) ? DHD0 : s_VoSecondDev;
#endif

	if(PRV_CurDecodeMode == param->flag  && 
		s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode == param->mode && 
		s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsAlarm == 0 &&
		param->reserve != 1 && param->reserve != 2\
		&&param->mode != LinkFourScene && param->mode != LinkNineScene)
	{
		PRV_CurDecodeMode = param->flag;		
		//printf("%s Line %d ---------> PRV_CurDecodeMode:%d\n",__func__,__LINE__,PRV_CurDecodeMode);
		RET_SUCCESS("");
	}
	PRV_CurDecodeMode = param->flag;
	
	//printf("%s Line %d ---------> PRV_CurDecodeMode:%d\n",__func__,__LINE__,PRV_CurDecodeMode);
	Achn = -1;
	IsChoosePlayAudio = 0;
	PreAudioChn = -1;
	CurAudioChn = -1;
	PRV_SetVoPreview(msg_req);

#if 0 /*支持GUI从任意通道开始显示多画面！*/
	CHECK_RET(PRV_Chn2Index(param->dev, param->chn, &s32Index));
#else
	s32Index = param->chn;
#endif

#if 0
	if(param->flag == 1)
	{
		CHECK_RET(PRV_MultiChn(param->dev, param->mode, s32Index));
	}
	else
	{
		if (param->mode == SingleScene)
		{
			CHECK_RET(PRV_SingleChn(param->dev, param->chn));
		} 
		else
		{
			CHECK_RET(PRV_MultiChn(param->dev, param->mode, s32Index));
		}
	}
#else
	/*2010-8-24 修正预览为： 配置为单画面后再双击则依旧回到之前的多画面。而不是停留在单画面了。*/
/*
	if(param->flag == 1)
	{ //菜单配置时
		if (param->mode == SingleScene)
		{
			CHECK_RET(PRV_SingleChn(param->dev, s_astVoDevStatDflt[param->dev].as32ChnOrder[0]));
		} 
		else
		{
			CHECK_RET(PRV_MultiChn(param->dev, param->mode, s32Index));
		}
	}
	else
*/
#if 0
	{ //右键选择时
		if (param->mode == SingleScene)
		{
			CHECK_RET(PRV_SingleChn(param->dev, param->chn));
		} 
		else
		{
			CHECK_RET(PRV_MultiChn(param->dev, param->mode, s32Index));
		}
	}
#else
#if defined(SN_SLAVE_ON)
	//发送消息给从片
	{
		if(param->mode == LinkFourScene || param->mode == LinkNineScene)
		{
			LinkAgeGroup_ChnState LinkageGroup;
			SN_MEMSET(&LinkageGroup,0,sizeof(LinkAgeGroup_ChnState));
			Scm_GetLinkGroup(&LinkageGroup);
			SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_LINKAGEGROUP_CFG_REQ, &LinkageGroup, sizeof(LinkAgeGroup_ChnState));
		}
		Prv_Slave_Layout_crtl_Req slave_req;
		SN_MEMSET(&slave_req, 0, sizeof(Prv_Slave_Layout_crtl_Req));
		int ret=0;
		slave_req.chn = param->chn;
		slave_req.enPreviewMode = param->mode;
		slave_req.dev = rsp->dev;
		slave_req.flag = param->flag;
		slave_req.reserve[0] = param->reserve;
		slave_req.reserve[1] = ScmGetListCtlState();
		s_slaveVoStat.enPreviewMode = param->mode;
		s_slaveVoStat.s32SingleIndex = param->chn;
		s_slaveVoStat.s32PreviewIndex = param->chn;
		//printf("----------Send Message: MSG_ID_PRV_MCC_LAYOUT_CTRL_REQ\n");
		ret = SN_SendMccMessageEx(PRV_SLAVE_1, msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_LAYOUT_CTRL_REQ, &slave_req, sizeof(Prv_Slave_Layout_crtl_Req));
		//启动定时器
		TimerReset(s_State_Info.f_timer_handle, 15);
		TimerResume(s_State_Info.f_timer_handle, 0);
		PRV_Msg_Cpy(msg_req);
		return SN_SLAVE_MSG;
	}		
#endif

	if (param->mode== SingleScene)
	{
#if defined(SN9234H1)	
			vochn = param->chn;
#else
		if(param->reserve == 1)//键盘操作切换到单画面
			LayoutToSingleChn = param->chn;
		else
			LayoutToSingleChn = -1;
#endif		
		CHECK_RET(PRV_SingleChn(vodev, vochn));
	} 
	else
	{
		CHECK_RET(PRV_MultiChn(vodev, param->mode, s32Index));
	}
	rsp->result = 0;
#endif
#endif
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_MCC_LayoutCtrl
Description: // 收到从片返回后//切换画面布局消息处理
Calls: 
Called By: //
Input: // msg_req :收到的从片消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_MSG_MCC_LayoutCtrl_Rsp(const Prv_Slave_Layout_crtl_Rsp *slave_rsp,Msg_id_prv_Rsp *rsp)
{

	if (NULL == rsp || NULL == slave_rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->chn = slave_rsp->chn;
	rsp->dev = slave_rsp->dev;
	rsp->flag = 0;
	rsp->result = -1;
	
	//如果从片返回，那么置位从片的返回标志位
	s_State_Info.bIsReply |= 1<<(slave_rsp->slaveid-1);
	if(slave_rsp->result == SN_RET_OK)
	{	
		s_State_Info.g_slave_OK = 1<<(slave_rsp->slaveid-1);
	}
	//如果返回值加1 等于1左移芯片个数的数值，那么表示所有从片都返回了
	if((s_State_Info.bIsReply+1) == 1<<(PRV_CHIP_NUM-1))	
	{
		s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
		TimerPause(s_State_Info.f_timer_handle); //暂停定时器
		s_State_Info.bIsReply = 0;//回复状态退出
		s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
		
		if((s_State_Info.g_slave_OK+1) == 1<<(PRV_CHIP_NUM-1))
		{//如果返回的消息中没有错误消息
			//主片切换画面处理
			if (slave_rsp->enPreviewMode== SingleScene)
			{
				if(slave_rsp->reserve[0] == 1)//键盘操作切换到单画面
					LayoutToSingleChn = slave_rsp->chn;
				else
					LayoutToSingleChn = -1;
				CHECK_RET(PRV_SingleChn(slave_rsp->dev, slave_rsp->chn));	
			} 
			else
			{
				CHECK_RET(PRV_MultiChn(slave_rsp->dev, slave_rsp->enPreviewMode, slave_rsp->chn));
			}
			rsp->result = 0;	
		}
		s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
		if(s_State_Info.Prv_msg_Cur)
		{
			SN_FREE(s_State_Info.Prv_msg_Cur);//释放已完成的消息结构体指针
		}
		
	}
	RET_SUCCESS("");
}

//GUI输出口切换
STATIC HI_S32 PRV_MSG_OutputChange(const Output_Change_Req *param, Msg_id_prv_Rsp *rsp)
{
	unsigned char vodev = param->dev;
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	
	rsp->chn = 0;
	rsp->dev = param->dev;
	rsp->flag = 0;
	rsp->result = -1;

#if defined(Hi3520)
	if(param->dev == SPOT_VO_DEV|| param->dev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}

	if (param->dev >= PRV_VO_MAX_DEV)
	{
		RET_FAILURE("Invalid VoDev Id!");
	}
	if(s_VoSecondDev == AD)
	{
		if(param->dev == SD)
		{
			RET_FAILURE("Invalid VoDev Id!");
		}
	}
	vodev = (param->dev == HD) ? HD:s_VoSecondDev;
	
#else
	if(param->dev > DHD0)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}

	if (param->dev > DHD0)
	//if (param->dev >= PRV_VO_MAX_DEV)
	{
		RET_FAILURE("Invalid VoDev Id!");
	}
#if defined(Hi3531)
	if(s_VoSecondDev == DSD0)
	{
		if(param->dev == DSD1)
		{
			RET_FAILURE("Invalid VoDev Id!");
		}
	}
#endif
	vodev = (param->dev == DHD0) ? DHD0 : s_VoSecondDev;
#endif	
	
	if (s_VoDevCtrlDflt != vodev)
	{
		//切换GUI输出设备与分辨率
		s_VoDevCtrlDflt = vodev;
		s_u32GuiWidthDflt = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stDispRect.u32Width;
		s_u32GuiHeightDflt = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stDispRect.u32Height;
		MMICloseFB(s_devfb[G4],s_u32GuiWidthDflt,s_u32GuiHeightDflt - s_VoDevCtrlDflt, 16);
		PRV_BindGuiVo(s_VoDevCtrlDflt);
		MMIOpenFB(s_devfb[G4],s_u32GuiWidthDflt,s_u32GuiHeightDflt - s_VoDevCtrlDflt, 16);
	}
	PRINT_YELLOW("This message is NOT recommended for changing GUI!");
	rsp->result = 0;
	RET_SUCCESS("");
}

//报警图标/录像图标显示
STATIC HI_S32 PRV_MSG_ChnIconCtrl(const Chn_icon_ctrl_Req *param, Msg_id_prv_Rsp *rsp)
{
	HI_S32 s32Ret=-1;
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	
	rsp->chn = param->chn;
	rsp->dev = 0;
	rsp->flag = 0;
	rsp->result = -1;
#if 0
	
	//TODO
	if(param->icon == 1)
	{
		s32Ret = OSD_Ctl(param->chn,param->bshow,OSD_ALARM_TYPE);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"PRV_MSG_ChnIconCtrl OSD_Ctl faild 0x%x!\n",s32Ret);
			rsp->result = -1;
			RET_FAILURE("");
		}
	}
	else if(param->icon == 2)
	{
		s32Ret = OSD_Ctl(param->chn,param->bshow,OSD_REC_TYPE);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"PRV_MSG_ChnIconCtrl OSD_Ctl faild 0x%x!\n",s32Ret);
			rsp->result = -1;
			RET_FAILURE("");
		}
	}
#else
	s32Ret = OSD_Ctl(param->chn,param->bshow,param->icon);
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV,"PRV_MSG_ChnIconCtrl OSD_Ctl param->icon 0x%x, param->bshow=%d, param->chn = %d!\n",param->icon, param->bshow, param->chn);

	if(s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"PRV_MSG_ChnIconCtrl OSD_Ctl faild 0x%x!\n",s32Ret);
		rsp->result = -1;
		RET_FAILURE("");
	}
#endif	
	rsp->result = 0;
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_EnterChnCtrl
Description: 主片进入通道控制处理消息处理
Calls: 
Called By: //
Input: // msg_req :收到的消息结构体
		rsp : 回复给GUI的消息
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_MSG_EnterChnCtrl(const SN_MSG *msg_req, Msg_id_prv_Rsp *rsp)
{
	VO_CHN VoChn = 0;
	HI_U32 s32Index= 0;

	Enter_chn_ctrl_Req *param = (Enter_chn_ctrl_Req *)msg_req->para;
	
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	//printf("---------------flag: %d,param->chn: %d\n", param->flag, param->chn);
	rsp->chn = param->chn;
	rsp->dev = 0;
	rsp->flag = param->flag;
	rsp->result = -1;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "######PRV_MSG_EnterChnCtrl s32Flag = %d s_astVoDevStatDflt[VoDev].enPreviewStat = %d, param->chn: %d###################\n",param->flag,s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat, param->chn);
	switch (param->flag)
	{
		case 1://消息来源：1-进入区域选择；2-电子放大；3-云台控制；4-切换到回放；5-报警弹出；6-鼠标双击;7-遮盖区域;8-进入区域控制并隐藏OSD,9-进入视频显示参数配置界面
		case 8:
		case 3:
		case 5:
		case 7:
			VoChn = param->chn;//1、 消息来源为1(进入区域选择)或5(报警弹出)时，必须指定有效的通道号chn。
			break;
		case 9:
			//保存画中画的起始位置
			s_astVoDevStatDflt[s_VoDevCtrlDflt].Pip_rect.s32X = param->Pip_x;
			s_astVoDevStatDflt[s_VoDevCtrlDflt].Pip_rect.s32Y= param->Pip_y;
			s_astVoDevStatDflt[s_VoDevCtrlDflt].Pip_rect.u32Width= param->Pip_w;
			s_astVoDevStatDflt[s_VoDevCtrlDflt].Pip_rect.u32Height= param->Pip_h;
			VoChn = param->chn;//1、 消息来源为1(进入区域选择)或5(报警弹出)时，必须指定有效的通道号chn。
			break;
		case 2:
		case 6:	
		{
			//printf("param->flag: %d, param->mouse_pos.x: %d, param->mouse_pos.x: %d\n", param->flag, param->mouse_pos.x, param->mouse_pos.y);
			{
				if(s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsAlarm)
				{
					VoChn = s_astVoDevStatDflt[s_VoDevCtrlDflt].s32AlarmChn;
				}
				else
				{
					CHECK_RET(PRV_Point2Index(&param->mouse_pos, &s32Index,s_astVoDevStatDflt[s_VoDevCtrlDflt].as32ChnOrder[s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode]));//2、 消息来源为2(电子放大)或3(云台控制)或4(切换到回放)或6(鼠标双击)时，必须指定有效的鼠标位置mouse_pos。
					VoChn = s_astVoDevStatDflt[s_VoDevCtrlDflt].as32ChnOrder[s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode][s32Index];
					if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
					{
						VoChn = s32Index;
						break;
					}
					if(LayoutToSingleChn >= 0)
						VoChn = LayoutToSingleChn;
				}
			}
			//printf("----------VoChn: %d, mouse_pos X: %d, Y: %d\n", VoChn, param->mouse_pos.x, param->mouse_pos.y);
		}	
			break;
		case 4:
		case SLC_CTL_FLAG:
		case PIC_CTL_FLAG:
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Master Enter PB Control, param->chn = %d!\n", param->chn);
			VoChn = param->chn;
			break;
		default:
			RET_FAILURE("param->flag out off range");
	}
	//进入电子放大、显示管理，所有通道停止轮询
	if(param->flag == 2 || param->flag == 9)//电子放大和进入显示管理操作互斥，进入/退出可以用同一个消息
	{
		ScmChnCtrlReq(1);
		//int EnterChnCtl = 1;
		//SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_SCM, 0, 0, MSG_ID_SCM_CHNCTRL_IND, &EnterChnCtl, sizeof(EnterChnCtl));						
	}
#if defined(SN_SLAVE_ON)	
	{
		Prv_Slave_Enter_chn_ctrl_Req slave_req;
		//发送通道控制消息到从片
		slave_req.chn = VoChn;
		slave_req.flag = param->flag;
		slave_req.mouse_pos = param->mouse_pos;
		SN_SendMccMessageEx(PRV_SLAVE_1, msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_ENTER_CHN_CTRL_REQ, &slave_req, sizeof(slave_req));
		//SN_SendMccMessageEx(VochnInfo[index].SlaveId,msg_req->user, MOD_PRV, MOD_DEC, 0, 0,  MSG_ID_DEC_ZOOM_REQ, &slave_req, sizeof(slave_req));

		//启动定时器
		TimerReset(s_State_Info.f_timer_handle, 15);
		TimerResume(s_State_Info.f_timer_handle, 0);
		
		PRV_Msg_Cpy(msg_req);		//保存当前消息，以便后面消息重发
		return SN_SLAVE_MSG;
	}
#endif

	switch(param->flag)
	{
		case 1:
		case 8:
		case 2:
		case 3:
		case 7:		
		case 9:	
			CHECK_RET(PRV_EnterChnCtrl(s_VoDevCtrlDflt, VoChn, param->flag));
			break;
		case 4:
		case SLC_CTL_FLAG:
		case PIC_CTL_FLAG:	
			CHECK_RET(PRV_EnterPB(s_VoDevCtrlDflt,param->flag));
			break;
		case 5:
			//printf(TEXT_COLOR_PURPLE("Alarm!!! dev:%d, chn:%d\n"),s_VoDevAlarmDflt,VoChn);
			CHECK_RET(PRV_AlarmChn(s_VoDevAlarmDflt, VoChn));
			//完成后需要通知GUI切换焦点框
			SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_SCREEN_CTRL_IND, NULL, 0);
			break;
		case 6:
			CHECK_RET(PRV_DoubleClick(&param->mouse_pos));
			break;
		default:
			RET_FAILURE("unknown param->flag");
	}

	rsp->result = 0;
	RET_SUCCESS("");

}

/*************************************************
Function: //PRV_MSG_MCC_EnterChnCtrl_Rsp
Description: 收到从片进入通道控制消息后处理程序
Calls: 
Called By: //
Input: // msg_req :从片消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_MSG_MCC_EnterChnCtrl_Rsp(const Prv_Slave_Enter_chn_ctrl_Rsp *param,Msg_id_prv_Rsp *rsp)
{
	VO_CHN VoChn = 0;
	HI_S32 s32Ret = 0;
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->chn = param->chn;
	rsp->dev = 0;
	rsp->flag = param->flag;
	rsp->result = -1; 		
	//如果从片返回，那么置位从片的返回标志位
	s_State_Info.bIsReply |= 1<<(param->slaveid-1);
	
	if(param->result == SN_RET_OK)
	{	//如果从片返回正确值
		s_State_Info.g_slave_OK = 1<<(param->slaveid-1);
	}
	//如果返回值加1 等于1左移芯片个数的数值，那么表示所有从片都返回了
	if((s_State_Info.bIsReply+1) == 1<<(PRV_CHIP_NUM-1))	
	{
		if((s_State_Info.g_slave_OK+1) == 1<<(PRV_CHIP_NUM-1))
		{//如果返回的消息中没有错误消息
			VoChn = param->chn;
			switch(param->flag)
			{
				case 1:
				case 8:
				case 2:
				case 3: 
				case 7:	
				case 9:
				{
					//将状态位置位移到前面，
					//防止消息已经返回，但因为对应处理的接口中有锁，导致处理时间过长，而导致消息超时
					s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
					TimerPause(s_State_Info.f_timer_handle); //暂停定时器
					s_State_Info.bIsReply = 0;//回复状态退出
					s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
					s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
					CHECK_RET(PRV_EnterChnCtrl(s_VoDevCtrlDflt, VoChn, param->flag));
				}
					break;
				case 4:
				case SLC_CTL_FLAG:
				case PIC_CTL_FLAG:
					{
						s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
						TimerPause(s_State_Info.f_timer_handle); //暂停定时器
						s_State_Info.bIsReply = 0;//回复状态退出
						s_State_Info.bIsSlaveConfig = HI_FALSE; //重新置位配置标志位
						s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
						
						s32Ret = HostCleanAllHostToSlaveStream(MasterToSlaveChnId, 0, 1);
						if (s32Ret != HI_SUCCESS)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "slave %d HostCleanAllHostToSlaveStream error", MasterToSlaveChnId);
						}
#if defined(Hi3531)||defined(Hi3535)				
						s32Ret = HostStopHostToSlaveStream(MasterToSlaveChnId);
						if (s32Ret != HI_SUCCESS)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "slave %d HostStopHostToSlaveStream error", MasterToSlaveChnId);
						}
						else
						{
							MasterToSlaveChnId = 0;
						}	
#endif						
					}
					CHECK_RET(PRV_EnterPB(s_VoDevCtrlDflt, param->flag));
					break;
				case 5:
					{
						s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
						TimerPause(s_State_Info.f_timer_handle); //暂停定时器
						s_State_Info.bIsReply = 0;//回复状态退出
						s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
						s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
						//printf(TEXT_COLOR_PURPLE("Alarm!!! dev:%d, chn:%d\n"),s_VoDevAlarmDflt,VoChn);
						CHECK_RET(PRV_AlarmChn(s_VoDevAlarmDflt, VoChn));
						//完成后需要通知GUI切换焦点框
						SN_SendMessageEx(s_State_Info.Prv_msg_Cur->user, MOD_PRV, MOD_MMI, s_State_Info.Prv_msg_Cur->xid, s_State_Info.Prv_msg_Cur->thread, 
								MSG_ID_PRV_SCREEN_CTRL_IND, NULL, 0);
					}
					break;
				case 6:
					{
						s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
						TimerPause(s_State_Info.f_timer_handle); //暂停定时器
						s_State_Info.bIsReply = 0;//回复状态退出
						s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
						s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
						CHECK_RET(PRV_DoubleClick(&param->mouse_pos));
					}
					break;
				default:
					RET_FAILURE("unknown param->flag");
			}
			rsp->result = 0;
		}

		if(s_State_Info.Prv_msg_Cur)
		{
			SN_FREE(s_State_Info.Prv_msg_Cur);//释放已完成的消息结构体指针
		}
	}
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_ExitChnCtrl
Description: //主片退出通道控制消息处理
Calls: 
Called By: //
Input: // msg_req :收到的消息结构体
		rsp : 回复给GUI的消息
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_MSG_ExitChnCtrl(const SN_MSG *msg_req, Msg_id_prv_Rsp *rsp)
{
	Exit_chn_ctrl_Req *param = (Exit_chn_ctrl_Req *)msg_req->para;
	
	if ((NULL == param) || (NULL == rsp))
	{
		RET_FAILURE("NULL pointer!!!");
	}
	
	rsp->chn = 0;
	rsp->dev = 0;
	rsp->flag = param->flag;
	rsp->result = -1;
#if defined(SN_SLAVE_ON)	
	{
		Prv_Slave_Exit_chn_ctrl_Req slave_req;
		//发送通道控制消息到从片
		slave_req.flag = param->flag;
		SN_SendMccMessageEx(PRV_SLAVE_1, msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_EXIT_CHN_CTRL_REQ, &slave_req, sizeof(slave_req));
		//启动定时器
		TimerReset(s_State_Info.f_timer_handle, 15);
		TimerResume(s_State_Info.f_timer_handle, 0);
		PRV_Msg_Cpy(msg_req);		//保存当前消息，以便后面消息重发
		return SN_SLAVE_MSG;
	}
#endif
	switch (param->flag)
	{
	case 1://消息来源：1-进入区域选择；2-电子放大；3-云台控制；4-切换到回放；5-报警弹出；6-鼠标双击;7-遮盖区域;8-进入区域控制并隐藏OSD,9-进入视频显示参数配置界面
	case 8:
	case 2:
	case 3:
	case 7:	
	case 9:	
			CHECK_RET(PRV_ExitChnCtrl(param->flag));
			break;
	case 4:
		case SLC_CTL_FLAG:
		case PIC_CTL_FLAG:
			//printf("--------Master Exit PB\n");
			CHECK_RET(PRV_ExitPB(s_VoDevCtrlDflt,param->flag));
			break;
	case 5:
			CHECK_RET(PRV_AlarmOff(s_VoDevAlarmDflt));
			//完成后需要通知GUI切换焦点框
			SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
								MSG_ID_PRV_SCREEN_CTRL_IND, NULL, 0);
			break;
	case 6:
			RET_FAILURE("double click should not use this message!!!");
		default:
			RET_FAILURE("param->flag out off range");
	}
	//退出电子放大、显示管理，所有通道开始轮询
	if(param->flag == 2 || param->flag == 9)
	{
		ScmChnCtrlReq(0);
		//int ExitChnCtl = 0;
		//SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_SCM, 0, 0, MSG_ID_SCM_CHNCTRL_IND, &ExitChnCtl, sizeof(ExitChnCtl));	
	}
	rsp->result = 0;
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_MCC_ExitChnCtrl_Rsp
Description: //从片返回后退出通道控制消息处理
Calls: 
Called By: //
Input: // msg_req :收到的从片消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 PRV_MSG_MCC_ExitChnCtrl_Rsp(const Prv_Slave_Exit_chn_ctrl_Rsp *slave_rsp, Msg_id_prv_Rsp *rsp)
{
//	 int i = 0;
	 if (NULL == rsp || NULL == slave_rsp)
	 {
		 RET_FAILURE("NULL pointer!!!");
	 }
	 
	 rsp->chn = 0;
	 rsp->dev = 0;
	 rsp->flag = slave_rsp->flag;
	 rsp->result = -1;
	 
	//如果从片返回，那么置位从片的返回标志位
	s_State_Info.bIsReply |= 1<<(slave_rsp->slaveid-1);
	
	if(slave_rsp->result == SN_RET_OK)
	{	//如果从片返回正确值
		s_State_Info.g_slave_OK = 1<<(slave_rsp->slaveid-1);
	}
	//如果返回值加1 等于1左移芯片个数的数值，那么表示所有从片都返回了
	if((s_State_Info.bIsReply+1) == 1<<(PRV_CHIP_NUM-1))	
	{
		if((s_State_Info.g_slave_OK+1) == 1<<(PRV_CHIP_NUM-1))
		{//如果返回的消息中没有错误消息
			switch (slave_rsp->flag)
			{
			case 1://消息来源：1-进入区域选择；2-电子放大；3-云台控制；4-切换到回放；5-报警弹出；6-鼠标双击;7-遮盖区域;8-进入区域控制并隐藏OSD,9-进入视频显示参数配置界面
			case 8:
			case 2:
			case 3:
			case 7:	
			case 9:	
				{
					s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
					TimerPause(s_State_Info.f_timer_handle); //暂停定时器
					s_State_Info.bIsReply = 0;//回复状态退出
					s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
					s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
					CHECK_RET(PRV_ExitChnCtrl(slave_rsp->flag));
				}
					break;
			case 4:
				case SLC_CTL_FLAG:
				case PIC_CTL_FLAG:
				{
					s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
					TimerPause(s_State_Info.f_timer_handle); //暂停定时器
					s_State_Info.bIsReply = 0;//回复状态退出
					s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
					s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
#if defined(Hi3531)||defined(Hi3535)					
					PRV_InitHostToSlaveStream();
#endif
						
					CHECK_RET(PRV_ExitPB(s_VoDevCtrlDflt,slave_rsp->flag));
				}
					break;
			case 5:
				{
					s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
					TimerPause(s_State_Info.f_timer_handle); //暂停定时器
					s_State_Info.bIsReply = 0;//回复状态退出
					s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
					s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
					CHECK_RET(PRV_AlarmOff(s_VoDevAlarmDflt));
				}
					break;
			case 6:
					RET_FAILURE("double click should not use this message!!!");
				default:
					RET_FAILURE("param->flag out off range");
			}
			rsp->result = 0;
		}

		if(s_State_Info.Prv_msg_Cur)
		{
			SN_FREE(s_State_Info.Prv_msg_Cur);//释放已完成的消息结构体指针
		}
		//退出电子放大、显示管理，所有通道开始轮询
		if(slave_rsp->flag == 2 || slave_rsp->flag == 9)
		{
			int ExitChnCtl = 0;
			SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_SCM, 0, 0, MSG_ID_SCM_CHNCTRL_IND, &ExitChnCtl, sizeof(ExitChnCtl));	
		}
	}
	RET_SUCCESS("");
}

/**********************************************************************
* 函数名称：   MPRV_OSD_SetGuiAlpha
* 功能描述：   UI在进行区域选择时需要将画面半透明
* 输入参数：   unsigned char alpha 透明度 0-255
* 输出参数：   无
* 返 回 值：    MMI_ERR_OK -成功，其他-失败
* 其    他:  无
***********************************************************************/
int PRV_SetGuiAlpha (int b_isnew, int Transparency)
{
	int ret = ERROR;
	UINT8 alpha = 0;
	PRM_GENERAL_CFG_ADV  stGenPrm;

	if(b_isnew)
	{
		alpha = 0xFF - Transparency * 25;
	}
	else
	{
		if (PARAM_OK != GetParameter (PRM_ID_GENERAL_CFG_ADV, NULL, &stGenPrm, sizeof (stGenPrm), 1, SUPER_USER_ID, NULL))
		{
			return ret;
		}

		alpha = 0xFF - stGenPrm.MenuTransparency * 25;
	}
	
#if(DEV_TYPE == DEV_SN_9234_H_1)
	ret = PRV_OSD_SetGuiAlpha (alpha);
#elif defined(Hi3531)||defined(Hi3535)	
	ret = FBSetAlphaKey (alpha, TRUE);
#endif	
	
	return ret;
}

/*************************************************
Function: //PRV_MSG_SetGeneralCfg
Description: 通用配置消息处理程序
Calls: 
Called By: //
Input: // // msg_req:通用配置消息
			rsp:返回GUI的RSP
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //在N\P制式改变的时候，需要通知从片，这时候其他的操作都等到从片返回后处理
************************************************************************/
STATIC HI_S32 PRV_MSG_SetGeneralCfg(const SN_MSG *msg_req,Set_general_cfg_Rsp *rsp)
{
	VO_INTF_SYNC_E enIntfSync,old_enIntfSync;		/*VGA分辨率*/
	VO_DEV VoDev;					/*GUI输出设备*/
	HI_S32 s32NPFlag=0,old_s32NPFlag=0;				/*NP制式*/
	Set_general_cfg_Req *param = (Set_general_cfg_Req *)msg_req->para;
	int Changeflag=0,osd_flag=0,ret=0;			//变化标志位，第0位表示N\P制式变化，第1位表示分辨率变化，第2位表示输出设备变化
	
	//char gui_flag = 0;

	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->result = -1;
	rsp->general_info = param->general_info;

	/*NP制式*/
	s32NPFlag = (0 == param->general_info.CVBSOutputType) ? 1 : 0;
	
	old_s32NPFlag = s_s32NPFlagDflt;
#if defined(SN9234H1)
	old_enIntfSync = s_astVoDevStatDflt[HD].stVoPubAttr.enIntfSync;
#else
	old_enIntfSync = s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfSync;
#endif	
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%d  , %d  ,%d ,  %d,  %d\n", param->general_info.CVBSOutputType,param->general_info.Language,param->general_info.OutputSel,
						param->general_info.OutPutType,param->general_info.VGAResolution);

	/*VGA分辨率*/
	
	switch(param->general_info.VGAResolution)
	{
		case VGA_1080P:
#if defined(Hi3531)||defined(Hi3535)			
			enIntfSync = VO_OUTPUT_1080P60;
			break;
#endif
			
			
		case VGA_720P:
#if defined(Hi3531)||defined(Hi3535)			
			enIntfSync = VO_OUTPUT_720P60;
			break;
#endif
			
			
		case VGA_1024X768:
#if defined(SN9234H1)
			enIntfSync = VO_OUTPUT_720P60;
#else
			enIntfSync = VO_OUTPUT_1024x768_60;
#endif
			break;
		
		case VGA_1280X1024:
#if defined(SN9234H1)
			enIntfSync = VO_OUTPUT_720P60;
#else
			enIntfSync = VO_OUTPUT_1280x1024_60;
#endif
			break;
		case VGA_800X600:
			enIntfSync = VO_OUTPUT_800x600_60;
			break;
		case VGA_1366x768:
			enIntfSync = VO_OUTPUT_1366x768_60;
			break;
		case VGA_1440x900:
			enIntfSync = VO_OUTPUT_1440x900_60;
			break;
		default:
			RET_FAILURE("Unsupport VGA Resolution");
	}
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "=============enIntfSync: %d\n", enIntfSync);
#if defined(SN_SLAVE_ON)
		if(s_State_Info.bslave_IsInit == HI_TRUE)
		{
		//从片已经初始化
#if defined(SN6104) || defined(SN8604D) || defined(SN8604M) || defined(SN6108) || defined(SN8608D) || defined(SN8608M)
			if ((enIntfSync != s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfSync) || (s32NPFlag != s_s32NPFlagDflt))
#else
			if ((param->general_info.VGAResolution != s_s32VGAResolution) || (s32NPFlag != s_s32NPFlagDflt))
#endif
			{
			//如果分辨率改变。那么需要通知从片,其他操作等从片返回后操作
			
				Prv_Slave_Set_general_cfg_Req slave_req;
				//printf("###########PRV_MSG_SetGeneralCfg Send msg !######################\n");
				//把当前的消息信息传给从片，从片需要配置
				slave_req.general_info = param->general_info;
				//需要保存录象模块返回的数据
				slave_req.vam_result = param->reserve[0];
				//发送下一屏请求给从片
				SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_SET_GENERAL_CFG_REQ, &slave_req, sizeof(slave_req));
				//启动定时器
				TimerReset(s_State_Info.f_timer_handle,15); 
				TimerResume(s_State_Info.f_timer_handle,0);
				PRV_Msg_Cpy(msg_req);	//保存当前消息内容
				return SN_SLAVE_MSG;
			}
		}
#endif

	/*GUI输出设备*/
	switch(param->general_info.OutputSel)
	{
#if defined(SN9234H1)
		case 0:
			VoDev = HD;
			break;
		case 1:
			VoDev = s_VoSecondDev;
			break;
		case 2:
			VoDev = SD;
			RET_FAILURE("SD is not supported for Gui!");
		default :
			RET_FAILURE("general_info.OutputSel is out off range!");
#else
		case 0:
			VoDev = DHD0;
			break;
		case 1:
			VoDev = s_VoSecondDev;
			break;
		case 2:
			VoDev = DSD0;
		default :
			RET_FAILURE("general_info.OutputSel is out off range!");
#endif			
	}
    pthread_mutex_lock(&s_osd_mutex);

//step1:
	//step1: CVBS输出制式
	if (s32NPFlag != s_s32NPFlagDflt)
	{
		//Changeflag |= 1<<0; 
	}
#if defined(SN9234H1)
	if( (enIntfSync != s_astVoDevStatDflt[HD].stVoPubAttr.enIntfSync) || (param->general_info.VGAResolution != s_s32VGAResolution))
	{
		Changeflag |= 1<<1; 
		s_s32VGAResolution = param->general_info.VGAResolution;
	}
#else
	if( (enIntfSync != s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfSync) || (param->general_info.VGAResolution != s_s32VGAResolution))
	{
		Changeflag |= 1<<1; 
		s_s32VGAResolution = param->general_info.VGAResolution;
	}
#endif
	if (s_VoDevCtrlDflt != VoDev)
	{
		Changeflag |= 1<<2; 
	}
#if defined(SN_SLAVE_ON)

	if(s_State_Info.bslave_IsInit == HI_FALSE)
		Changeflag = 2;
#endif
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========Changeflag: %d\n", Changeflag);
	switch(Changeflag)
	{
		case 1://只有N\P制式变化
		case 3://N\P制式变化、分辨率变化
		case 5://N\P制式变化、输出设备变化
		case 7://N\P制式变化、分辨率变化、输出设备变化
		{
			s_s32NPFlagDflt = s32NPFlag;
			s_State_Info.bIsRe_Init = 0;
			if((s_State_Info.bIsOsd_Init))
			{
#if defined(SN9234H1)
				Prv_OSD_Close_fb(HD);
				Prv_OSD_Close_fb(AD);		
				osd_flag = 1;
#else
				Prv_OSD_Close_fb(DHD0);
				//Prv_OSD_Close_fb(DSD0);
				osd_flag = 1;
#endif				
			}
			PRV_CloseVoFb(s_VoDevCtrlDflt);

#if defined(SN9234H1)
			//先释放所有绑定关系
			PRV_ViUnBindAllVoChn(VoDev);
			PRV_VdecUnBindAllVoChn1(VoDev);
			
			PRV_ViUnBindAllVoChn(s_VoSecondDev);
			PRV_VdecUnBindAllVoChn1(s_VoSecondDev);

			//VI重新初始化
			ret = PRV_ViInit();
			if(ret == HI_FAILURE)
			{//如果返回错误,恢复旧的配置，返回失败
				goto Falied;
			}
			//VO重新初始化
			s_astVoDevStatDflt[HD].stVoPubAttr.enIntfSync = enIntfSync;
			ret = PRV_ResetVo(HD);
			if(ret == HI_FAILURE)
			{//如果返回错误,恢复旧的配置，返回失败
				goto Falied;
			}
			ret = PRV_ResetVo(s_VoSecondDev);
			if(ret == HI_FAILURE)
			{//如果返回错误,恢复旧的配置，返回失败
				goto Falied;
			}

#else
			//先释放所有绑定关系
			//PRV_ViUnBindAllVoChn(VoDev);
			//PRV_VdecUnBindAllVoChn(VoDev);
			
		//	PRV_ViUnBindAllVoChn(s_VoSecondDev);
			//PRV_VdecUnBindAllVoChn(s_VoSecondDev);
			//VI重新初始化
			//ret = PRV_ViInit();
			if(ret == HI_FAILURE)
			{//如果返回错误,恢复旧的配置，返回失败
				goto Falied;
			}
			//VO重新初始化
			s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfSync = enIntfSync;
			ret = PRV_ResetVo(DHD0);
			if(ret == HI_FAILURE)
			{//如果返回错误,恢复旧的配置，返回失败
				goto Falied;
			}
			//ret = PRV_ResetVo(s_VoSecondDev);
			//if(ret == HI_FAILURE)
			{//如果返回错误,恢复旧的配置，返回失败
			//	goto Falied;
			}
#endif			
			s_VoDevCtrlDflt = VoDev;			
			PRV_OpenVoFb(s_VoDevCtrlDflt);
			
			if(osd_flag)
			{
				OSD_Set_Rec_Range_NP(s_s32NPFlagDflt);
#if defined(SN9234H1)
				Prv_OSD_Open_fb( HD);
				Prv_OSD_Open_fb( AD);
#else
				Prv_OSD_Open_fb( DHD0);
				//Prv_OSD_Open_fb( DSD0);
#endif				
			}
			s_State_Info.bIsRe_Init = 1;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "step1: N/P mode changed! N-0,P-1: %d", s_s32NPFlagDflt);
			break;
Falied:		

#if defined(SN9234H1)	
			s_s32NPFlagDflt = old_s32NPFlag;
			PRV_ViInit();
			s_s32NPFlagDflt = old_enIntfSync;
			PRV_ResetVoDev(HD);
			PRV_ResetVoDev(s_VoSecondDev);
			PRV_OpenVoFb(s_VoDevCtrlDflt);
			
			if(osd_flag)
			{
				OSD_Set_Rec_Range_NP(s_s32NPFlagDflt);
				Prv_OSD_Open_fb( HD);
				Prv_OSD_Open_fb( AD);
			}
#else
			s_s32NPFlagDflt = old_s32NPFlag;
////			PRV_ViInit();
			s_s32NPFlagDflt = old_enIntfSync;
			PRV_ResetVoDev(DHD0);
			//PRV_ResetVoDev(s_VoSecondDev);
			PRV_OpenVoFb(s_VoDevCtrlDflt);
			
			if(osd_flag)
			{
				OSD_Set_Rec_Range_NP(s_s32NPFlagDflt);
				Prv_OSD_Open_fb( DHD0);
				//Prv_OSD_Open_fb( DSD0);
			}
#endif
			
			s_State_Info.bIsRe_Init = 1;
            pthread_mutex_unlock(&s_osd_mutex);
			RET_FAILURE("PRV_Vireset or PRV_Voreset falied!\n");
		}
		case 2://只有分辨率变化
		{
#if defined(SN9234H1)
			s_astVoDevStatDflt[HD].stVoPubAttr.enIntfSync = enIntfSync;
			
			//printf("##########s_astVoDevStatDflt[HD]   s_VoDevCtrlDflt = %d,VoDev = %d################\n",s_VoDevCtrlDflt,VoDev);
			s_State_Info.bIsRe_Init = 0;
			if((s_State_Info.bIsOsd_Init))
			{
				Prv_OSD_Close_fb(HD);
				osd_flag = 1;
			}
			if(s_VoDevCtrlDflt == HD)
			{//如果当前输出设备在HD，那么需要关闭GUI
				PRV_CloseVoFb(s_VoDevCtrlDflt);
			}
			
			ret = PRV_ResetVoDev(HD);
			if(ret == HI_FAILURE)
			{//如果返回错误,恢复旧的配置，返回失败
				s_s32NPFlagDflt = old_enIntfSync;
				PRV_ResetVoDev(HD);
			}
			
			if(s_VoDevCtrlDflt == HD)
			{//如果当前输出设备在HD，那么需要开启GUI
				PRV_OpenVoFb(s_VoDevCtrlDflt);
			}
			if(osd_flag)
			{
				OSD_Set_Rec_Range_NP(s_s32NPFlagDflt);
				Prv_OSD_Open_fb( HD);
			}
			s_State_Info.bIsRe_Init = 1;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "step2: VGA resolution changed! %dx%d", s_astVoDevStatDflt[HD].stVideoLayerAttr.stDispRect.u32Width, s_astVoDevStatDflt[HD].stVideoLayerAttr.stDispRect.u32Height);
#else
			s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfSync = enIntfSync;
			
			//printf("##########s_astVoDevStatDflt[DHD0]   s_VoDevCtrlDflt = %d,VoDev = %d################\n",s_VoDevCtrlDflt,VoDev);
			s_State_Info.bIsRe_Init = 0;
			if((s_State_Info.bIsOsd_Init))
			{
				Prv_OSD_Close_fb(DHD0);
				osd_flag = 1;
			}
			if(s_VoDevCtrlDflt == DHD0)
			{//如果当前输出设备在HD，那么需要关闭GUI
				PRV_CloseVoFb(s_VoDevCtrlDflt);
			}
			
			ret = PRV_ResetVoDev(DHD0);
			if(ret == HI_FAILURE)
			{//如果返回错误,恢复旧的配置，返回失败
				s_s32NPFlagDflt = old_enIntfSync;
				PRV_ResetVoDev(DHD0);
			}
			
			if(s_VoDevCtrlDflt == DHD0)
			{//如果当前输出设备在HD，那么需要开启GUI
				PRV_OpenVoFb(s_VoDevCtrlDflt);
			}
			if(osd_flag)
			{
				OSD_Set_Rec_Range_NP(s_s32NPFlagDflt);
				Prv_OSD_Open_fb( DHD0);
			}
			s_State_Info.bIsRe_Init = 1;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "step2: VGA resolution changed! %dx%d", s_astVoDevStatDflt[DHD0].stVideoLayerAttr.stDispRect.u32Width, s_astVoDevStatDflt[DHD0].stVideoLayerAttr.stDispRect.u32Height);
#endif
			if(ret == HI_FAILURE)
			{
                pthread_mutex_unlock(&s_osd_mutex);
				RET_FAILURE("PRV_Voreset falied!\n");
			}
		}
			break;
		case 4://只有输出设备变化
		{
			s_State_Info.bIsRe_Init = 0;
			PRV_CloseVoFb(s_VoDevCtrlDflt);
			s_VoDevCtrlDflt = VoDev;
			PRV_OpenVoFb(VoDev);
			s_State_Info.bIsRe_Init = 1;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "do nothing!! step4: Default GUI output dev changed! HD-0,AD-1,SD-2: %d", s_VoDevCtrlDflt);
		}
			break;
		case 6://分辨率变化、输出设备变化
		{
			s_astVoDevStatDflt[HD].stVoPubAttr.enIntfSync = enIntfSync;
			s_State_Info.bIsRe_Init = 0;
			if((s_State_Info.bIsOsd_Init))
			{
				Prv_OSD_Close_fb(HD);
				osd_flag = 1;
			}
			PRV_CloseVoFb(s_VoDevCtrlDflt);
			ret = PRV_ResetVoDev(HD);
			if(ret == HI_FAILURE)
			{//如果返回错误,恢复旧的配置，返回失败
				s_s32NPFlagDflt = old_enIntfSync;
				PRV_ResetVoDev(HD);
				goto Err;
			}
			s_VoDevCtrlDflt = VoDev;
Err:			
			PRV_OpenVoFb(s_VoDevCtrlDflt);		
			if(osd_flag)
			{
				OSD_Set_Rec_Range_NP(s_s32NPFlagDflt);
				Prv_OSD_Open_fb( HD);
			}
			s_State_Info.bIsRe_Init = 1;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "step2: VGA resolution changed! %dx%d\n", s_astVoDevStatDflt[HD].stVideoLayerAttr.stDispRect.u32Width, s_astVoDevStatDflt[HD].stVideoLayerAttr.stDispRect.u32Height);
			if(ret == HI_FAILURE)
			{
                pthread_mutex_unlock(&s_osd_mutex);
				RET_FAILURE("PRV_VoReset falied!\n");
			}
		}
			break;
		default:
			break;	
 	}
	//OSD_G1_open();//显示其中的一个GUI层
	////2010-9-30
	if(Changeflag & (1<<0))
	{
		VIDEO_FRAME_INFO_S *pstVFrame;
		pstVFrame = (0==s_s32NPFlagDflt)?&s_stUserFrameInfo_P:&s_stUserFrameInfo_N;
#if defined(SN9234H1)		
		CHECK(HI_MPI_VI_SetUserPic(0, 0, pstVFrame));
#endif	
		Loss_State_Pre = 0;
	}
//step2:
	//step2: 显示器分辨率  对应enum VGAResolution_enum{VGA_1024X768,VGA_1280_960,VGA_1280X1024}
//step3:
	//step3: 电视去抖NoShake   0-不启动   1-启动
	Prv_Set_Flicker(param->general_info.NoShake);
//step4:
	//step4: 输出设备选择 0-DHD0/VGA   1-CVBS1 2- CVBS2
	s_u32GuiWidthDflt = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stDispRect.u32Width;
	s_u32GuiHeightDflt = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stDispRect.u32Height;
//step5:
	//step5: 日期显示格式DataDisplayForm  0为 YYYY-MM-DD   1为 MM-DD-YYYY   2为DD-MM-YYYY   3为YYYY年MM月DD日   4为MM月DD日YYYY年
	//TODO
	if(param->general_info.DataDisplayForm != s_OSD_Time_type)
	{
#if defined(SN_SLAVE_ON)

		Prv_Slave_Set_general_cfg_Req slave_req;
		//把当前的消息信息传给从片，从片需要配置
		slave_req.general_info = param->general_info;
		//发送时间格式修改通知给从片
		SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_TIME_TYPE_CHANGE_IND, &slave_req, sizeof(slave_req));
#endif
		s_OSD_Time_type = param->general_info.DataDisplayForm;
	}
//step6:
	//step6: 时区设置TimeZoneSlt TimeZoneSlt_enum
	//TODO
//step7:
	//step7: HDVGA输出选择OutPutType 0-DHD0   1-VGA
	//TODO
//finished:
	//添加报警端口修改，因为目前报警端口是同显的，
#if defined(SN9234H1)
	s_VoDevAlarmDflt = s_VoDevCtrlDflt;//
	{//n\p制式切换后，亮度值被修改，所以这里需要回复数据库的数值
		int i=0,ret=0;
		PRM_DISPLAY_CFG_CHAN disp_info;
		for(i=0;i<g_Max_Vo_Num;i++)
		{
			//图像配置
			ret= GetParameter(PRM_ID_DISPLAY_CFG,NULL,&disp_info,sizeof(disp_info),i+1,SUPER_USER_ID,NULL);
			if(ret!= PARAM_OK)
			{
				TRACE(SCI_TRACE_HIGH, MOD_PRV,"get Preview_VideoParam param error!");
			}	
			ret = Preview_SetVideoParam(i,&disp_info);
			if(ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_HIGH, MOD_PRV,"Preview_initVideoParam error1!");
			}
		}
	}
#endif
	rsp->result = 0;
    pthread_mutex_unlock(&s_osd_mutex);
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_MCC_SetGeneralCfg
Description: 通用配置消息处理程序
Calls: 
Called By: //
Input: // // param:通用配置预览模块结构体
			rsp:返回GUI的RSP
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_MCC_SetGeneralCfg(const Prv_Slave_Set_general_cfg_Rsp *param)
{
	VO_INTF_SYNC_E enIntfSync;		/*VGA分辨率*/
	VO_DEV VoDev;					/*GUI输出设备*/
	HI_S32 s32NPFlag=0;				/*NP制式*/
	int Changeflag=0,osd_flag=0;			//变化标志位，第0位表示N\P制式变化，第1位表示分辨率变化，第2位表示输出设备变化
	//printf("###########PRV_MSG_MCC_SetGeneralCfg Get msg !######################\n");

	if (NULL == param)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	
	/*NP制式*/
	s32NPFlag = (0 == param->general_info.CVBSOutputType) ? 1 : 0;
	/*VGA分辨率*/
	switch(param->general_info.VGAResolution)
	{
	case VGA_1080P:
	case VGA_720P:
	case VGA_1024X768:	
		enIntfSync = VO_OUTPUT_720P60;
		break;
	case VGA_1280X1024:	
		enIntfSync = VO_OUTPUT_720P60;
		break;
	case VGA_800X600:
		enIntfSync = VO_OUTPUT_800x600_60;
		break;
	case VGA_1366x768:
		enIntfSync = VO_OUTPUT_1366x768_60;
		break;
	case VGA_1440x900:
		enIntfSync = VO_OUTPUT_1440x900_60;
		break;
	default:
		RET_FAILURE("Unsupport VGA Resolution");
	}

	/*GUI输出设备*/
	switch(param->general_info.OutputSel)
	{
	case 0:
		VoDev = HD;
		break;
	case 1:
		VoDev = s_VoSecondDev;
		break;
	
#if defined(SN9234H1)
		case 2:	
		VoDev = SD;
#elif defined(Hi3531)
		case 2:
		VoDev = DSD1;
#endif
		RET_FAILURE("SD is not supported for Gui!");
	default :
		RET_FAILURE("general_info.OutputSel is out off range!");
	}
    pthread_mutex_lock(&s_osd_mutex);

//step1:
	//step1: CVBS输出制式
	if (s32NPFlag != s_s32NPFlagDflt)
	{
		//Changeflag |= 1<<0; 
	}
#if defined(SN9234H1)
	if( (enIntfSync != s_astVoDevStatDflt[HD].stVoPubAttr.enIntfSync) ||( param->general_info.VGAResolution != s_s32VGAResolution))
	{
		Changeflag |= 1<<1; 
		s_s32VGAResolution = param->general_info.VGAResolution;
	}
#else
	if( (enIntfSync != s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfSync) ||( param->general_info.VGAResolution != s_s32VGAResolution))
	{
		Changeflag |= 1<<1; 
		s_s32VGAResolution = param->general_info.VGAResolution;
	}
#endif

	if (s_VoDevCtrlDflt != VoDev)
	{
		Changeflag |= 1<<2; 
	}
	switch(Changeflag)
	{
		case 1://只有N\P制式变化
		case 3://N\P制式变化、分辨率变化
		case 5://N\P制式变化、输出设备变化
		case 7://N\P制式变化、分辨率变化、输出设备变化
		{
			s_s32NPFlagDflt = s32NPFlag;
			s_State_Info.bIsRe_Init = 0;
			if((s_State_Info.bIsOsd_Init))
			{
#if defined(SN9234H1)
				Prv_OSD_Close_fb(HD);
				Prv_OSD_Close_fb(AD);
				Prv_OSD_Close_fb(SD);
#else
				Prv_OSD_Close_fb(DHD0);
				//Prv_OSD_Close_fb(DSD0);
#endif				
				osd_flag = 1;
			}
			PRV_CloseVoFb(s_VoDevCtrlDflt);
			
			//先释放所有绑定关系
#if defined(SN9234H1)
			PRV_ViUnBindAllVoChn(VoDev);
			PRV_VdecUnBindAllVoChn1(VoDev);
			PRV_ViUnBindAllVoChn(s_VoSecondDev);
			PRV_VdecUnBindAllVoChn1(s_VoSecondDev);
			//VI
			PRV_ViInit();
			//VO
			s_astVoDevStatDflt[HD].stVoPubAttr.enIntfSync = enIntfSync;
			PRV_ResetVoDev(HD);
#else
		//	PRV_ViUnBindAllVoChn(VoDev);
			PRV_VdecUnBindAllVpss(VoDev);
		//	PRV_ViUnBindAllVoChn(s_VoSecondDev);
		//	PRV_VdecUnBindAllVoChn1(s_VoSecondDev);
			//VI
			PRV_ViInit();
			//VO
			s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfSync = enIntfSync;
			PRV_ResetVoDev(DHD0);
#endif			
			PRV_ResetVoDev(s_VoSecondDev);
	
			PRV_OpenVoFb(VoDev);
			s_VoDevCtrlDflt = VoDev;
			if(osd_flag)
			{
				OSD_Set_Rec_Range_NP(s_s32NPFlagDflt);
#if defined(SN9234H1)
				Prv_OSD_Open_fb( HD);
				Prv_OSD_Open_fb( AD);
#else
				Prv_OSD_Open_fb( DHD0);
				//Prv_OSD_Open_fb( DSD0);
#endif				
			}
			s_State_Info.bIsRe_Init = 1;
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "step1: N/P mode changed! N-0,P-1: %d", s_s32NPFlagDflt);
		}
			break;
		case 2://只有分辨率变化
		{
#if defined(SN9234H1)
			s_astVoDevStatDflt[HD].stVoPubAttr.enIntfSync = enIntfSync;
			
			//printf("##########s_astVoDevStatDflt[HD]   s_VoDevCtrlDflt = %d,VoDev = %d################\n",s_VoDevCtrlDflt,VoDev);
			s_State_Info.bIsRe_Init = 0;
			if((s_State_Info.bIsOsd_Init))
			{
				Prv_OSD_Close_fb(HD);
				osd_flag = 1;
			}
			if(s_VoDevCtrlDflt == HD)
			{//如果当前输出设备在HD，那么需要关闭GUI
				PRV_CloseVoFb(s_VoDevCtrlDflt);
			}
			PRV_ResetVoDev(HD);
			if(s_VoDevCtrlDflt == HD)
			{//如果当前输出设备在HD，那么需要开启GUI
				PRV_OpenVoFb(s_VoDevCtrlDflt);
			}
			if(osd_flag)
			{
				OSD_Set_Rec_Range_NP(s_s32NPFlagDflt);
				Prv_OSD_Open_fb(HD);
			}
			s_State_Info.bIsRe_Init = 1;
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "step2: VGA resolution changed! %dx%d", s_astVoDevStatDflt[HD].stVideoLayerAttr.stDispRect.u32Width, s_astVoDevStatDflt[HD].stVideoLayerAttr.stDispRect.u32Height);
#else
			s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfSync = enIntfSync;
			
			//printf("##########s_astVoDevStatDflt[DHD0]   s_VoDevCtrlDflt = %d,VoDev = %d################\n",s_VoDevCtrlDflt,VoDev);
			s_State_Info.bIsRe_Init = 0;
			if((s_State_Info.bIsOsd_Init))
			{
				Prv_OSD_Close_fb(DHD0);
				osd_flag = 1;
			}
			if(s_VoDevCtrlDflt == DHD0)
			{//如果当前输出设备在HD，那么需要关闭GUI
				PRV_CloseVoFb(s_VoDevCtrlDflt);
			}
			PRV_ResetVoDev(DHD0);
			if(s_VoDevCtrlDflt == DHD0)
			{//如果当前输出设备在HD，那么需要开启GUI
				PRV_OpenVoFb(s_VoDevCtrlDflt);
			}
			if(osd_flag)
			{
				OSD_Set_Rec_Range_NP(s_s32NPFlagDflt);
				Prv_OSD_Open_fb(DHD0);
			}
			s_State_Info.bIsRe_Init = 1;
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "step2: VGA resolution changed! %dx%d", s_astVoDevStatDflt[DHD0].stVideoLayerAttr.stDispRect.u32Width, s_astVoDevStatDflt[DHD0].stVideoLayerAttr.stDispRect.u32Height);
#endif
		}
			break;
		case 4://只有输出设备变化
		{
			s_State_Info.bIsRe_Init = 0;
			PRV_CloseVoFb(s_VoDevCtrlDflt);
			PRV_OpenVoFb(VoDev);
			s_State_Info.bIsRe_Init = 1;
			s_VoDevCtrlDflt = VoDev;
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "do nothing!! step4: Default GUI output dev changed! DHD0-0,AD-1,SD-2: %d", s_VoDevCtrlDflt);
		}
			break;
		case 6://分辨率变化、输出设备变化
		{
#if defined(SN9234H1)
			s_astVoDevStatDflt[HD].stVoPubAttr.enIntfSync = enIntfSync;
			s_State_Info.bIsRe_Init = 0;
			if((s_State_Info.bIsOsd_Init))
			{
				Prv_OSD_Close_fb(HD);
				osd_flag = 1;
			}
			PRV_CloseVoFb(s_VoDevCtrlDflt);
			PRV_ResetVoDev(HD);
			PRV_OpenVoFb(VoDev);
			s_VoDevCtrlDflt = VoDev;
			if(osd_flag)
			{
				OSD_Set_Rec_Range_NP(s_s32NPFlagDflt);
				Prv_OSD_Open_fb(HD);
			}
			s_State_Info.bIsRe_Init = 1;
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "step2: VGA resolution changed! %dx%d", s_astVoDevStatDflt[HD].stVideoLayerAttr.stDispRect.u32Width, s_astVoDevStatDflt[HD].stVideoLayerAttr.stDispRect.u32Height);
#else
			s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfSync = enIntfSync;
			s_State_Info.bIsRe_Init = 0;
			if((s_State_Info.bIsOsd_Init))
			{
				Prv_OSD_Close_fb(DHD0);
				osd_flag = 1;
			}
			PRV_CloseVoFb(s_VoDevCtrlDflt);
			PRV_ResetVoDev(DHD0);
			PRV_OpenVoFb(VoDev);
			s_VoDevCtrlDflt = VoDev;
			if(osd_flag)
			{
				OSD_Set_Rec_Range_NP(s_s32NPFlagDflt);
				Prv_OSD_Open_fb(DHD0);
			}
			s_State_Info.bIsRe_Init = 1;
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "step2: VGA resolution changed! %dx%d", s_astVoDevStatDflt[DHD0].stVideoLayerAttr.stDispRect.u32Width, s_astVoDevStatDflt[DHD0].stVideoLayerAttr.stDispRect.u32Height);
#endif
		}
			break;
		default:
			break;	
 	}
	////2010-9-30
	if(Changeflag & (1<<0))
	{
		VIDEO_FRAME_INFO_S *pstVFrame;
		pstVFrame = (0==s_s32NPFlagDflt)?&s_stUserFrameInfo_P:&s_stUserFrameInfo_N;
#if defined(SN9234H1)
		CHECK(HI_MPI_VI_SetUserPic(0, 0, pstVFrame));
#endif	
		Loss_State_Pre = 0;
	}
//step2:
	//step2: 显示器分辨率  对应enum VGAResolution_enum{VGA_1024X768,VGA_1280_960,VGA_1280X1024}
//step3:
	//step3: 电视去抖NoShake   0-不启动   1-启动
	Prv_Set_Flicker(param->general_info.NoShake);
//step4:
	//step4: 输出设备选择 0-DHD0/VGA   1-CVBS1 2- CVBS2
	s_u32GuiWidthDflt = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stDispRect.u32Width;
	s_u32GuiHeightDflt = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stDispRect.u32Height;
//step5:
	//step5: 日期显示格式DataDisplayForm  0为 YYYY-MM-DD   1为 MM-DD-YYYY   2为DD-MM-YYYY   3为YYYY年MM月DD日   4为MM月DD日YYYY年
	//TODO
	if(param->general_info.DataDisplayForm != s_OSD_Time_type)
	{
		s_OSD_Time_type = param->general_info.DataDisplayForm;
	}
    pthread_mutex_unlock(&s_osd_mutex);

	RET_SUCCESS("");
}
/*************************************************
Function: //PRV_MSG_MCC_SetGeneralCfg_Rsp
Description: 从片返回后通用配置消息处理程序
Calls: 
Called By: //
Input: // // msg_req:通用配置消息
			rsp:返回GUI的RSP
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_MCC_SetGeneralCfg_Rsp(const Prv_Slave_Set_general_cfg_Rsp *slave_rsp,Set_general_cfg_Rsp *rsp)
{
	
	if (NULL == slave_rsp || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->result = -1;
	rsp->general_info = slave_rsp->general_info;
	
	//如果从片返回，那么置位从片的返回标志位
	s_State_Info.bIsReply |= 1<<(slave_rsp->slaveid-1);
	
	if(slave_rsp->result == SN_RET_OK)
	{	//如果从片返回正确值
		s_State_Info.g_slave_OK = 1<<(slave_rsp->slaveid-1);
	}
	//如果返回值加1 等于1左移芯片个数的数值，那么表示所有从片都返回了
	if((s_State_Info.bIsReply+1) == 1<<(PRV_CHIP_NUM-1))	
	{
		s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
		TimerPause(s_State_Info.f_timer_handle); //暂停定时器
		s_State_Info.bIsReply = 0;//回复状态退出
		s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
		if((s_State_Info.g_slave_OK+1) == 1<<(PRV_CHIP_NUM-1))
		{//如果返回的消息中没有错误消息，那么此处理通用配置消息
			PRV_MSG_MCC_SetGeneralCfg(slave_rsp);//主片进入画面模式处理
			rsp->result = 0;
		}
		s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
		if(s_State_Info.Prv_msg_Cur)
		{
			SN_FREE(s_State_Info.Prv_msg_Cur);//释放已完成的消息结构体指针
		}
	}
	PRV_SetGuiAlpha(0, 0);
	
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_SetChnCfg
Description: //主片通道配置消息处理
//点位控制下，需调用此接口配置通道名称OSD
Calls: 
Called By: //
Input: // // msg_req:通用配置消息
			rsp : 回复给GUI的消息
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
int PRV_MSG_SetChnCfg(Set_chn_cfg_Req *param, Set_chn_cfg_Rsp *rsp)
{
	HI_S32 s32Ret = -1;
	//Set_chn_cfg_Req *param = (Set_chn_cfg_Req *)msg_req->para;
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->chn = param->chn;
	rsp->result = -1;
	rsp->chn_info = param->chn_info;
	//printf("11111vparam->chn_info.ChannelName = %s  ,param->chn_info.ChanNameDsp=%d,param->chn_info.ChanDataDsp=%d\n",param->chn_info.ChannelName,param->chn_info.ChanNameDsp,param->chn_info.ChanDataDsp);
	//TODO
	if(param->reserve[0] != 1)//通道名称配置
	{
		if(param->reserve[1] != 1 && ScmGetListCtlState() == 1)//点位控制下配置通道名称OSD不及时生效
		{
			rsp->result = 0;
			ScmAllChnSetOsd(EnterListCtl);
			return HI_SUCCESS;
		}
		
		if(param->chn == 0)
		{//如果通道号为0，表示全部通道配置
			int i = 0;
			for(i = 0; i < g_Max_Vo_Num; i++)
			{
			//通道名称配置
				//if(param->chn_info.ChanNameDsp)
				{//
					s32Ret = OSD_Set_Ch(i, param->chn_info.ChannelName);	//显示通道名称
					if(s32Ret != HI_SUCCESS)
					{
						TRACE(SCI_TRACE_HIGH, MOD_PRV,"PRV_MSG_SetChnCfg OSD_Set_Ch faild 0x%x!\n",s32Ret);
						rsp->chn = i;
						rsp->result = -1;
						RET_FAILURE("");
					}
						
				}
				s32Ret = OSD_Ctl(i, param->chn_info.ChanNameDsp,OSD_NAME_TYPE);
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH, MOD_PRV,"PRV_MSG_SetChnCfg OSD_Ctl name faild 0x%x!\n",s32Ret);
					rsp->chn = i;
					rsp->result = -1;
					RET_FAILURE("");
				}
				#if 0
				//日期显示
				s32Ret = OSD_Ctl(i,param->chn_info.ChanDataDsp,OSD_TIME_TYPE);
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH, MOD_PRV,"PRV_MSG_SetChnCfg OSD_Ctl time faild 0x%x!\n",s32Ret);
					rsp->chn = i;
					rsp->result = -1;
					RET_FAILURE("");
				}
				#endif
			}
#if 0
#if defined(SN_SLAVE_ON)		
			//发送消息给从片
			{
				Prv_Slave_Set_chn_cfg_Req slave_req;
				slave_req.chn_info = param->chn_info;
				slave_req.chn = param->chn;
				
				SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_SET_CHN_CFG_REQ, &slave_req, sizeof(slave_req));
				//启动定时器
				TimerReset(s_State_Info.f_timer_handle,10);
				TimerResume(s_State_Info.f_timer_handle,0);
				PRV_Msg_Cpy(msg_req);		//保存当前消息，以便后面消息重发
				s32Ret = SN_SLAVE_MSG;
			}
#endif
#endif
		}
		else
		{
			//通道名称配置
			//if(param->chn_info.ChanNameDsp)
			{//
				s32Ret = OSD_Set_Ch(param->chn - 1, param->chn_info.ChannelName);	//显示通道名称
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH, MOD_PRV,"PRV_MSG_SetChnCfg OSD_Set_Ch faild 0x%x!\n",s32Ret);
					rsp->result = -1;
					RET_FAILURE("");
				}
			}
			s32Ret = OSD_Ctl(param->chn-1, param->chn_info.ChanNameDsp, OSD_NAME_TYPE);
			if(s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_HIGH, MOD_PRV,"PRV_MSG_SetChnCfg OSD_Ctl faild 0x%x!\n",s32Ret);
				rsp->result = -1;
				RET_FAILURE("");
			}
			#if 0
			//日期显示
			s32Ret = OSD_Ctl(param->chn-1,param->chn_info.ChanDataDsp,OSD_TIME_TYPE);
			if(s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_HIGH, MOD_PRV,"PRV_MSG_SetChnCfg OSD_Ctl time faild 0x%x!\n",s32Ret);
				rsp->result = -1;
				RET_FAILURE("");
			}
			#endif
#if 0
#if defined(SN_SLAVE_ON)		
			if(param->chn > PRV_CHAN_NUM)//如果通道号大于主片最大通道数
			{
				//发送消息给从片
				Prv_Slave_Set_chn_cfg_Req slave_req;
				
				slave_req.chn_info = param->chn_info;
				slave_req.chn = param->chn;
				
				SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_SET_CHN_CFG_REQ, &slave_req, sizeof(slave_req));
				//启动定时器
				TimerReset(s_State_Info.f_timer_handle,10);
				TimerResume(s_State_Info.f_timer_handle,0);
				PRV_Msg_Cpy(msg_req);		//保存当前消息，以便后面消息重发
				s32Ret = SN_SLAVE_MSG;
			}
#endif	
#endif
		}
	}
	else//日期显示
	{
		//printf("param->chn_info.ChanDataDsp: %d\n", param->chn_info.ChanDataDsp);
		s32Ret = OSD_Ctl(0, param->chn_info.ChanDataDsp, OSD_TIME_TYPE);
		if(s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV,"PRV_MSG_SetChnCfg OSD_Ctl time faild 0x%x!\n",s32Ret);
			rsp->result = -1;
			RET_FAILURE("");
		}		
	}
	rsp->result = 0;
	return s32Ret;
}
/*************************************************
Function: //PRV_MSG_MCC_SetChnCfg_Rsp
Description: //从片通道配置消息处理
Calls: 
Called By: //
Input: // // msg_req:通用配置消息
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_MCC_SetChnCfg_Rsp(const Prv_Slave_Set_chn_cfg_Rsp *slave_rsp,Set_chn_cfg_Rsp *rsp)
{
	
	if (NULL == slave_rsp || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->chn = slave_rsp->chn;
	rsp->result = -1;
	rsp->chn_info = slave_rsp->chn_info;
	
	//如果从片返回，那么置位从片的返回标志位
	s_State_Info.bIsReply |= 1<<(slave_rsp->slaveid-1);
	
	if(slave_rsp->result == SN_RET_OK)
	{	//如果从片返回正确值
		s_State_Info.g_slave_OK = 1<<(slave_rsp->slaveid-1);
	}
	//如果返回值加1 等于1左移芯片个数的数值，那么表示所有从片都返回了
	if((s_State_Info.bIsReply+1) == 1<<(PRV_CHIP_NUM-1))	
	{
		if((s_State_Info.g_slave_OK+1) == 1<<(PRV_CHIP_NUM-1))
		{//如果返回的消息中没有错误消息
			rsp->result = 0;
		}
		s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
		TimerPause(s_State_Info.f_timer_handle); //暂停定时器
		s_State_Info.bIsReply = 0;//回复状态退出
		s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
		s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
		if(s_State_Info.Prv_msg_Cur)
		{
			SN_FREE(s_State_Info.Prv_msg_Cur);//释放已完成的消息结构体指针
		}
		
	}
	
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_SetChnCover
Description: //主片通道遮盖配置消息处理
Calls: 
Called By: //
Input: // // param:通道遮盖请求消息结构体
			rsp: 回复消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_SetChnCover(const SN_MSG *msg_req, Set_chn_cover_Rsp *rsp)
{
	int i=0,ret=-1;
	Set_chn_cover_Req *param = (Set_chn_cover_Req *)msg_req->para;
	
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->chn = param->chn;
	rsp->result = -1;
	rsp->cover_info = param->cover_info;
	
	//TODO
	if(param->chn == 0)
	{//如果通道数为0，那么表示所有通道配置
		for(i=0;i<PRV_CHAN_NUM;i++)
		{
			ret = OSD_Mask_update(i,param->cover_info.AreaHideScape,MAX_HIDE_AREA_NUM);
			if(ret != HI_SUCCESS)
			{
				rsp->chn = i;
				rsp->result = -1;
				RET_FAILURE("");
			}
			ret = OSD_Mask_Ctl(i,param->cover_info.EnableAreaHide);
			if(ret != HI_SUCCESS)
			{
				rsp->chn = i;
				rsp->result = -1;
				RET_FAILURE("");
			}
		}
#if defined(SN_SLAVE_ON)		
		{//发送消息给从片
			Prv_Slave_Set_chn_cover_Req slave_req;
			slave_req.cover_info = param->cover_info;
			slave_req.chn = param->chn;
			
			SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_SET_CHN_COVER_REQ, &slave_req, sizeof(slave_req));
			//启动定时器
			TimerReset(s_State_Info.f_timer_handle,10);
			TimerResume(s_State_Info.f_timer_handle,0);
			PRV_Msg_Cpy(msg_req);		//保存当前消息，以便后面消息重发
			ret = SN_SLAVE_MSG;
			return ret;
		}
#endif
	}
	else
	{
#if defined(SN_SLAVE_ON)	
		if(param->chn > PRV_CHAN_NUM)
		{//如果通道数大于主片最大通道数，那么发送消息给从片
			//发送消息给从片
			Prv_Slave_Set_chn_cover_Req slave_req;
			slave_req.cover_info = param->cover_info;
			slave_req.chn = param->chn;
			
			SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_SET_CHN_COVER_REQ, &slave_req, sizeof(slave_req));
			//启动定时器
			TimerReset(s_State_Info.f_timer_handle,10);
			TimerResume(s_State_Info.f_timer_handle,0);
			PRV_Msg_Cpy(msg_req);		//保存当前消息，以便后面消息重发
			ret = SN_SLAVE_MSG;
			return ret;
		}
#endif		
		//通道号在主片不要发送消息给从片
		ret = OSD_Mask_update(param->chn-1,param->cover_info.AreaHideScape,MAX_HIDE_AREA_NUM);
		if(ret != HI_SUCCESS)
		{
			rsp->result = -1;
			RET_FAILURE("");
		}
		ret = OSD_Mask_Ctl(param->chn-1,param->cover_info.EnableAreaHide);
		if(ret != HI_SUCCESS)
		{
			rsp->result = -1;
			RET_FAILURE("");
		}
	}
	rsp->result = 0;
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_MCC_SetChnCover_Rsp
Description: //主片通道遮盖配置消息处理
Calls: 
Called By: //
Input: // // param:通道遮盖请求消息结构体
			rsp: 回复消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_MCC_SetChnCover_Rsp(const Prv_Slave_Set_chn_cover_Rsp *slave_rsp,Set_chn_cover_Rsp *rsp)
{
	if (NULL == slave_rsp || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->chn = slave_rsp->chn;
	rsp->result = -1;
	rsp->cover_info = slave_rsp->cover_info;
	
	//TODO
	//如果从片返回，那么置位从片的返回标志位
	s_State_Info.bIsReply |= 1<<(slave_rsp->slaveid-1);
	
	if(slave_rsp->result == SN_RET_OK)
	{	//如果从片返回正确值
		s_State_Info.g_slave_OK = 1<<(slave_rsp->slaveid-1);
	}
	//如果返回值加1 等于1左移芯片个数的数值，那么表示所有从片都返回了
	if((s_State_Info.bIsReply+1) == 1<<(PRV_CHIP_NUM-1))	
	{
		if((s_State_Info.g_slave_OK+1) == 1<<(PRV_CHIP_NUM-1))
		{//如果返回的消息中没有错误消息
			rsp->result = 0;
		}
		s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
		TimerPause(s_State_Info.f_timer_handle); //暂停定时器
		s_State_Info.bIsReply = 0;//回复状态退出
		s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
		s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
		if(s_State_Info.Prv_msg_Cur)
		{
			SN_FREE(s_State_Info.Prv_msg_Cur);//释放已完成的消息结构体指针
		}
	}
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_SetDisplay_Time
Description: //通道效果实时显示消息处理
Calls: 
Called By: //
Input: // // param:通道效果实时显示请求消息结构体
			rsp: 回复消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_SetDisplay_Time(const Chn_disp_change_Req *param, Msg_id_prv_Rsp *rsp)
{
	HI_S32 s32Ret=-1;
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->chn = param->chn;
	rsp->result = -1;
	//TODO
	switch(param->index)
	{
		case 0:
			//对比度
			s32Ret = Preview_SetVideo_Cont(param->chn,param->value);
			if(s32Ret != HI_SUCCESS)
			{
				rsp->result = -1;
				RET_FAILURE("Preview_SetVideo_Cont error!");
			}
			break;
		case 1:
			//亮度
			s32Ret = Preview_SetVideo_Brt(param->chn,param->value);
			if(s32Ret != HI_SUCCESS)
			{
				rsp->result = -1;
				RET_FAILURE("Preview_SetVideo_Brt error!");
			}
			break;
		case 2:
			//色调
			s32Ret = Preview_SetVideo_Hue(param->chn,param->value);
			if(s32Ret != HI_SUCCESS)
			{
				rsp->result = -1;
				RET_FAILURE("Preview_SetVideo_Hue error!");
			}
			break;
		case 3:
			//饱和度
			s32Ret = Preview_SetVideo_Sat(param->chn,param->value);
			if(s32Ret != HI_SUCCESS)
			{
				rsp->result = -1;
				RET_FAILURE("Preview_SetVideo_Sat error!");
			}
			break;
		case 4:
			//x横坐标位移
			s32Ret = Preview_SetVideo_x(param->chn,param->value);
			if(s32Ret != HI_SUCCESS)
			{
				rsp->result = -1;
				RET_FAILURE("Preview_SetVideo_x error!");
			}
			break;
		case 5:
			//y纵坐标位移
			s32Ret = Preview_SetVideo_y(param->chn,param->value);
			if(s32Ret != HI_SUCCESS)
			{
				rsp->result = -1;
				RET_FAILURE("Preview_SetVideo_y error!");
			}
			break;
		default:
			rsp->result = -1;
			RET_FAILURE("the param is not surpport!");
			break;
	}
	rsp->result = 0;
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_SetChnDisplay
Description: //通道显示效果配置消息处理
Calls: 
Called By: //
Input: // // param:通道显示效果配置请求消息结构体
			rsp: 回复消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_SetChnDisplay(const Set_chn_display_Req *param, Set_chn_display_Rsp *rsp)
{
	HI_S32 s32Ret=-1;
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->chn = param->chn;
	rsp->result = -1;
	rsp->display_info = param->display_info;
	
	if (param->chn > g_Max_Vo_Num)
	{
		RET_FAILURE("param->chn out off range:0~16");
	}
	//TODO
	if(param->chn == 0)
	{//如果通道号为0，表示全部通道配置
		int i=0;
		for(i=0;i<g_Max_Vo_Num;i++)
		{
			s32Ret = Preview_SetVideoParam(i,(PRM_DISPLAY_CFG_CHAN *)&param->display_info);
			if(s32Ret != HI_SUCCESS)
			{
				rsp->chn = i;
				rsp->result = -1;
				RET_FAILURE("Preview_SetVideoParam error!");
			}
		}
	}
	else
	{
		s32Ret = Preview_SetVideoParam(param->chn-1,(PRM_DISPLAY_CFG_CHAN *)&param->display_info);
		if(s32Ret != HI_SUCCESS)
		{
			rsp->result = -1;
			RET_FAILURE("Preview_SetVideoParam error!");
		}
	}
	rsp->result = 0;
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_SetChnDisplay
Description: //主片通道OSD配置消息处理
Calls: 
Called By: //
Input: // // msg_req:通道OSD配置请求消息结构体
			rsp: 回复消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_SetChnOsd(const SN_MSG *msg_req, Set_chn_osd_Rsp *rsp)
{
	HI_S32 s32Ret=-1;
	Set_chn_osd_Req *param = (Set_chn_osd_Req*)msg_req->para;
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->chn = param->chn;
	rsp->result = -1;
	rsp->osd_info = param->osd_info;
	
	//TODO
	if(param->chn == 0)
	{//如果通道号为0，表示全部通道配置
		int i = 0;
		for(i = 0;i < g_Max_Vo_Num; i++)
		{
			/*//时间位置
			s32Ret = OSD_Set_Time_xy(i,param->osd_info.ChannelTimePosition_x,param->osd_info.ChannelTimePosition_y);
			if(s32Ret != HI_SUCCESS)
			{
				rsp->chn = i;
				rsp->result = -1;
				RET_FAILURE("");
			}
			//通道名称位置
			s32Ret = OSD_Set_CH_xy(i,param->osd_info.ChannelNamePosition_x,param->osd_info.ChannelNamePosition_y);
			if(s32Ret != HI_SUCCESS)
			{
				rsp->chn = i;
				rsp->result = -1;
				RET_FAILURE("");
			}*/
			s32Ret = OSD_Set_xy(i,param->osd_info.ChannelNamePosition_x,param->osd_info.ChannelNamePosition_y,param->osd_info.ChannelTimePosition_x,param->osd_info.ChannelTimePosition_y);
			if(s32Ret != HI_SUCCESS)
			{
				rsp->chn = i;
				rsp->result = -1;
				RET_FAILURE("");
			}
		}
#if defined(SN_SLAVE_ON)		
		{//发送消息给从片
			Prv_Slave_Set_chn_osd_Req slave_req;
			slave_req.osd_info = param->osd_info;
			slave_req.chn = param->chn;
			
			SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_SET_CHN_OSD_REQ, &slave_req, sizeof(slave_req));
			//启动定时器
			TimerReset(s_State_Info.f_timer_handle,10);
			TimerResume(s_State_Info.f_timer_handle,0);
			PRV_Msg_Cpy(msg_req);		//保存当前消息，以便后面消息重发
			s32Ret = SN_SLAVE_MSG;
			return s32Ret;
		}
#endif
	}
	else
	{
		//时间位置
	/*	s32Ret = OSD_Set_Time_xy(param->chn-1,param->osd_info.ChannelTimePosition_x,param->osd_info.ChannelTimePosition_y);
		if(s32Ret != HI_SUCCESS)
		{
			rsp->result = -1;
			RET_FAILURE("");
		}
		//通道名称位置
		s32Ret = OSD_Set_CH_xy(param->chn-1,param->osd_info.ChannelNamePosition_x,param->osd_info.ChannelNamePosition_y);
		if(s32Ret != HI_SUCCESS)
		{
			rsp->result = -1;
			RET_FAILURE("");
		}*/
		s32Ret = OSD_Set_xy(param->chn-1,param->osd_info.ChannelNamePosition_x,param->osd_info.ChannelNamePosition_y,param->osd_info.ChannelTimePosition_x,param->osd_info.ChannelTimePosition_y);
		if(s32Ret != HI_SUCCESS)
		{
			rsp->result = -1;
			RET_FAILURE("");
		}
#if defined(SN_SLAVE_ON)		
		if(param->chn > PRV_CHAN_NUM)
		{//如果通道号在从片上，发送消息给从片
			Prv_Slave_Set_chn_osd_Req slave_req;
			slave_req.osd_info= param->osd_info;
			slave_req.chn = param->chn;
			
			SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_SET_CHN_OSD_REQ, &slave_req, sizeof(slave_req));
			//启动定时器
			TimerReset(s_State_Info.f_timer_handle,10);
			TimerResume(s_State_Info.f_timer_handle,0);
			PRV_Msg_Cpy(msg_req);		//保存当前消息，以便后面消息重发
			s32Ret = SN_SLAVE_MSG;
			return s32Ret;
		}
#endif	
		//rsp->chn = param->chn-1;
	}
	rsp->result = 0;
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_MCC_SetChnOsd_Rsp
Description: //从片返回通道OSD配置消息处理
Calls: 
Called By: //
Input: // // msg_req:通道OSD配置请求消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_MCC_SetChnOsd_Rsp(const Prv_Slave_Set_chn_osd_Rsp *slave_rsp, Set_chn_osd_Rsp *rsp)
{
	
	if (NULL == slave_rsp || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->chn = slave_rsp->chn;
	rsp->result = -1;
	rsp->osd_info = slave_rsp->osd_info;
	
	//TODO
	//TODO
	//如果从片返回，那么置位从片的返回标志位
	s_State_Info.bIsReply |= 1<<(slave_rsp->slaveid-1);
	if(slave_rsp->result == SN_RET_OK)
	{	//如果从片返回正确值
		s_State_Info.g_slave_OK = 1<<(slave_rsp->slaveid-1);
	}
	//如果返回值加1 等于1左移芯片个数的数值，那么表示所有从片都返回了
	if((s_State_Info.bIsReply+1) == 1<<(PRV_CHIP_NUM-1))	
	{
		if((s_State_Info.g_slave_OK+1) == 1<<(PRV_CHIP_NUM-1))
		{//如果返回的消息中没有错误消息
			rsp->result = 0;
		}
		s_State_Info.TimeoutCnt = HI_FALSE;//重新置位超时标志位
		TimerPause(s_State_Info.f_timer_handle); //暂停定时器
		s_State_Info.bIsReply = 0;//回复状态退出
		s_State_Info.bIsSlaveConfig = HI_FALSE;	//重新置位配置标志位
		s_State_Info.g_slave_OK = 0;	//重置回复数值标志位
		if(s_State_Info.Prv_msg_Cur)
		{
			SN_FREE(s_State_Info.Prv_msg_Cur);//释放已完成的消息结构体指针
		}
	}
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_SetVoPreview
Description: //输出设备预览配置
Calls: 
Called By: //
Input: // // param:预览输出配置请求消息结构体
			rsp: 回复消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_SetVoPreview(const SN_MSG *msg_req, Set_vo_preview_Rsp *rsp)
{
	HI_S32 i=0;
	unsigned char vodev = 0,vodev1=0;
	Set_vo_preview_Req *param = (Set_vo_preview_Req*)msg_req->para;
	
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	
	rsp->dev = param->dev;
	rsp->result = -1;
	rsp->preview_info = param->preview_info;
	rsp->preview_info_exp=param->preview_info_exp;
	vodev = param->dev;
	
#if defined(SN9234H1)

	vodev1 = vodev;
again:	
	if(vodev == SPOT_VO_DEV|| vodev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	if (vodev >= PRV_VO_MAX_DEV)
		
#else

	if(vodev == DHD1)
		vodev = DSD0;
	vodev1 = vodev;
again:	
	if (vodev > DHD0)
		
#endif		
	{
		RET_FAILURE("param->dev out off range: 0,2");
	}	
	s_astVoDevStatDflt[vodev].enPreviewMode = param->preview_info.PreviewMode;
	/*配置预览通道顺序*/
	for (i = 0; i < g_Max_Vo_Num; i++)
	{	
		//配置预览单画面顺序
		s_astVoDevStatDflt[vodev].as32ChnOrder[SingleScene][i] = UCHAR2INIT32(param->preview_info.SingleOrder[i]);
		//配置预览4画面顺序
		s_astVoDevStatDflt[vodev].as32ChnOrder[FourScene][i] = UCHAR2INIT32( param->preview_info.FourOrder[i]);
		//配置预览8画面顺序
		s_astVoDevStatDflt[vodev].as32ChnOrder[EightScene][i] = UCHAR2INIT32( param->preview_info.EightOrder[i]);
		//配置预览16画面顺序
		s_astVoDevStatDflt[vodev].as32ChnOrder[SixteenScene][i] = UCHAR2INIT32( param->preview_info.SixteenOrder[i]);
		//配置单画面轮询通道顺序
		s_astVoDevStatDflt[vodev].as32ChnpollOrder[SingleScene][i] = UCHAR2INIT32( param->preview_info.SingleSel[i]);
		//配置4画面轮询通道顺序
		s_astVoDevStatDflt[vodev].as32ChnpollOrder[FourScene][i] = UCHAR2INIT32( param->preview_info.FourSel[i/4]);
		//配置8画面轮询通道顺序
		s_astVoDevStatDflt[vodev].as32ChnpollOrder[EightScene][i] = UCHAR2INIT32( param->preview_info.EightSel[i/8]);

	}
	for (i = 0; i < THREEINDEX; i++)
	{	
		//配置预览3画面顺序
		s_astVoDevStatDflt[vodev].as32ChnOrder[ThreeScene][i] = UCHAR2INIT32(param->preview_info_exp.ThreeOrder[i]);
		//配置3画面轮询通道顺序
		s_astVoDevStatDflt[vodev].as32ChnpollOrder[ThreeScene][i] = UCHAR2INIT32( param->preview_info_exp.ThreeSel[i/3]);
	}
	for (i = 0; i < FIVEINDEX; i++)
	{	
		 //配置预览5画面顺序
		s_astVoDevStatDflt[vodev].as32ChnOrder[FiveScene][i] = UCHAR2INIT32(param->preview_info_exp.FiveOrder[i]);
		 //配置5画面轮询通道顺序
		s_astVoDevStatDflt[vodev].as32ChnpollOrder[FiveScene][i] = UCHAR2INIT32( param->preview_info_exp.FiveSel[i/5]);
	}
	for (i = 0; i < SEVENINDEX; i++)
	{	
		 //配置预览7画面顺序
		s_astVoDevStatDflt[vodev].as32ChnOrder[SevenScene][i] = UCHAR2INIT32(param->preview_info_exp.SevenOrder[i]);
         //配置7画面轮询通道顺序
		s_astVoDevStatDflt[vodev].as32ChnpollOrder[SevenScene][i] = UCHAR2INIT32( param->preview_info_exp.SevenSel[i/7]);
	}
	for (i = 0; i < SIXINDEX; i++)
	{	
		//配置预览6画面顺序
		s_astVoDevStatDflt[vodev].as32ChnOrder[SixScene][i] = UCHAR2INIT32( param->preview_info.SixOrder[i]);
		//配置6画面轮询通道顺序
		s_astVoDevStatDflt[vodev].as32ChnpollOrder[SixScene][i] = UCHAR2INIT32( param->preview_info.Sixel[i/6]);
		//printf("#############s_astVoDevStatDflt[param->dev].as32ChnOrder[SixScene][i] = %d##############\n",s_astVoDevStatDflt[vodev].as32ChnOrder[SixScene][i]);
		//printf("#############s_astVoDevStatDflt[param->dev].as32ChnpollOrder[SixScene][i] = %d##############\n",s_astVoDevStatDflt[vodev].as32ChnpollOrder[SixScene][i]);
	}
	for (i = 0; i < NINEINDEX; i++)
	{	
		//配置预览9画面顺序
		s_astVoDevStatDflt[vodev].as32ChnOrder[NineScene][i] = UCHAR2INIT32( param->preview_info.NineOrder[i]);
		//配置9画面轮询通道顺序
		s_astVoDevStatDflt[vodev].as32ChnpollOrder[NineScene][i] = UCHAR2INIT32( param->preview_info.NineSel[i/9]);	
		//printf("#############s_astVoDevStatDflt[param->dev].as32ChnOrder[NineScene][i] = %d##############\n",s_astVoDevStatDflt[vodev].as32ChnOrder[NineScene][i]);
		//printf("#############s_astVoDevStatDflt[param->dev].as32ChnpollOrder[NineScene][i] = %d##############\n",s_astVoDevStatDflt[vodev].as32ChnpollOrder[NineScene][i]);
	}

#if defined(SN9234H1)
/*2010-11-4 通道显示与隐藏*/
	PRV_SortChnOrder(param->dev);
	if(vodev1 ==  vodev && vodev != SPOT_VO_DEV)
	{
		vodev =  (param->dev == HD)? s_VoSecondDev: HD;
		goto again;
	}	
#else
	/*设置设备的预览模式*/
	if(vodev1 == vodev)
	{
		vodev = (vodev == DHD0) ? DSD0 : DHD0;
		goto again;
	}	
#endif

#if defined(SN_SLAVE_ON)
	//发送消息给从片
	{
		Prv_Slave_Set_vo_preview_Req slave_req;
		int ret=0;
		
		slave_req.preview_info = param->preview_info;
		slave_req.preview_info_exp = param->preview_info_exp;
		slave_req.dev = rsp->dev;
		
		ret = SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_SET_VO_PREVIEW_REQ, &slave_req, sizeof(Prv_Slave_Set_vo_preview_Req));
	}		
#endif
	rsp->result = 0;
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_SetVoPreview_Adv
Description: //输出设备预览配置
Calls: 
Called By: //
Input: // // param:预览输出高级配置请求消息结构体
			rsp: 回复消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_SetVoPreview_Adv(const Set_vo_preview_Adv_Req *param, Set_vo_preview_Adv_Rsp *rsp)
{
	VO_DEV VoDev;

	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->dev = param->dev;
	rsp->result = -1;
	rsp->preview_adv_info = param->preview_adv_info;
	
	/*配置报警触发端口*/
	switch(param->preview_adv_info.AlarmHandlePort)/*报警触发端口对应0-DHD0/VGA, 2-DSD0/CVBS2*/
	{
#if defined(SN9234H1)
		case 0:
			VoDev = HD;
			break;
		case 1:
			VoDev = s_VoSecondDev;
			break;
		case 2:
			VoDev = SD;
			break;
#else
		case 0:
			VoDev = DHD0;
			break;
		case 1:
			VoDev = DSD0;
			break;
		case 2:
			VoDev = DSD0;
			break;
#endif			
		default:
			RET_FAILURE("param->preview_info.AlarmHandlePort out off range: 0~2");
	}
#if defined(SN9234H1)
	if (VoDev != s_VoDevAlarmDflt && (param->dev == HD || param->dev == s_VoSecondDev))/*目前只读取HD口（即SD口中的报警触发端口配置将被忽略）记录中的报警触发端口配置作为有效的报警触发端口*/
#else
	if (VoDev != s_VoDevAlarmDflt && (param->dev == DHD0 || param->dev == s_VoSecondDev))
#endif		
	{
		if (s_astVoDevStatDflt[s_VoDevAlarmDflt].bIsAlarm)
		{
			PRV_AlarmOff(s_VoDevAlarmDflt);
			PRV_AlarmChn(VoDev, s_astVoDevStatDflt[s_VoDevAlarmDflt].s32AlarmChn);
		}
		s_VoDevAlarmDflt = VoDev;
	}
	/*配置预览音频*/
	PRV_Set_AudioPreview_Enable(param->preview_adv_info.AudioPreview);	
	IsAudioOpen = param->preview_adv_info.AudioPreview[0];
	if(!IsAudioOpen)//关闭音频总开关后，重设状态
	{
		CurAudioChn = -1;
		PreAudioChn = -1;
		CurAudioPlayStat = 0;
		IsChoosePlayAudio = 0;
		Achn = -1;
	}
	else
	{		
		PRV_PlayAudio(VoDev);
	}
	rsp->result = 0;
	RET_SUCCESS("");
}
/*************************************************
Function: //PRV_MSG_SetPreview_Audio
Description: //输出设备预览音频映射配置
Calls: 
Called By: //
Input: // // param:预览输出高级配置请求消息结构体
			rsp: 回复消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_SetPreview_Audio(const Set_preview_Audio_Req *param, Set_preview_Audio_Rsp *rsp)
{
	//int i=0,j=0;
	//VO_CHN chn;
	
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->result = -1;
	rsp->preview_audio_info = param->preview_audio_info;
	//保存音频配置关系
	//PRV_Set_AudioMap(param->preview_audio_info.AudioMap);
	CHECK(PRV_PlayAudio(s_VoDevCtrlDflt));
	rsp->result = 0;
	RET_SUCCESS("");

}

//获取GUI所在输出口
STATIC HI_S32 PRV_MSG_GetGuiVo(Get_gui_vo_Rsp *rsp)
{
	if (NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}

	rsp->dev = s_VoDevCtrlDflt;
	
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_GetGuiVo
Description: //获取GUI所在输出口
Calls: 
Called By: //
Input: // // pdev_id:返回设备ID
Output: // 
Return: //详细参看文档的错误码
Others: //回放模块使用
***********************************************************************/
int PRV_GetGuiVo(int *pdev_id )
{
	if (NULL == pdev_id)
	{
		RET_FAILURE("NULL pointer!!!");
	}

	*pdev_id = s_VoDevCtrlDflt;
	
	RET_SUCCESS("");
}


/*************************************************
Function: //PRV_MSG_ChnZoomIn
Description: //通道电子放大消息处理
Calls: 
Called By: //
Input: // // msg_req:通道OSD配置请求消息结构体
		rsp : 回复给GUI的消息
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_ChnZoomIn(const SN_MSG *msg_req, Msg_id_prv_Rsp *rsp)
{
	Chn_zoom_in_Req *param = (Chn_zoom_in_Req *)msg_req->para;
	
	rsp->chn = param->chn;
	rsp->dev = 0;//param->dev;
	rsp->flag = 0;
	rsp->result = -1;
	
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	//CHECK_RET(PRV_ChnZoomIn(param->dev,param->chn, param->ratio, &param->point));
	CHECK_RET(PRV_ChnZoomIn(param->chn, param->ratio, &param->point));

	rsp->result = 0;
	RET_SUCCESS("");
}

HI_S32 PRV_GetVoChnAttr_ForPB(VO_DEV VoDev, VO_CHN VoChn, RECT_S *pstRect)
{
	HI_S32 div = 0, s32ChnCnt = 0;
	//VO_CHN_ATTR_S stChnAttr;
	//SIZE_S disp_size,img_size;	
	g_PlayInfo stPlayInfo;
	int u32Width = 1040, u32Height = 571;
	int u32Width_s = 0,u32Height_s = 0;
	PlayBack_GetPlaySize((HI_U32 *)&u32Width, (HI_U32 *)&u32Height);

	PRV_GetPlayInfo(&stPlayInfo);
	s32ChnCnt = stPlayInfo.ImagCount;
    u32Width_s = u32Width;
	u32Height_s = u32Height;
    if(s32ChnCnt == 9)
    {
		while(u32Width%6 != 0)
			u32Width++;
		while(u32Height%6 != 0)
			u32Height++;
	}

	div = sqrt(s32ChnCnt);		/* 计算每个通道的宽度和高度 */
	if(stPlayInfo.bISDB==1 || stPlayInfo.IsZoom==1)
	{
        pstRect->s32X = 0;
		pstRect->s32Y = 0;
		pstRect->u32Height = u32Height;
		pstRect->u32Width = u32Width;
		RET_SUCCESS("");
	}
	if(div == 1)
	{
		pstRect->s32X = 0;
		pstRect->s32Y = 0;
		pstRect->u32Height = u32Height;
		pstRect->u32Width = u32Width;
		RET_SUCCESS("");
	}
	else if(div == 2)
	{
		switch(VoChn)
		{
			case 0:
				pstRect->s32X = 0;
				pstRect->s32Y = 0;
				pstRect->u32Height = u32Height/2;
				pstRect->u32Width = u32Width/2;
				break;
			case 1:
				pstRect->s32X = u32Width/2;
				pstRect->s32Y = 0;
				pstRect->u32Height = u32Height/2;
				pstRect->u32Width = u32Width/2;
				break;
			case 2:
				pstRect->s32X = 0;
				pstRect->s32Y = u32Height/2;
				pstRect->u32Height = u32Height/2;
				pstRect->u32Width = u32Width/2;
				break;
			case 3:
				pstRect->s32X = u32Width/2;
				pstRect->s32Y = u32Height/2;
				pstRect->u32Height = u32Height/2;
				pstRect->u32Width = u32Width/2;
				break;
		}
	}
	else if(div == 3)
	{
		switch(VoChn)
		{
			case 0:
				pstRect->s32X = 0;
				pstRect->s32Y = 0;
				pstRect->u32Height = u32Height/3;
				pstRect->u32Width = u32Width/3;
				break;
			case 1:
				pstRect->s32X = u32Width/3;
				pstRect->s32Y = 0;
				pstRect->u32Height = u32Height/3;
				pstRect->u32Width = u32Width/3;
				break;
			case 2:
				pstRect->s32X = u32Width*2/3;
				pstRect->s32Y = 0;
				pstRect->u32Height = u32Height/3;
				pstRect->u32Width = u32Width/3;
				break;
			case 3:
				pstRect->s32X = 0;
				pstRect->s32Y = u32Height/3;
				pstRect->u32Height = u32Height/3;
				pstRect->u32Width = u32Width/3;
				break;
			case 4:
				pstRect->s32X = u32Width/3;
				pstRect->s32Y = u32Height/3;
				pstRect->u32Height = u32Height/3;
				pstRect->u32Width = u32Width/3;
				break;
			case 5:
				pstRect->s32X = u32Width*2/3;
				pstRect->s32Y = u32Height/3;
				pstRect->u32Height = u32Height/3;
				pstRect->u32Width = u32Width/3;
				break;
			case 6:
				pstRect->s32X = 0;
				pstRect->s32Y = u32Height*2/3;
				pstRect->u32Height = u32Height/3;
				pstRect->u32Width = u32Width/3;
				break;
			case 7:
				pstRect->s32X = u32Width/3;
				pstRect->s32Y = u32Height*2/3;
				pstRect->u32Height = u32Height/3;
				pstRect->u32Width = u32Width/3;
				break;
			case 8:
				pstRect->s32X = u32Width*2/3;
				pstRect->s32Y = u32Height*2/3;
				pstRect->u32Height = u32Height/3;
				pstRect->u32Width = u32Width/3;
				break;
		}
        if(s32ChnCnt == 9)
        {
		   if((VoChn + 1) % 3 == 0)//最后一列
			  pstRect->u32Width = u32Width_s- pstRect->s32X;
		   if(VoChn> 5 && VoChn< 9)//最后一行
		      pstRect->u32Height = u32Height_s- pstRect->s32Y;
        }
	}

#if 0	
	s32Ret = HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stChnAttr);
	printf("pstRect.s32X=%d, s32Y=%d, u32Width=%d, u32Height=%d\n", stChnAttr.stRect.s32X, stChnAttr.stRect.s32Y, stChnAttr.stRect.u32Width, stChnAttr.stRect.u32Height);

	CHECK_RET(PRV_GetVoDevDispSize(VoDev,&disp_size));
	printf("disp_size.u32Width=%d, u32Height=%d\n", disp_size.u32Width, disp_size.u32Height);

	CHECK_RET(PRV_GetVoDevImgSize(VoDev,&img_size));
	printf("img_size.u32Width=%d, u32Height=%d\n", img_size.u32Width, img_size.u32Height);
	
	pstRect->s32X = stChnAttr.stRect.s32X*disp_size.u32Width/img_size.u32Width;
	pstRect->s32Y = stChnAttr.stRect.s32Y*disp_size.u32Height/img_size.u32Height;
	pstRect->u32Height= stChnAttr.stRect.u32Height*disp_size.u32Height/img_size.u32Height;
	pstRect->u32Width= stChnAttr.stRect.u32Width*disp_size.u32Width/img_size.u32Width;
#endif	

	RET_SUCCESS("");
}

int PRV_GetVoChn_ForPB(Preview_Point stPoint)
{
	HI_S32 i = 0, s32ChnCnt = 0, div = 0;
	g_PlayInfo stPlayInfo;
	RECT_S *pstLayout = NULL;
	int u32Width = 1040, u32Height = 571;

	PlayBack_GetPlaySize((HI_U32 *)&u32Width, (HI_U32 *)&u32Height);
	
	PRV_GetPlayInfo(&stPlayInfo);
	s32ChnCnt = stPlayInfo.ImagCount;

	div = sqrt(s32ChnCnt);		/* 计算每个通道的宽度和高度 */
	
	if(div == 1)
	{
		pstLayout = s_astPreviewLayout1;
	}
	else if(div == 2)
	{
		pstLayout = s_astPreviewLayout4;
	}
	else
	{
		pstLayout = s_astPreviewLayout9;
	}
	
	//查找符合当前位置的通道索引ID
	if(stPlayInfo.bISDB==1)
	{
        return stPlayInfo.DBClickChn;
	}
	for (i = 0; i<s32ChnCnt; i++)
	{
		if (
			(pstLayout[i].s32X * u32Width) / (PRV_PREVIEW_LAYOUT_DIV) <= stPoint.x
			&& (pstLayout[i].s32Y * u32Height) / PRV_PREVIEW_LAYOUT_DIV <= stPoint.y
			&& ((pstLayout[i].s32X + pstLayout[i].u32Width) * u32Width) / PRV_PREVIEW_LAYOUT_DIV >= stPoint.x
			&& ((pstLayout[i].s32Y + pstLayout[i].u32Height) * u32Height) / PRV_PREVIEW_LAYOUT_DIV >= stPoint.y
			)
		{
			return i;
		}
	}
	

	return -1;
	
}
/*************************************************
Function: //PRV_MSG_GetChnByXY
Description: //获取坐标位置所在的通道号
Calls: 
Called By: //
Input: // // param:获取坐标位置请求消息结构体
			rsp: 回复消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
int PRV_MSG_GetChnByXY(const Get_chn_by_xy_Req *param, Get_chn_by_xy_Rsp *rsp)
{
	Preview_Point stPoint;
	HI_U32 u32Index = 0, i = 0,u32ChnNum = 0;
	VO_CHN VoChn = 0;
	RECT_S rect;
	RECT_S pstRect;
	//HI_U32 Max_num;
	unsigned char mode =s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode;
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	rsp->x = param->x;
	rsp->y = param->y;
	rsp->w = 0;
	rsp->h = 0;
	rsp->chn = 0;
#if defined(SN9234H1)
	rsp->dev = (s_VoDevCtrlDflt == HD)? HD : AD;
#else
	rsp->dev = (s_VoDevCtrlDflt == DHD0)? DHD0 : DSD0;
#endif
	rsp->result = -1;
	stPoint.x = param->x;
	stPoint.y = param->y;
	if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat != PRV_STAT_NORM)
	{
		if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_PB)
		{
			g_ChnPlayStateInfo stPlayStateInfo;
      
			VoChn = PRV_GetVoChn_ForPB(stPoint);
			PRV_GetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);
			if(PlayBack_QueryPbStat(VoChn)==HI_FAILURE)
	        {
	            rsp->result = -1;
                return HI_FAILURE;
		    }
			if(VoChn < 0)
			{
				RET_FAILURE("VoChn=-1, error");
			}

			PRV_GetVoChnAttr_ForPB(DHD0, VoChn, &pstRect);
			
			rsp->chn = VoChn;
			rsp->x = pstRect.s32X;
			rsp->y = pstRect.s32Y;
			rsp->w = pstRect.u32Width;
			rsp->h = pstRect.u32Height;
			
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VoChn=%d, pstRect.s32X=%d, s32Y=%d, u32Width=%d, u32Height=%d\n", VoChn, pstRect.s32X, pstRect.s32Y, pstRect.u32Width, pstRect.u32Height);

			rsp->result = 0;
			RET_SUCCESS("");
		}
		else
		{
			RET_FAILURE("not In PB or PIC or norm, Don't Get Rect By XY");
		}

	}
	
	if(s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsAlarm)
	{//报警状态
		VoChn = s_astVoDevStatDflt[s_VoDevCtrlDflt].s32AlarmChn;
	}
	else if(param->reserve[0] > 0)//直接带通道号
	{
		if(param->reserve[0] <= DEV_CHANNEL_NUM)
		{
			VoChn = param->reserve[0] - 1;
			if(PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VoChn) == HI_FAILURE)
			{
				RET_FAILURE("VoChn No In Current Layout!!!");			
			}
		}
		else
			RET_FAILURE("Invalid Parameter: VoChn!!!");			
	}
	else
	{
		CHECK_RET(PRV_Point2Index(&stPoint, &u32Index, s_astVoDevStatDflt[s_VoDevCtrlDflt].as32ChnOrder[mode]));
		if(s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsSingle && (mode != SingleScene) && (s_astVoDevStatDflt[s_VoDevCtrlDflt].s32DoubleIndex == 0))
		{
			mode = SingleScene;
		}
		VoChn = s_astVoDevStatDflt[s_VoDevCtrlDflt].as32ChnOrder[mode][u32Index];
		
#if defined(Hi3531)||defined(Hi3535)
		if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
		{
			VoChn = u32Index;
		}
#endif
	}
	if (VoChn < 0 || VoChn >= g_Max_Vo_Num)
	{//如果当前的通道为禁用通道，那么需要重新选择焦点位置
		switch(mode)
		{
			case SingleScene:
				u32ChnNum = 1;
				break;
			case TwoScene:
				u32ChnNum = 2;
				break;
			case ThreeScene:
				u32ChnNum = 3;
				break;
			case FourScene:
			case LinkFourScene:
				u32ChnNum = 4;
				break;
			case FiveScene:
				u32ChnNum = 5;
				break;
			case SixScene:
				u32ChnNum = 6;
				break;
			case SevenScene:
				u32ChnNum = 7;
				break;
			case EightScene:
				u32ChnNum = 8;
				break;
			case NineScene:
			case LinkNineScene:
				u32ChnNum = 9;
				break;
			case SixteenScene:
				u32ChnNum = 16;
				break;
			default:
				RET_FAILURE("Invalid Parameter: enPreviewMode");
		}
		for(i = 0; i < u32ChnNum; i++)
		{//如果当前通道隐藏，返回下一个通道，直至没有通道返回，那么返回错误
			if((u32Index+i) >= ((u32Index/u32ChnNum+1)*u32ChnNum))
			{
				u32Index = (u32Index/u32ChnNum)*u32ChnNum - i;
			}
			VoChn = s_astVoDevStatDflt[s_VoDevCtrlDflt].as32ChnOrder[mode][u32Index+i];

			if (VoChn < 0 || VoChn >= g_Max_Vo_Num)
			{
				continue;
			}
			else
			{
				break;
			}
		}
		if(i >= u32ChnNum)
		{//如果i的数值大于最大值，说明没有找到，那么返回错误
			RET_FAILURE("chn id hiden!!");
		}
	}
	CHECK_RET(PRV_GetVoChnDispRect_Forxy(s_VoDevCtrlDflt,VoChn, &rect));
	if(LayoutToSingleChn >= 0)
	{
		VoChn = LayoutToSingleChn;
		rect.s32X = 0;
		rect.s32Y = 0;
		rect.u32Height = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stDispRect.u32Height;
		rect.u32Width = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stDispRect.u32Width;			
	}
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "--------enPreviewStat: %d, enPreviewMode: %d, VoChn: %d, X: %d, Y: %d, Width: %d, Height: %d\n",
		s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat, s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode, VoChn, rect.s32X, rect.s32Y, rect.u32Width, rect.u32Height);
	rsp->chn = VoChn;
	rsp->x = rect.s32X;
	rsp->y = rect.s32Y;
	rsp->w = rect.u32Width;
	rsp->h = rect.u32Height;
	rsp->result = 0;
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_GetPrvMode
Description: //获取GUI所在输出设备的预览模式
Calls: 
Called By: //
Input: // // rsp: 回复消息结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_GetPrvMode(Get_prv_mode_Rsp *rsp)
{
	if (NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}

	rsp->result = -1;

	CHECK_RET(PRV_GetPrvMode(&rsp->prv_mode));

	rsp->result = 0;
	RET_SUCCESS("");
}

/*************************************************
Function: //PRV_MSG_GetRec_Resolution
Description: //获取GUI所在输出设备的预览模式
Calls: 
Called By: //
Input: // // req: 录像分辨率修改通知结构体
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: //
***********************************************************************/
STATIC HI_S32 PRV_MSG_GetRec_Resolution(const SN_MSG *msg_req)
{
	Prv_Rec_Osd_Resolution_Ind *req = (Prv_Rec_Osd_Resolution_Ind*)msg_req->para;
	if (NULL == req)
	{
		RET_FAILURE("NULL pointer!!!");
	}
    OSD_Get_Rec_Range_Ch(req->rec_group,req->chn,req->w,req->h);;
	if(req->chn >= PRV_CHAN_NUM)
	{//如果通道号在从片发送消息给从片
		SN_SendMccMessageEx(PRV_SLAVE_1,msg_req->user, msg_req->source, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_GET_REC_OSDRES_IND, (void*)msg_req->para, msg_req->size);		
	}
	RET_SUCCESS("");
}

/********************************************************
函数名:PRV_WaitDestroyVdecChn
功     能:销毁指定解码通道
参     数:[in]VdecChn  指定解码通道?
返回值:  0成功
		    -1失败

*********************************************************/
HI_S32 PRV_WaitDestroyVdecChn(HI_S32 VdecChn)
{
#if defined(SN9234H1)
	HI_S32 i = 0, index = 0, s32Ret = 0;
	if(VdecChn < 0/* || VdecChn == DetVLoss_VdecChn*/)
#else
	HI_S32 index = 0, s32Ret = 0;
	if(VdecChn < 0 || VdecChn == DetVLoss_VdecChn)
#endif		
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"-------Invalid VdecChn = %d\n ", VdecChn);  
		return HI_FAILURE;
	}
	
	index = PRV_GetVoChnIndex(VdecChn);
	if((VdecChn != DetVLoss_VdecChn && VdecChn != NoConfig_VdecChn) && index < 0)
	{
		RET_FAILURE("------ERR: index");
	}
	CHECK(HI_MPI_VDEC_StopRecvStream(VdecChn));//销毁通道前先停止接收数据
	
	if(VdecChn != DetVLoss_VdecChn && VdecChn != NoConfig_VdecChn)//JPEG不支持reset功能
		CHECK(HI_MPI_VDEC_ResetChn(VdecChn)); //解码器复位 
	
	if((VdecChn != DetVLoss_VdecChn && VdecChn != NoConfig_VdecChn)
		&& s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat != PRV_STAT_PB
		&& s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat != PRV_STAT_PIC)
	{
		s32Ret = PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[index].VoChn);
	}
	
	if(VdecChn != DetVLoss_VdecChn && VdecChn != NoConfig_VdecChn)
	{		
#if defined(SN9234H1)
		for(i = 0; i < PRV_VO_DEV_NUM; i++)
		{			
			if(i == AD || i == SPOT_VO_DEV)
				continue;
			//if(VochnInfo[index].IsBindVdec[i] == 1)
			{
				if(i == SPOT_VO_DEV)
				{
					if(VochnInfo[index].VoChn == s_astVoDevStatDflt[SPOT_VO_DEV].as32ChnOrder[SingleScene][s_astVoDevStatDflt[SPOT_VO_DEV].s32SingleIndex])
					{
						(HI_MPI_VDEC_UnbindOutputChn(VdecChn, SPOT_VO_DEV, SPOT_VO_CHAN));/* 解绑定通道 */
						CHECK(HI_MPI_VO_ClearChnBuffer(SPOT_VO_DEV, SPOT_VO_CHAN, 1)); /* 清除VO缓存 */
					}
						
				}
				else 
				{
					(HI_MPI_VDEC_UnbindOutputChn(VdecChn, i, VdecChn));/* 解绑定通道 */
					CHECK(HI_MPI_VO_ClearChnBuffer(i, VochnInfo[index].VoChn, 1)); /* 清除VO缓存 */
					//只有在控制状态下需要对控制通道进行相应操作
					if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_CTRL
						&& (s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_REGION_SEL || s_astVoDevStatDflt[HD].enCtrlFlag == PRV_CTRL_ZOOM_IN)
						&& s32Ret == HI_SUCCESS && i == s_VoDevCtrlDflt)
					{
						(HI_MPI_VDEC_UnbindOutputChn(VdecChn, i, PRV_CTRL_VOCHN));							
						CHECK(HI_MPI_VO_ClearChnBuffer(i, PRV_CTRL_VOCHN, HI_TRUE)); /* 清除VO缓存 */
						
					}
				}
				
				VochnInfo[index].IsBindVdec[i] = -1;
			}
			
		}
#else
		CHECK(PRV_VDEC_UnBindVpss(VdecChn, VdecChn));// 解绑定通道 
		CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, VochnInfo[index].VoChn, 1)); 
		//CHECK(HI_MPI_VO_ClearChnBuffer(DSD0, VochnInfo[index].VoChn, 1)); 
		//只有在控制状态下需要对控制通道进行相应操作
		if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_CTRL
			&& (s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_REGION_SEL || s_astVoDevStatDflt[DHD0].enCtrlFlag == PRV_CTRL_ZOOM_IN)
			&& s32Ret == HI_SUCCESS)
		{
			CHECK(PRV_VDEC_UnBindVpss(VdecChn, PRV_CTRL_VOCHN));							
			CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, PRV_CTRL_VOCHN, HI_TRUE)); 
			//CHECK(HI_MPI_VO_ClearChnBuffer(DSD0, PRV_CTRL_VOCHN, HI_TRUE)); 
			
		}				
		VochnInfo[index].IsBindVdec[DHD0] = -1;								
		//VochnInfo[index].IsBindVdec[DSD0] = -1;								
#endif		
		CHECK_RET(HI_MPI_VDEC_DestroyChn(VdecChn)); // 销毁视频通道
		VochnInfo[VdecChn].IsHaveVdec = 0;
	}

	return HI_SUCCESS;
}

/********************************************************
函数名:PRV_MasterChnReChooseSlave
功     能:原在主片预览的通道重新选择放从片预览
参     数:[in]index:VochnInfo的下标
返回值:  

*********************************************************/

HI_VOID PRV_MasterChnReChooseSlave(int index)
{
	PRV_MccCreateVdecReq SlaveCreateVdecReq;
	//将找到合乎条件的通道放从片
	PRV_WaitDestroyVdecChn(VochnInfo[index].VdecChn);
	VochnInfo[index].SlaveId = PRV_SLAVE_1;
	CurSlaveCap += VochnInfo[index].VdecCap;
	CurSlaveChnCount++;
	SlaveCreateVdecReq.s32StreamChnIDs = MasterToSlaveChnId;
	SlaveCreateVdecReq.EncType = VochnInfo[index].VideoInfo.vdoType;
	SlaveCreateVdecReq.chn = VochnInfo[index].CurChnIndex;
	SlaveCreateVdecReq.VoChn = VochnInfo[index].VoChn;
	SlaveCreateVdecReq.VdecChn = VochnInfo[index].VoChn;
	SlaveCreateVdecReq.VdecCap = VochnInfo[index].VdecCap;	
	SlaveCreateVdecReq.height = VochnInfo[index].VideoInfo.height;
	SlaveCreateVdecReq.width = VochnInfo[index].VideoInfo.width;		
	SlaveCreateVdecReq.SlaveId = PRV_SLAVE_1;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "============Master Chn: %d---VdecCap: %d ReChoose Slave---\n", SlaveCreateVdecReq.VdecChn, VochnInfo[index].VdecCap);
	VochnInfo[index].MccCreateingVdec = 1;
#if defined(Hi3531)||defined(Hi3535)	
	MccCreateingVdecCount++;
#endif
	SN_SendMccMessageEx(SlaveCreateVdecReq.SlaveId, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_CREATEVDEC_REQ, &SlaveCreateVdecReq, sizeof(PRV_MccCreateVdecReq));					
	CurMasterCap -= VochnInfo[index].VdecCap;

}

/********************************************************
函数名:PRV_FindMasterChnReChooseSlave
功     能:在主片查找需要改放从片解码的通道
参     数:[in]ExtraCap:主片需要让出的性能
		[in]index:当前处理的通道下标(分辨率变大的通道)
		[in]TmpIndex:需要选择改放从片解码的通道组
返回值:  

*********************************************************/

HI_VOID PRV_FindMasterChnReChooseSlave(int ExtraCap, int index, int TmpIndex[])
{
	int i = 0, j = 0;
	if(ExtraCap == 1)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 1,Need Find One Master Chn!\n");			
		//查找主片有性能为2的通道
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == 1 && i != index)
			{
				TmpIndex[0] = i;
				break;	
			}
		}
	}
	//ExtraCap=2时，需要在主片找1个或2个通道放从片，二个性能为1，或一个性能为2
	else if(ExtraCap == 2)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 2,Need Find Two or Three Master Chn!\n");			
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == 2 && i != index)
			{
				TmpIndex[0] = i;
				return;
			}
		}
		//主片没有性能为2的通道，需要找2个通道放从片
		if(i == PRV_VO_CHN_NUM)
		{
			j = 0;
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				//查找二个满足性能为1的通道
				if(VochnInfo[i].SlaveId == PRV_MASTER
					&& VochnInfo[i].VdecCap == 1
					&& i != index)
				{
					TmpIndex[j] = i;
					j++;
				}
				//找到2个性能为1的通道
				if(TmpIndex[1] != -1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 3, Allready Find Three Master Chn: %d-%d-%d!\n", VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[1]].VoChn, VochnInfo[TmpIndex[2]].VoChn);		
					break;
				}
			}

		}

	}
	//ExtraCap=3时，需要在主片找2个通道放从片，一个性能为1，一个性能为2
	//或需要在主片找3个通道放从片，这3个通道性能均为1
	else if(ExtraCap == 3)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 3,Need Find Two or Three Master Chn!\n");			
		//查找主片有性能为2的通道
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == 2 && i != index)
				break;
	
		}
		//主片有性能为2的通道，则只需要找2个通道放从片
		if(i != PRV_VO_CHN_NUM)
		{
			int tempVdecCap1 = 0, tempVdecCap2 = 0;
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				//查找第一个满足性能(1或2)的通道
				if(TmpIndex[0] == -1)
				{
					if(VochnInfo[i].SlaveId == PRV_MASTER
						&& VochnInfo[i].VdecCap < ExtraCap
						&& VochnInfo[i].IsHaveVdec
						&& i != index)
					{
						TmpIndex[0] = i;
						tempVdecCap1 = VochnInfo[i].VdecCap;
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "One: Find Master Chn: %d---Cap: %d!\n", VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[0]].VdecCap);		
					}
				}
				//查找第二个满足性能的通道
				else
				{
					if(VochnInfo[i].SlaveId == PRV_MASTER
						&& VochnInfo[i].VdecCap == (ExtraCap - tempVdecCap1)						
						&& VochnInfo[i].IsHaveVdec
						&& i != index && i != TmpIndex[0])
					{
						TmpIndex[1] = i;
						tempVdecCap2 = VochnInfo[i].VdecCap;
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "TWO: Find Master Chn: %d---Cap: %d!\n", VochnInfo[TmpIndex[1]].VoChn, VochnInfo[TmpIndex[1]].VdecCap);		
					}
				}
				if(TmpIndex[0] != -1 && TmpIndex[1] != -1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 3, Allready Find Two Master Chn: %d-%d!\n", VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[1]].VoChn);		
					break;
				}
			}
		}
		//主片没有性能为2的通道，需要找3个通道放从片
		else
		{		
			j = 0;
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				//查找三个满足性能为1的通道
				if(VochnInfo[i].SlaveId == PRV_MASTER
					&& VochnInfo[i].VdecCap == 1
					&& i != index)
				{
					TmpIndex[j] = i;
					j++;
				}
				//找到3个性能为1的通道
				if(TmpIndex[2] != -1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 3, Allready Find Three Master Chn: %d-%d-%d!\n", VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[1]].VoChn, VochnInfo[TmpIndex[2]].VoChn);		
					break;
				}
			}
	
		}
	
	}
	//ExtraCap=4时，需要在主片找1个通道放从片，性能为4
	//或需要在主片找2个通道放从片，性能均为2
	//或需要在主片找3个通道放从片，性能为1，1，2
	//或需要在主片找4个通道放从片，性能均为1
	else if(ExtraCap == 4)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 4,Need Find one or Two or Three or Four Master Chn!\n");			
		//查找主片有性能为4的通道
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == 4 && i != index)
			{	
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 4, Allready Find One Master Chn: %d!\n", VochnInfo[i].VoChn);
				TmpIndex[0] = i;
				return;
			}	
		}
		int count =0;
		//查找主片有性能为2的通道
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == 2 && i != index)
			{
				count++;
			}	
		}
		//没有找到性能为2的通道，需要查找4个性能为1的通道放从片
		j = 0;
		if(count == 0)
		{
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				//查找四个满足性能为1的通道
				if(VochnInfo[i].SlaveId == PRV_MASTER
					&& VochnInfo[i].VdecCap == 1
					&& i != index)
				{
					TmpIndex[j] = i;
					j++;
				}
				//找到3个性能为1的通道
				if(TmpIndex[3] != -1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 4, Allready Find Four Master Chn: %d-%d-%d-%d!\n",
						VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[1]].VoChn, VochnInfo[TmpIndex[2]].VoChn, VochnInfo[TmpIndex[3]].VoChn);		

					break;
				}

			}
		}
		//找到1个性能为2的通道，需要查找3个通道放从片
		else if(count == 1)
		{
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == 2 && i != index)
				{
					TmpIndex[j] = i;
					j++;
					break;
				}	
			}
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == 1 && i != index)
				{
					TmpIndex[j] = i;
					j++;
				}
				if(TmpIndex[2] != -1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 4, Allready Find Three Master Chn: %d-%d-%d!\n", VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[1]].VoChn, VochnInfo[TmpIndex[2]].VoChn);		
					break;
				}
				
			}
		}
		//找到2个以上性能为2的通道，需要查找2个通道放从片
		else if(count >= 2)
		{
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == 2 && i != index)
				{
					TmpIndex[j] = i;
					j++;
				}
				if(TmpIndex[2] != -1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 4, Allready Find Two Master Chn: %d-%d-%d!\n", VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[1]].VoChn);		
					break;
				}
				
			}
		}

	}
	else
	{
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && i != index)
			{
				TmpIndex[j] = i;
				j++;
				if(j >= 8)
					break;
			}			
		}

	}
}

int PRV_FindMaster_Min(int ExtraCap, int index, int TmpIndex[])
{
	int i = 0, j = 0, b_sing = 1, b_most = 1;
	int SingChn = 0, SingCap = 0;
	int MostChn[PRV_VO_CHN_NUM] = {0}, MostCap = 0;

	
	for(i = 0; i < PRV_VO_CHN_NUM; i++)
	{
		if(VochnInfo[i].VdecCap > 0 && i != index)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VochnInfo[%d].SlaveId=%d, VdecCap=%d, ExtraCap=%d\n", i, VochnInfo[i].SlaveId, VochnInfo[i].VdecCap, ExtraCap);
		}
		
		if(b_sing && VochnInfo[i].SlaveId == PRV_MASTER && i != index)
		{
			if(VochnInfo[i].VdecCap >= ExtraCap)
			{
				b_sing = 0;
				SingChn = i;
				SingCap	= VochnInfo[i].VdecCap;
			}
		}
		else if(VochnInfo[i].SlaveId == PRV_MASTER && i != index)
		{
			if((VochnInfo[i].VdecCap >= ExtraCap) && (abs(SingCap - ExtraCap) > abs(VochnInfo[i].VdecCap - ExtraCap)))
			{
				SingChn = i;
				SingCap	= VochnInfo[i].VdecCap;
			}
		}

		if(b_most && VochnInfo[i].SlaveId == PRV_MASTER  && i != index)
		{
			if(VochnInfo[i].VdecCap <= ExtraCap)
			{
				b_most = 0;
				MostCap = VochnInfo[i].VdecCap;
				MostChn[i] = VochnInfo[i].VdecCap;
			}
		}
		else if(VochnInfo[i].SlaveId == PRV_MASTER && i != index)
		{
			if(MostCap > ExtraCap)
			{
				int temCap=0, needCap=0, nowcap=0, tempchn=0;
				nowcap = VochnInfo[i].VdecCap;
				needCap = MostCap - ExtraCap;
				for(j=0; j<i; j++)
				{
					if(MostChn[j] > 0 && MostChn[j] > nowcap && MostChn[j] <= (nowcap + needCap))
					{
						if(temCap == 0)
						{
							tempchn = j;
							temCap = MostChn[j];
						}
						else
						{
							if(temCap < MostChn[j])
							{
								tempchn = j;
								temCap = MostChn[j];
							}
						}
					}
				}
				if(temCap > 0)
				{
					MostCap -= (MostChn[tempchn] - nowcap);
					MostChn[tempchn] = 0;
					MostChn[i] = nowcap;
				}
			}
			else
			{
				MostCap += VochnInfo[i].VdecCap;
				MostChn[i] = VochnInfo[i].VdecCap;
			}
		}
		
	}

	if(SingCap > 0 || MostCap > 0)
	{
		if((SingCap > 0 && SingCap < MostCap) || (SingCap > 0 && MostCap <= 0))
		{
			if(SingCap + CurSlaveCap > TOTALCAPPCHIP || SingCap < ExtraCap)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d SingCap=%d, MostCap=%d, CurSlaveCap=%d, total=%d, SingCap is over total\n", __LINE__, SingCap, MostCap, CurSlaveCap, TOTALCAPPCHIP);
				return -1;
			}
			else
			{
				TmpIndex[0] = SingChn;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "SingCap=%d, MostCap=%d, CurSlaveCap=%d, total=%d, SingChn=%d\n", SingCap, MostCap, CurSlaveCap, TOTALCAPPCHIP, SingChn);
			}
			
		}
		else if(MostCap > 0)
		{
			if(MostCap + CurSlaveCap > TOTALCAPPCHIP || MostCap < ExtraCap)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d SingCap=%d, MostCap=%d, CurSlaveCap=%d, total=%d, MostCap is over total\n", __LINE__, SingCap, MostCap, CurSlaveCap, TOTALCAPPCHIP);
				return -1;
			}
			else
			{
				i = 0, j = 0;
				for(i=0; i<PRV_VO_CHN_NUM; i++)
				{
					if(MostChn[i] > 0)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "MostChn[%d]=%d\n", i, MostChn[i]);
						TmpIndex[j] = i;
						j++;
					}
				}
			}
			
		}
		else
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "SingCap=%d, MostCap=%d, CurSlaveCap=%d, total=%d\n", SingCap, MostCap, CurSlaveCap, TOTALCAPPCHIP);
			return -1;
		}

		return OK;
	}

	return -1;
}


/********************************************************
函数名:PRV_FindMasterChnReChooseSlave
功     能:在主片查找需要改放从片解码的通道
参     数:[in]ExtraCap:主片需要让出的性能
		[in]index:当前处理的通道下标(分辨率变大的通道)
		[in]TmpIndex:需要选择改放从片解码的通道组
返回值:  

*********************************************************/

HI_VOID PRV_FindMasterChnReChooseSlave_EX(int ExtraCap, int index, int TmpIndex[])
{
	int i = 0, j = 0;

	if(ExtraCap == QCIF)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 1,Need Find One Master Chn!\n");			
		//查找主片有性能为1的通道
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == QCIF && i != index)
			{
				TmpIndex[0] = i;
				break;	
			}
		}
	}
	else if(ExtraCap == CIF)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 1,Need Find One Master Chn!\n");			
		//查找主片有性能为1的通道
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == CIF && i != index)
			{
				TmpIndex[0] = i;
				break;	
			}
		}
	}
	else if(ExtraCap == D1)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 1,Need Find One Master Chn!\n");			
		//查找主片有性能为1的通道
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == D1 && i != index)
			{
				TmpIndex[0] = i;
				break;	
			}
		}
	}
	//ExtraCap=2时，需要在主片找1个或2个通道放从片，二个性能为1，或一个性能为2
	else if(ExtraCap == HIGH_SEVEN)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 2,Need Find Two or Three Master Chn!\n");			
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == HIGH_SEVEN && i != index)
			{
				TmpIndex[0] = i;
				return;
			}
		}
		//主片没有性能为2的通道，需要找2个通道放从片
		if(i == PRV_VO_CHN_NUM)
		{
			j = 0;
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				//查找二个满足性能为1的通道
				if(VochnInfo[i].SlaveId == PRV_MASTER
					&& VochnInfo[i].VdecCap == D1
					&& i != index)
				{
					TmpIndex[j] = i;
					j++;
				}
				//找到2个性能为1的通道
				if(TmpIndex[1] != -1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 3, Allready Find Three Master Chn: %d-%d-%d!\n", VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[1]].VoChn, VochnInfo[TmpIndex[2]].VoChn);		
					break;
				}
			}

		}

	}
	//ExtraCap=3时，需要在主片找2个通道放从片，一个性能为1，一个性能为2
	//或需要在主片找3个通道放从片，这3个通道性能均为1
	else if(ExtraCap == (MAX_3D1))
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 3,Need Find Two or Three Master Chn!\n");			
		//查找主片有性能为2的通道
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == HIGH_SEVEN && i != index)
				break;
	
		}
		//主片有性能为2的通道，则只需要找2个通道放从片
		if(i != PRV_VO_CHN_NUM)
		{
			int tempVdecCap1 = 0, tempVdecCap2 = 0;
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				//查找第一个满足性能(1或2)的通道
				if(TmpIndex[0] == -1)
				{
					if(VochnInfo[i].SlaveId == PRV_MASTER
						&& VochnInfo[i].VdecCap < ExtraCap
						&& VochnInfo[i].IsHaveVdec
						&& i != index)
					{
						TmpIndex[0] = i;
						tempVdecCap1 = VochnInfo[i].VdecCap;
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "One: Find Master Chn: %d---Cap: %d!\n", VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[0]].VdecCap);		
					}
				}
				//查找第二个满足性能的通道
				else
				{
					if(VochnInfo[i].SlaveId == PRV_MASTER
						&& VochnInfo[i].VdecCap == (ExtraCap - tempVdecCap1)						
						&& VochnInfo[i].IsHaveVdec
						&& i != index && i != TmpIndex[0])
					{
						TmpIndex[1] = i;
						tempVdecCap2 = VochnInfo[i].VdecCap;
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "TWO: Find Master Chn: %d---Cap: %d!\n", VochnInfo[TmpIndex[1]].VoChn, VochnInfo[TmpIndex[1]].VdecCap);		
					}
				}
				if(TmpIndex[0] != -1 && TmpIndex[1] != -1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 3, Allready Find Two Master Chn: %d-%d!\n", VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[1]].VoChn);		
					break;
				}
			}
		}
		//主片没有性能为2的通道，需要找3个通道放从片
		else
		{		
			j = 0;
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				//查找三个满足性能为1的通道
				if(VochnInfo[i].SlaveId == PRV_MASTER
					&& VochnInfo[i].VdecCap == D1
					&& i != index)
				{
					TmpIndex[j] = i;
					j++;
				}
				//找到3个性能为1的通道
				if(TmpIndex[2] != -1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 3, Allready Find Three Master Chn: %d-%d-%d!\n", VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[1]].VoChn, VochnInfo[TmpIndex[2]].VoChn);		
					break;
				}
			}
	
		}
	
	}
	//ExtraCap=4时，需要在主片找1个通道放从片，性能为4
	//或需要在主片找2个通道放从片，性能均为2
	//或需要在主片找3个通道放从片，性能为1，1，2
	//或需要在主片找4个通道放从片，性能均为1
	else if(ExtraCap == HIGH_TEN)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 4,Need Find one or Two or Three or Four Master Chn!\n");			
		//查找主片有性能为4的通道
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == HIGH_TEN && i != index)
			{	
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 4, Allready Find One Master Chn: %d!\n", VochnInfo[i].VoChn);
				TmpIndex[0] = i;
				return;
			}	
		}
		int count =0;
		//查找主片有性能为2的通道
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == HIGH_SEVEN && i != index)
			{
				count++;
			}	
		}
		//没有找到性能为2的通道，需要查找4个性能为1的通道放从片
		j = 0;
		if(count == 0)
		{
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				//查找四个满足性能为1的通道
				if(VochnInfo[i].SlaveId == PRV_MASTER
					&& VochnInfo[i].VdecCap == D1
					&& i != index)
				{
					TmpIndex[j] = i;
					j++;
				}
				//找到3个性能为1的通道
				if(TmpIndex[3] != -1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 4, Allready Find Four Master Chn: %d-%d-%d-%d!\n",
						VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[1]].VoChn, VochnInfo[TmpIndex[2]].VoChn, VochnInfo[TmpIndex[3]].VoChn);		

					break;
				}

			}
		}
		//找到1个性能为2的通道，需要查找3个通道放从片
		else if(count == 1)
		{
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == HIGH_SEVEN && i != index)
				{
					TmpIndex[j] = i;
					j++;
					break;
				}	
			}
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == D1 && i != index)
				{
					TmpIndex[j] = i;
					j++;
				}
				if(TmpIndex[2] != -1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 4, Allready Find Three Master Chn: %d-%d-%d!\n", VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[1]].VoChn, VochnInfo[TmpIndex[2]].VoChn);		
					break;
				}
				
			}
		}
		//找到2个以上性能为2的通道，需要查找2个通道放从片
		else if(count >= 2)
		{
			for(i = 0; i < PRV_VO_CHN_NUM; i++)
			{
				if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == HIGH_SEVEN && i != index)
				{
					TmpIndex[j] = i;
					j++;
				}
				if(TmpIndex[2] != -1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ExtraCap == 4, Allready Find Two Master Chn: %d-%d-%d!\n", VochnInfo[TmpIndex[0]].VoChn, VochnInfo[TmpIndex[1]].VoChn);		
					break;
				}
				
			}
		}

	}
	else
	{
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
			if(VochnInfo[i].SlaveId == PRV_MASTER && i != index)
			{
				TmpIndex[j] = i;
				j++;
				if(j >= 8)
					break;
			}			
		}

	}
}

/********************************************************
函数名:PRV_ReCreateVdecChn
功     能:重新创建解码通道。
		主片解码通道接收数据信息发生改变时(分辨率)，调用此接口
参     数:[in]VdecChn  指定解码通道
		    [in]EncType  创建的解码器解码类型
		    [in]new_height  分辨率高(新)
		    [in]new_width   分辨率宽(新)
		    [in]NewVdeCap  该解码器占用的性能(新)
返回值:  HI_SUCCESS成功
		    HI_FAILURE失败

*********************************************************/

HI_S32 PRV_ReCreateVdecChn(HI_S32 chn, HI_S32 EncType, HI_S32 new_height, HI_S32 new_width, HI_U32 u32RefFrameNum, HI_S32 NewVdeCap)
{
	int index = chn + LOCALVEDIONUM, PreChnIndex = 0;	
	PRV_MccCreateVdecReq SlaveCreateVdecReq;
	if(HI_SUCCESS == PRV_WaitDestroyVdecChn(chn))//先销毁解码通道
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "chn: %d, CurMasterCap: %d, OldVdeCap: %d, NewVdecCap: %d\n", chn, CurMasterCap, VochnInfo[index].VdecCap, NewVdeCap);	
		CurCap -= VochnInfo[index].VdecCap;
		CurMasterCap -= VochnInfo[index].VdecCap;			
		
		VochnInfo[index].IsHaveVdec = 0;	
		VochnInfo[index].VdecChn = NoConfig_VdecChn;
		//VochnInfo[index].VdecCap = 0;
		PreChnIndex = VochnInfo[index].CurChnIndex;
		//VochnInfo[index].CurChnIndex = -1;
		
	}
	else
	{
		//VochnInfo[index].bIsWaitIFrame = 1;
		VochnInfo[index].bIsWaitGetIFrame = 1;
		
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_WaitDestroyVdecChn fail---chn: %d\n", chn);	
		return HI_FAILURE;
	}
	//新性能过大，主片性能不够，查看从片是否足够
	if((CurMasterCap + NewVdeCap) > TOTALCAPPCHIP)
	{
	
#if defined(Hi3531)||defined(Hi3535)//3531解码器不包含从片
		CurSlaveCap = TOTALCAPPCHIP;
#endif
		//CurIPCCount--;
		//如果从片还有足够性能，重新选择从片解码
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "CurSlaveCap---%d, NewVdeCap: %d\n", CurSlaveCap, NewVdeCap);
		if(!VochnInfo[index].bIsPBStat && (CurSlaveCap + NewVdeCap) <= TOTALCAPPCHIP)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "============ReChoose Slave, Vdec---%d\n", chn);
			VochnInfo[chn].SlaveId = PRV_SLAVE_1;
			VochnInfo[chn].CurChnIndex = PreChnIndex;
			VochnInfo[chn].VoChn = chn;
			VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;
			VochnInfo[chn].IsConnect = 1;
			VochnInfo[chn].VdecCap = NewVdeCap;
			CurSlaveCap += NewVdeCap;
			CurSlaveChnCount++;
			//从片创建解码通道信息
			//SlaveCreateVdec->SlaveId = VochnInfo[chn].SlaveId;	
			SlaveCreateVdecReq.s32StreamChnIDs = MasterToSlaveChnId;
			SlaveCreateVdecReq.EncType = VochnInfo[chn].VideoInfo.vdoType;
			SlaveCreateVdecReq.chn = VochnInfo[chn].CurChnIndex;
			SlaveCreateVdecReq.VoChn = VochnInfo[chn].VoChn;
			SlaveCreateVdecReq.VdecChn = VochnInfo[chn].VoChn;
			SlaveCreateVdecReq.VdecCap = VochnInfo[chn].VdecCap;	
			SlaveCreateVdecReq.height = VochnInfo[chn].VideoInfo.height;
			SlaveCreateVdecReq.width = VochnInfo[chn].VideoInfo.width;		
			SlaveCreateVdecReq.SlaveId = PRV_SLAVE_1;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "============Send: MSG_ID_PRV_MCC_CREATEVDEC_REQ---%d\n", SlaveCreateVdecReq.VdecChn);
			VochnInfo[index].MccCreateingVdec = 1;
#if defined(Hi3531)||defined(Hi3535)			
			MccCreateingVdecCount++;
#endif
			SN_SendMccMessageEx(SlaveCreateVdecReq.SlaveId, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_CREATEVDEC_REQ, &SlaveCreateVdecReq, sizeof(PRV_MccCreateVdecReq));					
			return 1;

		}
		//主片、从片性能均不够，查看总空余性能是否足够，
		//如果足够，重新分配主从片
		else if((CurMasterCap + CurSlaveCap + NewVdeCap) <= DEV_CHIP_NUM * TOTALCAPPCHIP)
		{
			int tempVdecCap = 0, i = 0, ret = 0;
			deluser_used DelUserReq;
			DelUserReq.channel = chn;
			int TmpIndex[8] = {-1, -1, -1, -1, -1, -1, -1, -1,};
			SCM_Link_Rsp Rsp;
			SN_MEMSET(&Rsp, 0, sizeof(Rsp));
			
			Rsp.channel = VochnInfo[index].VoChn;
			Rsp.endtype = LINK_OVERFLOW;
			//该通道放主片需要额外的性能
			tempVdecCap = NewVdeCap - (TOTALCAPPCHIP - CurMasterCap);

			//tempVdecCap = PRV_ComPare(tempVdecCap);
			
			//在主片上找到满足额外性能的通道(1个或2个或3个或4个或更多)改放从片
			if(tempVdecCap >= TOTALCAPPCHIP)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "chn: %d, No Vaild Cap: %d\n", chn, VochnInfo[chn].VdecCap);
				SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ, &DelUserReq, sizeof(deluser_used));
				goto DiscardChn;
			}
			
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s line:%d CurMasterCap=%d, CurSlaveCap=%d, tempVdecCap=%d, total=%d, Need Find Master Chn!\n", __FUNCTION__, __LINE__, CurMasterCap, CurSlaveCap, tempVdecCap, TOTALCAPPCHIP);			

			ret = PRV_FindMaster_Min(tempVdecCap, chn, TmpIndex);
			if(ret < 0)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Master: =======tempVdecCap: %d, No Find Master Chn ReChoose Slave---\n", tempVdecCap);
				SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ, &DelUserReq, sizeof(deluser_used));
				goto DiscardChn;
			}

			for(i = 0; i < 8; i++)
			{
				//将在主片找到的通道改放从片
				if(TmpIndex[i] > -1)
					PRV_MasterChnReChooseSlave(TmpIndex[i]);
				else
					break;
					
			}
			
#if 0			
			PRV_FindMasterChnReChooseSlave(tempVdecCap, chn, TmpIndex);
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Master: tempVdecCap: %d <= 4,Need Find Master Chn!\n", tempVdecCap);			
			for(i = 0; i < 8; i++)
			{
				//将在主片找到的通道改放从片
				if(TmpIndex[i] > -1)
					PRV_MasterChnReChooseSlave(TmpIndex[i]);
				else
					break;
					
			}
			if(i == 0)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Master: =======tempVdecCap: %d, No Find Master Chn ReChoose Slave---\n", tempVdecCap);
				SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ, &DelUserReq, sizeof(deluser_used));
				goto DiscardChn;
			}					
#endif			
		}
		//总性能不够，暂不解此通道数据，即收到此通道数据后，丢弃；绑定"无网络视频"
		else
		{
DiscardChn:
			if(VochnInfo[index].bIsPBStat)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_ReCreateVdecChn fail----chn: %d, CurMasterCap: %d, NewVdeCap: %d\n", chn, CurMasterCap, NewVdeCap);	
				Prv_Chn_ChnPBOverFlow_Ind ChnPbOver;
				SN_MEMSET(&ChnPbOver, 0, sizeof(Prv_Chn_ChnPBOverFlow_Ind));
				ChnPbOver.Chn = chn;
				ChnPbOver.NewWidth = new_width;
				ChnPbOver.NewHeight = new_height;							
				SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_FWK, 0, 0, MSG_ID_PRV_CHNPBOVERFLOW_IND, &ChnPbOver, sizeof(Prv_Chn_ChnPBOverFlow_Ind));					
				PRV_VoChnStateInit(PreChnIndex);
				
				PRM_ID_TIME tmpQueryStartTime = PtsInfo[chn].QueryStartTime; 
				PRM_ID_TIME tmpQueryFinalTime = PtsInfo[chn].QueryFinalTime;
				PRV_PtsInfoInit(PreChnIndex);		
				PRV_InitVochnInfo(chn);
				VochnInfo[index].bIsWaitGetIFrame = 1;
				VochnInfo[index].bIsPBStat = 1;
				PtsInfo[chn].QueryStartTime = tmpQueryStartTime;
				PtsInfo[chn].QueryFinalTime = tmpQueryFinalTime;
				return HI_FAILURE;
			}	
			PRV_VoChnStateInit(PreChnIndex);			
			PRV_PtsInfoInit(PreChnIndex);		
			PRV_InitVochnInfo(chn);
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_ReCreateVdecChn++++++++++++++Discard---chn: %d, CurMasterCap: %d, NewVdeCap: %d\n", chn, CurMasterCap, NewVdeCap);	
			/*
			SCM_Link_Rsp rsp;
			SN_MEMSET(&rsp, 0, sizeof(rsp));
			rsp.channel = chn;
			rsp.endtype = LINK_OVERFLOW;
			SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_FWK, 0, 0, MSG_ID_NTRANS_ONCEOVER_IND, &rsp, sizeof(rsp));
			*/
			if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[chn].VoChn))
			{
#if defined(SN9234H1)
				CHECK(HI_MPI_VO_DisableChn(HD, VochnInfo[chn].VoChn));			
			//	CHECK(HI_MPI_VO_DisableChn(s_VoSecondDev, VochnInfo[chn].VoChn));
				PRV_VLossVdecBindVoChn(HD, VochnInfo[index].VoChn,	s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);
			//	PRV_VLossVdecBindVoChn(s_VoSecondDev, VochnInfo[chn].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
				CHECK(HI_MPI_VO_EnableChn(HD, VochnInfo[chn].VoChn));			
			//	CHECK(HI_MPI_VO_EnableChn(s_VoSecondDev, VochnInfo[chn].VoChn));
#else
				CHECK(HI_MPI_VO_DisableChn(DHD0, VochnInfo[chn].VoChn));			
				//CHECK(HI_MPI_VO_DisableChn(DSD0, VochnInfo[chn].VoChn));
				PRV_VLossVdecBindVoChn(DHD0, VochnInfo[index].VoChn,	s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);
				//PRV_VLossVdecBindVoChn(DSD0, VochnInfo[chn].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
				CHECK(HI_MPI_VO_EnableChn(DHD0, VochnInfo[chn].VoChn));			
				//CHECK(HI_MPI_VO_EnableChn(DSD0, VochnInfo[chn].VoChn));
#endif		
			//	sem_post(&sem_SendNoVideoPic);
				
			//	if(!IsChoosePlayAudio)
					PRV_PlayAudio(s_VoDevCtrlDflt);				
			}
			else
			{
#if defined(SN9234H1)
				(HI_MPI_VDEC_BindOutput(NoConfig_VdecChn, HD, VochnInfo[index].VoChn));	
				VochnInfo[index].IsBindVdec[HD] = 0;				
		//		CHECK(HI_MPI_VDEC_BindOutput(DetVLoss_VdecChn, s_VoSecondDev, VochnInfo[chn].VoChn));	
		//		VochnInfo[chn].IsBindVdec[s_VoSecondDev] = 0;				
#else				
				PRV_VPSS_ResetWH(VochnInfo[index].VoChn,NoConfig_VdecChn,704,576);
				PRV_VDEC_BindVpss(NoConfig_VdecChn, VochnInfo[index].VoChn);
				VochnInfo[index].IsBindVdec[DHD0] = 0;	
				//VochnInfo[index].IsBindVdec[DSD0] = 0;	
#endif				
			}
			//VochnInfo[index].IsDiscard = 1;
			//VochnInfo[index].bIsWaitIFrame = 1;
			VochnInfo[index].bIsWaitGetIFrame = 1;
			return HI_FAILURE;
		}
	}
	if(HI_SUCCESS == PRV_CreateVdecChn(EncType, new_height, new_width, u32RefFrameNum, chn))
	{
		VochnInfo[index].VideoInfo.height = new_height;
		VochnInfo[index].VideoInfo.width = new_width;
		VochnInfo[index].VdecCap = NewVdeCap;			
		VochnInfo[index].CurChnIndex = PreChnIndex;
		VochnInfo[index].VdecChn = chn;
		VochnInfo[index].IsHaveVdec = 1;	
		
		CurCap += VochnInfo[index].VdecCap;
		CurMasterCap += VochnInfo[index].VdecCap;
#if defined(SN9234H1)
		PRV_BindVoChnInMaster(HD, VochnInfo[index].VoChn, s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);			
#else
		PRV_BindVoChnInMaster(DHD0, VochnInfo[index].VoChn, s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);	
#endif
		PRV_PlayAudio(s_VoDevCtrlDflt);
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Recreate Vdec: %d Ok---CurMasterCap: %d, NewVdecCap: %d!\n", chn, CurMasterCap, NewVdeCap);		
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ReCreate Vdec: fail!\n", chn);
		return HI_FAILURE;
	}

	return HI_SUCCESS;
}


/********************************************************
函数名:PRV_MCC_RecreateVdecRsp
功     能:处理从片重新创建解码通道返回信息
参     数:[in]msg_rsp  从片重新创建解码通道返回的消息
返回值:  HI_SUCCESS成功
		    HI_FAILURE失败

*********************************************************/
HI_S32 PRV_MCC_RecreateVdecRsp(const SN_MSG * msg_rsp)
{	
	PRV_MccReCreateVdecRsp *SlaveReCreateRsp = (PRV_MccReCreateVdecRsp *)(msg_rsp->para);
	HI_S32 index = -1, chn = -1;
	index = PRV_GetVoChnIndex(SlaveReCreateRsp->VoChn);
	if(index < 0)
		RET_FAILURE("Invalid Channel");
	chn = VochnInfo[index].CurChnIndex;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_MCC_RecreateVdecRsp, Vdec: %d, SlaveReCreateRsp->VdecCap: %d\n", SlaveReCreateRsp->VdecChn, SlaveReCreateRsp->VdecCap);
	
	VochnInfo[index].MccReCreateingVdec = 0;
#if defined(Hi3531)||defined(Hi3535)	
	MccReCreateingVdecCount--;
#endif
	if(0 != SlaveReCreateRsp->Result)
	{
		if (-1 == SlaveReCreateRsp->Result)	//从片 没有进行重新创建操作
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave Not ReCreate\n");
			//VochnInfo[index].bIsWaitIFrame = 1;
			VochnInfo[chn].bIsWaitGetIFrame = 1;
			CurCap = CurCap + VochnInfo[index].VdecCap - SlaveReCreateRsp->VdecCap;	
			CurSlaveCap = CurSlaveCap + VochnInfo[index].VdecCap - SlaveReCreateRsp->VdecCap;;
			return HI_FAILURE;
		}
		else if(-2 == SlaveReCreateRsp->Result)//从片销毁解码通道失败
		{	
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave Destroy vdec fail\n");
			//VochnInfo[index].bIsWaitIFrame = 1;
			VochnInfo[index].bIsWaitGetIFrame = 1;
			CurCap = CurCap + VochnInfo[index].VdecCap - SlaveReCreateRsp->VdecCap;	
			CurSlaveCap = CurSlaveCap + VochnInfo[index].VdecCap - SlaveReCreateRsp->VdecCap;;
			return HI_FAILURE;
		}
		else if(-3 == SlaveReCreateRsp->Result)//从片销毁解码通道成功，重新创建解码通道失败(从片性能不够)
		{
#if defined(SN9234H1)
			(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, HD, VochnInfo[index].VoChn));					
			CHECK(HI_MPI_VO_ClearChnBuffer(HD, VochnInfo[index].VoChn, 1));
			VochnInfo[index].IsBindVdec[HD] = -1;
#else
			//(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, DHD0, VochnInfo[index].VoChn));					
			CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, VochnInfo[index].VoChn, 1));
			VochnInfo[index].IsBindVdec[DHD0] = -1;
#endif			
			//BufferSet(chn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);			
			//BufferSet(chn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);			
			//BufferClear(chn + PRV_VIDEOBUFFER);
			//BufferClear(chn + PRV_AUDIOBUFFER);
			CurCap -= SlaveReCreateRsp->VdecCap;	
			CurSlaveCap -= SlaveReCreateRsp->VdecCap;
			CurSlaveChnCount--;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave Destroy vdec OK, Create Vdec: %d fail!,CurMasterCap: %d, CurSlaveCap: %d, SlaveReCreateRsp->VdecCap: %d\n", SlaveReCreateRsp->VdecChn, CurMasterCap, CurSlaveCap, SlaveReCreateRsp->VdecCap);
			//查看主片性能是否还足够，如果足够，放主片解码
			if((CurMasterCap + SlaveReCreateRsp->VdecCap) <= TOTALCAPPCHIP)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "=============ReChoose master chip!\n");			
				CurCap = CurCap + SlaveReCreateRsp->VdecCap; 
				VochnInfo[index].CurChnIndex = chn;
				VochnInfo[index].SlaveId = PRV_MASTER;
				VochnInfo[index].VoChn = SlaveReCreateRsp->VoChn; 
				VochnInfo[index].VdecCap = SlaveReCreateRsp->VdecCap;
				VochnInfo[index].IsConnect = 1;
				VochnInfo[index].VdecChn = SlaveReCreateRsp->VdecChn;	
				VochnInfo[index].IsChooseSlaveId = 1;
				PRV_CreateVdecChn_EX(index);
				//CurIPCCount++;
			}
			//查看总性能是否还足够
			//如果总性能足够，则在主片中选择合适的通道，改放从片解码；
			//然后将当前通道改放主片解码
			else if((CurMasterCap + CurSlaveCap + SlaveReCreateRsp->VdecCap) <= 2 * TOTALCAPPCHIP)
			{
				int tempVdecCap, i = 0, ret = 0;
				int TmpIndex[8] = {-1};
				SCM_Link_Rsp Rsp;
				SN_MEMSET(&Rsp, 0, sizeof(Rsp));
				
				Rsp.channel = VochnInfo[index].VoChn;
				Rsp.endtype = LINK_OVERFLOW;
				//将该通道放主片需要额外的性能
				tempVdecCap = SlaveReCreateRsp->VdecCap - (TOTALCAPPCHIP - CurMasterCap);

				//tempVdecCap = PRV_ComPare(tempVdecCap);

				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s line:%d CurMasterCap=%d, CurSlaveCap=%d, tempVdecCap=%d, total=%d, Need Find Master Chn!\n", __FUNCTION__, __LINE__, CurMasterCap, CurSlaveCap, tempVdecCap, TOTALCAPPCHIP);			

				ret = PRV_FindMaster_Min(tempVdecCap, index, TmpIndex);
				if(ret < 0)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s, line:%d tempVdecCap=%d, No Find Master Chn ReChoose Slave---\n", __FUNCTION__, __LINE__, tempVdecCap);
					goto DiscardChn;
				}

				for(i = 0; i < 8; i++)
				{
					//将在主片找到的通道改放从片
					if(TmpIndex[i] >= 0)
						PRV_MasterChnReChooseSlave(TmpIndex[i]);
					else
						break;
						
				}

				CurCap += SlaveReCreateRsp->VdecCap; 
#if 0				
				//在主片上找到满足额外性能的通道(1个或2个)改放从片
				if(tempVdecCap == MAX_3D1 || tempVdecCap == HIGH_TEN)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave: tempVdecCap: %d >= 3,Need Find Two Master Chn!\n", tempVdecCap);			
					int TmpIndex[4] = {-1, -1, -1, -1};
					PRV_FindMasterChnReChooseSlave(tempVdecCap, index, TmpIndex);
					for(i = 0; i < 4; i++)
					{
						//将在主片找到的通道改放从片
						if(TmpIndex[i] > -1)
							PRV_MasterChnReChooseSlave(TmpIndex[i]);
						else
							break;
							
					}
					if(i == 0)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave: =======tempVdecCap: %d >= 3, No Find Master Chn ReChoose Slave---\n", tempVdecCap);
						goto DiscardChn;
					}
					else
					{
						CurCap += SlaveReCreateRsp->VdecCap; 
					}
				}
				//tempVdecCap=1或2时，只需要找一个通道
				else if (tempVdecCap <= D1 || tempVdecCap == HIGH_SEVEN)
				{
					for(i = 0; i < PRV_VO_CHN_NUM; i++)
					{
						if(VochnInfo[i].SlaveId == PRV_MASTER && VochnInfo[i].VdecCap == tempVdecCap && i != index)
						{
							break;
						}
					}
					if(i != PRV_VO_CHN_NUM)
					{
						CurCap += SlaveReCreateRsp->VdecCap; 
						PRV_MasterChnReChooseSlave(i);
					}
					else
					{
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave: =======tempVdecCap < 3, No Find Master Chn ReChoose Slave---\n");
						goto DiscardChn;
					}
					
				}
				else
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave: =======tempVdecCap > 4, No Find Master Chn ReChoose Slave---\n");
					goto DiscardChn;

				}
#endif				
				//将当前通道放主片
				if(HI_SUCCESS == PRV_CreateVdecChn(VochnInfo[index].VideoInfo.vdoType, SlaveReCreateRsp->height, SlaveReCreateRsp->width, VochnInfo[index].u32RefFrameNum, SlaveReCreateRsp->VdecChn))
				{
					VochnInfo[index].VideoInfo.height = SlaveReCreateRsp->height;
					VochnInfo[index].VideoInfo.width = SlaveReCreateRsp->width;
					VochnInfo[index].VdecCap = SlaveReCreateRsp->VdecCap;			
					VochnInfo[index].CurChnIndex = chn;
					VochnInfo[index].VdecChn = SlaveReCreateRsp->VdecChn;
					VochnInfo[index].IsHaveVdec = 1;	
					VochnInfo[index].SlaveId = PRV_MASTER;
					
					CurMasterCap += VochnInfo[index].VdecCap;
					if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[index].VoChn))
					{
#if defined(SN9234H1)
						if(s_astVoDevStatDflt[HD].enPreviewStat == PRV_STAT_CTRL)
						{
							if(VochnInfo[index].VoChn == s_astVoDevStatDflt[HD].s32CtrlChn)
							{
								CHECK(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, HD, PRV_CTRL_VOCHN));					
								CHECK(HI_MPI_VO_ClearChnBuffer(HD, PRV_CTRL_VOCHN, 1));
							}
						}
						//必须disable Vo否则如果该"无网络"图片显示异常，且如果该通道下次在主片显示，也是异常
						CHECK(HI_MPI_VO_DisableChn(HD, VochnInfo[chn].VoChn));			
						PRV_BindVoChnInMaster(HD, VochnInfo[index].VoChn, s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);			
						CHECK(HI_MPI_VO_EnableChn(HD, VochnInfo[chn].VoChn));			
#else
						if(s_astVoDevStatDflt[DHD0].enPreviewStat == PRV_STAT_CTRL)
						{
							if(VochnInfo[index].VoChn == s_astVoDevStatDflt[DHD0].s32CtrlChn)
							{
								//CHECK(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, DHD0, PRV_CTRL_VOCHN));					
								CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, PRV_CTRL_VOCHN, 1));
							}
						}
						//必须disable Vo否则如果该"无网络"图片显示异常，且如果该通道下次在主片显示，也是异常
						CHECK(HI_MPI_VO_DisableChn(DHD0, VochnInfo[chn].VoChn));			
						PRV_BindVoChnInMaster(DHD0, VochnInfo[index].VoChn, s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);			
						CHECK(HI_MPI_VO_EnableChn(DHD0, VochnInfo[chn].VoChn));	
#endif						
						PRV_PlayAudio(s_VoDevCtrlDflt);
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ReChoose Master---Vdec: %d Ok---CurMasterCap: %d, NewVdecCap: %d!\n", chn, CurMasterCap, VochnInfo[index].VdecCap);
					}			
					else
					{
#if defined(SN9234H1)
						(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, HD, VochnInfo[index].VoChn)); 	
						VochnInfo[index].IsBindVdec[HD] = 0;				
#else
						PRV_VPSS_ResetWH(VochnInfo[index].VoChn,VochnInfo[index].VdecChn,VochnInfo[index].VideoInfo.width,VochnInfo[index].VideoInfo.height);
						PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VochnInfo[index].VoChn);
						//(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, DHD0, VochnInfo[index].VoChn)); 	
						VochnInfo[index].IsBindVdec[DHD0] = 0;	
#endif
			//			CHECK(HI_MPI_VDEC_BindOutput(DetVLoss_VdecChn, s_VoSecondDev, VochnInfo[index].VoChn)); 	
			//			VochnInfo[index].IsBindVdec[s_VoSecondDev] = 0;				
					}
					
				}
				else
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d---ReCreate Vdec: fail!\n", __LINE__, chn);
					return HI_FAILURE;				
				}
			}
			else
			{
DiscardChn:
				PRV_InitVochnInfo(VochnInfo[index].VoChn);
				PRV_VoChnStateInit(chn);			
				PRV_PtsInfoInit(chn);		
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_MCC_RecreateVdecRsp++++++++++++++Discard---chn: %d, CurMasterCap: %d, NewVdeCap: %d\n", chn, CurMasterCap, SlaveReCreateRsp->VdecCap);	
				/*
				SCM_Link_Rsp rsp;
				SN_MEMSET(&rsp, 0, sizeof(rsp));
				rsp.channel = chn;
				rsp.endtype = LINK_OVERFLOW;
				SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_FWK, 0, 0, MSG_ID_NTRANS_ONCEOVER_IND, &rsp, sizeof(rsp));
				*/
				//VochnInfo[index].IsDiscard = 1;
				
				//VochnInfo[index].bIsWaitIFrame = 1;
				VochnInfo[index].bIsWaitGetIFrame = 1;
				if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[index].VoChn))
				{
#if defined(SN9234H1)
					if(s_astVoDevStatDflt[HD].enPreviewStat == PRV_STAT_CTRL)
					{
						if(VochnInfo[index].VoChn == s_astVoDevStatDflt[HD].s32CtrlChn)
						{
							CHECK(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, HD, PRV_CTRL_VOCHN));					
							CHECK(HI_MPI_VO_ClearChnBuffer(HD, PRV_CTRL_VOCHN, 1));
						}
					}
					//必须disable Vo否则如果该"无网络"图片显示异常，且如果该通道下次在主片显示，也是异常
					CHECK(HI_MPI_VO_DisableChn(HD, VochnInfo[index].VoChn));			
				//	CHECK(HI_MPI_VO_DisableChn(s_VoSecondDev, VochnInfo[index].VoChn));
					PRV_VLossVdecBindVoChn(HD, VochnInfo[index].VoChn, s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);
				//	PRV_VLossVdecBindVoChn(s_VoSecondDev, VochnInfo[index].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
					CHECK(HI_MPI_VO_EnableChn(HD, VochnInfo[index].VoChn));
				//	CHECK(HI_MPI_VO_EnableChn(s_VoSecondDev, VochnInfo[index].VoChn));

					//sem_post(&sem_SendNoVideoPic);
					
#else
					if(s_astVoDevStatDflt[DHD0].enPreviewStat == PRV_STAT_CTRL)
					{
						if(VochnInfo[index].VoChn == s_astVoDevStatDflt[DHD0].s32CtrlChn)
						{
							//CHECK(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, DHD0, PRV_CTRL_VOCHN));					
							CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, PRV_CTRL_VOCHN, 1));
						}
					}
					//必须disable Vo否则如果该"无网络"图片显示异常，且如果该通道下次在主片显示，也是异常
					CHECK(HI_MPI_VO_DisableChn(DHD0, VochnInfo[index].VoChn));			
				//	CHECK(HI_MPI_VO_DisableChn(s_VoSecondDev, VochnInfo[index].VoChn));
					PRV_VLossVdecBindVoChn(DHD0, VochnInfo[index].VoChn, s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);
				//	PRV_VLossVdecBindVoChn(s_VoSecondDev, VochnInfo[index].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
					CHECK(HI_MPI_VO_EnableChn(DHD0, VochnInfo[index].VoChn));
				//	CHECK(HI_MPI_VO_EnableChn(s_VoSecondDev, VochnInfo[index].VoChn));

					//sem_post(&sem_SendNoVideoPic);
#endif					
					//if(!IsChoosePlayAudio)
						PRV_PlayAudio(s_VoDevCtrlDflt);
				}			
				else
				{
#if defined(SN9234H1)
					(HI_MPI_VDEC_BindOutput(DetVLoss_VdecChn, HD, VochnInfo[index].VoChn)); 	
					VochnInfo[index].IsBindVdec[HD] = 0;				
#else
					
					PRV_VPSS_ResetWH(VochnInfo[index].VoChn,NoConfig_VdecChn,704,576);
					PRV_VDEC_BindVpss(NoConfig_VdecChn, VochnInfo[index].VoChn);
					//(HI_MPI_VDEC_BindOutput(DetVLoss_VdecChn, DHD0, VochnInfo[index].VoChn)); 	
					VochnInfo[index].IsBindVdec[DHD0] = 0;
#endif					
		//			CHECK(HI_MPI_VDEC_BindOutput(DetVLoss_VdecChn, s_VoSecondDev, VochnInfo[index].VoChn)); 	
		//			VochnInfo[index].IsBindVdec[s_VoSecondDev] = 0;				
				}
			}
		}
		
	}
	else//重新创建成功
	{		
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "=============Slave ReCreate VdecChn: %d OK, CurSlaveCap: %d, OldVdecCap: %d, NewVdecCap: %d, SlaveId: %d!\n", VochnInfo[index].VdecChn, CurSlaveCap, VochnInfo[index].VdecCap, SlaveReCreateRsp->VdecCap, VochnInfo[index].SlaveId);			
		VochnInfo[index].VdecCap = SlaveReCreateRsp->VdecCap;
		VochnInfo[index].VideoInfo.height = SlaveReCreateRsp->height;
		VochnInfo[index].VideoInfo.width = SlaveReCreateRsp->width;
		VochnInfo[index].VdecChn = SlaveReCreateRsp->VdecChn;
		VochnInfo[index].IsHaveVdec = 1;
		if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[index].VoChn))
		{
#if defined(SN9234H1)
			if(s_astVoDevStatDflt[HD].enPreviewStat == PRV_STAT_CTRL)
			{
				if(VochnInfo[index].VoChn == s_astVoDevStatDflt[HD].s32CtrlChn)
				{
					CHECK(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, HD, PRV_CTRL_VOCHN));					
					CHECK(HI_MPI_VO_ClearChnBuffer(HD, PRV_CTRL_VOCHN, 1));
				}
			}
			HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, s_VoDevCtrlDflt, VochnInfo[index].VoChn);					
			CHECK(HI_MPI_VO_ClearChnBuffer(s_VoDevCtrlDflt, VochnInfo[index].VoChn, 1));
			
			CHECK(HI_MPI_VO_DisableChn(s_VoDevCtrlDflt, VochnInfo[index].VoChn));			
			PRV_BindVoChnInSlave(s_VoDevCtrlDflt, VochnInfo[index].VoChn, s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);				
#else
			if(s_astVoDevStatDflt[DHD0].enPreviewStat == PRV_STAT_CTRL)
			{
				if(VochnInfo[index].VoChn == s_astVoDevStatDflt[DHD0].s32CtrlChn)
				{
					//CHECK(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, DHD0, PRV_CTRL_VOCHN));					
					CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, PRV_CTRL_VOCHN, 1));
				}
			}
			//HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, s_VoDevCtrlDflt, VochnInfo[index].VoChn);					
			CHECK(HI_MPI_VO_ClearChnBuffer(s_VoDevCtrlDflt, VochnInfo[index].VoChn, 1));
			
			CHECK(HI_MPI_VO_DisableChn(s_VoDevCtrlDflt, VochnInfo[index].VoChn));			
			PRV_BindVoChnInSlave(s_VoDevCtrlDflt, VochnInfo[index].VoChn, s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);	
#endif			
			CHECK(HI_MPI_VO_EnableChn(s_VoDevCtrlDflt, VochnInfo[index].VoChn));
			PRV_PlayAudio(s_VoDevCtrlDflt);
		}			

	}
	RET_SUCCESS("");
}

/********************************************************
函数名:PRV_MSG_DestroyAllVdec
功     能:销毁所有解码通道。预览模式切换时调用
参     数:flag:---0:销毁所有解码通道后，不需要绑定"无网络视频"
				1:销毁所有解码通道后，需要绑定"无网络视频"
返回值:  无

*********************************************************/

HI_VOID PRV_MSG_DestroyAllVdec(int flag)
{
	int i = 0;
	//先销毁主片上所有解码通道，再通知从片销毁所有解码通道
	for(i = LOCALVEDIONUM; i < CHANNEL_NUM; i++)
	{
		if(VochnInfo[i].SlaveId == PRV_MASTER)
		{
			if(VochnInfo[i].IsHaveVdec)
			{
				PRV_WaitDestroyVdecChn(VochnInfo[i].VdecChn);
				BufferSet(VochnInfo[i].VoChn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);						
				BufferSet(VochnInfo[i].VoChn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
				CurCap -= VochnInfo[i].VdecCap;
				CurMasterCap -= VochnInfo[i].VdecCap;			
				PRV_VoChnStateInit(VochnInfo[i].CurChnIndex);
				//CurIPCCount--;			
				PRV_PtsInfoInit(VochnInfo[i].CurChnIndex);		
				PRV_InitVochnInfo(i);	
			}
			else
			{
#if defined(SN9234H1)
				HI_MPI_VDEC_UnbindOutputChn(VochnInfo[i].VdecChn, HD, VochnInfo[i].VoChn);/* 解绑定通道 */
				//CHECK(HI_MPI_VO_ClearChnBuffer(HD, VochnInfo[i].VoChn, 1)); /* 清除VO缓存 */
#else			
				PRV_VDEC_UnBindVpss(NoConfig_VdecChn, VochnInfo[i].VoChn);
				//CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, VochnInfo[i].VoChn, 1)); /* 清除VO缓存 */
#endif				
				VochnInfo[i].IsBindVdec[DHD0] = -1;
			}
			if(flag == 0||flag == LayOut_KeyBoard||flag == ParamUpdate_Switch)//画面切换
				VochnInfo[i].VdecChn = NoConfig_VdecChn;	
			else if(flag == 1)//被动解码下连接失败
				VochnInfo[i].VdecChn = DetVLoss_VdecChn;	
			else//网络断开
			{
				if(SCM_ChnConfigState(VochnInfo[i].VoChn) == 0)
					VochnInfo[i].VdecChn = NoConfig_VdecChn;	
				else
					VochnInfo[i].VdecChn = DetVLoss_VdecChn;

			}
			VochnInfo[i].VdecChn = NoConfig_VdecChn;
#if defined(SN9234H1)
			CHECK(HI_MPI_VO_DisableChn(HD, VochnInfo[i].VoChn));			
			if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[i].VoChn))
			{
			//	CHECK(HI_MPI_VO_DisableChn(s_VoSecondDev, VochnInfo[chn].VoChn));
				PRV_VLossVdecBindVoChn(HD, VochnInfo[i].VoChn,  s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);
			//	PRV_VLossVdecBindVoChn(s_VoSecondDev, VochnInfo[chn].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
			//	CHECK(HI_MPI_VO_EnableChn(s_VoSecondDev, VochnInfo[chn].VoChn));

			//	sem_post(&sem_SendNoVideoPic);
				
			//	if(!IsChoosePlayAudio)
			//		PRV_PlayAudio(s_VoDevCtrlDflt);
				CHECK(HI_MPI_VO_EnableChn(HD, VochnInfo[i].VoChn)); 		
					
			}
			else
			{	
				(HI_MPI_VDEC_BindOutput(VochnInfo[i].VdecChn, HD, VochnInfo[i].VoChn)); 	
				VochnInfo[i].IsBindVdec[HD] = 0;				
		//		CHECK(HI_MPI_VDEC_BindOutput(DetVLoss_VdecChn, s_VoSecondDev, VochnInfo[chn].VoChn)); 	
		//		VochnInfo[chn].IsBindVdec[s_VoSecondDev] = 0;				
			}		

#else			
			
			CHECK(HI_MPI_VO_DisableChn(DHD0, VochnInfo[i].VoChn));			
			//CHECK(HI_MPI_VO_DisableChn(DSD0, VochnInfo[i].VoChn));			
			if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[i].VoChn))
			{
				PRV_VLossVdecBindVoChn(DHD0, VochnInfo[i].VoChn,  s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);
			//	PRV_VLossVdecBindVoChn(DSD0, VochnInfo[i].VoChn, s_astVoDevStatDflt[DSD0].s32PreviewIndex, s_astVoDevStatDflt[DSD0].enPreviewMode);
				CHECK(HI_MPI_VO_EnableChn(DHD0, VochnInfo[i].VoChn));
			//	CHECK(HI_MPI_VO_EnableChn(DSD0, VochnInfo[i].VoChn)); 		

			//	sem_post(&sem_SendNoVideoPic);
				
			//	if(!IsChoosePlayAudio)
			//		PRV_PlayAudio(s_VoDevCtrlDflt);
					
			}
			else
			{	
				(PRV_VDEC_BindVpss(VochnInfo[i].VdecChn, VochnInfo[i].VoChn)); 	
				VochnInfo[i].IsBindVdec[DHD0] = 0;				
			//	VochnInfo[i].IsBindVdec[DSD0] = 0;				
			}
#endif					
		}
	}
#if defined(SN_SLAVE_ON)	
	HI_S32 s32Ret = 0;
	BufferSet(0, MAX_ARRAY_NODE);						
	
	s32Ret = HostCleanAllHostToSlaveStream(MasterToSlaveChnId, 0, 1);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "slave %d HostCleanAllHostToSlaveStream error");
	}	
	SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_DESALLVDEC_REQ, &flag, sizeof(int));
#endif	
}


/********************************************************
函数名:PRV_MSG_MCC_DesAllVdecRsp
功     能:从片销毁所有解码通道返回消息处理
参     数:无
返回值:  无

*********************************************************/
HI_VOID PRV_MSG_MCC_DesAllVdecRsp(int flag)
{
	int i = 0;
	for(i = LOCALVEDIONUM; i < CHANNEL_NUM; i++)
	{
		if(VochnInfo[i].IsHaveVdec && VochnInfo[i].SlaveId > PRV_MASTER)
		{
#if defined(SN9234H1)
			(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, HD, VochnInfo[i].VoChn));					
			CHECK(HI_MPI_VO_ClearChnBuffer(HD, VochnInfo[i].VoChn, 1));
			VochnInfo[i].IsBindVdec[HD] = -1;

#else		
			//(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, DHD0, VochnInfo[i].VoChn));					
			CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, VochnInfo[i].VoChn, 1));
			VochnInfo[i].IsBindVdec[DHD0] = -1;
#endif			
			BufferSet(VochnInfo[i].VoChn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);						
			BufferSet(VochnInfo[i].VoChn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
			CurCap -= VochnInfo[i].VdecCap;
			CurSlaveCap -= VochnInfo[i].VdecCap;			
			CurSlaveChnCount--;
			PRV_VoChnStateInit(VochnInfo[i].CurChnIndex);
			//CurIPCCount--;			
			PRV_PtsInfoInit(VochnInfo[i].CurChnIndex);		
			PRV_InitVochnInfo(i);
			if(flag == 0)//画面切换
				VochnInfo[i].VdecChn = NoConfig_VdecChn;	
			else if(flag == 1)//被动解码下连接失败
				VochnInfo[i].VdecChn = DetVLoss_VdecChn;	
			else//网络断开
			{
				if(SCM_ChnConfigState(VochnInfo[i].VoChn) == 0)
					VochnInfo[i].VdecChn = NoConfig_VdecChn;	
				else
					VochnInfo[i].VdecChn = DetVLoss_VdecChn;

			}
			VochnInfo[i].VdecChn = NoConfig_VdecChn;
			if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[i].VoChn))
			{
#if defined(SN9234H1)
				if(s_astVoDevStatDflt[HD].enPreviewStat == PRV_STAT_CTRL)
				{
					if(VochnInfo[i].VoChn == s_astVoDevStatDflt[HD].s32CtrlChn)
					{
						(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, HD, PRV_CTRL_VOCHN));					
						(HI_MPI_VO_ClearChnBuffer(HD, PRV_CTRL_VOCHN, 1));
					}
				}
				//必须disable Vo否则如果该"无网络"图片显示异常，且如果该通道下次在主片显示，也是异常
				CHECK(HI_MPI_VO_DisableChn(HD, VochnInfo[i].VoChn));			
			//	CHECK(HI_MPI_VO_DisableChn(s_VoSecondDev, VochnInfo[index].VoChn));
				PRV_VLossVdecBindVoChn(HD, VochnInfo[i].VoChn, s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);
			//	PRV_VLossVdecBindVoChn(s_VoSecondDev, VochnInfo[index].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
				CHECK(HI_MPI_VO_EnableChn(HD, VochnInfo[i].VoChn));
			//	CHECK(HI_MPI_VO_EnableChn(s_VoSecondDev, VochnInfo[index].VoChn));

#else			
				if(s_astVoDevStatDflt[DHD0].enPreviewStat == PRV_STAT_CTRL)
				{
					if(VochnInfo[i].VoChn == s_astVoDevStatDflt[DHD0].s32CtrlChn)
					{
						//(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, DHD0, PRV_CTRL_VOCHN));					
						(HI_MPI_VO_ClearChnBuffer(DHD0, PRV_CTRL_VOCHN, 1));
					}
				}
				//必须disable Vo否则如果该"无网络"图片显示异常，且如果该通道下次在主片显示，也是异常
				CHECK(HI_MPI_VO_DisableChn(DHD0, VochnInfo[i].VoChn));			
			//	CHECK(HI_MPI_VO_DisableChn(s_VoSecondDev, VochnInfo[index].VoChn));
				PRV_VLossVdecBindVoChn(DHD0, VochnInfo[i].VoChn, s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);
			//	PRV_VLossVdecBindVoChn(s_VoSecondDev, VochnInfo[index].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
				CHECK(HI_MPI_VO_EnableChn(DHD0, VochnInfo[i].VoChn));
			//	CHECK(HI_MPI_VO_EnableChn(s_VoSecondDev, VochnInfo[index].VoChn));
#endif
				//sem_post(&sem_SendNoVideoPic);
				
				//if(!IsChoosePlayAudio)
				//	PRV_PlayAudio(s_VoDevCtrlDflt);
			}			
			else
			{
#if defined(SN9234H1)
				(HI_MPI_VDEC_BindOutput(VochnInfo[i].VdecChn, HD, VochnInfo[i].VoChn)); 	
				VochnInfo[i].IsBindVdec[HD] = 0;				
#else			
				PRV_VDEC_BindVpss(VochnInfo[i].VdecChn, VochnInfo[i].VoChn);
				VochnInfo[i].IsBindVdec[DHD0] = 0;	
#endif							
	//			CHECK(HI_MPI_VDEC_BindOutput(DetVLoss_VdecChn, s_VoSecondDev, VochnInfo[index].VoChn)); 	
	//			VochnInfo[index].IsBindVdec[s_VoSecondDev] = 0;				
			}			
		}
	}
#if defined(SN_SLAVE_ON)	

	HI_S32 s32Ret = 0;
	BufferSet(0, MAX_ARRAY_NODE);						
	
	s32Ret = HostCleanAllHostToSlaveStream(MasterToSlaveChnId, 0, 1);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "slave %d HostCleanAllHostToSlaveStream error");
	}	
#endif	
	if(PRV_CurIndex > 0)
	{
		for(i = 0; i < PRV_CurIndex; i++)
		{
			NTRANS_FreeMediaData(PRV_OldVideoData[i]);
			PRV_OldVideoData[i] = NULL;
		}
		PRV_CurIndex = 0;
		PRV_SendDataLen = 0;
	}
	
}

/********************************************************
函数名:PRV_MSG_CtrlVdec
功     能:销毁指定通道解码器
参     数:[in]msg_req  消息
返回值:  无

*********************************************************/

HI_VOID	PRV_MSG_CtrlVdec(const SN_MSG *msg_req)
{
	int chn = 0, flag = -1, index = -1, OldVdecChn = -1;
	SCM_CtrVdecInd	*CtrlVdec = (SCM_CtrVdecInd *)msg_req->para;
	chn = CtrlVdec->chn;
	flag = CtrlVdec->flag;
	index = chn + LOCALVEDIONUM;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Receive message: MSG_ID_PRV_CTRVDEC_IND---chn: %d, flag=%d\n", chn, flag);				

	if(chn < 0 || chn >= DEV_CHANNEL_NUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s,line: %d---------invalid channel: %d!!\n", __FUNCTION__, __LINE__, chn);
		if(PRV_GetDoubleIndex())//双击状态进入单画面
		{
			if(chn == DEV_CHANNEL_NUM)
			{
				chn = PRV_GetDoubleToSingleIndex();
				if(VochnInfo[chn].bIsDouble)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s===chn: %d stop===\n", __FUNCTION__, chn);
					VochnInfo[chn].bIsDouble = 0;
				}
			}
		}

		return;
	}

	if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat != PRV_STAT_NORM
		|| VochnInfo[index].bIsPBStat)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "In PB or PIC, Don't PRV_MSG_CtrlVdec"); 
		return;
	}
	
	if(PRV_CurDecodeMode == SwitchDecode || ScmGetListCtlState() == 1)//主动解码下，需要销毁解码通道
	{
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "flag: %d, IsHaveVdec: %d, SlaveId: %d\n", flag, VochnInfo[index].IsHaveVdec, VochnInfo[index].SlaveId);				
		if(VochnInfo[index].SlaveId == PRV_MASTER)
		{
			//if(/*VochnInfo[chn + LOCALVEDIONUM].IsHaveVdec || */VochnInfo[chn + LOCALVEDIONUM].VdecChn != DetVLoss_VdecChn)
			if(VochnInfo[index].bIsPBStat != 1)
			{
				OldVdecChn = VochnInfo[index].VdecChn;
				if(VochnInfo[chn + LOCALVEDIONUM].IsHaveVdec)
				{
					PRV_WaitDestroyVdecChn(VochnInfo[index].VdecChn);
					BufferSet(VochnInfo[index].VoChn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);						
					BufferSet(VochnInfo[index].VoChn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
					CurCap -= VochnInfo[index].VdecCap;
					CurMasterCap -= VochnInfo[index].VdecCap;			
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d chn: %d, CurMasterCap: %d, VochnInfo[chn + LOCALVEDIONUM].VdecCap: %d\n", __LINE__, chn, CurMasterCap, VochnInfo[index].VdecCap); 
					PRV_VoChnStateInit(VochnInfo[index].CurChnIndex);
					PRV_PtsInfoInit(VochnInfo[index].CurChnIndex);
					PRV_InitVochnInfo(index);	
				}
				//CurIPCCount--;	
				PRV_InitVochnInfo(index);				
			//	CHECK(HI_MPI_VO_ClearChnBuffer(DSD0, VochnInfo[index].VoChn, 1)); 
				//if(ScmGetListCtlState() == 1)//点位控制下连接失败时，绑定"无网络视频"
				//	VochnInfo[index].VdecChn = DetVLoss_VdecChn;
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VochnInfo[index].VdecChn: %d\n", VochnInfo[index].VdecChn);				
#if defined(SN9234H1)
				VochnInfo[index].IsBindVdec[HD] = -1;
#else			
				VochnInfo[index].IsBindVdec[DHD0] = -1;	
#endif		
				//解码通道仍然是"无网络视频"时，不刷新通道数据		
				if(OldVdecChn == DetVLoss_VdecChn && VochnInfo[index].VdecChn == DetVLoss_VdecChn)
					return;
				if(OldVdecChn == DetVLoss_VdecChn || OldVdecChn == NoConfig_VdecChn)
#if defined(SN9234H1)
				{
					CHECK(HI_MPI_VDEC_UnbindOutputChn(OldVdecChn, HD, VochnInfo[index].VoChn));
				}
				//CHECK(HI_MPI_VO_ClearChnBuffer(HD, VochnInfo[index].VoChn, 1)); 
#else				
				{				
					PRV_VDEC_UnBindVpss(OldVdecChn,VochnInfo[index].VoChn);	
				}			
				//CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, VochnInfo[index].VoChn, 1)); // 清除VO缓存 
#endif				
				VochnInfo[index].IsBindVdec[DHD0] = -1;
				if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[index].VoChn))
				{
#if defined(SN9234H1)
					CHECK(HI_MPI_VO_DisableChn(HD, VochnInfo[index].VoChn));			
				//	CHECK(HI_MPI_VO_DisableChn(s_VoSecondDev, VochnInfo[chn].VoChn));
					PRV_VLossVdecBindVoChn(HD, VochnInfo[index].VoChn, s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);
				//	PRV_VLossVdecBindVoChn(s_VoSecondDev, VochnInfo[chn].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
					CHECK(HI_MPI_VO_EnableChn(HD, VochnInfo[index].VoChn));			

#else				
					PRV_InitVochnInfo(index);	
					//if(ScmGetListCtlState() == 1)//点位控制下连接失败时，绑定"无网络视频"
					//	VochnInfo[index].VdecChn = DetVLoss_VdecChn;
					CHECK(HI_MPI_VO_DisableChn(DHD0, VochnInfo[index].VoChn));			
					//CHECK(HI_MPI_VO_DisableChn(DSD0, VochnInfo[chn].VoChn));
					PRV_VLossVdecBindVoChn(DHD0, VochnInfo[index].VoChn, s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);
					//PRV_VLossVdecBindVoChn(DSD0, VochnInfo[chn].VoChn, s_astVoDevStatDflt[DSD0].s32PreviewIndex, s_astVoDevStatDflt[DSD0].enPreviewMode);
					CHECK(HI_MPI_VO_EnableChn(DHD0, VochnInfo[index].VoChn));			
					//CHECK(HI_MPI_VO_EnableChn(DSD0, VochnInfo[index].VoChn));	
#endif							
				}
				else// if(VochnInfo[index].IsBindVdec[DHD0] < 0)
				{
#if defined(SN9234H1)
					(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, HD, VochnInfo[index].VoChn));	
					VochnInfo[index].IsBindVdec[HD] = 0;				
		//			CHECK(HI_MPI_VDEC_BindOutput(DetVLoss_VdecChn, s_VoSecondDev, VochnInfo[index].VoChn)); 	
		//			VochnInfo[index].IsBindVdec[s_VoSecondDev] = 0; 			
#else				
					PRV_VPSS_ResetWH(VochnInfo[index].VoChn,VochnInfo[index].VdecChn,VochnInfo[index].VideoInfo.width,VochnInfo[index].VideoInfo.height);
					PRV_VDEC_BindVpss(VochnInfo[index].VdecChn,VochnInfo[index].VoChn);
					VochnInfo[index].IsBindVdec[DHD0] = 0;				
					//VochnInfo[index].IsBindVdec[DSD0] = 0;	
#endif								
				}			
			}
			
		}
		else if(VochnInfo[index].SlaveId > PRV_MASTER && VochnInfo[chn + LOCALVEDIONUM].IsHaveVdec)
		{
			PRV_MccDestroyVdecReq DestroyVdecReq;
			DestroyVdecReq.VdecChn = VochnInfo[index].VdecChn;
			
			SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_DESVDEC_REQ, &DestroyVdecReq, sizeof(PRV_MccDestroyVdecReq));

		}
	}
	else//被动解码下时，不销毁解码通道，不预览此通道的数据
	{
		
	}	
	
}


/********************************************************
函数名:PRV_CheckIsCreateVdec
功     能:判断是否已经创建指定通道的解码器。
参     数:[in]index  视频输出通道的通道信息下标
返回值:  HI_SUCCESS成功
		    HI_FAILURE失败

*********************************************************/

HI_S32 PRV_CheckIsCreateVdec(int index)
{
	if(index < 0)
		return HI_FAILURE;

	if(VochnInfo[index].VdecChn >= 0
		&& VochnInfo[index].VdecChn != DetVLoss_VdecChn && VochnInfo[index].VdecChn != NoConfig_VdecChn
		&& VochnInfo[index].SlaveId == PRV_MASTER)//已创建解码通道
	{
		CHECK_RET(PRV_WaitDestroyVdecChn(VochnInfo[index].VdecChn));				
	}
	
	return HI_SUCCESS;

}

/********************************************************
函数名:PRV_ReCreateAllVdec
功     能:重新创建主片所有解码器。
性能足够，因为内存不足(存在内存碎片)导致创建解码器失败时调用
参     数:无
返回值:  无

*********************************************************/

HI_VOID PRV_ReCreateAllVdec()
{
	HI_S32 i = 0;
	//先销毁所有解码器
	for(i = LOCALVEDIONUM; i < DEV_CHANNEL_NUM; i++)
	{
		if(VochnInfo[i].IsHaveVdec
			&& VochnInfo[i].SlaveId == PRV_MASTER
			&& VochnInfo[i].VdecChn >= 0
			&& VochnInfo[i].VdecChn != DetVLoss_VdecChn && VochnInfo[i].VdecChn != NoConfig_VdecChn)
		{
			CHECK(HI_MPI_VDEC_StopRecvStream(VochnInfo[i].VdecChn));//销毁通道前先停止接收数据
			CHECK(HI_MPI_VDEC_ResetChn(VochnInfo[i].VdecChn)); //解码器复位 	
#if defined(SN9234H1)
			CHECK(HI_MPI_VDEC_UnbindOutput(VochnInfo[i].VdecChn));// 解绑定通道 
#else					
			CHECK(PRV_VDEC_UnBindVpss(VochnInfo[i].VdecChn,VochnInfo[i].VdecChn));// 解绑定通道 
#endif		
			VochnInfo[i].IsBindVdec[DHD0] = -1;
			HI_MPI_VO_ClearChnBuffer(i, VochnInfo[i].VoChn, 1); // 清除VO缓存 
			HI_MPI_VDEC_DestroyChn(VochnInfo[i].VdecChn); // 销毁视频通道 
			
			BufferSet(VochnInfo[i].VoChn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);						
			BufferSet(VochnInfo[i].VoChn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
		}
	}
	//重建已经存在的所有解码器	
	for(i = LOCALVEDIONUM; i < DEV_CHANNEL_NUM; i++)
	{
		if(VochnInfo[i].IsHaveVdec
			&& VochnInfo[i].SlaveId == PRV_MASTER
			&& VochnInfo[i].VdecChn >= 0
			&& VochnInfo[i].VdecChn != DetVLoss_VdecChn && VochnInfo[i].VdecChn != NoConfig_VdecChn)
		{	
			CHECK(PRV_CreateVdecChn(VochnInfo[i].VideoInfo.vdoType, VochnInfo[i].VideoInfo.height, VochnInfo[i].VideoInfo.width, VochnInfo[i].u32RefFrameNum, VochnInfo[i].VoChn));
#if defined(SN9234H1)
			PRV_BindVoChnInMaster(HD, VochnInfo[i].VoChn, s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);			
#else			
			PRV_BindVoChnInMaster(DHD0, VochnInfo[i].VoChn, s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);			
			//PRV_BindVoChnInMaster(DSD0, VochnInfo[i].VoChn, s_astVoDevStatDflt[DSD0].s32PreviewIndex, s_astVoDevStatDflt[DSD0].enPreviewMode);
#endif						
			//VochnInfo[i].bIsWaitIFrame = 1;
			VochnInfo[i].bIsWaitGetIFrame = 1;
		}
	}
}

/*************************************************
Function: PRV_MSG_NTRANSLinkReq
Description: 创建解码通道，绑定VO输出，启动接收对应通道数据标识，实现预览
Calls: 
Called By: 
Input:  msg_req链接请求结构体，包含链接的通道号
Output: 返回系统定义的错误码
Return: 详细参看文档的错误码
Others: 
***********************************************************************/
STATIC HI_S32 PRV_MSG_NTRANSLinkReq(const SN_MSG *msg_req)
{
	HI_S32 chn = 0, s32Ret = 0;
	if(msg_req->msgId == MSG_ID_NTRANS_ADDUSER_RSP)
	{					
		NTRANS_ENDOFCOM_RSP *AddUser_Rsp = (NTRANS_ENDOFCOM_RSP*)msg_req->para;
		if(AddUser_Rsp->endtype != LINK_ALL_SUCCESS)
		{
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ADDUSER_RSP fail!!!!chn: %d, endtype: %d\n", AddUser_Rsp->channel, AddUser_Rsp->endtype);
			return HI_FAILURE;			
		}
		chn = AddUser_Rsp->channel;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------------Receive MSG: MSG_ID_NTRANS_ADDUSER_RSP---%d\n", chn);
	}
	else if(msg_req->msgId == MSG_ID_NTRANS_BEGINLINK_IND)
	{				
		beginlink_rsp *BeginLinkReq = (beginlink_rsp *)msg_req->para;
		chn = BeginLinkReq->channel;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------------Receive MSG: MSG_ID_NTRANS_BEGINLINK_IND---%d\n", chn);
	}
	
	if(chn < 0 || chn >= MAX_IPC_CHNNUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Invalid Channel: %d, line: %d\n", chn, __LINE__);
		return HI_FAILURE;		
	}
	
	deluser_used DelUserReq;
	DelUserReq.channel = chn;
	//当前从片正在创建此通道
	if(SlaveState.SlaveCreatingVdec[chn + LOCALVEDIONUM])
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave Is Creating Vdec: %d\n", chn);
		//SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ, &DelUserReq, sizeof(deluser_used));			
		return HI_FAILURE;
	}

	//当前通道在从片，销毁解码通道过程中
	if(SlaveState.SlaveIsDesingVdec[chn + LOCALVEDIONUM])
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Slave Is Destroying vdec: %d\n", chn);
		//请求断开连接
		SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ, &DelUserReq, sizeof(deluser_used));			
		return HI_FAILURE;
	}
	
	
	PRV_MccCreateVdecReq SlaveCreateVdec;		
	RTSP_C_SDPInfo RTSP_SDP;				
	SN_MEMSET(&SlaveCreateVdec, 0, sizeof(SlaveCreateVdec));
	SN_MEMSET(&RTSP_SDP, 0, sizeof(RTSP_C_SDPInfo));
	
	if(RTSP_C_getParam(chn, &RTSP_SDP) != 0)
	{		
		RET_FAILURE("RTSP_C_getParam!!!");
	}
	
	if(RTSP_SDP.vdoType == SN_UNKOWN_VDOTYPE )
	{			
		RET_FAILURE("Video Type!!!");
	}
	
	TRACE(SCI_TRACE_NORMAL, MOD_PRV,"---------chn: %d, RTSP_SDP.high:%d, RTSP_SDP.width:%d, RTSP_SDP.vdoType: %d\n", chn, RTSP_SDP.high, RTSP_SDP.width, RTSP_SDP.vdoType);

	TRACE(SCI_TRACE_NORMAL, MOD_PRV,"-----------adoType: %d, samrate: %d, soundchannel: %d\n", RTSP_SDP.adoType, RTSP_SDP.samrate, RTSP_SDP.soundchannel);
	if(RTSP_SDP.high > 4096 || RTSP_SDP.width > 4096
		|| RTSP_SDP.high <= 0 || RTSP_SDP.width <= 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Not Support Res---height: %d, width: %d\n", RTSP_SDP.high, RTSP_SDP.width);
		//请求断开连接
		SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ, &DelUserReq, sizeof(deluser_used));			
		return SN_SLAVE_MSG;
	}
#if 0
	//各种型号下，支持的最大分辨率
#if defined(SN8604D) || defined(SN8608D) ||defined(SN8608D_LE) ||defined(SN8616D_LE)
	if(RTSP_SDP.width * RTSP_SDP.high > D1_PIXEL)
#elif defined(SN8604M)|| defined(SN8608M) ||defined(SN8608M_LE) ||defined(SN8616M_LE) || defined(SN9234H1)
	if(RTSP_SDP.width * RTSP_SDP.high > _1080P_PIXEL)
#endif
	{
		RET_FAILURE("Not Supported Resolution!");
	}
#endif
#if 1
	if(RTSP_SDP.adoType != 0)//如果没音频，没必要获取音视频基准值做同步
	{
		NTRANS_getFirstMediaPts(chn, &(PtsInfo[chn].FirstVideoPts), &(PtsInfo[chn].FirstAudioPts));
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------chn: %d, FistVideopts: %llu, FirstAudiopts: %llu\n", chn, PtsInfo[chn].FirstVideoPts, PtsInfo[chn].FirstAudioPts);
		
	}
#endif

	chn = chn + LOCALVEDIONUM;				
	CHECK(PRV_CheckIsCreateVdec(chn));
		
	if(!VochnInfo[chn].IsHaveVdec)
	{
		PRV_SetVochnInfo(&(VochnInfo[chn]), &RTSP_SDP);
		CHECK_RET(PRV_ChooseSlaveId(chn, &SlaveCreateVdec));
	
		if(SlaveCreateVdec.SlaveId > PRV_MASTER)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Send MSG_ID_PRV_MCC_CREATEVDEC_REQ!\n");								
			SlaveState.SlaveCreatingVdec[chn] = 1;
			s_State_Info.bIsSlaveConfig = HI_TRUE;
			SN_SendMccMessageEx(SlaveCreateVdec.SlaveId, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_CREATEVDEC_REQ, &SlaveCreateVdec, sizeof(PRV_MccCreateVdecReq)); 					
			return HI_SUCCESS;
		}			
		else
		{
			if(PRV_STAT_PB == s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat || PRV_STAT_PIC == s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat)
			{	
				CurMasterCap += VochnInfo[chn].VdecCap;
				RET_FAILURE("In PB or Pic Stat!");
			}
			else
			{
				s32Ret = PRV_CreateVdecChn(VochnInfo[chn].VideoInfo.vdoType, VochnInfo[chn].VideoInfo.height, VochnInfo[chn].VideoInfo.width, VochnInfo[chn].u32RefFrameNum, VochnInfo[chn].VoChn);

				if(s32Ret == HI_ERR_VDEC_NOMEM)
				{
					PRV_ReCreateAllVdec();
					//再次创建此解码通道
					s32Ret = PRV_CreateVdecChn(VochnInfo[chn].VideoInfo.vdoType, VochnInfo[chn].VideoInfo.height, VochnInfo[chn].VideoInfo.width, VochnInfo[chn].u32RefFrameNum, VochnInfo[chn].VoChn);
				}

				if(s32Ret == HI_SUCCESS)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------Master-----Create Vdec: %d OK!\n", VochnInfo[chn].VdecChn);					

					//判断新连接的通道是否在当前预览画面中
					//在当前画面中，绑定VO，接收数据显示
					//if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[chn].VoChn))//只需对HD判断，s_VoSecondDev与HD一致，同下
					{	
						if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_CTRL
							&&(s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_REGION_SEL || s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_ZOOM_IN)
							&& VochnInfo[chn].VoChn == s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn)
#if defined(SN9234H1)
						{
							CHECK(HI_MPI_VDEC_UnbindOutputChn(NoConfig_VdecChn, s_VoDevCtrlDflt, PRV_CTRL_VOCHN));
							//CHECK_RET(HI_MPI_VDEC_BindOutput(VochnInfo[chn].VdecChn, s_VoDevCtrlDflt, PRV_CTRL_VOCHN)); 
						}
						//else
						{
							//CHECK(HI_MPI_VO_ClearChnBuffer(s_VoDevCtrlDflt, VochnInfo[chn].VoChn, 1));
							//CHECK(HI_MPI_VO_ClearChnBuffer(s_VoSecondDev, VochnInfo[chn].VoChn, 1));	
							//if(VochnInfo[chn].IsBindVdec[s_VoDevCtrlDflt] != -1)
							{
								(HI_MPI_VDEC_UnbindOutputChn(NoConfig_VdecChn, HD, VochnInfo[chn].VoChn));
								VochnInfo[chn].IsBindVdec[HD] = -1;
							//	CHECK(HI_MPI_VDEC_UnbindOutputChn(DetVLoss_VdecChn, s_VoSecondDev, VochnInfo[chn].VoChn));
							//	VochnInfo[chn].IsBindVdec[s_VoSecondDev] = -1;
							}
							PRV_BindVoChnInMaster(HD, VochnInfo[chn].VoChn, s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode); 			
							//PRV_BindVoChnInMaster(s_VoSecondDev, VochnInfo[chn].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
							
							//VochnInfo[chn].bIsStopGetVideoData = 0;

						}
#else
						{
							PRV_VDEC_UnBindVpss(NoConfig_VdecChn,PRV_CTRL_VOCHN);							
							//CHECK_RET(HI_MPI_VDEC_BindOutput(VochnInfo[chn].VdecChn, s_VoDevCtrlDflt, PRV_CTRL_VOCHN)); 
						}
						//CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, VochnInfo[chn].VoChn, 1));
						//CHECK(HI_MPI_VO_ClearChnBuffer(DSD0, VochnInfo[chn].VoChn, 1));	
						PRV_VDEC_UnBindVpss(NoConfig_VdecChn,VochnInfo[chn].VoChn);
						VochnInfo[chn].IsBindVdec[DHD0] = -1;
						//VochnInfo[chn].IsBindVdec[DSD0] = -1;
						PRV_BindVoChnInMaster(DHD0, VochnInfo[chn].VoChn, s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode); 			
						//PRV_BindVoChnInMaster(DSD0, VochnInfo[chn].VoChn, s_astVoDevStatDflt[DSD0].s32PreviewIndex, s_astVoDevStatDflt[DSD0].enPreviewMode); 			
#endif
					}

#if defined(SN9234H1)
					//存在Spot口，判断SPOT口当前的VO通道是否就是新连接的通道，
					if(DEV_SPOT_NUM > 0)
					{
						//如果是，VO需要先解绑定与DetVLoss_VdecChn(30)
						if(VochnInfo[chn].VoChn == s_astVoDevStatDflt[SPOT_VO_DEV].as32ChnOrder[SingleScene][s_astVoDevStatDflt[SPOT_VO_DEV].s32SingleIndex])
						{				
							//if(VochnInfo[chn].IsBindVdec[SD] == 0)
							{
								(HI_MPI_VDEC_UnbindOutputChn(NoConfig_VdecChn, SPOT_VO_DEV, SPOT_VO_CHAN));						
								VochnInfo[chn].IsBindVdec[SD] = -1;
							}
							CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[chn].VdecChn, SPOT_VO_DEV, SPOT_VO_CHAN));			
							VochnInfo[chn].IsBindVdec[SD] = 1;
						}
					}
#endif
					CurMasterCap += VochnInfo[chn].VdecCap;
					VochnInfo[chn].IsHaveVdec = 1;
					
					//if(!IsChoosePlayAudio)
						PRV_PlayAudio(s_VoDevCtrlDflt);
					
				}
				else
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Create Vdec: %d fail!!!\n", VochnInfo[chn].VdecChn);
					CurCap -= VochnInfo[chn].VdecCap;
					
					SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ, &DelUserReq, sizeof(deluser_used));			
					PRV_InitVochnInfo(chn);
#if defined(SN9234H1)
					VochnInfo[chn].IsBindVdec[HD] = 0;
#else					
					VochnInfo[chn].IsBindVdec[DHD0] = 0;
#endif					
					return HI_FAILURE;
				}				
			}
		}
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Channel: %d allready Create Vdec!!!\n", chn);
		return HI_FAILURE;
	}

	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------Master-----chn: %d, VdecCap: %d, CurMasterCap: %d, Begin receive data!!!\n\n", 
									VochnInfo[chn].CurChnIndex, VochnInfo[chn].VdecCap, CurMasterCap);

	return HI_SUCCESS;
}
/********************************************************
函数名:PRV_CreateVdecChn_EX
功     能:创建解码通道。解码器(Decoder)中使用
参     数:[in]chn指定通道号
返回值:  无

*********************************************************/

HI_S32 PRV_CreateVdecChn_EX(HI_S32 chn)
{
	HI_S32 s32Ret = 0;
	s32Ret = PRV_CreateVdecChn(VochnInfo[chn].VideoInfo.vdoType, VochnInfo[chn].VideoInfo.height, VochnInfo[chn].VideoInfo.width, VochnInfo[chn].u32RefFrameNum, VochnInfo[chn].VoChn);

	//因为内存不够导致创建解码通道失败，
	//重建主片所有通道再创建此通道(可使内存重新分配)
	if(s32Ret == HI_ERR_VDEC_NOMEM)
	{
		PRV_ReCreateAllVdec();
		s32Ret = PRV_CreateVdecChn(VochnInfo[chn].VideoInfo.vdoType, VochnInfo[chn].VideoInfo.height, VochnInfo[chn].VideoInfo.width, VochnInfo[chn].u32RefFrameNum, VochnInfo[chn].VoChn);
	}
	
	if(s32Ret == HI_SUCCESS)
	{	
#if defined(SN9234H1)
		(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[chn].VdecChn, HD, VochnInfo[chn].VoChn));
		CHECK(HI_MPI_VO_ClearChnBuffer(s_VoDevCtrlDflt, VochnInfo[chn].VoChn, 1));
		VochnInfo[chn].IsBindVdec[HD] = -1;
		//判断新连接的通道是否在当前预览画面中
		//在当前画面中，绑定VO，接收数据显示
		if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[chn].VoChn))//只需对HD判断，s_VoSecondDev与HD一致，同下
		{	
			if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_CTRL
				&&(s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_REGION_SEL || s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_ZOOM_IN)
				&& VochnInfo[chn].VoChn == s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn)
			{
				(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[chn].VdecChn, s_VoDevCtrlDflt, PRV_CTRL_VOCHN));
				CHECK(HI_MPI_VO_ClearChnBuffer(s_VoDevCtrlDflt, VochnInfo[chn].VoChn, 1));
			}
			//else
			{

				VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;	
				CHECK(HI_MPI_VO_DisableChn(HD, VochnInfo[chn].VoChn));			
				PRV_BindVoChnInMaster(HD, VochnInfo[chn].VoChn, s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);			
				CHECK(HI_MPI_VO_EnableChn(HD, VochnInfo[chn].VoChn));			
				//PRV_BindVoChnInMaster(s_VoSecondDev, VochnInfo[chn].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);				
				//VochnInfo[chn].bIsStopGetVideoData = 0;
			}
		}
		else
		{
			VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;	
			(HI_MPI_VDEC_BindOutput(VochnInfo[chn].VdecChn, HD, VochnInfo[chn].VoChn));	
			VochnInfo[chn].IsBindVdec[HD] = 1;				
	//		CHECK(HI_MPI_VDEC_BindOutput(DetVLoss_VdecChn, s_VoSecondDev, VochnInfo[chn].VoChn));	
	//		VochnInfo[chn].IsBindVdec[s_VoSecondDev] = 0;				
		}
		CurMasterCap += VochnInfo[chn].VdecCap;
		VochnInfo[chn].IsHaveVdec = 1;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Choose Master Chip---Create Vdec: %d OK!,CurMasterCap: %d, VochnInfo[chn].VdecCap: %d, totalcap: %d \n",
			VochnInfo[chn].VdecChn, CurMasterCap, VochnInfo[chn].VdecCap, TOTALCAPPCHIP);	
		PRV_PlayAudio(s_VoDevCtrlDflt);
#else	
	
		PRV_VDEC_UnBindVpss(VochnInfo[chn].VdecChn, VochnInfo[chn].VoChn);
		//(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[chn].VdecChn, DHD0, VochnInfo[chn].VoChn));
		CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, VochnInfo[chn].VoChn, 1));
		//CHECK(HI_MPI_VO_ClearChnBuffer(DSD0, VochnInfo[chn].VoChn, 1));
		VochnInfo[chn].IsBindVdec[DHD0] = -1;
		//VochnInfo[chn].IsBindVdec[DSD0] = -1;
		//判断新连接的通道是否在当前预览画面中
		//在当前画面中，vdec绑定vpss
		if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(DHD0, VochnInfo[chn].VoChn))//只需对HD判断，s_VoSecondDev与HD一致，同下
		{	
			if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_CTRL
				&&(s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_REGION_SEL || s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_ZOOM_IN)
				&& VochnInfo[chn].VoChn == s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn)
			{
			
				PRV_VDEC_UnBindVpss(VochnInfo[chn].VdecChn, PRV_CTRL_VOCHN);
				CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, VochnInfo[chn].VoChn, 1));
				//CHECK(HI_MPI_VO_ClearChnBuffer(DSD0, VochnInfo[chn].VoChn, 1));
			}

			VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;	
			CHECK(HI_MPI_VO_DisableChn(DHD0, VochnInfo[chn].VoChn));			
			//CHECK(HI_MPI_VO_DisableChn(DSD0, VochnInfo[chn].VoChn));			
			PRV_BindVoChnInMaster(DHD0, VochnInfo[chn].VoChn, s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);			
			//PRV_BindVoChnInMaster(DSD0, VochnInfo[chn].VoChn, s_astVoDevStatDflt[DSD0].s32PreviewIndex, s_astVoDevStatDflt[DSD0].enPreviewMode);			
			CHECK(HI_MPI_VO_EnableChn(DHD0, VochnInfo[chn].VoChn));			
			//CHECK(HI_MPI_VO_EnableChn(DSD0, VochnInfo[chn].VoChn));			
			
		}
		else
		{
			VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;	
			PRV_VPSS_ResetWH(VochnInfo[chn].VoChn,VochnInfo[chn].VdecChn,VochnInfo[chn].VideoInfo.width,VochnInfo[chn].VideoInfo.height);
			PRV_VDEC_BindVpss(VochnInfo[chn].VdecChn,VochnInfo[chn].VoChn);
			VochnInfo[chn].IsBindVdec[DHD0] = 1;				
			//VochnInfo[chn].IsBindVdec[DSD0] = 1;				
		}
		CurMasterCap += VochnInfo[chn].VdecCap;
		VochnInfo[chn].IsHaveVdec = 1;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Choose Master Chip---Create Vdec: %d OK!,CurMasterCap: %d, VochnInfo[chn].VdecCap: %d, totalcap: %d\n",
			VochnInfo[chn].VdecChn, CurMasterCap, VochnInfo[chn].VdecCap, TOTALCAPPCHIP);					
		PRV_PlayAudio(s_VoDevCtrlDflt);
#endif
	}	
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Create Vdec: %d fail!!!\n", VochnInfo[chn].VoChn);
		CurCap -= VochnInfo[chn].VdecCap;
		
		PRV_InitVochnInfo(chn);
		//VochnInfo[chn].bIsWaitIFrame = 1;
		VochnInfo[chn].bIsWaitGetIFrame = 1;
		
		return HI_FAILURE;
	}
	return HI_SUCCESS;
}
/*************************************************
Function: PRV_MSG_MCC_CreateVdecRsp
Description: 从片创建解码通道返回后，根据是否创建成功，设置对应的状态
Called By: 
Input:  msg_req链接请求结构体，包含创建的解码通道号
Output: 返回系统定义的错误码
Return: 详细参看文档的错误码
Others: 
***********************************************************************/
STATIC HI_S32 PRV_MSG_MCC_CreateVdecRsp(const SN_MSG *msg_rsp)
{
	PRV_MccCreateVdecRsp *Rsp = (PRV_MccCreateVdecRsp *)msg_rsp->para;
	HI_S32 index = PRV_GetVoChnIndex(Rsp->VoChn);
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "MCC Create index: %d, Vdec: %d, Rsp->Result: %d\n", index, Rsp->VdecChn, Rsp->Result);
	if(index < 0)
		RET_FAILURE("------ERR: Invalid Index!");

	VochnInfo[index].MccCreateingVdec = 0;
#if defined(Hi3531)||defined(Hi3535)
	MccCreateingVdecCount--;
#endif
	if(Rsp->Result < 0)
	{
		CurCap -= VochnInfo[index].VdecCap;
		CurSlaveCap -= VochnInfo[index].VdecCap;
		CurSlaveChnCount--;
		
		PRV_InitVochnInfo(Rsp->VoChn);
		//VochnInfo[index].bIsWaitIFrame = 1;
		VochnInfo[index].bIsWaitGetIFrame = 1;

#if defined(SN9234H1)
		VochnInfo[index].IsBindVdec[HD] = 0;
#else
		VochnInfo[index].IsBindVdec[DHD0] = 0;
#endif
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ERR: MCC Create fail!  Rsp->Result: %d\n", Rsp->Result);
		return HI_FAILURE;
	}

	//退出回放、图片浏览时，从片会重新创建进入回放、图片浏览时销毁的解码通道
	//由于之前进入回放、图片浏览时，大部分状态未变，所以退出回放、图片浏览状态时，
	//从片创建解码通道，并返回此消息后，主片只需要开启接收数据标识	
	//从片ExitPB()接口返回此分支
	else if(1 == Rsp->Result)
	{
		VochnInfo[index].IsHaveVdec = 1;
		
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Exiting PB, Vdec: %d!\n", VochnInfo[index].VdecChn);
		RET_SUCCESS("");
	}
	//正常预览状态，且创建成功
	else//(0 == Rsp->Result)
	{
 		//回放状态下，从片还未创建解码通道，保存信息
		if(PRV_STAT_PB == s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat || PRV_STAT_PIC == s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Warning: In PB, Slave Create Vdec: %d later!\n", Rsp->VdecChn);
						
			VochnInfo[index].IsHaveVdec = 0;
			VochnInfo[index].VdecChn = Rsp->VdecChn;
			VochnInfo[index].SlaveId = Rsp->SlaveId;		
			VochnInfo[index].IsConnect = 1;
			
			RET_SUCCESS();			
		}
		else
		{
			int i = 0;
#if defined(SN9234H1)
			//先解绑定VO
			for(i = 0; i < PRV_VO_DEV_NUM; i++)
			{
				if(i == SPOT_VO_DEV || i == AD)
					continue;
				//if(VochnInfo[index].IsBindVdec[i] != -1)
				{
					(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[index].VdecChn, i, VochnInfo[index].VoChn));
					VochnInfo[index].IsBindVdec[i] = -1;
				}
			}
#else			
			//先解绑定VO
			for(i = 0; i < PRV_VO_MAX_DEV; i++)
			{
				if(i > DHD0)
					continue;
				//if(VochnInfo[index].IsBindVdec[i] != -1)
				{
					PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn,VochnInfo[index].VoChn);
					//(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[index].VdecChn, i, VochnInfo[index].VoChn));
					VochnInfo[index].IsBindVdec[i] = -1;
				}
			}
#endif			
			VochnInfo[index].SlaveId = Rsp->SlaveId;
			VochnInfo[index].VoChn = Rsp->VoChn;
			VochnInfo[index].IsHaveVdec = 1;
			VochnInfo[index].IsConnect = 1;
			//PRV_RefreshVoDevScreen(DHD0, DISP_DOUBLE_DISP, s_astVoDevStatDflt[DHD0].as32ChnOrder[s_astVoDevStatDflt[DHD0].enPreviewMode]);
		}			
		
		//对应输出通道是否在当前画面布局中
		if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[index].VoChn))
		{
			//通过绑定VI(0，0)，显示从片的数据
			if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_CTRL
				&&(s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_REGION_SEL || s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_ZOOM_IN)
				&& VochnInfo[index].VoChn == s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn)
			{
#if defined(SN9234H1)
				HI_MPI_VDEC_UnbindOutputChn(VochnInfo[index].VdecChn, s_VoDevCtrlDflt, PRV_CTRL_VOCHN);
#else			
				PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn,PRV_CTRL_VOCHN);
#endif				
				CHECK(HI_MPI_VO_ClearChnBuffer(s_VoDevCtrlDflt, PRV_CTRL_VOCHN, 1));
			}
			//else
			{
				VochnInfo[index].VdecChn = Rsp->VdecChn;
				CHECK(HI_MPI_VO_DisableChn(s_VoDevCtrlDflt, VochnInfo[index].VoChn));
#if defined(SN9234H1)
				PRV_BindVoChnInSlave(s_VoDevCtrlDflt, VochnInfo[index].VoChn, s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);				
#else							
				PRV_BindVoChnInSlave(s_VoDevCtrlDflt, VochnInfo[index].VoChn, s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);
#endif								
				CHECK(HI_MPI_VO_EnableChn(s_VoDevCtrlDflt, VochnInfo[index].VoChn));			
				//PRV_BindVoChnInSlave(s_VoSecondDev, VochnInfo[index].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
			}
			#if 0
			//隐藏视频丢失报警图标
			if(IsOSDAlarmOn[index - LOCALVEDIONUM])
			{
				CHECK_RET(OSD_Ctl(VochnInfo[index].VoChn, 0, OSD_ALARM_TYPE));						
				IsOSDAlarmOn[index - LOCALVEDIONUM] = 0;
			}
			#endif
			//if(!IsChoosePlayAudio)
				PRV_PlayAudio(s_VoDevCtrlDflt);
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "---------Slave-----chn: %d, CurSlaveCap: %d, VochnInfo[index].VdecCap: %d\n", VochnInfo[index].CurChnIndex, CurSlaveCap, VochnInfo[index].VdecCap);
			
		}
		else
		{
			VochnInfo[index].VdecChn = Rsp->VdecChn;
		}
#if defined(SN9234H1)
		//SPOT停止轮询时，SPOT的当前的输出通道画面变化(从无网络到有视频，或反之)，
		//而且此通道在有视频时在从片上(无网络视频时均在主片)
		if(DEV_SPOT_NUM > 0)
		{
			UINT8 SpotPollDelay;
			if (PARAM_OK != GetParameter(PRM_ID_PREVIEW_CFG, "ChanSwitchDelay", &SpotPollDelay, sizeof(SpotPollDelay), 1 + SD, SUPER_USER_ID, NULL))
			{			
				RET_FAILURE("ERR: get parameter ChanSwitchDelay fail!");
			}
			VO_CHN SpotVoChn = s_astVoDevStatDflt[SPOT_VO_DEV].as32ChnOrder[SingleScene][s_astVoDevStatDflt[SPOT_VO_DEV].s32SingleIndex];
			if(VochnInfo[index].VoChn == SpotVoChn)//Spot当前输出通道画面变化
			{				
				CHECK(HI_MPI_VO_ClearChnBuffer(SPOT_VO_DEV, SPOT_VO_CHAN, 1)); /* 清除VO缓存 */
				//if(VochnInfo[index].IsBindVdec[SPOT_VO_DEV] == 0)
				{
					(HI_MPI_VDEC_UnbindOutputChn(NoConfig_VdecChn, SPOT_VO_DEV, SPOT_VO_CHAN));
					VochnInfo[index].IsBindVdec[SPOT_VO_DEV] = -1;
				}
				//Spot当前属于不切换状态，需要开启pciv传输视频数据；
				//如果是开启状态，则pciv已经开启
				if(SpotPollDelay == UnSwitch )
					PRV_start_pciv(VochnInfo[index].VoChn);
				//PRV_RefreshSpotOsd(VoChn);
			}
		}
#endif		
	}
	RET_SUCCESS("");
}

/*************************************************
Function: PRV_MSG_MCC_DesVdecRsp
Description: 从片销毁解码通道返回后，根据是否销毁成功，设置对应的状态
Called By: 
Input:  msg_req链接请求结构体，包含销毁的解码通道号
Output: 返回系统定义的错误码
Return: 详细参看文档的错误码
Others: 
***********************************************************************/
STATIC HI_S32 PRV_MSG_MCC_DesVdecRsp(const SN_MSG *msg_rsp)
{
	PRV_MccDestroyVdecRsp *DestroyVdecRsp = (PRV_MccDestroyVdecRsp *)msg_rsp->para;
	HI_S32 index = 0;
	index = PRV_GetVoChnIndex(DestroyVdecRsp->VdecChn);
	if(index < 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "------ERR: Invalid Index: %d, VdecChn: %d!\n", index, DestroyVdecRsp->VdecChn);
		return HI_FAILURE;
	}
#if (IS_DECODER_DEVTYPE == 0)	

	SlaveState.SlaveIsDesingVdec[index] = 0;
	s_State_Info.bIsSlaveConfig = HI_FALSE;
#endif
	//从片没有创建对应的解码通道
	if(-1 == DestroyVdecRsp->Result)
	{
		VochnInfo[index].IsHaveVdec = 0;
		VochnInfo[index].IsConnect = 0;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ERR: MCC destroy Vdec: %d fail!\n", DestroyVdecRsp->VdecChn);
		return HI_FAILURE;
	}
#if defined(Hi3531)||defined(Hi3535)	
	//进入回放、图片浏览状态时，从片也需要销毁解码通道，并返回此消息，
	//主片只需要关闭接收数据标识,其他状态不变。
	//退出回放时要根据其他状态重新创建解码通道
	//从片EnterPB()接口中返回此状态
	else if(1 == DestroyVdecRsp->Result)
	{	
		VochnInfo[index].IsHaveVdec = 0;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Enter into PB, VdecChn: %d!\n", DestroyVdecRsp->VdecChn);
		RET_SUCCESS("");
	}	
#endif	
	//从片销毁解码通道成功
	else//(0 == DestroyVdecRsp->Result)
	{
		//处于回放状态下断开连接时，因为进入回放状态前已经销毁解码通道，在此只需重设置一些状态
		//此时不需要绑定VO操作，VO已被回放占用
#if defined(SN9234H1)
		if(PRV_STAT_PB == s_astVoDevStatDflt[HD].enPreviewStat || PRV_STAT_PIC == s_astVoDevStatDflt[HD].enPreviewStat)
#else		
		if(PRV_STAT_PB == s_astVoDevStatDflt[DHD0].enPreviewStat || PRV_STAT_PIC == s_astVoDevStatDflt[DHD0].enPreviewStat)
#endif		
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Warning: In PB, Vdec: %d allready destroy!\n", DestroyVdecRsp->VdecChn);
			CurCap -= VochnInfo[index].VdecCap;
			CurSlaveCap -= VochnInfo[index].VdecCap;	
			CurSlaveChnCount--;
			PRV_VoChnStateInit(VochnInfo[index].CurChnIndex);
			PRV_PtsInfoInit(VochnInfo[index].CurChnIndex);		
			PRV_InitVochnInfo(VochnInfo[index].VoChn);
			RET_SUCCESS();			
		}
		else
		{			
#if defined(SN9234H1)		
			CHECK(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, HD, VochnInfo[index].VoChn));					
			CHECK(HI_MPI_VO_ClearChnBuffer(HD, VochnInfo[index].VoChn, 1));
			//CHECK(HI_MPI_VO_DisableChn(i, VochnInfo[index].VoChn));
			VochnInfo[index].IsBindVdec[HD] = -1;
#else		
			//CHECK(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, DHD0, VochnInfo[index].VoChn));					
			CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, VochnInfo[index].VoChn, 1));
			//CHECK(HI_MPI_VO_DisableChn(i, VochnInfo[index].VoChn));
			VochnInfo[index].IsBindVdec[DHD0] = -1;
#endif
			BufferSet(VochnInfo[index].VoChn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);			
			BufferSet(VochnInfo[index].VoChn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);			
			//BufferClear(chn + PRV_VIDEOBUFFER);
			//BufferClear(chn + PRV_AUDIOBUFFER);
			CurCap -= VochnInfo[index].VdecCap;	
			CurSlaveCap -= VochnInfo[index].VdecCap;
			CurSlaveChnCount--;
			PRV_VoChnStateInit(VochnInfo[index].CurChnIndex);
			PRV_PtsInfoInit(VochnInfo[index].CurChnIndex);					
			PRV_InitVochnInfo(VochnInfo[index].VoChn);
			//if(ScmGetListCtlState() == 1)//点位控制下连接失败时，绑定"无网络视频"
			//	VochnInfo[index].VdecChn = DetVLoss_VdecChn;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "slave destroy vdec: %d OK, CurSlaveCap: %d\n", DestroyVdecRsp->VdecChn, CurSlaveCap);
			if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[index].VoChn))
			{
#if defined(SN9234H1)
				if(s_astVoDevStatDflt[HD].enPreviewStat == PRV_STAT_CTRL)
				{
					if(VochnInfo[index].VoChn == s_astVoDevStatDflt[HD].s32CtrlChn)
					{
						CHECK(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, HD, PRV_CTRL_VOCHN));					
						CHECK(HI_MPI_VO_ClearChnBuffer(HD, PRV_CTRL_VOCHN, 1));
					}
				}
				//必须disable Vo否则如果该"无网络"图片显示异常，且如果该通道下次在主片显示，也是异常
				CHECK(HI_MPI_VO_DisableChn(HD, VochnInfo[index].VoChn));			
			//	CHECK(HI_MPI_VO_DisableChn(s_VoSecondDev, VochnInfo[index].VoChn));
				PRV_VLossVdecBindVoChn(HD, VochnInfo[index].VoChn, s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);
			//	PRV_VLossVdecBindVoChn(s_VoSecondDev, VochnInfo[index].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
				CHECK(HI_MPI_VO_EnableChn(HD, VochnInfo[index].VoChn));
			//	CHECK(HI_MPI_VO_EnableChn(s_VoSecondDev, VochnInfo[index].VoChn));
#else			
				if(s_astVoDevStatDflt[DHD0].enPreviewStat == PRV_STAT_CTRL)
				{
					if(VochnInfo[index].VoChn == s_astVoDevStatDflt[DHD0].s32CtrlChn)
					{
						//CHECK(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, DHD0, PRV_CTRL_VOCHN));					
						CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, PRV_CTRL_VOCHN, 1));
					}
				}
				//必须disable Vo否则如果该"无网络"图片显示异常，且如果该通道下次在主片显示，也是异常
				CHECK(HI_MPI_VO_DisableChn(DHD0, VochnInfo[index].VoChn));			
			//	CHECK(HI_MPI_VO_DisableChn(s_VoSecondDev, VochnInfo[index].VoChn));
				PRV_VLossVdecBindVoChn(DHD0, VochnInfo[index].VoChn, s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);
			//	PRV_VLossVdecBindVoChn(s_VoSecondDev, VochnInfo[index].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
				CHECK(HI_MPI_VO_EnableChn(DHD0, VochnInfo[index].VoChn));
			//	CHECK(HI_MPI_VO_EnableChn(s_VoSecondDev, VochnInfo[index].VoChn));
#endif
				//sem_post(&sem_SendNoVideoPic);
				
				//if(!IsChoosePlayAudio)
					PRV_PlayAudio(s_VoDevCtrlDflt);
			}			
			else
			{
#if defined(SN9234H1)
				CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, HD, VochnInfo[index].VoChn)); 	
				VochnInfo[index].IsBindVdec[HD] = 0;				
#else			
				PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VochnInfo[index].VoChn);
				//CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, DHD0, VochnInfo[index].VoChn)); 	
				VochnInfo[index].IsBindVdec[DHD0] = 0;	
#endif							
	//			CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, s_VoSecondDev, VochnInfo[index].VoChn)); 	
	//			VochnInfo[index].IsBindVdec[s_VoSecondDev] = 0;				
			}

#if defined(SN9234H1)
			if(DEV_SPOT_NUM > 0)
			{
				UINT8 SpotPollDelay;
				if (PARAM_OK != GetParameter(PRM_ID_PREVIEW_CFG, "ChanSwitchDelay", &SpotPollDelay, sizeof(SpotPollDelay), 1 + SD, SUPER_USER_ID, NULL))
				{
					RET_FAILURE("ERR: get parameter ChanSwitchDelay fail!");
				}
				VO_CHN SpotVoChn = s_astVoDevStatDflt[SPOT_VO_DEV].as32ChnOrder[SingleScene][s_astVoDevStatDflt[SPOT_VO_DEV].s32SingleIndex];
				if(VochnInfo[index].VoChn == SpotVoChn)
				{
					CHECK(HI_MPI_VO_ClearChnBuffer(SPOT_VO_DEV, SPOT_VO_CHAN, 1)); //清除VO缓存 
					if(SpotPollDelay == UnSwitch )
						PRV_HostStopPciv(CurrertPciv, MSG_ID_PRV_MCC_SPOT_PREVIEW_STOP_REQ);
					PRV_PrevInitSpotVo(VochnInfo[index].VoChn);
				}
			}
#endif
		}		
	}
	RET_SUCCESS("");
}
/*************************************************
Function: PRV_MSG_OverLinkReq
Description: 断开连接
Called By: 
Input:  msg_req链接请求结构体，包含断开连接的通道号
Output: 返回系统定义的错误码
Return: 详细参看文档的错误码
Others: 
***********************************************************************/
STATIC HI_S32 PRV_MSG_OverLinkReq(const SN_MSG *msg_req)
{
	HI_S32 i = 0, chn = 0, index = 0, s32Ret = 0;
	if(msg_req->msgId == MSG_ID_NTRANS_DELUSER_RSP)
	{					
		deluser_used *DelUser_Rsp = (deluser_used*)msg_req->para;
		chn = DelUser_Rsp->channel;
		index = chn + LOCALVEDIONUM;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------------Receive MSG: MSG_ID_NTRANS_DELUSER_RSP---%d\n", DelUser_Rsp->channel);
	}
	else if(msg_req->msgId == MSG_ID_NTRANS_ONCEOVER_IND)
	{				
		NTRANS_ENDOFCOM_RSP *DisconNetReq = (NTRANS_ENDOFCOM_RSP *)msg_req->para;
		chn = DisconNetReq->channel;
		index = chn + LOCALVEDIONUM;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------------Receive MSG: MSG_ID_NTRANS_ONCEOVER_IND---%d\n", DisconNetReq->channel);
	}	
	if(chn < 0 || chn >= MAX_IPC_CHNNUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Invalid Channel: %d, line: %d\n", chn, __LINE__);
		return HI_FAILURE;		
	}
	if(VochnInfo[index].VdecChn == DetVLoss_VdecChn
		|| VochnInfo[index].VdecChn == NoConfig_VdecChn)
	{
		RET_FAILURE("Error: Vdec for Picture---30 or 31");
	}
	
	//当前通道已经正在销毁通道
	if(SlaveState.SlaveIsDesingVdec[index] == 1)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave Is Destroying Vdec: %d\n", VochnInfo[index].VdecChn);
		return HI_FAILURE;
	}
		
	//断开连接的通道是在从片创建的解码通道，发送消息通知从片销毁解码通道
	if(VochnInfo[index].SlaveId > PRV_MASTER && VochnInfo[index].VdecChn >= 0)
	{
		PRV_GetVoChnIndex(VochnInfo[index].VoChn);
		PRV_MccDestroyVdecReq DestroyVdecReq;
		DestroyVdecReq.VdecChn = VochnInfo[index].VdecChn;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------Send Msg to Slave, Destroy Vdec: %d\n", DestroyVdecReq.VdecChn);

		SlaveState.SlaveIsDesingVdec[index] = 1;
		s_State_Info.bIsSlaveConfig = HI_TRUE;
		SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_DESVDEC_REQ, &DestroyVdecReq, sizeof(PRV_MccDestroyVdecReq));
		for(i = 0; i < PRV_CurIndex; i++)
		{
			if(PRV_OldVdec[i] == VochnInfo[index].VdecChn)
			{
				NTRANS_FreeMediaData(PRV_OldVideoData[i]);
				PRV_OldVideoData[i] = NULL;
			}
		}
		
		return SN_SLAVE_MSG;
	}
	//主片创建的解码通道
	else
	{		
		//当前处于回放状态，因为进入回放前已经销毁了解码通道，在此只需要重设一些状态
		if(PRV_STAT_PB == s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat || PRV_STAT_PIC == s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Warning: In PB!!!Already destroy vdec: %d\n", VochnInfo[chn].VdecChn);
			CurCap -= VochnInfo[index].VdecCap;
			CurMasterCap -= VochnInfo[index].VdecCap; 				
			PRV_VoChnStateInit(chn);
			PRV_PtsInfoInit(VochnInfo[index].CurChnIndex);		
			PRV_InitVochnInfo(index);
			RET_SUCCESS("");			
		}
		//正常预览状态
		else
		{	
			s32Ret = PRV_WaitDestroyVdecChn(VochnInfo[index].VdecChn);
			if(s32Ret != HI_SUCCESS)//销毁解码通道失败，不接数据
			{
				VochnInfo[index].IsHaveVdec = 0;	
				VochnInfo[index].IsConnect = 0;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Destroy Vdec: %d fail!", VochnInfo[index].VdecChn);
				return HI_FAILURE;
			}

			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Master destroy vdec: %d Ok!!\n", VochnInfo[index].VdecChn);
			
			//CHECK(HI_MPI_VO_DisableChn(DHD0, VochnInfo[chn].VoChn));			
			//CHECK(HI_MPI_VO_DisableChn(s_VoSecondDev, VochnInfo[chn].VoChn));
			BufferSet(VochnInfo[index].VoChn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);						
			BufferSet(VochnInfo[index].VoChn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
			CurCap -= VochnInfo[index].VdecCap;
			CurMasterCap -= VochnInfo[index].VdecCap;			
			PRV_VoChnStateInit(chn);
			//CurIPCCount--;			
			PRV_PtsInfoInit(VochnInfo[index].CurChnIndex);		
			PRV_InitVochnInfo(index);
			//VochnInfo[chn].CurChnIndex = -1;
#if 1
			//处于当前画面布局中，绑定VO与无视频信号解码通道DetVLoss_VdecChn(30)
			if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[index].VoChn))
			{
#if defined(SN9234H1)
				CHECK(HI_MPI_VO_DisableChn(HD, VochnInfo[chn].VoChn));			
			//	CHECK(HI_MPI_VO_DisableChn(s_VoSecondDev, VochnInfo[chn].VoChn));
				PRV_VLossVdecBindVoChn(HD, VochnInfo[index].VoChn,  s_astVoDevStatDflt[HD].s32PreviewIndex, s_astVoDevStatDflt[HD].enPreviewMode);
			//	PRV_VLossVdecBindVoChn(s_VoSecondDev, VochnInfo[chn].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
				CHECK(HI_MPI_VO_EnableChn(HD, VochnInfo[chn].VoChn));			
			//	CHECK(HI_MPI_VO_EnableChn(s_VoSecondDev, VochnInfo[chn].VoChn));
#else			
			
				CHECK(HI_MPI_VO_DisableChn(DHD0, VochnInfo[chn].VoChn));			
			//	CHECK(HI_MPI_VO_DisableChn(s_VoSecondDev, VochnInfo[chn].VoChn));
				PRV_VLossVdecBindVoChn(DHD0, VochnInfo[index].VoChn,  s_astVoDevStatDflt[DHD0].s32PreviewIndex, s_astVoDevStatDflt[DHD0].enPreviewMode);
			//	PRV_VLossVdecBindVoChn(s_VoSecondDev, VochnInfo[chn].VoChn, s_astVoDevStatDflt[s_VoSecondDev].s32PreviewIndex, s_astVoDevStatDflt[s_VoSecondDev].enPreviewMode);
				CHECK(HI_MPI_VO_EnableChn(DHD0, VochnInfo[chn].VoChn));			
			//	CHECK(HI_MPI_VO_EnableChn(s_VoSecondDev, VochnInfo[chn].VoChn));
#endif
			//	sem_post(&sem_SendNoVideoPic);
				
			//	if(!IsChoosePlayAudio)
					PRV_PlayAudio(s_VoDevCtrlDflt);
					
			}
			else
			{
#if defined(SN9234H1)
				CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, HD, VochnInfo[index].VoChn)); 	
				VochnInfo[index].IsBindVdec[HD] = 0;
#else			
				PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VochnInfo[index].VoChn);
				//CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, DHD0, VochnInfo[index].VoChn)); 	
				VochnInfo[index].IsBindVdec[DHD0] = 0;
#endif								
		//		CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, s_VoSecondDev, VochnInfo[chn].VoChn)); 	
		//		VochnInfo[chn].IsBindVdec[s_VoSecondDev] = 0;				
			}
#endif						

#if defined(SN9234H1)
			if(DEV_SPOT_NUM > 0)
			{
				//SPOT口判断
				if(VochnInfo[index].VoChn == s_astVoDevStatDflt[SPOT_VO_DEV].as32ChnOrder[SingleScene][s_astVoDevStatDflt[SPOT_VO_DEV].s32SingleIndex])
				{			
					CHECK(HI_MPI_VO_ClearChnBuffer(SPOT_VO_DEV, SPOT_VO_CHAN, 1)); 
					//if(VochnInfo[index].IsBindVdec[SD] != 0)
					{
						CHECK(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, SPOT_VO_DEV, SPOT_VO_CHAN));				
						VochnInfo[index].IsBindVdec[SD] = 0;	
					}
				}
			}
#endif
			//PRV_RefreshVoDevScreen(DHD0, DISP_DOUBLE_DISP, s_astVoDevStatDflt[DHD0].as32ChnOrder[s_astVoDevStatDflt[DHD0].enPreviewMode]);
		}
	}

	RET_SUCCESS("");
}
#if 0
/*************************************************
Function: PRV_MSG_ReCreateVdecIND
Description: 重新创建解码通道。
			接收到的I帧分辨率改变时但不断开连接的情况下，需要主动重新创建解码通道
Called By: 
Input:  msg_req链接请求结构体，包含重新创建的解码通道号
Output: 返回系统定义的错误码
Return: 详细参看文档的错误码
Others: 
***********************************************************************/

HI_S32 PRV_MSG_ReCreateVdecIND(const SN_MSG *msg_req)
{
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------------Receive MSG: MSG_ID_PRV_RECREATEVDEC_IND\n");
	PRV_ReCreateVdecIND *ReCreateReq = (PRV_ReCreateVdecIND *)msg_req->para;
	HI_S32 index = PRV_GetVoChnIndex(ReCreateReq->VoChn);
	if(index < 0)
		RET_FAILURE("ERR: Invalid Channel");
	
	HI_S32 PreChnIndex = -1, IsReCreateOK = 1, VdecChn = -1, tmpCap = 0;
//	HI_S32 tmpCap = PRV_CompareToCif(ReCreateReq->height, ReCreateReq->width);
	TRACE(SCI_TRACE_HIGH, MOD_PRV, "New Height: %dNew Width: %d\n", ReCreateReq->height, ReCreateReq->width);			

	tmpCap = (ReCreateReq->height * ReCreateReq->width);

#if defined (SN_SLAVE_ON)	
	if(VochnInfo[index].SlaveId > 0)
	{
		PRV_MccReCreateVdecReq SlaveReCreateReq;
		SlaveReCreateReq.SlaveId = VochnInfo[index].SlaveId;
		SlaveReCreateReq.VoChn = ReCreateReq->VoChn;
		SlaveReCreateReq.VdecChn = ReCreateReq->VdecChn;
		SlaveReCreateReq.height = ReCreateReq->height;
		SlaveReCreateReq.width = ReCreateReq->width;
		SlaveReCreateReq.VdecCap = tmpCap;
		SN_SendMccMessageEx(SlaveReCreateReq.SlaveId, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_RECREATEVDEC_REQ, &SlaveReCreateReq, sizeof(PRV_MccReCreateVdecReq)); 					
	}
	else
#endif
	{
		if(HI_SUCCESS == PRV_WaitDestroyVdecChn(ReCreateReq->VdecChn))//先销毁解码通道
		{
			BufferSet(index + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);						
			BufferSet(index + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
			//BufferClear(chn + PRV_VIDEOBUFFER);					
			//BufferClear(chn + PRV_AUDIOBUFFER);					
			CurCap -= VochnInfo[index].VdecCap;
			CurMasterCap -= VochnInfo[index].VdecCap;			
			
			VochnInfo[index].IsHaveVdec = 0;	
			VochnInfo[index].VdecChn = DetVLoss_VdecChn;
#if (DEV_TYPE == DEV_SN_9234_H4_1 || DEV_TYPE == DEV_SN_9234_H_V1_6 || DEV_TYPE == DEV_SN_9234_H_V1_8)			
			PtsInfo[VochnInfo[index].CurChnIndex].NewFramePts = 0;
#endif
			PreChnIndex = VochnInfo[index].CurChnIndex;
			VochnInfo[index].CurChnIndex = -1;
			
		}
		else
		{
			IsReCreateOK = 0;
			VochnInfo[index].IsHaveVdec = 0;//销毁失败，不接数据
			goto BindVdec;

		}
		//性能变大，需要进行性能检测
		if(tmpCap > VochnInfo[index].VdecCap)
		{
			//超出性能
			if((CurCap - VochnInfo[index].VdecCap + tmpCap) > (TOTALCAPPCHIP * DEV_CHIP_NUM - LOCALVEDIONUM * D1) //分辨率变化导致总性能超出
				|| (VochnInfo[index].SlaveId == 0 && (CurMasterCap - VochnInfo[index].VdecCap + tmpCap) > (TOTALCAPPCHIP - LOCALVEDIONUM * D1)))//分辨率变化导致单片性能超出
			{	
				//请求断开连接
				deluser_used DelUserReq;
				DelUserReq.channel = ReCreateReq->VoChn;
				SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ, &DelUserReq, sizeof(deluser_used));			
				TRACE(SCI_TRACE_HIGH, MOD_PRV,"----------The current capbility is beyond the TOTALCAP, discard the newest channel---%d\n",  index);			
				IsReCreateOK = 0;
				goto BindVdec;
				//PRV_RefreshVoDevScreen(DHD0, DISP_DOUBLE_DISP, s_astVoDevStatDflt[DHD0].as32ChnOrder[s_astVoDevStatDflt[DHD0].enPreviewMode]);
				//return HI_FAILURE;
			}
		}
		//分辨率合法性判断
		if(ReCreateReq->height <= 0 || ReCreateReq->width <=0
			|| ReCreateReq->height > 4096 || ReCreateReq->width > 4096)
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "InValid Height or Width: %d, %d\n", ReCreateReq->height, ReCreateReq->width);			
			IsReCreateOK = 0;
			goto BindVdec;
			//return HI_FAILURE;			
		}
		
		if(ReCreateReq->height <= 16)
		{
			ReCreateReq->height = 64;
		}
		if(ReCreateReq->width <=16)
		{
			ReCreateReq->width = 64;
		}
		
		if(HI_SUCCESS == PRV_CreateVdecChn(VochnInfo[index].VideoInfo.vdoType, ReCreateReq->height, ReCreateReq->width, ReCreateReq->VdecChn))
		{
			VochnInfo[index].VideoInfo.height = ReCreateReq->height;
			VochnInfo[index].VideoInfo.width = ReCreateReq->width;
			VochnInfo[index].VdecCap = ReCreateReq->height * ReCreateReq->width;			
			VochnInfo[index].CurChnIndex = PreChnIndex;
			VochnInfo[index].VdecChn = ReCreateReq->VdecChn;
			VochnInfo[index].IsHaveVdec = 1;	
			
			CurCap += VochnInfo[index].VdecCap;
			CurMasterCap += VochnInfo[index].VdecCap;
		}
		else
		{
			IsReCreateOK = 0;//重建解码通道失败标识
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ReCreate Vdec: fail!\n", ReCreateReq->VoChn);

		}
BindVdec:	
		//if(HI_SUCCESS == PRV_VoChnIsInCurLayOut(s_VoDevCtrlDflt, VochnInfo[index].VoChn))
		{	
			if(!IsReCreateOK)
				VdecChn = DetVLoss_VdecChn;
			else
				VdecChn = VochnInfo[index].VdecChn;
			if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_CTRL
				&&(s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_REGION_SEL || s_astVoDevStatDflt[s_VoDevCtrlDflt].enCtrlFlag == PRV_CTRL_ZOOM_IN))
			{
				CHECK(HI_MPI_VDEC_BindOutput(VdecChn, s_VoDevCtrlDflt, PRV_CTRL_VOCHN)); 
			}
			else
			{
#if defined(SN9234H1)
				CHECK_RET(HI_MPI_VDEC_BindOutput(VdecChn, HD, VochnInfo[index].VoChn));
				VochnInfo[index].IsBindVdec[HD] = IsReCreateOK ? 1 : 0;	
#else			
				CHECK_RET(HI_MPI_VDEC_BindOutput(VdecChn, DHD0, VochnInfo[index].VoChn));
				VochnInfo[index].IsBindVdec[DHD0] = IsReCreateOK ? 1 : 0;
#endif					
			//	CHECK_RET(HI_MPI_VDEC_BindOutput(VdecChn, s_VoSecondDev, VochnInfo[index].VoChn));
			//	VochnInfo[index].IsBindVdec[s_VoSecondDev] = IsReCreateOK ? 1 : 0;

			}
			
			if(!IsChoosePlayAudio)
				PRV_PlayAudio(s_VoDevCtrlDflt);
		}	

		//PRV_RefreshVoDevScreen(DHD0, DISP_DOUBLE_DISP, s_astVoDevStatDflt[DHD0].as32ChnOrder[s_astVoDevStatDflt[DHD0].enPreviewMode]);
	}
	return HI_SUCCESS;
}
#endif


/*************************************************
Function: PRV_MSG_ReCreateAdecIND
Description: 重新创建音频解码通道。
Called By: 
Input:  msg_req链接请求结构体，包含重新创建的音频通道号
Output: 返回系统定义的错误码
Return: 详细参看文档的错误码
Others: 
***********************************************************************/
HI_S32 PRV_MSG_ReCreateAdecIND(const SN_MSG * msg_req)
{
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------------Receive MSG: MSG_ID_PRV_RECREATEADEC_IND\n");
	PRV_ReCreateAdecIND *ReCreateAdec = (PRV_ReCreateAdecIND *)msg_req->para;
	int chn = ReCreateAdec->chn;
	IsCreatingAdec = 1;
	CHECK_RET(PRV_StopAdec());
	IsCreateAdec = 0;

	if(ReCreateAdec->NewPtNumPerFrm == 160 || ReCreateAdec->NewPtNumPerFrm == 320)
	{
		VochnInfo[chn].AudioInfo.PtNumPerFrm = ReCreateAdec->NewPtNumPerFrm;
	}
	else
	{
		VochnInfo[chn].AudioInfo.PtNumPerFrm = 320;
	}
	
	CHECK_RET(PRV_StartAdecAo(VochnInfo[chn]));
	IsCreateAdec = 1;
#if defined(Hi3531)||defined(Hi3535)	
	IsAdecBindAo = 1;
#endif	
	Achn = chn;
	IsCreatingAdec = 0;
	CurPlayAudioInfo = VochnInfo[chn].AudioInfo;
	RET_SUCCESS("");
}
/************************************************************************/
/*                       END OF PRV_MSG_???()
                                               */
/************************************************************************/
static int get_chn_param_init(void)
{
	HI_S32 s32Ret = -1, i = 0, j = 0;
	UINT8	index = 255;
	PRM_PREVIEW_CFG_EX preview_info;
	PRM_PREVIEW_CFG_EX_EXTEND preview_info_exn;
	PRM_PREVIEW_ADV_CFG preview_Adv_info;
	PRM_Decode	stDecode;
	if (PARAM_OK != GetParameter(PRM_ID_PREVIEW_CFG_EX, NULL, &preview_info, sizeof(preview_info), 0, SUPER_USER_ID, NULL))
	{
		RET_FAILURE("get parameter PRM_PREVIEW_CFG_EX fail!");
	}
	if (PARAM_OK != GetParameter(PRM_ID_PREVIEW_CFG_EX_EXTEND, NULL, &preview_info_exn, sizeof(preview_info_exn), 0, SUPER_USER_ID, NULL))
	{
		RET_FAILURE("get parameter PRM_PREVIEW_CFG_EX_EXTN fail!");
	}

	if (PARAM_OK != GetParameter(PRM_ID_DECODEMODE_CFG, NULL, &stDecode, sizeof(stDecode), 0, SUPER_USER_ID, NULL))
	{
		RET_FAILURE("get parameter PRM_ID_DECODEMODE_CFG fail!");
	}
	
	PRV_CurDecodeMode = stDecode.DecodeMode;
	g_PrvType = stDecode.reserve0;
	for(i = 0; i < DEV_CHANNEL_NUM; i++)
		VochnInfo[i].PrvType = stDecode.reserve0;
	//预览配置
	for(i = 0; i < PRV_VO_MAX_DEV; i++)
	{//对所有的通道先进性一次初始化
		for(j = 0; j < SEVENINDEX; j++)
		{
			if(j < 7)
			{
				s_astVoDevStatDflt[i].AudioChn[j] = -1;
			}
			s_astVoDevStatDflt[i].as32ChnOrder[SingleScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnOrder[TwoScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnOrder[ThreeScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnOrder[FourScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnOrder[LinkFourScene][j] = -1;
				s_astVoDevStatDflt[i].as32ChnOrder[FiveScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnOrder[SixScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnOrder[SevenScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnOrder[EightScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnOrder[NineScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnOrder[LinkNineScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnOrder[SixteenScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnpollOrder[SingleScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnpollOrder[TwoScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnpollOrder[ThreeScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnpollOrder[FourScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnpollOrder[FiveScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnpollOrder[SixScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnpollOrder[SevenScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnpollOrder[EightScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnpollOrder[NineScene][j] = -1;
			s_astVoDevStatDflt[i].as32ChnpollOrder[SixteenScene][j] = -1;
		}
		/*配置预览通道顺序*/
		s_astVoDevStatDflt[i].as32ChnOrder[SingleScene][0] = UCHAR2INIT32(preview_info.SingleOrder);
		for(j = 0; j < CHANNEL_NUM; j++)
		{
            if(j < 3)
			{
				s_astVoDevStatDflt[i].as32ChnOrder[ThreeScene][j] = UCHAR2INIT32(preview_info_exn.ThreeOrder[j]);
			}
			if(j < 5)
			{
				s_astVoDevStatDflt[i].as32ChnOrder[FiveScene][j] = UCHAR2INIT32(preview_info_exn.FiveOrder[j]);
			}
			if(j < 7)
			{
				s_astVoDevStatDflt[i].as32ChnOrder[SevenScene][j] = UCHAR2INIT32(preview_info_exn.SevenOrder[j]);
			}
			if(j < 4)
			{
				s_astVoDevStatDflt[i].as32ChnOrder[FourScene][j] = UCHAR2INIT32(preview_info.FourOrder[j]);
				s_astVoDevStatDflt[i].as32ChnOrder[NineScene][j] = UCHAR2INIT32(preview_info.NineOrder[j]);
				s_astVoDevStatDflt[i].as32ChnOrder[SixteenScene][j] = UCHAR2INIT32(preview_info.SixteenOrder[j]);
			}
			else if(j < 9)
			{
				s_astVoDevStatDflt[i].as32ChnOrder[NineScene][j] = UCHAR2INIT32(preview_info.NineOrder[j]);
				s_astVoDevStatDflt[i].as32ChnOrder[SixteenScene][j] = UCHAR2INIT32(preview_info.SixteenOrder[j]);
			}
			else
			{
				s_astVoDevStatDflt[i].as32ChnOrder[SixteenScene][j] = UCHAR2INIT32(preview_info.SixteenOrder[j]);

			}
		}

		for(j = 0; j < 4; j++)
		{
			index = preview_info.AudioChn[j];
			//保存音频通道号
			if(j == 0 && index == 0)
				s_astVoDevStatDflt[i].AudioChn[j] = UCHAR2INIT32(preview_info.SingleOrder);
			else if(j == 1 && index < 4)
				s_astVoDevStatDflt[i].AudioChn[j] = UCHAR2INIT32(preview_info.FourOrder[index]);
			else if(j == 2 && index < 9)
				s_astVoDevStatDflt[i].AudioChn[j] = UCHAR2INIT32(preview_info.NineOrder[index]);
			else if(j == 3 && index < 16)
				s_astVoDevStatDflt[i].AudioChn[j] = UCHAR2INIT32(preview_info.SixteenOrder[index]);
		}
		for(j = 0; j < 3; j++)
		{
			index = preview_info_exn.AudioChn[j];
			//保存音频通道号
			if(j == 0 && index < 3)
				s_astVoDevStatDflt[i].AudioChn[4+j] = UCHAR2INIT32(preview_info_exn.ThreeOrder[index]);
			else if(j == 1 && index < 5)
				s_astVoDevStatDflt[i].AudioChn[4+j] = UCHAR2INIT32(preview_info_exn.FiveOrder[index]);
			else if(j == 2 && index < 7)
				s_astVoDevStatDflt[i].AudioChn[4+j] = UCHAR2INIT32(preview_info_exn.SevenOrder[index]);
			
		}

		/*设置设备的预览模式*/
		s_astVoDevStatDflt[i].enPreviewMode = preview_info.PreviewMode;
		if(s_astVoDevStatDflt[i].enPreviewMode == SingleScene)
		{
			s_astVoDevStatDflt[i].bIsSingle = HI_TRUE;
			s_astVoDevStatDflt[i].s32SingleIndex = 0;			
		}
	}
	if (preview_info.reserve[1] > IntelligentMode){
		preview_info.reserve[1] = IntelligentMode;
	}
	OutPutMode = preview_info.reserve[1];
	if (PARAM_OK != GetParameter(PRM_ID_PREVIEW_ADV_CFG,NULL,&preview_Adv_info,sizeof(preview_Adv_info),1,SUPER_USER_ID,NULL))
	{
		RET_FAILURE("get parameter PRM_PREVIEW_CFG fail!");
	}
	/*配置报警触发端口*/
	switch(preview_Adv_info.AlarmHandlePort)/*报警触发端口对应0-DHD0/VGA 1-CVBS1, 2-CVBS2*/
	{
#if defined(SN9234H1)	
		case 0:
			s_VoDevAlarmDflt = HD;
			break;
		case 1:
			s_VoDevAlarmDflt = s_VoSecondDev;
			break;
		case 2:
			s_VoDevAlarmDflt = SD;
			break;
#else
		case 0:
			s_VoDevAlarmDflt = DHD0;
			break;
		case 1:
			s_VoDevAlarmDflt = s_VoSecondDev;
			break;
		case 2:
			s_VoDevAlarmDflt = DSD0;
			break;
#endif			
		default:
			RET_FAILURE("param->preview_info.AlarmHandlePort out off range: 0~2");
	}

	/*配置预览音频*/
	PRV_Set_AudioPreview_Enable(preview_Adv_info.AudioPreview);
	IsAudioOpen = preview_Adv_info.AudioPreview[0];
	
#if !defined(Hi3535)
	PRM_DISPLAY_CFG_CHAN disp_info;
	for(i=0;i<g_Max_Vo_Num;i++)
	{
		//图像配置
		s32Ret= GetParameter(PRM_ID_DISPLAY_CFG,NULL,&disp_info,sizeof(disp_info),i+1,SUPER_USER_ID,NULL);
		if(s32Ret!= PARAM_OK)
		{
			RET_FAILURE("get Preview_VideoParam param error!");
		}	
		s32Ret = Preview_SetVideoParam(i,&disp_info);
		if(s32Ret != HI_SUCCESS)
		{
			RET_FAILURE("Preview_initVideoParam error!");
		}
	}
#endif
	GetParameter (PRM_ID_LINKAGE_GROUP_CFG, NULL, g_PrmLinkAge_Cfg,sizeof (g_PrmLinkAge_Cfg), 0, SUPER_USER_ID, NULL);
	return s32Ret;
}


static void exit_mpp_sys(void)
{
	int i = 0;
	TRACE(SCI_TRACE_NORMAL, MOD_VAM, "exit_mpp_sys\n");
	s_State_Info.bIsRe_Init = 0;
#if defined(SN9234H1)
	int chan = 0;
	Prv_OSD_Close_fb(HD);
	Prv_OSD_Close_fb(AD);
	Prv_OSD_Close_fb(SD);
	if(DEV_SPOT_NUM > 0)
	{
		chan = s_astVoDevStatDflt[SPOT_VO_DEV].as32ChnOrder[SingleScene][s_astVoDevStatDflt[SPOT_VO_DEV].s32SingleIndex];
		if(chan > PRV_CHAN_NUM)
		{
			PRV_HostStopPciv(CurrertPciv, MSG_ID_PRV_MCC_SPOT_PREVIEW_STOP_REQ);
		}	
	}
#else	
	Prv_OSD_Close_fb(DHD0);
	//Prv_OSD_Close_fb(DSD0);
#endif
	//PRV_DestroyAllVdecChn();
	//PRV_DisableAllVoChn(DHD0);	
	//CHECK(HI_MPI_VO_DisableVideoLayer(DHD0));
	//PRV_DisableVoDev(DHD0);
	//PRV_DisableAllVoChn(s_VoSecondDev);		
	//CHECK(HI_MPI_VO_DisableVideoLayer(s_VoSecondDev));
	//PRV_DisableVoDev(s_VoSecondDev);

	PRV_DisableAudioPreview();
	PRV_SysExit();
	for(i = LOCALVEDIONUM; i < DEV_CHANNEL_NUM; i++)
		VochnInfo[i].IsHaveVdec = 0;
	s_bIsSysInit = HI_FALSE;
	PRINT_RED("MPP System (MOD_PRV) exit!\n");
}


/************************************************************************/
/* PRV模块OSD时间设置线程函数。
                                                                     */
/************************************************************************/
char *weekday[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
char *weekday_en[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

sem_t OSD_time_Sem;
unsigned int osdtime;
unsigned int getosdtime()
{
	return osdtime;
}
STATIC void *SendNvrNoVideoPicThread(void *parg)
{
#if defined(SN9234H1)
	char PicBuff[VLOSSPICBUFFSIZE] = {0};
	int dataLen = 0, s32Ret = 0;	

	dataLen = PRV_ReadNvrNoVideoPic(NVR_NOVIDEO_FILE, PicBuff);	
	if(dataLen <= 0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "--------Read NoVideoFile: %s---fail!\n", NVR_NOVIDEO_FILE);
		return (void*)(-1);
	}

	VDEC_STREAM_S stVstream; 
	stVstream.pu8Addr = PicBuff;
	stVstream.u32Len = dataLen;	
	stVstream.u64PTS = 0;

	char PicBuff_1[VLOSSPICBUFFSIZE] = {0};
	int dataLen_1 = 0;	

	dataLen_1 = PRV_ReadNvrNoVideoPic(DVS_NOCONFIG_FILE, PicBuff_1);	
	if(dataLen_1 <= 0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "--------Read NoVideoFile: %s---fail!\n", NVR_NOVIDEO_FILE);
		return (void*)(-1);
	}

	VDEC_STREAM_S stVstream_1; 
	stVstream_1.pu8Addr = PicBuff_1;
	stVstream_1.u32Len = dataLen_1;	
	stVstream_1.u64PTS = 0;	
	
#else

	unsigned char PicBuff1[VLOSSPICBUFFSIZE] = {0}, PicBuff2[VLOSSPICBUFFSIZE] = {0};
	HI_U32 dataLen1 = 0, dataLen2 = 0;	
	int s32Ret = 0;	

	dataLen1 = PRV_ReadNvrNoVideoPic(NVR_NOVIDEO_FILE_1, PicBuff1);	
	if(dataLen1 <= 0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "--------Read NoVideoFile: %s---fail!\n", NVR_NOVIDEO_FILE_1);
		return (void*)(-1);
	}

	VDEC_STREAM_S stVstream1; 
	stVstream1.pu8Addr = PicBuff1;
	stVstream1.u32Len = dataLen1;	
	stVstream1.u64PTS = 0;
	
	VDEC_STREAM_S stVstream2;
	dataLen2 = PRV_ReadNvrNoVideoPic(NVR_NOVIDEO_FILE_2, PicBuff2);
	if(dataLen2 <= 0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "--------Read NoVideoFile: %s---fail!\n", NVR_NOVIDEO_FILE_2);
		return (void*)(-1);
	}
	stVstream2.pu8Addr = PicBuff2;
	stVstream2.u32Len = dataLen2;	
	stVstream2.u64PTS = 0;
#endif
	int count = 0;
	//printf("--------------dataLen1: %d, dataLen2: %d\n", dataLen1, dataLen2);
	//if(MAX_IPC_CHNNUM > 0)
	//	PRV_NvrNoVideoDet();
	while(1)
	{
		//数据会保存在VO缓冲区，所以无需一直发送数据。只需在画面切换，或断开通道连接时，再发送数据
		sem_wait(&sem_SendNoVideoPic);
	
		//pthread_mutex_lock(&send_data_mutex);
		//设备重启时，退出系统后，停止发送图片
		if(!s_bIsSysInit)
			continue;
		//当前属于回放状态，所有VO由回放模块占用，不发送图片
		if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_PB || s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_PIC)
			continue;
		
		//if(MAX_IPC_CHNNUM > 0)
		//	PRV_NVRChnVLossDet();
		#if 0
		int s32Ret = 0, index = 0;
		VO_CHN SpotVoChn = 0;
		s32Ret = PRV_VLossInCurLayOut();
		//当前画面布局中所有数字通道都有视频，此时不需要发送无网络视频图片
		if(0 == DEV_SPOT_NUM && HI_FAILURE == s32Ret)
		{
			continue;	
		}
		else//NVR暂时不支持SPOT口，为以后兼容
		{//如果包含SPOT口，需要判断当前SPOT口对应的输出通道是否有视频
			SpotVoChn = s_astVoDevStatDflt[SPOT_VO_DEV].as32ChnOrder[SingleScene][s_astVoDevStatDflt[SPOT_VO_DEV].s32SingleIndex];
			index = PRV_GetVoChnIndex(SpotVoChn);
			if(index < 0)
				continue;

			if(index >= 0 && HI_FAILURE == s32Ret
				&& VochnInfo[index].VdecChn != DetVLoss_VdecChn)//当前SPOT输出通道为有视频信号
				continue;
		}
		#endif
		//PRV_ReleaseVdecData(DetVLoss_VdecChn);
		count = 0;
		while(count < 3)
		{
		//if(s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewStat == PRV_STAT_NORM)
#if defined(SN9234H1)
			s32Ret= HI_MPI_VDEC_SendStream(DetVLoss_VdecChn, &stVstream, HI_IO_BLOCK);
#else		
			s32Ret= HI_MPI_VDEC_SendStream(DetVLoss_VdecChn, &stVstream1, HI_IO_BLOCK);
#endif			
			if(s32Ret != HI_SUCCESS) //送至视频解码器
			{
				TRACE(SCI_TRACE_HIGH, MOD_PRV, "===========HI_MPI_VDEC_SendStream fail---0x%x!\n", s32Ret);
				//CHECK(HI_MPI_VDEC_SendStream(NoConfig_VdecChn, &stVstream_1, HI_IO_BLOCK)); //送至视频解码器
				//CHECK(HI_MPI_VDEC_StopRecvStream(DetVLoss_VdecChn));/*销毁通道前先停止接收数据*/
				//CHECK(HI_MPI_VDEC_DestroyChn(DetVLoss_VdecChn)); /* 销毁视频通道 */			
				//PRV_CreateVdecChn(JPEGENC, NOVIDEO_VDECHEIGHT, NOVIDEO_VDECWIDTH, DetVLoss_VdecChn);//创建解码通道存放"无网络视频"图片
			}
			usleep(40 * 1000);//保证发送的2帧数据间隔40ms以上
			count++;
		}
		//pthread_mutex_unlock(&send_data_mutex);
		
		//usleep(100 * 1000);//保证发送的2帧数据间隔40ms以上
		
	}
	
	return NULL;
}

STATIC void *Set_OSD_TimeProc(void *parg)
{
	HI_S32 s32Ret;
	HI_U32 	VLoss_Cnt=0;//,OSD_Re_Cnt=0;
	time_t rawtime;
	struct tm newtime;
	char m_strTime[MAX_BMP_STR_LEN], m_strQTime[MAX_BMP_STR_LEN];
	Log_pid(__FUNCTION__);
	struct timeval tv;

	Loss_State_Pre = Loss_State_Cur;
	s_State_Info.bIsRe_Init = 1;
	//sleep(5);
    while (1)
    {
    	//sem_wait(&OSD_time_Sem);
    	
    	tv.tv_sec = 0;
		tv.tv_usec = 500 * 1000;
		select(0, NULL, NULL, NULL, &tv);//等待500ms
    	
		pthread_mutex_lock(&s_osd_mutex);

		//	printf("osd change: bIsRe_Init=%d-->%d, bIsOsd_Init =%d-->%d ", bIsRe_Init, s_State_Info.bIsRe_Init, bIsOsd_Init, s_State_Info.bIsOsd_Init);
    	if(s_State_Info.bIsRe_Init && s_State_Info.bIsOsd_Init)
    	{
	    	time(&rawtime);
			osdtime = rawtime;
			localtime_r(&rawtime, &newtime);
			switch(s_OSD_Time_type)
			{
				case 0:
					if (MMI_GetLangID() == Chinese)
						SN_SPRINTF(m_strTime, sizeof(m_strTime), "%04d年%02d月%02d日 %s %02d:%02d:%02d",newtime.tm_year + 1900,newtime.tm_mon + 1,newtime.tm_mday,weekday[newtime.tm_wday],newtime.tm_hour,newtime.tm_min,newtime.tm_sec);
					else
						SN_SPRINTF(m_strTime, sizeof(m_strTime), "%04d-%02d-%02d  %s %02d:%02d:%02d",newtime.tm_year + 1900,newtime.tm_mon + 1,newtime.tm_mday,weekday_en[newtime.tm_wday],newtime.tm_hour,newtime.tm_min,newtime.tm_sec);
					SN_SPRINTF(m_strQTime, sizeof(m_strQTime), "%04d-%02d-%02d %02d:%02d:%02d",newtime.tm_year + 1900,newtime.tm_mon + 1,newtime.tm_mday,newtime.tm_hour,newtime.tm_min,newtime.tm_sec);				
					break;
				case 1:
					if (MMI_GetLangID() == Chinese)
						SN_SPRINTF(m_strTime, sizeof(m_strTime), "%02d月%02d日%04d年 %s %02d:%02d:%02d",newtime.tm_mon + 1,newtime.tm_mday,newtime.tm_year + 1900,weekday[newtime.tm_wday],newtime.tm_hour,newtime.tm_min,newtime.tm_sec);
					else
						SN_SPRINTF(m_strTime, sizeof(m_strTime), "%02d-%02d-%04d  %s %02d:%02d:%02d",newtime.tm_mon + 1,newtime.tm_mday,newtime.tm_year + 1900,weekday_en[newtime.tm_wday],newtime.tm_hour,newtime.tm_min,newtime.tm_sec);
					SN_SPRINTF(m_strQTime, sizeof(m_strQTime), "%02d-%02d-%04d %02d:%02d:%02d",newtime.tm_mon + 1,newtime.tm_mday,newtime.tm_year + 1900,newtime.tm_hour,newtime.tm_min,newtime.tm_sec);				
					break;
				case 2:
					if (MMI_GetLangID() == Chinese)
						SN_SPRINTF(m_strTime, sizeof(m_strTime), "%02d日%02d月%04d年 %s %02d:%02d:%02d",newtime.tm_mday,newtime.tm_mon + 1,newtime.tm_year + 1900,weekday[newtime.tm_wday],newtime.tm_hour,newtime.tm_min,newtime.tm_sec);
					else
						SN_SPRINTF(m_strTime, sizeof(m_strTime), "%02d-%02d-%04d  %s %02d:%02d:%02d",newtime.tm_mday,newtime.tm_mon + 1,newtime.tm_year + 1900,weekday_en[newtime.tm_wday],newtime.tm_hour,newtime.tm_min,newtime.tm_sec);
					SN_SPRINTF(m_strQTime, sizeof(m_strQTime), "%02d-%02d-%04d %02d:%02d:%02d",newtime.tm_mday,newtime.tm_mon + 1,newtime.tm_year + 1900,newtime.tm_hour,newtime.tm_min,newtime.tm_sec);				
					break;	
			}
			//更新OSD时间
			s32Ret = OSD_Set_Time(m_strTime, m_strQTime);
			if(s32Ret != HI_SUCCESS)
			{
				TRACE(SCI_TRACE_HIGH, MOD_PRV,"Set_OSD_TimeProc OSD_Set_Time faild 0x%x!\n",s32Ret);
			}
    	}
		/*else if(!s_State_Info.bIsOsd_Init)
		{
			if(s_State_Info.bIsInit)
			{//预览已经初始化
				if(!(OSD_Re_Cnt % 30))
				{
					s32Ret = OSD_init(s_OSD_Time_type);
					if(s32Ret == 0)
					{
						s_State_Info.bIsOsd_Init = 1;
						OSD_Re_Cnt = 0;
					}
				}
				OSD_Re_Cnt ++;
			}
		}*/
		
		if(VLoss_Cnt %2 == 0)
		{
			//更新视频丢失报警参数
			if(LOCALVEDIONUM > 0)
			{
				s32Ret = PRV_VLossDet();
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_HIGH, MOD_PRV,"Set_OSD_TimeProc PRV_VLossDet faild 0x%x!\n",s32Ret);
				}
			}
			//无视频信号的数字通道需要显示"无视频信号"报警图片
			//if(MAX_IPC_CHNNUM > 0)
			//	PRV_NVRChnVLossDet();

			VLoss_Cnt++;
		}
		else
		{
			VLoss_Cnt = 0;
		}

		pthread_mutex_unlock(&s_osd_mutex);
	}
		
    return NULL;
}
#if defined(SN9234H1)
/************************************************************************/
/* 开启生产测试中的无视频信号检测功能。
                                                                     */
/************************************************************************/

STATIC void* PRV_VLossDetProc()
{
	//printf("------Local VLossDetProc\n");
    int i = 0,is_lost = 0;
	Log_pid(__FUNCTION__);

	//printf("------------------Begin Local Video Loss Detect!!!\n");
	for (;;)
	{
		for (i = 0; i < LOCALVEDIONUM; i++)
		{
			is_lost = Preview_GetAVstate(i);
			//printf("------------is_lost: %d\n", is_lost);
			if(is_lost == -1)
			{
				fprintf(stderr, "%s: Preview_GetAVstate error!!!", __FUNCTION__);
				return NULL;
			}

			if (is_lost)
			{
#if defined(SN6116HE)||defined(SN6116LE) || defined(SN6108HE)  || defined(SN6108LE) || defined(SN8608D_LE) || defined(SN8608M_LE) || defined(SN8616D_LE) || defined(SN8616M_LE)|| defined(SN9234H1)
	        	//printf("@####################i = %d##################\n",i);
				if(i< PRV_CHAN_NUM)
	        	{//如果为前8个通道，那么在主片配置无视频信号，如果是后8路，需要发送消息给从片，让从片配置无视频吸纳好哦
					if(i>= PRV_VI_CHN_NUM)
					{	//如果为通道5到8，那么配置输入设备2						
		            	CHECK(HI_MPI_VI_EnableUserPic(PRV_656_DEV, i%PRV_VI_CHN_NUM));
					}
					else
					{//如果为通道1到4，那么配置输入设备3						
						CHECK(HI_MPI_VI_EnableUserPic(PRV_656_DEV_1, i%PRV_VI_CHN_NUM));
					}
	        	}
#if defined(SN_SLAVE_ON)				
				else	
				//如果为通道9到16，那么发送消息给从片。
				//不等待从片返回信息
				{
					Prv_Slave_Vloss_Ind slave_req;
					slave_req.chn = i;
					slave_req.state = HI_TRUE;
					//printf("@####################loss i = %d##################\n",i);
					SN_SendMccMessageEx(PRV_SLAVE_1,SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_VLOSS_IND, &slave_req, sizeof(Prv_Slave_Vloss_Ind));		
				}
#endif
#else
				CHECK(HI_MPI_VI_EnableUserPic(i/PRV_VI_CHN_NUM, i%PRV_VI_CHN_NUM));
#endif
	        }
			else
			{
#if defined(SN6116HE)||defined(SN6116LE) || defined(SN6108HE)  || defined(SN6108LE) ||defined(SN8608D) || defined(SN8608M) || defined(SN8608D_LE) || defined(SN8608M_LE) || defined(SN8616D_LE) || defined(SN8616M_LE)|| defined(SN9234H1)
	        	//printf("@####################i = %d##################\n",i);
				if(i< PRV_CHAN_NUM)
	        	{//如果为前8个通道，那么在主片取消无视频信号，如果是后8路，需要发送消息给从片，让从片取消无视频吸纳好哦
					if(i>= PRV_VI_CHN_NUM)
					{
		            	CHECK(HI_MPI_VI_DisableUserPic(PRV_656_DEV, i%PRV_VI_CHN_NUM));
					}
					else
					{
						CHECK(HI_MPI_VI_DisableUserPic(PRV_656_DEV_1, i%PRV_VI_CHN_NUM));
					}
				}
#if defined(SN_SLAVE_ON)				
				else	
				//如果为通道9到16，那么发送消息给从片。
				//不等待从片返回信息
				{
					Prv_Slave_Vloss_Ind slave_req;
					slave_req.chn = i;
					slave_req.state = HI_FALSE;
					//printf("@####################unloss i = %d##################\n",i);
					SN_SendMccMessageEx(PRV_SLAVE_1,0xFFFFFFFF, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_VLOSS_IND, &slave_req, sizeof(Prv_Slave_Vloss_Ind));		
				}
#endif				
#else
				CHECK(HI_MPI_VI_DisableUserPic(i/PRV_VI_CHN_NUM, i%PRV_VI_CHN_NUM));
#endif
			}      
		}	
		
		usleep(1000*800);
	}	

	return NULL;	
}
#endif
/*************************************************
Function: //PRV_TimeOut_Proc
Description: //超时处理程序
Calls: 
Called By: //
Input: // 
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
static int PRV_TimeOut_Proc(HI_VOID)
{
	int ret=0;
	switch(s_State_Info.TimerType)
	{//判断当前超时类型
		case PRV_INIT://初始化超时
			ret = PRV_Init_TimeOut(1);
			break;
		case PRV_LAY_OUT://画面切换超时
			//ret = PRV_LayOut_TimeOut();
			break;
		default:
			break;
	}
	return ret;
}

int PRV_SetPreviewVoDevInMode(int s32ChnCount)
{
	int i = 0;
	g_PlayInfo PlayState;
	
	SN_MEMSET(&PlayState, 0, sizeof(g_PlayInfo));
	//PlayState.FullScreenId = 0;
	PlayState.ImagCount = s32ChnCount;
	PlayState.IsSingle = (s32ChnCount == 1) ? 1 : 0;
	PlayState.PlayBackState = PLAY_ENTER;
	PlayState.IsPlaySound = 1;
	MMI_GetReplaySize(&PlayState.SubWidth, &PlayState.SubHeight);	

	int u32Width, u32Height;
	PlayBack_GetPlaySize((HI_U32 *)&u32Width, (HI_U32 *)&u32Height);
	
	PRV_SetPlayInfo(&PlayState);
	
	for(i = 0; i < CHANNEL_NUM; i++)
		VochnInfo[i].bIsPBStat = 1;

	PlayBack_StartVo();

	return 0;
}


/*************************************************
Function: //PRV_PreviewVoDevSingle
Description: 回放时多画面显示。
Calls: 
Called By: //
Input: //VoDev:设备号
		u32chn:回放显示画面索引
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
HI_S32 Playback_VoDevMul(VO_DEV VoDev, HI_U32 s32ChnCnt)
{	
		HI_U32 u32Width = 0, u32Height = 0,i=0,div=0,u32Width_s=0,u32Hight_s=0,width,height;
		g_PlayInfo stPlayInfo;
		HI_S32 s32Ret = 0;
		VO_VIDEO_LAYER_ATTR_S pstLayerAttr;
#if defined(SN9234H1)	
		VO_ZOOM_ATTR_S stVoZoomAttr;
#endif
		VO_CHN_ATTR_S stChnAttr;
	    RECT_S stSrcRect;
#if defined(SN9234H1)
		if(VoDev == SPOT_VO_DEV || VoDev == AD)
		{
			RET_FAILURE("Not Support Dev: SD!!");
		}
		
		if ( (VoDev < 0 || VoDev >= PRV_VO_MAX_DEV))
		{
			RET_FAILURE("Invalid Parameter: VoDev or u32Index");
		}
#else	
		
		if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
		{
			RET_FAILURE("Invalid Parameter: VoDev or u32Index");
		}
		if(VoDev == DHD1)
			VoDev = DSD0;
#endif	

		if (s32ChnCnt<0)//PRV_VO_CHN_NUM)
		{
			RET_FAILURE("Invalid Parameter: VoChn ");
		}

		
		PRV_GetPlayInfo(&stPlayInfo);
		s32Ret = HI_MPI_VO_GetVideoLayerAttr(VoDev,&pstLayerAttr);
		u32Width = pstLayerAttr.stImageSize.u32Width;
		u32Height = pstLayerAttr.stImageSize.u32Height;
		PlayBack_GetPlaySize(&u32Width,&u32Height);
		u32Width_s = u32Width;
		u32Hight_s = u32Height;
	    if(s32ChnCnt==9)
	   	{
             while(u32Width%6 != 0)
			     u32Width++;
		     while(u32Height%6 != 0)
			     u32Height++;
		}
		 for(i=0;i<s32ChnCnt;i++)
		{
          #if defined(Hi3535)
			  HI_MPI_VO_ResumeChn(VoDev, i);
			  HI_MPI_VO_HideChn(VoDev,i);
          #else
			  HI_MPI_VO_ChnResume(VoDev,i);
			  HI_MPI_VO_ChnHide(VoDev,i);
          #endif
		}
		 div = sqrt(s32ChnCnt);		/* 计算每个通道的宽度和高度 */

	     width = (HI_S32)(u32Width/div);//每个输出通道的宽高
	     height = (HI_S32)(u32Height/div);
		for (i = 0; i < s32ChnCnt; i++)
		{
			s32Ret = HI_MPI_VO_GetChnAttr(VoDev, i, &stChnAttr);
			stChnAttr.stRect.s32X = width*(i%div);/* 其它画面显示时通道号从小到大依次排列 */
			stChnAttr.stRect.s32Y = height*(i/div);
			stChnAttr.stRect.u32Width= width;
			stChnAttr.stRect.u32Height = height;
		    stSrcRect.s32X	= stChnAttr.stRect.s32X;
		    stSrcRect.s32Y	= stChnAttr.stRect.s32Y;
		    stSrcRect.u32Width	 = stChnAttr.stRect.u32Width;
		    stSrcRect.u32Height  = stChnAttr.stRect.u32Height;	
			if(s32ChnCnt==9)
            { 
			   if((i + 1) % 3 == 0)//最后一列
				stSrcRect.u32Width = u32Width_s- stSrcRect.s32X;
			   if(i > 5 && i < 9)//最后一行
				stSrcRect.u32Height = u32Hight_s- stSrcRect.s32Y;
		     }
		    stChnAttr.stRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[i].VideoInfo.width, VochnInfo[i].VideoInfo.height, stSrcRect);
            
			stChnAttr.stRect.s32X 		&= (~0x01);
		    stChnAttr.stRect.s32Y		&= (~0x01);
		    stChnAttr.stRect.u32Width   &= (~0x01);
		    stChnAttr.stRect.u32Height  &= (~0x01);
		    s32Ret = HI_MPI_VO_SetChnAttr(VoDev, i, &stChnAttr);
		    if (s32Ret != HI_SUCCESS)
		    {
			   TRACE(SCI_TRACE_NORMAL, MOD_PRV,"VoDevID:%d--In set channel %d attr failed with %x! ",VoDev, i, s32Ret);
		    }
				  
#if !defined(Hi3535)
		    HI_MPI_VO_SetChnField(VoDev,i, VO_FIELD_BOTH);
#endif
		
#if defined(SN9234H1)
			if(VochnInfo[i].SlaveId > PRV_MASTER)
			{
			   stVoZoomAttr.stZoomRect=stChnAttr.stRect;
				stVoZoomAttr.enField = VIDEO_FIELD_FRAME;
				HI_MPI_VO_SetZoomInWindow(VoDev, i, &stVoZoomAttr);//genggai
			
			}
				//s32Ret = HI_MPI_VO_SetChnAttr(VoDev, i, &stChnAttr);
#endif
			HI_MPI_VO_EnableChn(VoDev, i);
           #if defined(Hi3535)
				HI_MPI_VO_ShowChn(VoDev, i);
           #else
				HI_MPI_VO_ChnShow(VoDev, i);
           #endif
	   }
	
		 RET_SUCCESS("");
	
}



/*************************************************
Function: //PRV_PreviewVoDevSingle
Description: 回放时单画面显示。
Calls: 
Called By: //
Input: //VoDev:设备号
		u32chn:回放显示画面索引
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/

HI_S32 Playback_VoDevSingle(VO_DEV VoDev, HI_U32 u32chn)
{	
    HI_U32 u32Width = 0, u32Height = 0,i=0;
	g_PlayInfo stPlayInfo;
	VO_CHN VoChn = 0;
	HI_S32 s32Ret = 0;
	VO_VIDEO_LAYER_ATTR_S pstLayerAttr;
#if defined(SN9234H1)	
	VO_ZOOM_ATTR_S stVoZoomAttr;
#endif
	VO_CHN_ATTR_S stChnAttr;
     RECT_S stSrcRect;
	VoChn=u32chn;
#if defined(SN9234H1)
	if(VoDev == SPOT_VO_DEV || VoDev == AD)
	{
		RET_FAILURE("Not Support Dev: SD!!");
	}
	
	if ( (VoDev < 0 || VoDev >= PRV_VO_MAX_DEV))
	{
		RET_FAILURE("Invalid Parameter: VoDev or u32Index");
	}
#else	
	
	if (VoDev < 0 || VoDev > DHD0/*VoDev >= PRV_VO_MAX_DEV*/)
	{
		RET_FAILURE("Invalid Parameter: VoDev or u32Index");
	}
	if(VoDev == DHD1)
		VoDev = DSD0;
#endif	
	if (VoChn < 0 || VoChn >= 4)//PRV_VO_CHN_NUM)
	{
		RET_FAILURE("Invalid Parameter: VoChn ");
	}

	PRV_GetPlayInfo(&stPlayInfo);
	s32Ret = HI_MPI_VO_GetVideoLayerAttr(VoDev,&pstLayerAttr);
	u32Width = pstLayerAttr.stImageSize.u32Width;
	u32Height = pstLayerAttr.stImageSize.u32Height;
	if(stPlayInfo.FullScreenId==1||stPlayInfo.IsZoom==1)
	{   
	}
	else
	{
         u32Width = u32Width - stPlayInfo.SubWidth;
	     u32Height = u32Height - stPlayInfo.SubHeight;
	}
     PlayBack_GetPlaySize(&u32Width,&u32Height);
     for(i=0;i<stPlayInfo.ImagCount;i++)
    {
          #if defined(Hi3535)
    	  HI_MPI_VO_ResumeChn(VoDev, i);
		  HI_MPI_VO_HideChn(VoDev,i);
          #else
		  HI_MPI_VO_ChnResume(VoDev,i);
		  HI_MPI_VO_ChnHide(VoDev,i);
          #endif
    }

   s32Ret = HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stChnAttr);
		
   stChnAttr.stRect.s32X = 0;
   stChnAttr.stRect.s32Y =0;
   stChnAttr.stRect.u32Width= u32Width;
   stChnAttr.stRect.u32Height = u32Height;
   stSrcRect.s32X	= stChnAttr.stRect.s32X;
   stSrcRect.s32Y	= stChnAttr.stRect.s32Y;
	stSrcRect.u32Width	 = stChnAttr.stRect.u32Width;
	stSrcRect.u32Height  = stChnAttr.stRect.u32Height;				
	stChnAttr.stRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[VoChn].VideoInfo.width, VochnInfo[VoChn].VideoInfo.height, stSrcRect);
	stChnAttr.stRect.s32X 		&= (~0x01);
	stChnAttr.stRect.s32Y		&= (~0x01);
	stChnAttr.stRect.u32Width   &= (~0x01);
	stChnAttr.stRect.u32Height  &= (~0x01);
	s32Ret = HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stChnAttr);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"VoDevID:%d--In set channel %d attr failed with %x! ",VoDev, i, s32Ret);
	}
   s32Ret = HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stChnAttr);
   if (s32Ret != HI_SUCCESS)
  {
	  TRACE(SCI_TRACE_NORMAL, MOD_PRV,"VoDevID:%d--In set channel %d attr failed with %x! ",VoDev, i, s32Ret);
  }
#if !defined(Hi3535)
	HI_MPI_VO_SetChnField(VoDev,VoChn, VO_FIELD_BOTH);
#endif
#if defined(SN9234H1)
       if(VochnInfo[VoChn].SlaveId > PRV_MASTER)
       	{
		stVoZoomAttr.stZoomRect=stChnAttr.stRect;
        stVoZoomAttr.enField = VIDEO_FIELD_FRAME;
		HI_MPI_VO_SetZoomInWindow(VoDev, VoChn, &stVoZoomAttr);
       	}
#endif
		HI_MPI_VO_EnableChn(VoDev, VoChn);
#if defined(Hi3535)
		HI_MPI_VO_ShowChn(VoDev, VoChn);
#else
		HI_MPI_VO_ChnShow(VoDev, VoChn);
#endif

     RET_SUCCESS("");

}


/*************************************************
Function: //PRV_ZoomInPic
Description://回放画中画,电子放大状态时专用
Calls: 
Called By: //
Input: // bIsShow:是否显示
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
***********************************************************************/
	
	HI_S32 Playback_ZoomInPic(HI_BOOL bIsShow)
	{
	
		VO_DEV VoDev = s_VoDevCtrlDflt;
		VO_CHN VoChn = PRV_CTRL_VOCHN;
#if defined(SN9234H1)	
		VI_DEV ViDev = -1;
		VI_CHN ViChn = -1;
#endif
		VO_CHN_ATTR_S stVoChnAttr;
		int index = 0;
		RECT_S stSrcRect, stDestRect;
	    g_PlayInfo stPlayInfo;
        PRV_GetPlayInfo(&stPlayInfo);
        if(stPlayInfo.IsZoom==0)
        {
            RET_FAILURE("Warning!!! not int zoom in stat now!!!");
		}
#if defined(SN9234H1)
	
	 /*2010-9-19 双屏！*/
	//VoDev = HD;
	//again:
	//获取s32CtrlChn对应的VI
		if(OldCtrlChn >= 0)
		{
			if(OldCtrlChn < (LOCALVEDIONUM < PRV_CHAN_NUM ? LOCALVEDIONUM : PRV_CHAN_NUM))
			{
				if(OldCtrlChn < PRV_VI_CHN_NUM)
				{
					ViDev = PRV_656_DEV_1;
				}
				else
				{
					ViDev = PRV_656_DEV;
				}
				ViChn = OldCtrlChn%PRV_VI_CHN_NUM;
			}
			//在从片
			else if(OldCtrlChn >= PRV_CHAN_NUM && OldCtrlChn < LOCALVEDIONUM)
			{
				ViDev = PRV_HD_DEV;
			}
		}
#endif
		//1.解绑VO通道
		if(OldCtrlChn >= 0)
		{	
#if defined(SN9234H1)
			if(ViDev != -1)//模拟视频通道
				CHECK(HI_MPI_VI_UnBindOutput(ViDev, ViChn, VoDev, VoChn));
			else
			{
				index = PRV_GetVoChnIndex(OldCtrlChn);
				if(index < 0)
					RET_FAILURE("-----------Invalid Index!");
				if(VochnInfo[index].SlaveId > PRV_MASTER)
					(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, VoDev, VoChn));
				else
				{
					(HI_MPI_VDEC_UnbindOutputChn(VochnInfo[index].VdecChn, VoDev, VoChn));
					VochnInfo[index].IsBindVdec[VoDev] = -1;
				}
				
			}
#else
			index = PRV_GetVoChnIndex(stPlayInfo.ZoomChn);
			if(index < 0)
				RET_FAILURE("-----------Invalid Index!");
#if defined(Hi3535)
			CHECK(HI_MPI_VO_HideChn(VoDev, VoChn));
#else
			CHECK(HI_MPI_VO_ChnHide(VoDev, VoChn));
#endif
			CHECK(PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn, VoChn));
			VochnInfo[index].IsBindVdec[VoDev] = -1;
#if defined(Hi3535)
			CHECK(PRV_VO_UnBindVpss(PIP,VoChn,VoChn,VPSS_BSTR_CHN));
#else
			CHECK(PRV_VO_UnBindVpss(VoDev, VoChn, VoChn, VPSS_PRE0_CHN));
#endif
#endif		
		}
		//2.关闭VO通道
#if defined(Hi3535)
		CHECK(HI_MPI_VO_DisableChn(PIP ,VoChn));
#else	
		CHECK(HI_MPI_VO_DisableChn(VoDev ,VoChn));
#endif
	
		if (bIsShow)
		{		
#if defined(SN9234H1)
             HI_S32 Temp_Chn = (HI_S32)stPlayInfo.ZoomChn;
			if(Temp_Chn< (LOCALVEDIONUM < PRV_CHAN_NUM ? LOCALVEDIONUM : PRV_CHAN_NUM))
			{
				if(Temp_Chn < PRV_VI_CHN_NUM)
				{
					ViDev = PRV_656_DEV_1;
				}
				else
				{
					ViDev = PRV_656_DEV;
				}
				ViChn = stPlayInfo.ZoomChn %PRV_VI_CHN_NUM;
			}
			else if(Temp_Chn >= PRV_CHAN_NUM && Temp_Chn  < LOCALVEDIONUM)
			{
				ViDev = PRV_HD_DEV;
			}
	
#endif
			//3.设置VO通道		
			index = PRV_GetVoChnIndex(stPlayInfo.ZoomChn);
			if(index < 0)
				RET_FAILURE("------ERR: Invalid Index!");
#if defined(Hi3535)
			CHECK_RET(HI_MPI_VO_GetChnAttr(PIP,stPlayInfo.ZoomChn, &stVoChnAttr));
#else
			CHECK_RET(HI_MPI_VO_GetChnAttr(VoDev, stPlayInfo.ZoomChn, &stVoChnAttr));
#endif
			stVoChnAttr.stRect.s32X 	 = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width*3/4;
			stVoChnAttr.stRect.s32Y 	 = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height*3/4;
			stVoChnAttr.stRect.u32Height = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height*1/4;
			stVoChnAttr.stRect.u32Width  = s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width*1/4;
			stSrcRect.s32X		 = stVoChnAttr.stRect.s32X;
			stSrcRect.s32Y		 = stVoChnAttr.stRect.s32Y;
			stSrcRect.u32Width	 = stVoChnAttr.stRect.u32Width;
			stSrcRect.u32Height  = stVoChnAttr.stRect.u32Height;
			
			stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
			stVoChnAttr.stRect.s32X 	 = stDestRect.s32X		& (~0x01);
			stVoChnAttr.stRect.s32Y 	 = stDestRect.s32Y		& (~0x01);
			stVoChnAttr.stRect.u32Width  = stDestRect.u32Width	& (~0x01);
			stVoChnAttr.stRect.u32Height = stDestRect.u32Height & (~0x01);
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----------w=%d, h=%d, d_w=%d, d_h=%d, x=%d, y=%d, s_w=%d, s_h=%d\n", s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Width, s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stDispRect.u32Height,
			//	s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Width, s_astVoDevStatDflt[VoDev].stVideoLayerAttr.stImageSize.u32Height,
			//	stVoChnAttr.stRect.s32X ,stVoChnAttr.stRect.s32Y,stVoChnAttr.stRect.u32Width,stVoChnAttr.stRect.u32Height);
#if defined(Hi3535)
			stVoChnAttr.u32Priority = 1;
			CHECK_RET(HI_MPI_VO_SetChnAttr(PIP, VoChn, &stVoChnAttr));
			CHECK_RET(PRV_VO_BindVpss(PIP,VoChn,VoChn,VPSS_BSTR_CHN));
#elif defined(Hi3531)		
			stVoChnAttr.u32Priority = 1;
			CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stVoChnAttr));
			CHECK_RET(PRV_VO_BindVpss(VoDev,VoChn,VoChn,VPSS_PRE0_CHN));
#else
			CHECK_RET(HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stVoChnAttr));
#endif
			//stVoZoomAttr.stZoomRect = stVoChnAttr.stRect;
			//stVoZoomAttr.enField = VIDEO_FIELD_INTERLACED;
			//CHECK_RET(HI_MPI_VO_SetZoomInWindow(VoDev, VoChn, &stVoZoomAttr));
	
#if defined(SN9234H1)
			//4.绑定VO通道
			if(-1 == ViDev)
			{			
				if(VochnInfo[index].VdecChn >= 0)
				{			
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----index: %d---SlaveId: %d, VoDev: %d----Bind---VochnInfo[index].VdecChn: %d, VoChn: %d\n",index, VochnInfo[index].SlaveId, VoDev, VochnInfo[index].VdecChn, VoChn);
					if(VochnInfo[index].SlaveId == PRV_MASTER )
					{
						CHECK_RET(HI_MPI_VDEC_BindOutput(VochnInfo[index].VdecChn, VoDev, VoChn)); 
					}				
					else if(VochnInfo[index].SlaveId > PRV_MASTER)
					{
						ViDev = PRV_HD_DEV;
						ViChn = 0;
					}
				}
			}
			
#if defined(SN_SLAVE_ON)
			if(ViDev == PRV_HD_DEV)
			{			
				VO_ZOOM_ATTR_S stVoZoomAttr;
				HI_U32 u32Width = 0, u32Height = 0;
#if defined(SN8604M) || defined(SN8608M) || defined(SN8608M_LE) || defined(SN8616M_LE)|| defined(SN9234H1)
				//u32Width = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Width;
				//u32Height = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Height;
				u32Width = PRV_BT1120_SIZE_W;
				u32Width = PRV_BT1120_SIZE_H;
#else
				u32Width = PRV_SINGLE_SCREEN_W;
				u32Height = PRV_SINGLE_SCREEN_H;
#endif				
				u32Width = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Width;
				u32Height = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Height;
				stSrcRect.s32X		= 0;
				stSrcRect.s32Y		= 0;
				stSrcRect.u32Width	= u32Width;
				stSrcRect.u32Height = u32Height;
				
				stDestRect = PRV_ReSetVoRect(OutPutMode, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height, stSrcRect);
				
				stVoZoomAttr.stZoomRect.s32X		= stDestRect.s32X	   & (~0x01);
				stVoZoomAttr.stZoomRect.s32Y		= stDestRect.s32Y	   & (~0x01);
				stVoZoomAttr.stZoomRect.u32Width	= stDestRect.u32Width  & (~0x01);
				stVoZoomAttr.stZoomRect.u32Height	= stDestRect.u32Height & (~0x01);
	
				//stVoZoomAttr.enField = VIDEO_FIELD_INTERLACED;
				stVoZoomAttr.enField = VIDEO_FIELD_FRAME;
#if defined(Hi3535)
				CHECK_RET(HI_MPI_VO_SetZoomInWindow(PIP, VoChn, &stVoZoomAttr));
#else
				CHECK_RET(HI_MPI_VO_SetZoomInWindow(VoDev, VoChn, &stVoZoomAttr));
#endif
			}
#endif
			if(-1 != ViDev)
			{
				//printf("--------Bind ViDev: %d, ViChn: %d\n", ViDev, ViChn);
				CHECK_RET(HI_MPI_VI_BindOutput(ViDev, ViChn, VoDev, VoChn));
			}
	
#else
			//4.绑定VO通道
			if(VochnInfo[index].VdecChn >= 0)
			{			
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----index: %d---SlaveId: %d, VoDev: %d----Bind---VochnInfo[index].VdecChn: %d, VoChn: %d\n",index, VochnInfo[index].SlaveId, VoDev, VochnInfo[index].VdecChn, VoChn);
				if(VochnInfo[index].SlaveId == PRV_MASTER )
				{
					CHECK_RET(PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VoChn)); 
				}				
			}		
#endif
			VochnInfo[index].IsBindVdec[VoDev] = 1;
			//5.开启VO通道
#if defined(Hi3535)
			CHECK_RET(HI_MPI_VO_EnableChn(PIP, VoChn));
#else		
			CHECK_RET(HI_MPI_VO_EnableChn(VoDev, VoChn));
#endif
			OldCtrlChn = stPlayInfo.ZoomChn;
			sem_post(&sem_SendNoVideoPic);		
		}
		else
			OldCtrlChn = -1;
		RET_SUCCESS("");
	}



/*************************************************
Function: //PRV_RefreshVoDevScreen
Description: 回放根据VO状态刷新VO设备的显示
Calls: 
Called By: //
Input: //VoDev:设备号
		Is_Double: 是否双屏显示
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 Playback_RefreshVoDevScreen(VO_DEV VoDev, HI_U32 Is_Double)
	{
		VO_DEV VoDev2 = VoDev;
		g_PlayInfo stPlayInfo;
#if defined(SN9234H1)
	sem_post(&sem_SendNoVideoPic);
	again:
		if(VoDev != HD)
#else
		Is_Double = DISP_NOT_DOUBLE_DISP;
	again:
		if(VoDev != DHD0)
#endif
		{
			RET_SUCCESS("");
		}	
      PRV_GetPlayInfo(&stPlayInfo);
  if(stPlayInfo.PlayBackState > PLAY_INSTANT)
  {
        if(stPlayInfo.IsZoom==1)
       	{
            Playback_VoDevSingle(VoDev, stPlayInfo.ZoomChn);
		}
        else if(stPlayInfo.IsSingle)
        {
            Playback_VoDevSingle(VoDev,stPlayInfo.ZoomChn);  
		}
		else if(stPlayInfo.bISDB)
		{
            Playback_VoDevSingle(VoDev,stPlayInfo.DBClickChn);
		}
		else
		{
            Playback_VoDevMul(VoDev,stPlayInfo.ImagCount);
		}
 /*2010-9-19 双屏！*/
		//printf("##########s_VoSecondDev = %d######################\n",s_VoSecondDev);
		if(Is_Double == DISP_DOUBLE_DISP)
		{
			if (VoDev2 == VoDev)
			{
#if defined(SN9234H1)
				switch(VoDev)
				{
					case HD:
						{
							VO_PUB_ATTR_S stVoPubAttr = s_astVoDevStatDflt[s_VoSecondDev].stVoPubAttr;
							VO_VIDEO_LAYER_ATTR_S stVideoLayerAttr = s_astVoDevStatDflt[s_VoSecondDev].stVideoLayerAttr;
	
							s_astVoDevStatDflt[s_VoSecondDev] = s_astVoDevStatDflt[HD];
							s_astVoDevStatDflt[s_VoSecondDev].stVoPubAttr = stVoPubAttr;
							s_astVoDevStatDflt[s_VoSecondDev].stVideoLayerAttr = stVideoLayerAttr;
	
							VoDev = s_VoSecondDev;
							goto again;
						}
						break;
					//case s_VoSecondDev:
					case AD:
						{
							VO_PUB_ATTR_S stVoPubAttr = s_astVoDevStatDflt[HD].stVoPubAttr;
							VO_VIDEO_LAYER_ATTR_S stVideoLayerAttr = s_astVoDevStatDflt[HD].stVideoLayerAttr;
							
							s_astVoDevStatDflt[HD] = s_astVoDevStatDflt[AD];
							s_astVoDevStatDflt[HD].stVoPubAttr = stVoPubAttr;
							s_astVoDevStatDflt[HD].stVideoLayerAttr = stVideoLayerAttr;
							
							VoDev = HD;
							goto again;
						}
						break;
					case SD:
						{
							VO_PUB_ATTR_S stVoPubAttr = s_astVoDevStatDflt[HD].stVoPubAttr;
							VO_VIDEO_LAYER_ATTR_S stVideoLayerAttr = s_astVoDevStatDflt[HD].stVideoLayerAttr;
							
							s_astVoDevStatDflt[HD] = s_astVoDevStatDflt[SD];
							s_astVoDevStatDflt[HD].stVoPubAttr = stVoPubAttr;
							s_astVoDevStatDflt[HD].stVideoLayerAttr = stVideoLayerAttr;
							
							VoDev = HD;
							goto again;
						}
						break;
					default:
						break;
				}
#else
				switch(VoDev)
				{
					case DHD0:
						{
							VO_PUB_ATTR_S stVoPubAttr = s_astVoDevStatDflt[s_VoSecondDev].stVoPubAttr;
							VO_VIDEO_LAYER_ATTR_S stVideoLayerAttr = s_astVoDevStatDflt[s_VoSecondDev].stVideoLayerAttr;
	
							s_astVoDevStatDflt[s_VoSecondDev] = s_astVoDevStatDflt[DHD0];
							s_astVoDevStatDflt[s_VoSecondDev].stVoPubAttr = stVoPubAttr;
							s_astVoDevStatDflt[s_VoSecondDev].stVideoLayerAttr = stVideoLayerAttr;
	
							VoDev = s_VoSecondDev;
							goto again;
						}
						break;
					//case s_VoSecondDev:
					case DHD1:
					case DSD0:
						{
							VO_PUB_ATTR_S stVoPubAttr = s_astVoDevStatDflt[DHD0].stVoPubAttr;
							VO_VIDEO_LAYER_ATTR_S stVideoLayerAttr = s_astVoDevStatDflt[DHD0].stVideoLayerAttr;
							
							s_astVoDevStatDflt[DHD0] = s_astVoDevStatDflt[DSD0];
							s_astVoDevStatDflt[DHD0].stVoPubAttr = stVoPubAttr;
							s_astVoDevStatDflt[DHD0].stVideoLayerAttr = stVideoLayerAttr;
							
							VoDev = DHD0;
							goto again;
						}
						break;
					default:
						break;
				}
#endif			
			}
		}

		//播放音频
		PRV_PlayAudio(VoDev);
  }
	RET_SUCCESS("");
}

STATIC HI_S32 Playback_ChnZoomIn(VO_CHN VoChn, HI_U32 u32Ratio, const Preview_Point *pstPoint)
	{
       g_PlayInfo stPlayInfo;
	   PRV_GetPlayInfo(&stPlayInfo);
#if defined(SN9234H1)
		VoChn = stPlayInfo.ZoomChn;//暂时忽略参数VoChn
		int index = 0;
		index = VoChn;
		if(index < 0)
			RET_FAILURE("------ERR: Invalid Index!");
		if(VoChn < 0 || VoChn >= g_Max_Vo_Num
			|| u32Ratio < PRV_MIN_ZOOM_IN_RATIO || u32Ratio > PRV_MAX_ZOOM_IN_RATIO)
		{
			RET_FAILURE("Invalid Parameter: u32Ratio or VoChn");
		}
		
		if(NULL == pstPoint)
		{
			RET_FAILURE("NULL Pointer!!");
		}
		
		if (stPlayInfo.IsZoom==0)
		{
			RET_FAILURE("NOT in [zoom in ctrl] stat!!!");
		}
		
		
		//printf("vochn=%d, u32ratio=%d, pstPoint->x= %d, pstPoint->y=%d===========================\n",VoChn,u32Ratio,pstPoint->x, pstPoint->y);
	
	//M系列产品支持高清(1080P/720P)，如果显示高清的输出通道在主片，信号源分辨率太大，进行缩放后，
	//也大于VO_ZOOM_ATTR_S允许的范围，调用HI_MPI_VO_SetZoomInWindow会返回失败；
	//如果在从片，因为信号源已经经过级联进行了缩放，在主片上显示时，在VO_ZOOM_ATTR_S范围内
	//所以在支持高清的型号，且在主片显示高清时采用HI_MPI_VO_SetZoomInRatio进行电子放大
	
	//在主片上显示的高清(大于D1)通道采用HI_MPI_VO_SetZoomInRatio进行电子放大
		if(PRV_MASTER == VochnInfo[index].SlaveId
			&& VochnInfo[index].VdecChn >= 0 )		
		{
			VO_ZOOM_RATIO_S stZoomRatio;
	
			if (u32Ratio <= 1)
			{
				stZoomRatio.u32XRatio = 0;
				stZoomRatio.u32YRatio = 0;
				stZoomRatio.u32WRatio = 0;
				stZoomRatio.u32HRatio = 0;
			}
			else
			{
#if 0
				stZoomRatio.u32WRatio = 1000/u32Ratio;
				stZoomRatio.u32HRatio = 1000/u32Ratio;
				stZoomRatio.u32XRatio = ((pstPoint->x * 1000)/s_u32GuiWidthDflt + stZoomRatio.u32WRatio > 1000)
					? 1000 - stZoomRatio.u32WRatio
					: (pstPoint->x * 1000)/s_u32GuiWidthDflt;
				stZoomRatio.u32YRatio = ((pstPoint->y * 1000)/s_u32GuiHeightDflt + stZoomRatio.u32HRatio > 1000)
					? 1000 - stZoomRatio.u32HRatio
					: (pstPoint->y * 1000)/s_u32GuiHeightDflt;
	
#else /*将1到16倍放大转为1到4倍放大：y = (x - 1)/5 + 1*/
	
				u32Ratio += 4;
					
				stZoomRatio.u32WRatio = 5000/u32Ratio;
				stZoomRatio.u32HRatio = 5000/u32Ratio;
				stZoomRatio.u32XRatio = ((pstPoint->x * 1000)/s_u32GuiWidthDflt + stZoomRatio.u32WRatio > 1000)
					? 1000 - stZoomRatio.u32WRatio
					: (pstPoint->x * 1000)/s_u32GuiWidthDflt;
				stZoomRatio.u32YRatio = ((pstPoint->y * 1000)/s_u32GuiHeightDflt + stZoomRatio.u32HRatio > 1000)
					? 1000 - stZoomRatio.u32HRatio
					: (pstPoint->y * 1000)/s_u32GuiHeightDflt;
#endif
			}
	//	printf("==================stZoomRatio.u32XRatio = %d; stZoomRatio.u32YRatio = %d; stZoomRatio.u32WRatio =%d; stZoomRatio.u32HRatio = %d;\n",	
	//		stZoomRatio.u32XRatio ,stZoomRatio.u32YRatio,stZoomRatio.u32WRatio,stZoomRatio.u32HRatio);
#if 0 /*2010-8-31 优化：电子放大按中心放大方式进行放大*/
			stZoomRatio.u32XRatio = (stZoomRatio.u32XRatio < stZoomRatio.u32WRatio/2)?0:stZoomRatio.u32XRatio - stZoomRatio.u32WRatio/2;
			stZoomRatio.u32YRatio = (stZoomRatio.u32YRatio < stZoomRatio.u32HRatio/2)?0:stZoomRatio.u32YRatio - stZoomRatio.u32HRatio/2;
#endif
#if 1
			CHECK_RET(HI_MPI_VO_SetZoomInRatio(s_VoDevCtrlDflt, VoChn, &stZoomRatio));
#else /*2010-9-19 双屏！*/
			if(s_State_Info.g_zoom_first_in == HI_FALSE)
			{
				//CHECK_RET(HI_MPI_VO_GetZoomInRatio(HD,VoChn,&s_astZoomAttrDflt[HD]));
				CHECK_RET(HI_MPI_VO_GetZoomInRatio(s_VoDevCtrlDflt,VoChn,&s_astZoomAttrDflt[s_VoDevCtrlDflt]));
				s_State_Info.g_zoom_first_in = HI_TRUE;
			}
			//CHECK_RET(HI_MPI_VO_SetZoomInRatio(HD, VoChn, &stZoomRatio));
			CHECK_RET(HI_MPI_VO_SetZoomInRatio(s_VoDevCtrlDflt, VoChn, &stZoomRatio));
#endif
		}
		else
		//D系列的产品不支持高清，最大支持D1(704*576),均采用此种方法电子放大
		//M系列在从片上(SN8616M_LE)显示的通道以及主片显示的D1通道采用此种方法
		{
			VO_ZOOM_ATTR_S stVoZoomAttr;
			int w = 0, h = 0, x = 0, y = 0;
			HI_U32 u32Width = 0, u32Height = 0;
#if defined(SN8604M) || defined(SN8608M) || defined(SN8608M_LE) || defined(SN8616M_LE)|| defined(SN9234H1)
			u32Width = PRV_BT1120_SIZE_W;
			u32Height = PRV_BT1120_SIZE_H;
#else
			u32Width = PRV_SINGLE_SCREEN_W;
			u32Height = PRV_SINGLE_SCREEN_H;
#endif

			u32Width = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Width;
			u32Height = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Height;
			x = u32Width * pstPoint->x / s_u32GuiWidthDflt; //对应D1屏幕坐标X
			y = u32Height * pstPoint->y / s_u32GuiHeightDflt; //对应D1屏幕坐标Y
			w = u32Width * 5/(u32Ratio+4);					//放大矩形框宽度
			h = u32Height * 5/(u32Ratio+4); 				//放大矩形框高度
			stVoZoomAttr.stZoomRect.s32X		= (((x + w) > u32Width) ? (u32Width -w) : x) & (~0x01);;	//调整x位置，超过D1宽度要退步,2像素对其
			stVoZoomAttr.stZoomRect.s32Y		= (((y + h) > u32Height) ? (u32Height -h) : y) & (~0x01);		//调整y位置，超过D1高度要退步,2像素对其
			stVoZoomAttr.stZoomRect.u32Width	= w & (~0x01);
			stVoZoomAttr.stZoomRect.u32Height	= h & (~0x01);
            stVoZoomAttr.enField = VIDEO_FIELD_FRAME;	
       
			CHECK_RET(HI_MPI_VO_SetZoomInWindow(s_VoDevCtrlDflt, VoChn, &stVoZoomAttr));
	
		}
		
#else	
	
	
		//VoChn = s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn;//暂时忽略参数VoChn
		int index = 0;
		VoChn = stPlayInfo.ZoomChn;
		//index = PRV_GetVoChnIndex(VoChn);
		index = stPlayInfo.ZoomChn;
		if(index < 0)
			RET_FAILURE("------ERR: Invalid Index!");
		if(VoChn < 0 || VoChn >= PRV_VO_CHN_NUM
			|| u32Ratio < PRV_MIN_ZOOM_IN_RATIO || u32Ratio > PRV_MAX_ZOOM_IN_RATIO)
		{
			RET_FAILURE("Invalid Parameter: u32Ratio or VoChn");
		}
		
		if(NULL == pstPoint)
		{
			RET_FAILURE("NULL Pointer!!");
		}
		
		if (stPlayInfo.IsZoom==0)
		{
			RET_FAILURE("NOT in [zoom in ctrl] stat!!!");
		}
	//	if (VoChn != s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn)
		{
	//		RET_FAILURE("VoChn NOT match current VoChn!!!");
		}
		//printf("vochn=%d, u32ratio=%f, pstPoint->x= %d, pstPoint->y=%d===========================\n",VoChn,u32Ratio,pstPoint->x, pstPoint->y);
	
		VoChn = stPlayInfo.ZoomChn;
		VPSS_GRP VpssGrp = VoChn;
		VPSS_CROP_INFO_S stVpssCropInfo;
		HI_S32 w = 0, h = 0, x = 0, y = 0, s32X = 0, s32Y = 0;
		HI_U32 u32Width = 0, u32Height = 0, u32W = 0, u32H = 0;
		u32Width = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Width;
		u32Height = s_astVoDevStatDflt[s_VoDevCtrlDflt].stVideoLayerAttr.stImageSize.u32Height;
		w = u32Width/sqrt(u32Ratio);
		h = u32Height/sqrt(u32Ratio);
		stVpssCropInfo.bEnable = HI_TRUE;
	
		VO_CHN_ATTR_S stVoChnAttr;	
		CHECK_RET(HI_MPI_VO_GetChnAttr(s_VoDevCtrlDflt, VoChn, &stVoChnAttr));
		s32X = stVoChnAttr.stRect.s32X;
		s32Y = stVoChnAttr.stRect.s32Y;
		u32W = stVoChnAttr.stRect.u32Width;
		u32H = stVoChnAttr.stRect.u32Height;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VoChn: %d, s32X=%d, s32Y=%d, u32W=%d, u32H=%d\n", VoChn, s32X, s32Y, u32W, u32H);
		
	#if 1
		//x = u32Width * pstPoint->x / s_u32GuiWidthDflt; //对应D1屏幕坐标X
		//y = u32Height * pstPoint->y / s_u32GuiHeightDflt; //对应D1屏幕坐标Y
		//w = u32Width * 5/(u32Ratio+4);					//放大矩形框宽度
		//h = u32Height * 5/(u32Ratio+4);				//放大矩形框高度
		//x = pstPoint->x;
		//y = pstPoint->y;
		//w = u32Width/sqrt(u32Ratio);					//放大矩形框宽度
		//h = u32Height/sqrt(u32Ratio); 				//放大矩形框高度
		//走廊模式下电子放大
		if(u32H == u32Height && u32W != u32Width)
		{
			if(pstPoint->x < s32X)
			{
				if(pstPoint->x + w <= s32X)
				{
					CHECK(PRV_VPSS_Stop(VpssGrp));
					CHECK(PRV_VPSS_Start(VpssGrp));
					return 0;
				}
				else if(pstPoint->x + w <= s32X + u32W)
				{
					x = 0;
					y = pstPoint->y * VochnInfo[index].VideoInfo.height/u32Height;
					w = (w - (s32X - pstPoint->x)) * VochnInfo[index].VideoInfo.width/u32W;
					h = VochnInfo[index].VideoInfo.height/sqrt(u32Ratio);
				}
				else
				{
					x = 0;
					y = pstPoint->y * VochnInfo[index].VideoInfo.height/u32Height;
					w = u32W * VochnInfo[index].VideoInfo.width/u32W;
					h = VochnInfo[index].VideoInfo.height/sqrt(u32Ratio);
				}
			}
			else if(pstPoint->x >= s32X && pstPoint->x <= s32X + u32W)
			{
				if(pstPoint->x + w <= s32X + u32W)
				{
					x = (pstPoint->x - s32X) * VochnInfo[index].VideoInfo.width/u32W;
					y = pstPoint->y * VochnInfo[index].VideoInfo.height/u32Height;
					w = VochnInfo[index].VideoInfo.width * w/u32W;
					h = VochnInfo[index].VideoInfo.height/sqrt(u32Ratio);
	
				}
				else
				{
					x = (pstPoint->x - s32X) * VochnInfo[index].VideoInfo.width/u32W;
					y = pstPoint->y * VochnInfo[index].VideoInfo.height/u32Height;
					w = (u32W - (pstPoint->x - s32X)) * VochnInfo[index].VideoInfo.width/u32W;
					h = VochnInfo[index].VideoInfo.height/sqrt(u32Ratio);
				}
			}
			else
			{
				CHECK(PRV_VPSS_Stop(VpssGrp));
				CHECK(PRV_VPSS_Start(VpssGrp));
				return 0;
			}
			
		}
		else if(u32W == u32Width && u32H != u32Height)
		{
			if(pstPoint->y < s32Y)
			{
				if(pstPoint->y + h <= s32Y)
				{
					CHECK(PRV_VPSS_Stop(VpssGrp));
					CHECK(PRV_VPSS_Start(VpssGrp));
					return 0;
				}
				else if(pstPoint->y + h <= s32Y + u32H)
				{
					x = pstPoint->x * VochnInfo[index].VideoInfo.width/u32Width;
					y = 0;
					w = VochnInfo[index].VideoInfo.width/sqrt(u32Ratio);
					h = (h - (s32Y - pstPoint->y)) * VochnInfo[index].VideoInfo.height/u32Height;
				}
				else
				{
					x = pstPoint->x * 1000 /u32Width;
					y = 0;
					w = VochnInfo[index].VideoInfo.width/sqrt(u32Ratio);
					h = u32H * VochnInfo[index].VideoInfo.height/u32H;
				}
	
			}
			else if(pstPoint->y >= s32Y && pstPoint->y <= s32Y + u32H)
			{
				if(pstPoint->y + h <= s32Y + u32H)
				{
					x = pstPoint->x * VochnInfo[index].VideoInfo.width/u32Width;
					y = (pstPoint->y - s32Y) * VochnInfo[index].VideoInfo.height/u32H;
					w = VochnInfo[index].VideoInfo.width * h/u32H;
					h = VochnInfo[index].VideoInfo.height/sqrt(u32Ratio);
				}
				else
				{
					x = pstPoint->x * VochnInfo[index].VideoInfo.width/u32Width;
					y = (pstPoint->y - s32Y) * VochnInfo[index].VideoInfo.height/u32H;
					w = VochnInfo[index].VideoInfo.width/sqrt(u32Ratio);
					h = (u32H - (pstPoint->y - s32Y)) * VochnInfo[index].VideoInfo.height/u32H;
				}
			}
			else
			{
				CHECK(PRV_VPSS_Stop(VpssGrp));
				CHECK(PRV_VPSS_Start(VpssGrp));
				return 0;
			}
		}
		else
		{
			x = pstPoint->x * VochnInfo[index].VideoInfo.width/u32Width;
			y = pstPoint->y * VochnInfo[index].VideoInfo.height/u32Height;
			w = VochnInfo[index].VideoInfo.width/sqrt(u32Ratio);
			h = VochnInfo[index].VideoInfo.height/sqrt(u32Ratio);
		}
		x = ((x + w) > VochnInfo[index].VideoInfo.width) ? (VochnInfo[index].VideoInfo.width - w) : x;
		y = ((y + h) > VochnInfo[index].VideoInfo.height) ? (VochnInfo[index].VideoInfo.height - h) : y;
		//printf("pstPoint->x: %d, pstPoint->y: %d\n", pstPoint->x, pstPoint->y);
		x = ((x + w) > VochnInfo[index].VideoInfo.width) ? (VochnInfo[index].VideoInfo.width - w) : x;
		y = ((y + h) > VochnInfo[index].VideoInfo.height) ? (VochnInfo[index].VideoInfo.height - h) : y;
		w = w >= 32 ? w : 32;
		h = h >= 32 ? h : 32;
		x = ALIGN_BACK(x, 4);//起始点为4的整数倍，宽高为16的整数倍
		y = ALIGN_BACK(y, 4);
		w = ALIGN_BACK(w, 16);
		h = ALIGN_BACK(h, 16);
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "11111111u32Ratio: %u, x: %d, y: %d, w: %d, h: %d===width: %d, height: %d\n", u32Ratio, x, y, w, h, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height);
		stVpssCropInfo.enCropCoordinate = VPSS_CROP_ABS_COOR;
		stVpssCropInfo.stCropRect.s32X = x;
		stVpssCropInfo.stCropRect.s32Y = y;
		stVpssCropInfo.stCropRect.u32Width = w;
		stVpssCropInfo.stCropRect.u32Height = h;
	#else
	#if 0
		if(u32H == u32Height && u32W != u32Width)
		{
			if(pstPoint->x < s32X)
			{
				if(pstPoint->x + w <= s32X)
				{
					CHECK(PRV_VPSS_Stop(VpssGrp));
					CHECK(PRV_VPSS_Start(VpssGrp));
					return 0;
				}
				else if(pstPoint->x + w <= s32X + u32W)
				{
					x = s32X * u32W/u32Width;
					y = pstPoint->y;
					w = w - (s32X - pstPoint->x);
				}
				else
				{
					x = s32X;
					y = pstPoint->y;
					w = u32W;
				}
			}
			else if(pstPoint->x >= s32X && pstPoint->x <= s32X + u32W)
			{
				if(pstPoint->x + w <= s32X + u32W)
				{
					x = (pstPoint->x - s32X);
					y = pstPoint->y;
					w = u32W/sqrt(u32Ratio);
					h = u32H/sqrt(u32Ratio);
				}
				else
				{
					x = (pstPoint->x - s32X);
					y = pstPoint->y;
					w = u32W - (pstPoint->x - s32X);
				}
			}
			else
			{
				CHECK(PRV_VPSS_Stop(VpssGrp));
				CHECK(PRV_VPSS_Start(VpssGrp));
				return 0;
			}
		}
		else if(u32W == u32Width && u32H != u32Height)
		{
			if(pstPoint->y < s32Y)
			{
				if(pstPoint->y + h <= s32Y)
				{
					CHECK(PRV_VPSS_Stop(VpssGrp));
					CHECK(PRV_VPSS_Start(VpssGrp));
					return 0;
				}
				else if(pstPoint->y + h <= s32Y + u32H)
				{
					y = s32Y * 1000/u32Height;
					x = pstPoint->x * 1000 /u32Width;
					h = h - (s32Y - pstPoint->y);
				}
				else
				{
					y = s32Y * 1000/u32Height;
					x = pstPoint->x * 1000 /u32Width;
					h = u32H;
				}
	
			}
			else if(pstPoint->y >= s32Y && pstPoint->y <= s32Y + u32H)
			{
				if(pstPoint->y + h <= s32Y + u32H)
				{
					x = pstPoint->x * 1000/u32Width;
					y = pstPoint->y * 1000 /u32Height;
					w = u32W/sqrt(u32Ratio);
					h = u32H/sqrt(u32Ratio);
				}
				else
				{
					x = pstPoint->y * 1000/u32Width;
					y = pstPoint->y * 1000 /u32Height;
					h = u32H - (pstPoint->y - s32Y);
				}
	
			}
			else
			{
				CHECK(PRV_VPSS_Stop(VpssGrp));
				CHECK(PRV_VPSS_Start(VpssGrp));
				return 0;
			}
		}
		else
	#endif
		{
			x = pstPoint->y * 1000 /u32Width;
			y = pstPoint->y * 1000 /u32Height;
			w = u32Width/sqrt(u32Ratio);
			h = u32Height/sqrt(u32Ratio);
		}
		//printf("11111111u32Ratio: %f, x: %d, y: %d, w: %d, h: %d\n", u32Ratio, x, y, w, h);
		
		x = x > 999 ? 999 : x;
		y = y > 999 ? 999 : y;
		x = ALIGN_BACK(x, 4);//起始点为4的整数倍，宽高为16的整数倍
		y = ALIGN_BACK(y, 4);
		w = ALIGN_BACK(w, 16);
		h = ALIGN_BACK(h, 16);
		stVpssCropInfo.enCropCoordinate = VPSS_CROP_RITIO_COOR;
		stVpssCropInfo.stCropRect.s32X = x;
		stVpssCropInfo.stCropRect.s32Y = y;
		stVpssCropInfo.stCropRect.u32Width = w;
		stVpssCropInfo.stCropRect.u32Height = h;
	#endif
#if defined(Hi3535)
		CHECK_RET(HI_MPI_VPSS_SetGrpCrop(VpssGrp, &stVpssCropInfo));
		CHECK_RET(HI_MPI_VO_RefreshChn(VHD0, VoChn));
#else
		stVpssCropInfo.enCapSel = VPSS_CAPSEL_BOTH;
		CHECK_RET(HI_MPI_VPSS_SetCropCfg(VpssGrp, &stVpssCropInfo));
		CHECK_RET(HI_MPI_VO_ChnRefresh(0, VoChn));
#endif
#endif	
		RET_SUCCESS("Chn Zoom in!");
	}


/*************************************************
Function: //PRV_ExitChnCtrl
Description://退出通道控制状态
Calls: 
Called By: //
Input: // s32Flag: 通道状态
Output: // 返回系统定义的错误码
Return: //详细参看文档的错误码
Others: // 其它说明
************************************************************************/
STATIC HI_S32 Playback_ExitZoomChn(SN_MSG *msg_req)
{
        ZoomRsp Rsp;
		Rsp.result=0;
#if defined(SN9234H1)
     PRV_MccPBCtlReq Mcc_req;
#endif
	//	unsigned int is_double=DISP_DOUBLE_DISP;
        g_PlayInfo stPlayInfo;
	    PRV_GetPlayInfo(&stPlayInfo);
		if (stPlayInfo.IsZoom==0)
		{
			RET_FAILURE("NOT in ctrl or pb stat or pic stat!!!");
		}
		#if defined(SN9234H1)
				VO_ZOOM_RATIO_S stZoomRatio = {0};
#if defined(SN6108) || defined(SN8608D) || defined(SN8608M) || defined(SN6104) || defined(SN8604D) || defined(SN8604M)
				HI_MPI_VO_SetZoomInRatio(s_VoDevCtrlDflt, s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn, &stZoomRatio);
#else
				HI_MPI_VO_SetZoomInRatio(HD, stPlayInfo.ZoomChn, &stZoomRatio);
				//HI_MPI_VO_SetZoomInRatio(AD, s_astVoDevStatDflt[s_VoDevCtrlDflt].s32CtrlChn, &stZoomRatio);
#endif
				s_State_Info.g_zoom_first_in = HI_FALSE;
#else
				int index = stPlayInfo.ZoomChn;
				VPSS_GRP VpssGrp = VochnInfo[index].VoChn;
				//重启VPSS GROP，以清除电子放大时设置的属性
				
#if defined(Hi3535)
				CHECK(HI_MPI_VO_HideChn(s_VoDevCtrlDflt, VochnInfo[index].VoChn));
				CHECK(PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn, VpssGrp));
				CHECK(PRV_VO_UnBindVpss(s_VoDevCtrlDflt, VochnInfo[index].VoChn, VpssGrp, VPSS_BSTR_CHN));
#else
				CHECK(HI_MPI_VO_ChnHide(s_VoDevCtrlDflt, VochnInfo[index].VoChn));
				CHECK(PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn, VpssGrp));
				CHECK(PRV_VO_UnBindVpss(s_VoDevCtrlDflt, VochnInfo[index].VoChn, VpssGrp, VPSS_PRE0_CHN));
#endif
				CHECK(PRV_VPSS_Stop(VpssGrp));
				CHECK(PRV_VPSS_Start(VpssGrp));
#if defined(Hi3535)
				CHECK(PRV_VO_BindVpss(s_VoDevCtrlDflt, VochnInfo[index].VoChn, VpssGrp, VPSS_BSTR_CHN));
#else
				CHECK(PRV_VO_BindVpss(s_VoDevCtrlDflt, VochnInfo[index].VoChn, VpssGrp, VPSS_PRE0_CHN));
#endif
                PRV_VPSS_ResetWH(VpssGrp,VochnInfo[index].VdecChn,VochnInfo[index].VideoInfo.width,VochnInfo[index].VideoInfo.height);
                CHECK(PRV_VDEC_BindVpss(VochnInfo[index].VdecChn, VpssGrp));
				s_State_Info.g_zoom_first_in = HI_FALSE;
#endif				
			Playback_ZoomInPic(HI_FALSE);
		   stPlayInfo.IsZoom= 0;
		   PRV_SetPlayInfo(&stPlayInfo);
#if defined(SN9234H1)
       if(stPlayInfo.IsSingle)
		   Mcc_req.Single=1;
	   else
	       Mcc_req.Single=stPlayInfo.bISDB;
	   Mcc_req.FullScreenId=stPlayInfo.FullScreenId;
	   if(stPlayInfo.IsSingle)
		   Mcc_req.VoChn=0;
	   else
	       Mcc_req.VoChn=stPlayInfo.DBClickChn;
	   Mcc_req.ImagCount=stPlayInfo.ImagCount;
	   Mcc_req.flag=2;
	   SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_PBZOOM_REQ, &Mcc_req, sizeof(PRV_MccPBCtlReq));
#endif
#if !defined(SN9234H1)
       Playback_RefreshVoDevScreen(s_VoDevCtrlDflt,DISP_DOUBLE_DISP);
#endif
	   SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_EXIT_ZOOMIN_RSP,&Rsp,sizeof(ZoomRsp));
	   RET_SUCCESS("Enter Chn Ctrl!");
	
}



STATIC HI_S32 Playback_MSG_ChnZoomIn(const SN_MSG *msg_req)
{
	Chn_zoom_in_Req *param = (Chn_zoom_in_Req *)msg_req->para;
	
	g_PlayInfo stPlayInfo;
	PRV_GetPlayInfo(&stPlayInfo);
	
	param->chn=stPlayInfo.ZoomChn;
	
	if (NULL == param )
	{
		RET_FAILURE("NULL pointer!!!");
	}
	param->chn=stPlayInfo.ZoomChn;
	//CHECK_RET(PRV_ChnZoomIn(param->dev,param->chn, param->ratio, &param->point));
	CHECK_RET(Playback_ChnZoomIn(param->chn, param->ratio, &param->point));

	RET_SUCCESS("");
}
HI_S32 Playback_ZoomChn(SN_MSG *msg_req)
{
#if defined(SN9234H1)
      PRV_MccPBCtlReq Mcc_req;
#endif
	  Preview_Point stPoint;
	  g_PlayInfo stPlayInfo;
	  PlayBack_Set_ZoomInChn_Req *Req;
	  PlayBack_Set_ZoomInChn_Rsp Rsp;
	 // unsigned int is_double = DISP_DOUBLE_DISP;
	   PRV_GetPlayInfo(&stPlayInfo);
	  Req=(PlayBack_Set_ZoomInChn_Req *)msg_req->para;
	  stPoint.x=Req->x;
	  stPoint.y=Req->y;
	  stPlayInfo.ZoomChn = PRV_GetVoChn_ForPB(stPoint);
	  if(stPlayInfo.IsSingle)
	  	   stPlayInfo.ZoomChn=0;
      PRV_SetPlayInfo(&stPlayInfo);
#if defined(SN9234H1)
	     Mcc_req.flag=1;
		 Mcc_req.Single=1;
		 Mcc_req.FullScreenId=1;
		 Mcc_req.VoChn=stPlayInfo.ZoomChn;
		 Mcc_req.ImagCount=stPlayInfo.ImagCount;
		 SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_PBZOOM_REQ, &Mcc_req, sizeof(PRV_MccPBCtlReq));
#endif
#if !defined(SN9234H1)
	  Playback_RefreshVoDevScreen(s_VoDevCtrlDflt, DISP_DOUBLE_DISP);
	  Playback_ZoomInPic(HI_TRUE);
	  pthread_mutex_unlock(&send_data_mutex);
	  sem_post(&sem_PBGetData);
#endif
	  Rsp.result=0;
	 SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_SET_ZOOMINCHN_RSP,&Rsp,sizeof(PlayBack_Set_ZoomInChn_Rsp));
	 return HI_SUCCESS;
}
#if defined(SN9234H1)
HI_S32 Playback_MCC_ZoomRSP(SN_MSG *msg_rsp)
{
	 PRV_MccPBCtlRsp *Mcc_rsp=(PRV_MccPBCtlRsp *)msg_rsp->para;
	 g_PlayInfo stPlayInfo;
	 PRV_GetPlayInfo(&stPlayInfo);
	 switch(Mcc_rsp->flag)
	 {
        case 0:
			 if(stPlayInfo.IsSingle)
	         {
                Playback_VoDevSingle(s_VoDevCtrlDflt, 0);
	         }
	         else if(stPlayInfo.bISDB)
	         {
                Playback_VoDevSingle(s_VoDevCtrlDflt, stPlayInfo.DBClickChn);
	         }
	         else Playback_VoDevMul(s_VoDevCtrlDflt, stPlayInfo.ImagCount);
			 break;
		case 1:
			 Playback_RefreshVoDevScreen(s_VoDevCtrlDflt, DISP_DOUBLE_DISP);
	         Playback_ZoomInPic(HI_TRUE);
	         pthread_mutex_unlock(&send_data_mutex);
	         sem_post(&sem_PBGetData);
			break;
		case 2:
			Playback_RefreshVoDevScreen(s_VoDevCtrlDflt,DISP_DOUBLE_DISP);
			break;
		default:
			break;
	 }
	 return HI_SUCCESS;
}

HI_S32 Playback_MCC_DBRSP(SN_MSG *msg_rsp)
{
    HI_S32 s32Ret = 0;
	g_PlayInfo stPlayInfo;
	PRV_GetPlayInfo(&stPlayInfo);
    if(stPlayInfo.bISDB==0)
    {
          s32Ret = Playback_VoDevMul(s_VoDevCtrlDflt,stPlayInfo.ImagCount);
		  if (s32Ret != HI_SUCCESS)
	      {
		      return HI_FAILURE;
	      }
	}
	else
	{
          s32Ret = Playback_VoDevSingle(s_VoDevCtrlDflt,stPlayInfo.DBClickChn);
		  if (s32Ret != HI_SUCCESS)
	      {
		      return HI_FAILURE;
	      }
	}
	return HI_SUCCESS;

}
HI_S32 Playback_MCC_FullScrRSP(SN_MSG *msg_rsp)
{
    g_PlayInfo stPlayInfo;
	HI_S32 s32Ret = 0;
	PRV_GetPlayInfo(&stPlayInfo);
	if(stPlayInfo.IsSingle == 1)
    {
           s32Ret = Playback_VoDevSingle(s_VoDevCtrlDflt,stPlayInfo.DBClickChn);
		   if (s32Ret != HI_SUCCESS)
		   {
			   return HI_FAILURE;
		   }
    }
	else if(stPlayInfo.bISDB==0)
	{
            s32Ret = Playback_VoDevMul(s_VoDevCtrlDflt,stPlayInfo.ImagCount);
			 if (s32Ret != HI_SUCCESS)
			 {
				 return HI_FAILURE;
			 }
	}
	else
	{
            s32Ret = Playback_VoDevSingle(s_VoDevCtrlDflt,stPlayInfo.DBClickChn);
			 if (s32Ret != HI_SUCCESS)
			 {
				 return HI_FAILURE;
			 }
	}
	return HI_SUCCESS;

}
#endif

HI_S32 Playback_ZoomEnter(SN_MSG *msg_req)
{
     g_PlayInfo stPlayInfo;
     #if defined(SN9234H1)
    PRV_MccPBCtlReq Mcc_req;
     #endif
	 ZoomRsp Rsp;
     PRV_GetPlayInfo(&stPlayInfo);
     stPlayInfo.IsZoom=1;
	 
	 PRV_SetPlayInfo(&stPlayInfo);
#if !defined(SN9234H1)
	 if(stPlayInfo.IsSingle)
	 {
          Playback_VoDevSingle(s_VoDevCtrlDflt, 0);
	 }
	 else if(stPlayInfo.bISDB)
	 {
          Playback_VoDevSingle(s_VoDevCtrlDflt, stPlayInfo.DBClickChn);
	 }
	 else Playback_VoDevMul(s_VoDevCtrlDflt, stPlayInfo.ImagCount);
#endif	 
	
#if defined(SN9234H1)
		 if(stPlayInfo.IsSingle)
			 Mcc_req.Single=1;
		 else
			Mcc_req.Single=stPlayInfo.bISDB;
		 Mcc_req.FullScreenId=1;
		 if(stPlayInfo.IsSingle)
			 Mcc_req.VoChn=0;
		 else
			Mcc_req.VoChn=stPlayInfo.DBClickChn;
		 Mcc_req.ImagCount=stPlayInfo.ImagCount;
		 Mcc_req.flag=0;
		 SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_PBZOOM_REQ, &Mcc_req, sizeof(PRV_MccPBCtlReq));
#endif
	 Rsp.result=0;
	 SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
								 MSG_ID_PRV_ENTER_ZOOMIN_RSP,&Rsp,sizeof(ZoomRsp));

	 return HI_SUCCESS;
	 
}

HI_S32 Playback_FullScr(SN_MSG *msg_req)
{
#if defined(SN9234H1)
    PRV_MccPBCtlReq Mcc_req;
#endif
	PlaybackFullScreenReq *FullScreenreq=(PlaybackFullScreenReq *)msg_req->para;
	PlaybackFullScreenRsp Rsp;
	HI_S32 s32Ret = 0, GuiVoDev = 0, s32ChnCount = 0;
	HI_U32 u32Width = 0, u32Height = 0;
	g_PlayInfo stPlayInfo;
	VO_VIDEO_LAYER_ATTR_S pstLayerAttr;
	UINT8 IsSingle;
	UINT8 IsDB;
	UINT8 DB_Vo;
    PRV_GetPlayInfo(&stPlayInfo);
	IsSingle=stPlayInfo.IsSingle;
	IsDB=stPlayInfo.bISDB;
	DB_Vo=stPlayInfo.DBClickChn;

    PRV_GetGuiVo(&GuiVoDev);
    s32Ret = HI_MPI_VO_GetVideoLayerAttr(GuiVoDev, &pstLayerAttr);
	s32ChnCount = stPlayInfo.ImagCount;
	u32Width = pstLayerAttr.stImageSize.u32Width;
	u32Height = pstLayerAttr.stImageSize.u32Height;
	
	if(FullScreenreq->FullScreenId==1)
	{
            stPlayInfo.FullScreenId=1;
			Rsp.FullScreenId=1;
			SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_FULLSCREEN_RSP,&Rsp,sizeof(PlaybackFullScreenRsp));
			
	}
	else
	{
            stPlayInfo.FullScreenId=0;
			Rsp.FullScreenId=0;
			SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_FULLSCREEN_RSP,&Rsp,sizeof(PlaybackFullScreenRsp));
			u32Width = u32Width - stPlayInfo.SubWidth;
	        u32Height = u32Height - stPlayInfo.SubHeight;
	}
	PB_Full_id=stPlayInfo.FullScreenId;
	PRV_SetPlayInfo(&stPlayInfo);
#if defined(SN9234H1)
    if(stPlayInfo.IsSingle)
		Mcc_req.Single=1;
	else
	   Mcc_req.Single=stPlayInfo.bISDB;
	Mcc_req.FullScreenId=stPlayInfo.FullScreenId;
	if(stPlayInfo.IsSingle)
		Mcc_req.VoChn=0;
	else
	   Mcc_req.VoChn=stPlayInfo.DBClickChn;
	Mcc_req.ImagCount=stPlayInfo.ImagCount;
	Mcc_req.flag=0;
	 SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_PBFULLSCREEN_REQ, &Mcc_req, sizeof(PRV_MccPBCtlReq));

#endif

#if !defined(SN9234H1)
	if(IsSingle==1)
    {
           s32Ret = Playback_VoDevSingle(GuiVoDev,DB_Vo);
		   if (s32Ret != HI_SUCCESS)
		   {
			   return HI_FAILURE;
		   }
    }
	else if(IsDB==0)
	{
            s32Ret = Playback_VoDevMul(GuiVoDev,s32ChnCount);
			 if (s32Ret != HI_SUCCESS)
			 {
				 return HI_FAILURE;
			 }
	}
	else
	{
            s32Ret = Playback_VoDevSingle(GuiVoDev,DB_Vo);
			 if (s32Ret != HI_SUCCESS)
			 {
				 return HI_FAILURE;
			 }
	}
#endif
    return HI_SUCCESS;
	
}
HI_S32 PlayBack_QueryPbStat(HI_U32 Vochn)
{
      g_ChnPlayStateInfo stPlayStateInfo;
      PRV_GetVoChnPlayStateInfo(Vochn, &stPlayStateInfo);
	  if( stPlayStateInfo.SynState != SYN_NOPLAY&&stPlayStateInfo.SynState != SYN_OVER&&
	  	stPlayStateInfo.CurPlayState != DEC_STATE_STOP && stPlayStateInfo.CurPlayState != DEC_STATE_EXIT&&
	  	VochnInfo[Vochn].VdecChn<30&&VochnInfo[Vochn].VdecChn>=0)
	    {
            return HI_SUCCESS;
		}
	  else return HI_FAILURE;
}



/**********************************************************************************************************************************
函数名:Playback_MsgZoomReq
功能:  单击放大通道处理
输入参数: HI_S32 *ReSetType   定位类型
		  FileInfo *PlaybackFileInfo 回放信息结构体指针
		  SN_MSG *vam_msg_dec    消息结构体指针
		  ST_FMG_QUERY_FILE_RSP *st_file  文件列表指针
		  VIDEO_FRAME_INFO_S *stUserFrameInfo 无录像图片数据指针
输出:    无
***********************************************************************************************************************************/
HI_S32 Playback_DB(SN_MSG *msg_req)
{
	ZoomReq *Req=(ZoomReq *)msg_req->para;
	g_PlayInfo stPlayInfo;
	HI_U32 curVO,mode;
    ZoomRsp Rsp;
	HI_S32 s32Ret = 0, GuiVoDev = 0, s32ChnCount = 0;
	VO_VIDEO_LAYER_ATTR_S pstLayerAttr;
#if defined(SN9234H1)

    PRV_MccPBCtlReq Mcc_req;
#endif
	curVO=Req->ImagID;

    PRV_GetPlayInfo(&stPlayInfo);
	
    PRV_GetGuiVo(&GuiVoDev);
	s32Ret = HI_MPI_VO_GetVideoLayerAttr(GuiVoDev, &pstLayerAttr);
	s32ChnCount = stPlayInfo.ImagCount;
if(stPlayInfo.IsSingle==0)
{
	if(!stPlayInfo.bISDB)//非单通道放大播放
	{   
        if(PlayBack_QueryPbStat(curVO)!=HI_SUCCESS)
	    {
             Rsp.result=-1;
	         SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_PBDBCLICK_RSP,&Rsp,sizeof(ZoomRsp));
			 return HI_FAILURE;

		}
		Rsp.result=0;
	    SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_PBDBCLICK_RSP,&Rsp,sizeof(ZoomRsp));
        mode=stPlayInfo.bISDB;
		stPlayInfo.bISDB=1;
		stPlayInfo.DBClickChn=curVO;
		
	}
    else //单通道放大播放
    {
        mode=stPlayInfo.bISDB;
		Rsp.result=1;
		stPlayInfo.bISDB=0;
	    SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_PBDBCLICK_RSP,&Rsp,sizeof(ZoomRsp));
				
	}

	PRV_SetPlayInfo(&stPlayInfo);
#if defined(SN9234H1)
	Mcc_req.Single=stPlayInfo.bISDB;
	Mcc_req.FullScreenId=stPlayInfo.FullScreenId;
	Mcc_req.VoChn=stPlayInfo.DBClickChn;
	Mcc_req.ImagCount=stPlayInfo.ImagCount;
	Mcc_req.flag=0;
	SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_PBDBCLICK_REQ, &Mcc_req, sizeof(PRV_MccPBCtlReq));
#endif
#if !defined(SN9234H1)
    if(stPlayInfo.bISDB==0)
    {
          s32Ret = Playback_VoDevMul(GuiVoDev,s32ChnCount);
		  if (s32Ret != HI_SUCCESS)
	      {
		      return HI_FAILURE;
	      }
	}
	else
	{
          s32Ret = Playback_VoDevSingle(GuiVoDev,curVO);
		  if (s32Ret != HI_SUCCESS)
	      {
		      return HI_FAILURE;
	      }
	}
#endif 

}
 
	return HI_SUCCESS;
	
}

STATIC HI_S32 PRV_MSG_SetGroupNameCfg(const SN_MSG *msg_req)
{
	GetParameter (PRM_ID_LINKAGE_GROUP_CFG, NULL, g_PrmLinkAge_Cfg,sizeof (g_PrmLinkAge_Cfg), 0, SUPER_USER_ID, NULL);
#if defined(SN9234H1)
	PRV_Check_LinkageGroup(HD,s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode);
	OSD_Update_GroupName();
	Prv_Disp_OSD(HD);
#else
	PRV_Check_LinkageGroup(DHD0,s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode);
	OSD_Update_GroupName();
	Prv_Disp_OSD(DHD0);
#endif
	return 0;
}

STATIC HI_S32 PRV_MSG_SerialNoChange_Ind(const SN_MSG *msg_req)
{
	int ret = 0;
#if defined(SN9234H1)
	ret = PRV_Check_LinkageGroup(HD,s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode);
	if(ret == 1)
	{
		Prv_Disp_OSD(HD);
	}
#else
	ret = PRV_Check_LinkageGroup(DHD0,s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode);
	if(ret == 1)
	{
		Prv_Disp_OSD(DHD0);
	}
#endif
	return 0;
}
/*****************************************************************************************************************************/



/************************************************************************/
/* PRV模块消息线程函数。
                                                                     */
/************************************************************************/
STATIC HI_VOID *PRV_ParseMsgProc(HI_VOID *param)
{

	SN_MSG *msg_req = NULL;//,*msg_req1=NULL;
	

	int queue, ret;
	Log_pid(__FUNCTION__);

	queue = CreatQueque(MOD_PRV);
	if (queue <= 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_PRV: CreateQueue Failed: queue = %d", queue);
		return NULL;
	}
	//PRV_Init_TimeOut(0);
	for (;;)
	{
		msg_req = SN_GetMessage(queue, MSG_GET_WAIT_ROREVER, &ret);
		if (ret < 0)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_PRV: SN_GetMessage Failed: %#x", ret);
			//sleep(1000);
			continue;
		}
		if (NULL == msg_req)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_PRV: SN_GetMessage return Null Pointer!");
			//sleep(1000);
			continue;
		}
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV,"######get msg msg_req->msgId = %x, src:%d ##############\n",msg_req->msgId,msg_req->source);
		pthread_mutex_lock(&send_data_mutex);
		switch(msg_req->msgId)
		{
#if 1
			case MSG_ID_PRV_DISPLAY_TIMEOUT_IND://从片消息定时超时消息
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_DISPLAY_TIMEOUT_IND\n", __LINE__);
				//printf("###############MSG_ID_PRV_DISPLAY_TIMEOUT_IND !!!!msg_req->msgId = %x##########################\n",msg_req->msgId);
				s_State_Info.TimeoutCnt++;
				s_State_Info.bIsTimerState = HI_FALSE;
				PRV_TimeOut_Proc();
				break;
			}
#endif
			case MSG_ID_PRV_GET_REC_OSDRES_IND://获取当前录像分辨率
			{				
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Receive Message: MSG_ID_PRV_GET_REC_OSDRES_IND\n");
				PRV_MSG_GetRec_Resolution(msg_req);
			}
				break;
			case MSG_ID_PRV_LAYOUT_CTRL_REQ:
			{
				//printf("%s Line %d ---------> msg_req->source: %d\n",__func__,__LINE__, msg_req->source);
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "msg_req->source: %d, Receive Message: MSG_ID_PRV_LAYOUT_CTRL_REQ, SlaveConfig: %d\n", msg_req->source, s_State_Info.bIsSlaveConfig);
				if(s_State_Info.bIsSlaveConfig == HI_FALSE)
				{				
					//printf("%s Line %d ---------> here\n",__func__,__LINE__);
					Msg_id_prv_Rsp rsp;
					ret = PRV_MSG_LayoutCtrl(msg_req,&rsp);
					if(ret != SN_SLAVE_MSG)
					{
						SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_FWK, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_LAYOUT_CTRL_RSP, &rsp, sizeof(rsp));
					
					}else
					{
						s_State_Info.bIsSlaveConfig = HI_TRUE;
					}
				}	
#if defined(Hi3531)||defined(Hi3535)
				SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, MSG_ID_PRV_SCREEN_CTRL_IND, NULL, 0);
#endif
			}
				break;				
			case MSG_ID_PRV_SCREEN_CTRL_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_SCREEN_CTRL_REQ, s_State_Info.bIsSlaveConfig: %d\n", __LINE__, s_State_Info.bIsSlaveConfig);


#if (IS_DECODER_DEVTYPE == 1)
				SN_SendMessageEx(msg_req->user, msg_req->source, MOD_SCM, msg_req->xid, msg_req->thread, MSG_ID_PRV_SCREEN_CTRL_REQ, msg_req->para, msg_req->size);
#else				
				Screen_ctrl_Req *param = (Screen_ctrl_Req *)msg_req->para;				
				if(s_State_Info.bIsSlaveConfig == HI_FALSE || param->dev == SPOT_VO_DEV)
				{
					Msg_id_prv_Rsp rsp;
					ret = PRV_MSG_ScreenCtrl(msg_req,&rsp);
					if(ret != SN_SLAVE_MSG)
					{
						//SPOT的切换不需要通知UI
						
						/*SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_SCREEN_CTRL_RSP, &rsp, sizeof(rsp));*/
						if(param->dev != SPOT_VO_DEV)
						{
							SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
								MSG_ID_PRV_SCREEN_CTRL_IND, NULL, 0);
						}
						
					}else
					{
						s_State_Info.bIsSlaveConfig = HI_TRUE;
					}
				}
#endif
				break;
			}	
				
			case MSG_ID_PRV_ENTER_CHN_CTRL_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_ENTER_CHN_CTRL_REQ, SlaveConfig: %d\n", __LINE__, s_State_Info.bIsSlaveConfig);
				if(s_State_Info.bIsSlaveConfig == HI_FALSE)
				{
					Msg_id_prv_Rsp rsp;
					ret = PRV_MSG_EnterChnCtrl(msg_req, &rsp);
					if(ret != SN_SLAVE_MSG)
					{
						SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_FWK, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_ENTER_CHN_CTRL_RSP, &rsp, sizeof(rsp));
					}
					else
					{
						s_State_Info.bIsSlaveConfig = HI_TRUE;
					}
					
				}
#if defined(Hi3531)||defined(Hi3535)				
				pthread_mutex_unlock(&send_data_mutex);
				usleep(10000);
				pthread_mutex_lock(&send_data_mutex);
				
				g_ChnPlayStateInfo stPlayStateInfo;
				g_PlayInfo	stPlayInfo;
				PRV_GetPlayInfo(&stPlayInfo);
				PRV_GetVoChnPlayStateInfo(stPlayInfo.InstantPbChn, &stPlayStateInfo);
				if(stPlayInfo.PlayBackState == PLAY_INSTANT && stPlayStateInfo.CurPlayState == DEC_STATE_NORMALPAUSE)
				{
#if defined(Hi3535)
					HI_MPI_VO_PauseChn(0, stPlayInfo.InstantPbChn);
#else
					HI_MPI_VO_ChnPause(0, stPlayInfo.InstantPbChn);
#endif
					if(Achn == stPlayInfo.InstantPbChn)
					{
#if defined(Hi3531)
						HI_MPI_AO_PauseChn(4, AOCHN);
#else
						HI_MPI_AO_PauseChn(0, AOCHN);
#endif
						HI_MPI_ADEC_ClearChnBuf(DecAdec);
					}
				}
#endif				
			}
				break;
			case MSG_ID_PRV_EXIT_CHN_CTRL_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_EXIT_CHN_CTRL_REQ\n", __LINE__);
				Exit_chn_ctrl_Req *param = (Exit_chn_ctrl_Req *)msg_req->para;
				//预览状态下，收到退出回放控制消息不处理
#if defined(SN9234H1)
				if(s_astVoDevStatDflt[HD].enPreviewStat == PRV_STAT_NORM
					&& (param->flag == 4 || param->flag == SLC_CTL_FLAG || param->flag == PIC_CTL_FLAG))
#else				
				if(s_astVoDevStatDflt[DHD0].enPreviewStat == PRV_STAT_NORM
					&& (param->flag == 4 || param->flag == SLC_CTL_FLAG || param->flag == PIC_CTL_FLAG))
#endif					
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Current Stat: PRV_STAT_NORM, Discard MSG---param->flag: %d\n", param->flag);					
					break;
				}
				//if(s_State_Info.bIsSlaveConfig == HI_FALSE)
				{
					Msg_id_prv_Rsp rsp;
					ret = PRV_MSG_ExitChnCtrl(msg_req,&rsp);
				
					if(ret != SN_SLAVE_MSG)
					{
						if(rsp.flag == 4)
						{
							Exit_chn_ctrl_Req ExitPb;
							SN_MEMSET(&ExitPb, 0, sizeof(Exit_chn_ctrl_Req));
							ExitPb.flag = 4;
							SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_SCM, msg_req->xid, msg_req->thread, MSG_ID_PRV_EXIT_CHN_CTRL_REQ, &ExitPb, sizeof(Exit_chn_ctrl_Req));
						}
						else
						{
							SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
								MSG_ID_PRV_EXIT_CHN_CTRL_RSP, &rsp, sizeof(rsp));
						}
						if(s_astVoDevStatDflt[s_VoDevCtrlDflt].bIsAlarm)
						{
							SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
										MSG_ID_PRV_SCREEN_CTRL_IND, NULL, 0);
						}
					}
					else
					{
						s_State_Info.bIsSlaveConfig = HI_TRUE;
					}
				}
			}
				break;
			case MSG_ID_PRV_SET_VO_PREVIEW_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_SET_VO_PREVIEW_REQ\n", __LINE__);
				Set_vo_preview_Rsp rsp;
				PRV_MSG_SetVoPreview(msg_req, &rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_SET_VO_PREVIEW_RSP, &rsp, sizeof(rsp));
			}
				break;
			case MSG_ID_PRV_SET_GENERAL_CFG_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, The message is :MSG_ID_PRV_SET_GENERAL_CFG_REQ, bIsInit=%d\n", __LINE__, s_State_Info.bIsInit);
#if 1
				if(s_State_Info.bIsInit == 0)
				{
					Set_general_cfg_Rsp rsp;
					PRV_MSG_SetGeneralCfg(msg_req, &rsp);
#if defined(SN9234H1)
					PRV_ResetVoDev(HD);
#else					
					PRV_ResetVoDev(DHD0);
#endif					
					//PRV_ResetVoDev(s_VoSecondDev);
					PRV_SetGuiAlpha(0, 0);
					SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
						MSG_ID_PRV_SET_GENERAL_CFG_RSP, &rsp, sizeof(rsp));
					Fb_clear_step2();
					ret = OSD_init(s_OSD_Time_type);
					if(ret == 0)
					{
						s_State_Info.bIsOsd_Init = 1;
					}	
					if(s_State_Info.bIsVam_rsp == 0)
					{
						SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_VAM, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_OSD_GETRECT_RSP, NULL, 0);
					}
#if defined (SN6116HE) || defined (SN6116LE) || defined (SN6108HE)	 || defined(SN6108LE)	 || defined(SN8608D_LE) || defined(SN8608M_LE) || defined(SN8616D_LE) || defined(SN8616M_LE)|| defined(SN9234H1)
					PRV_Init_TimeOut(0);
#endif
					s_State_Info.bIsInit = 1;	//预览已经初始化
					
				}
				else
				{
#if 1				
					Set_general_cfg_Rsp rsp;
					//通用配置前把标志位清零
					s_State_Info.bIsNpfinish = 0;

					//SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_VAM, msg_req->xid, msg_req->thread, 
					//		MSG_ID_PRV_SET_GENERAL_CFG_REQ, msg_req->para,msg_req->size);
					
					ret = PRV_MSG_SetGeneralCfg(msg_req, &rsp);
					if(ret == SN_SLAVE_MSG)
					{//如果需要发送消息给从片
						s_State_Info.bIsSlaveConfig = HI_TRUE;	//置位配置标志位
						break;
					}
					PRV_SetGuiAlpha(0, 0);
					s_State_Info.bIsNpfinish = 1;
					if(msg_req->user == SUPER_USER_ID)
					{
						SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_FWK, msg_req->xid, msg_req->thread, 
								MSG_ID_PRV_SET_GENERAL_CFG_RSP, &rsp, sizeof(rsp));
					}
					else
					{
						SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
								MSG_ID_PRV_SET_GENERAL_CFG_RSP, &rsp, sizeof(rsp));
					}
#else					
					int cnt=0,s32Flag=0;
					Set_general_cfg_Req *req = (Set_general_cfg_Req *)msg_req->para;
					
					while(cnt<1000000)
					{
						s32Flag = (0 == s32Flag) ? 1 : 0;
						req->general_info.CVBSOutputType = s32Flag;
						SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_VAM, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_SET_GENERAL_CFG_REQ, msg_req->para,msg_req->size);
						msg_req1 = SN_GetMessage(queue, MSG_GET_WAIT_ROREVER, &ret);
						switch(msg_req1->msgId)
						{
							case MSG_ID_PRV_SET_GENERAL_CFG_RSP:
							{
								Set_general_cfg_Rsp rsp,*rsp1 = (Set_general_cfg_Rsp *)msg_req1->para;
								printf("#############req->general_info.CVBSOutputType =%d,CNT=%d\n",rsp1->general_info.CVBSOutputType,cnt);
								ret = PRV_MSG_SetGeneralCfg(msg_req1, &rsp);
								if(ret == SN_SLAVE_MSG)
								{//如果需要发送消息给从片
									s_State_Info.bIsSlaveConfig = HI_TRUE;	//置位配置标志位
									break;
								}
								/*如果回复成功，表示制式已修改，那么发送消息给VAM模块*/
								if(rsp1->result == 0)
								{
									if (rsp.result == 0)
									{
										SN_SendMessageEx(msg_req1->user, MOD_PRV, MOD_VAM, msg_req1->xid, msg_req1->thread, 
											MSG_ID_PRV_VI_START_REQ, NULL, 0);
									}
								}
								SN_SendMessageEx(msg_req1->user, MOD_PRV, MOD_DRV, msg_req1->xid, msg_req1->thread, 
										MSG_ID_PRV_SET_GENERAL_CFG_RSP, &rsp, sizeof(rsp));
							}
								break;
							default:
								break;
						}
						SN_FreeMessage(&msg_req1);
						sleep(3);
						cnt++;
					}
#endif					
				}
#else
					
				Set_general_cfg_Rsp rsp;
				PRV_MSG_SetGeneralCfg((Set_general_cfg_Req *)msg_req->para, &rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_SET_GENERAL_CFG_RSP, &rsp, sizeof(rsp));
					if(s_State_Info.bIsOsd_Init == 0)
					{
						OSD_init(s_OSD_Time_type);
						s_State_Info.bIsOsd_Init = 1;
				}
#endif
			}
				break;
			case MSG_ID_PRV_SET_CHN_CFG_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_SET_CHN_CFG_REQ\n", __LINE__);
				Set_chn_cfg_Rsp rsp;				
				Set_chn_cfg_Req *param = (Set_chn_cfg_Req *)msg_req->para;
				ret = PRV_MSG_SetChnCfg(param, &rsp);    
				if(ret != SN_SLAVE_MSG)
				{
					SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
						MSG_ID_PRV_SET_CHN_CFG_RSP, &rsp, sizeof(rsp));
				}
				else
				{
					s_State_Info.bIsSlaveConfig = HI_TRUE;
				}
				
			}
				break;
			case MSG_ID_PRV_SET_CHN_COVER_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_SET_CHN_COVER_REQ\n", __LINE__);
				Set_chn_cover_Rsp rsp;
				ret = PRV_MSG_SetChnCover(msg_req, &rsp);
				if(ret != SN_SLAVE_MSG)
				{
					SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
						MSG_ID_PRV_SET_CHN_COVER_RSP, &rsp, sizeof(rsp));
				}
				else
				{
					s_State_Info.bIsSlaveConfig = HI_TRUE;
				}
				
			}
				break;
			case MSG_ID_PRV_SET_CHN_OSD_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_SET_CHN_OSD_REQ\n", __LINE__);
				Set_chn_osd_Rsp rsp;
				ret = PRV_MSG_SetChnOsd(msg_req, &rsp);
				if(ret != SN_SLAVE_MSG)
				{
					SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
						MSG_ID_PRV_SET_CHN_OSD_RSP, &rsp, sizeof(rsp));
				}
				else
				{
					s_State_Info.bIsSlaveConfig = HI_TRUE;
				}
#if 0 /* 已改成由PRV_OSD_RECT_CHANGE_NOTIFY接口通知VAM模块 */
				SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_VAM, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_SET_CHN_OSD_RSP, &rsp, sizeof(rsp));
#endif
			}
				break;
			case MSG_ID_PRV_SET_CHN_DISPLAY_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_SET_CHN_DISPLAY_REQ\n", __LINE__);
				Set_chn_display_Rsp rsp;
				PRV_MSG_SetChnDisplay((Set_chn_display_Req *)msg_req->para, &rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_SET_CHN_DISPLAY_RSP, &rsp, sizeof(rsp));
			}
				break;
			case MSG_ID_PRV_CHN_ICON_CTRL_REQ:
			{
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_CHN_ICON_CTRL_REQ\n", __LINE__);
				Msg_id_prv_Rsp rsp;
				PRV_MSG_ChnIconCtrl((Chn_icon_ctrl_Req *)msg_req->para, &rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_CHN_ICON_CTRL_RSP, &rsp, sizeof(rsp));
			}
				break;
				
			case MSG_ID_PRV_OUTPUT_CHANGE_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_OUTPUT_CHANGE_REQ\n", __LINE__);
				Msg_id_prv_Rsp rsp;
				PRV_MSG_OutputChange((Output_Change_Req *)msg_req->para, &rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_OUTPUT_CHANGE_RSP, &rsp, sizeof(rsp));
			}
				break;
				
			case MSG_ID_PRV_CHN_ZOOM_IN_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_CHN_ZOOM_IN_REQ\n", __LINE__);
				Msg_id_prv_Rsp rsp;
				ret = PRV_MSG_ChnZoomIn(msg_req,&rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_CHN_ZOOM_IN_RSP, &rsp, sizeof(rsp));
			}
				break;
				
			case MSG_ID_PRV_GET_GUI_VO_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_GET_GUI_VO_REQ\n", __LINE__);
				Get_gui_vo_Rsp rsp;
				PRV_MSG_GetGuiVo(&rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_GET_GUI_VO_RSP, &rsp, sizeof(rsp));
			}
				break;
				
			case MSG_ID_PRV_CHN_DISPLAY_CHANGE_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_CHN_DISPLAY_CHANGE_REQ\n", __LINE__);
				Msg_id_prv_Rsp	rsp;
				PRV_MSG_SetDisplay_Time((Chn_disp_change_Req *)msg_req->para, &rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_CHN_DISPLAY_CHANGE_RSP, &rsp, sizeof(rsp));
			}
				break;
				
			case MSG_ID_PRV_GET_CHN_BY_XY_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_GET_CHN_BY_XY_REQ\n", __LINE__);
				Get_chn_by_xy_Rsp	rsp;
				PRV_MSG_GetChnByXY((Get_chn_by_xy_Req *)msg_req->para, &rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_GET_CHN_BY_XY_RSP, &rsp, sizeof(rsp));
			}
				break;
				
			case MSG_ID_PRV_GET_PRV_MODE_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_GET_PRV_MODE_REQ\n", __LINE__);
				Get_prv_mode_Rsp rsp;
				PRV_MSG_GetPrvMode(&rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_GET_PRV_MODE_RSP, &rsp, sizeof(rsp));
			}
				break;
				
			case MSG_ID_PRV_OSD_GETRECT_REQ:
			{//回复录像模块开启移动侦测
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_OSD_GETRECT_REQ\n", __LINE__);
				if(s_State_Info.bIsInit)
				{//预览初始化完成后，发送消息
					SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
						MSG_ID_PRV_OSD_GETRECT_RSP, NULL, 0);
				}
				else
				{//预览初始化未完成，置位回复标志位
					s_State_Info.bIsVam_rsp = 0;
				}
			}
				break;
				
			case MSG_ID_PRV_SET_GENERAL_CFG_RSP:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_SET_GENERAL_CFG_RSP\n", __LINE__);
				Set_general_cfg_Rsp rsp,*rsp1 = (Set_general_cfg_Rsp *)msg_req->para;

				//如果回复失败，那么不进行切换
				if(rsp1->result == SN_ERR_VAM_NPSET)
				{
					rsp.result = -1;
				}
				else
				{//其他情况下进行配置
					ret = PRV_MSG_SetGeneralCfg(msg_req, &rsp);
					if(ret == SN_SLAVE_MSG)
					{//如果需要发送消息给从片
						s_State_Info.bIsSlaveConfig = HI_TRUE;	//置位配置标志位
						break;
					}
				}
				s_State_Info.bIsNpfinish = 1;
				
				if(msg_req->user == SUPER_USER_ID)
				{
					SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_FWK, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_SET_GENERAL_CFG_RSP, &rsp, sizeof(rsp));
				}
				else
				{
					SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_DRV, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_SET_GENERAL_CFG_RSP, &rsp, sizeof(rsp));
				}
					
			}
				break;
				
			case MSG_ID_FWK_POWER_OFF_REQ:
			/*{
				int chan;
				chan = s_astVoDevStatDflt[SPOT_VO_DEV].as32ChnOrder[SingleScene][s_astVoDevStatDflt[SPOT_VO_DEV].s32SingleIndex];
		 		if(chan > PRV_CHAN_NUM)
		 		{
					PRV_HostStopPciv(SPOT_PCI_CHAN, MSG_ID_PRV_MCC_SPOT_PREVIEW_STOP_REQ);
		 		}
				if(msg_req->source == MOD_VAM)
				{
					exit_mpp_sys();
				}
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_FWK_POWER_OFF_RSP, NULL, 0);
			
				
			}	
				if(msg_req->source == MOD_VAM)
				{
					SN_FreeMessage(&msg_req);
					return NULL;
				}
				else
					break;*/
			case MSG_ID_FWK_REBOOT_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_FWK_REBOOT_REQ\n", __LINE__);

				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "---------PRV---Begin Exit Sys\n");
					exit_mpp_sys();
				}
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					msg_req->msgId+1, NULL, 0);

				{
					SN_FreeMessage(&msg_req);
					pthread_mutex_unlock(&send_data_mutex);
					return NULL;
				}

			}	
				break;

			case MSG_ID_NTRANS_MEMRESET_REQ:
			{
				int i = 0;
				for(i = 0; i < DEV_CHANNEL_NUM; i++)
				{
					BufferSet(i + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);
					BufferSet(i + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
					VochnInfo[i].bIsWaitIFrame = 1;					
				}
				SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_NTRANS, 0,  0,MSG_ID_NTRANS_MEMRESET_RSP,  NULL, 0);
			}
				break;
			case MSG_ID_FWK_UPGRADE_RSP:     /* 升级 */
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------receive MSG_ID_FWK_UPGRADE_RSP");
				IsUpGrade = 1;
			}
				break;
			case MSG_ID_ALM_EXCEPTION_ALARM_RSP:
				break;
				
			case MSG_ID_PRV_VI_START_RSP:
				break;
				
			case MSG_ID_PRV_SET_PREVIEW_ADV_REQ://预览音频高级配置请求
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_SET_PREVIEW_ADV_REQ\n", __LINE__);
				Set_vo_preview_Adv_Rsp rsp;
				PRV_MSG_SetVoPreview_Adv((Set_vo_preview_Adv_Req *)msg_req->para, &rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_SET_PREVIEW_ADV_RSP, &rsp, sizeof(rsp));
			}	
				break;
				
			case MSG_ID_PRV_SET_PREVIEW_AUDIO_REQ://系统音频配置请求
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_SET_PREVIEW_AUDIO_REQ\n", __LINE__);
				Set_preview_Audio_Rsp rsp;
				PRV_MSG_SetPreview_Audio((Set_preview_Audio_Req *)msg_req->para, &rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_SET_PREVIEW_AUDIO_RSP, &rsp, sizeof(rsp));
			}
				break;
				
			case MSG_ID_PRV_MCC_SLAVE_INIT_RSP: //从片初始化回复
			{
				PRV_MSG_Mcc_Init_Rsp(msg_req);
			}
				break;
				
			case MSG_ID_PRV_MCC_LAYOUT_CTRL_RSP: //从片画面切换回复
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_MCC_LAYOUT_CTRL_RSP\n", __LINE__);
				Msg_id_prv_Rsp rsp;
				PRV_MSG_MCC_LayoutCtrl_Rsp((Prv_Slave_Layout_crtl_Rsp *)msg_req->para,&rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_FWK, msg_req->xid, msg_req->thread, 
						MSG_ID_PRV_LAYOUT_CTRL_RSP, &rsp, sizeof(rsp));
				SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_SCREEN_CTRL_IND, NULL, 0);
			}
				break;
			
#if (IS_DECODER_DEVTYPE == 1)

#else
			case MSG_ID_PRV_MCC_SCREEN_CTRL_RSP://从片下一屏回复
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_MCC_SCREEN_CTRL_RSP\n", __LINE__);
				Msg_id_prv_Rsp rsp;
				PRV_MSG_Mcc_ScreenCtrl_Rsp((Prv_Slave_Screen_ctrl_Rsp *)msg_req->para,&rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_SCREEN_CTRL_RSP, &rsp, sizeof(rsp));
				SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_MMI, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_SCREEN_CTRL_IND, NULL, 0);
			}
				break;
#endif
			case MSG_ID_PRV_MCC_ENTER_CHN_CTRL_RSP:	//从片进入通道控制回复
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_MCC_ENTER_CHN_CTRL_RSP\n", __LINE__);
				Msg_id_prv_Rsp rsp;
				PRV_MSG_MCC_EnterChnCtrl_Rsp((Prv_Slave_Enter_chn_ctrl_Rsp *)msg_req->para,&rsp);
				SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_FWK, msg_req->xid, msg_req->thread, 
						MSG_ID_PRV_ENTER_CHN_CTRL_RSP, &rsp, sizeof(rsp));

				pthread_mutex_unlock(&send_data_mutex);
				usleep(100000);
				pthread_mutex_lock(&send_data_mutex);
				g_ChnPlayStateInfo stPlayStateInfo;
				g_PlayInfo	stPlayInfo;
				PRV_GetPlayInfo(&stPlayInfo);
				PRV_GetVoChnPlayStateInfo(stPlayInfo.InstantPbChn, &stPlayStateInfo);
				if(stPlayInfo.PlayBackState == PLAY_INSTANT && stPlayStateInfo.CurPlayState == DEC_STATE_NORMALPAUSE)
				{
#if defined(Hi3535)
					HI_MPI_VO_PauseChn(0, stPlayInfo.InstantPbChn);
#else
					HI_MPI_VO_ChnPause(0, stPlayInfo.InstantPbChn);
#endif
					if(Achn == stPlayInfo.InstantPbChn)
					{
#if defined(Hi3531)
						HI_MPI_AO_PauseChn(4, AOCHN);
#else
						HI_MPI_AO_PauseChn(0, AOCHN);
#endif
						HI_MPI_ADEC_ClearChnBuf(DecAdec);
					}
				}
			}	
				break;
				
			case MSG_ID_PRV_MCC_EXIT_CHN_CTRL_RSP://从片退出通道控制回复
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_MCC_EXIT_CHN_CTRL_RSP\n", __LINE__);
				Msg_id_prv_Rsp rsp;
				PRV_MSG_MCC_ExitChnCtrl_Rsp((Prv_Slave_Exit_chn_ctrl_Rsp *)msg_req->para,&rsp);
				//回复GUI消息
				if(rsp.flag == 4)//退出回放时通知SCM连接预览
				{					
					Exit_chn_ctrl_Req ExitPb;
					SN_MEMSET(&ExitPb, 0, sizeof(Exit_chn_ctrl_Req));
					ExitPb.flag = 4;
					SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_SCM, msg_req->xid, msg_req->thread, MSG_ID_PRV_EXIT_CHN_CTRL_REQ, &ExitPb, sizeof(Exit_chn_ctrl_Req));
				}
				else
				{
					SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
						MSG_ID_PRV_EXIT_CHN_CTRL_RSP, &rsp, sizeof(rsp));
				}
			}		
				break;
				
			case MSG_ID_PRV_MCC_SET_GENERAL_CFG_RSP:
			{//从片通用配置处理
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_MCC_SET_GENERAL_CFG_RSP\n", __LINE__);
				Set_general_cfg_Rsp rsp;
				Prv_Slave_Set_general_cfg_Rsp *slave_rsp = (Prv_Slave_Set_general_cfg_Rsp *)msg_req->para;
				PRV_MSG_MCC_SetGeneralCfg_Rsp(slave_rsp,&rsp);
				/*如果回复成功，表示制式已修改，那么发送消息给VAM模块*/
				if(slave_rsp->vam_result == 0)
				{
					if (rsp.result == 0)
					{
						SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_VAM, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_VI_START_REQ, NULL, 0);
					}
				}
				//发送消息给驱动适配层
				if(msg_req->user == SUPER_USER_ID)
				{
					SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_FWK, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_SET_GENERAL_CFG_RSP, &rsp, sizeof(rsp));
				}
				else
				{
					SN_SendMessageEx(msg_req->user, MOD_PRV, MOD_DRV, msg_req->xid, msg_req->thread, 
							MSG_ID_PRV_SET_GENERAL_CFG_RSP, &rsp, sizeof(rsp));
				}
			}	
				break;
				
			case MSG_ID_PRV_MCC_SET_CHN_CFG_RSP://从片通道配置回复
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_MCC_SET_CHN_CFG_RSP\n", __LINE__);
				Set_chn_cfg_Rsp rsp;
				PRV_MSG_MCC_SetChnCfg_Rsp((Prv_Slave_Set_chn_cfg_Rsp*)msg_req->para,&rsp);
				//回复GUI消息
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_SET_CHN_CFG_RSP, &rsp, sizeof(rsp));
			}
				break;
				
			case MSG_ID_PRV_MCC_SET_CHN_COVER_RSP://从片遮盖配置回复
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_MCC_SET_CHN_COVER_RSP\n", __LINE__);
				Set_chn_cover_Rsp rsp;
				PRV_MSG_MCC_SetChnCover_Rsp((Prv_Slave_Set_chn_cover_Rsp *)msg_req->para,&rsp);
				//回复GUI消息
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_SET_CHN_COVER_RSP, &rsp, sizeof(rsp));
			}
				break;
				
			case MSG_ID_PRV_MCC_SET_CHN_OSD_RSP: //从片通道录像OSD配置回复
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_MCC_SET_CHN_OSD_RSP\n", __LINE__);
				Set_chn_osd_Rsp rsp;
				PRV_MSG_MCC_SetChnOsd_Rsp((Prv_Slave_Set_chn_osd_Rsp*)msg_req->para,&rsp);
				//回复GUI消息
				SN_SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_PRV_SET_CHN_OSD_RSP, &rsp, sizeof(rsp));
			}
				break;
#if defined(SN9234H1)
			case MSG_ID_PRV_MCC_SPOT_PREVIEW_NEXT_RSP: //SPOT口切换到下一个通道的响应
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_PRV_MCC_SPOT_PREVIEW_NEXT_RSP\n", __LINE__);
				int ch = s_astVoDevStatDflt[SPOT_VO_DEV].as32ChnOrder[SingleScene][s_astVoDevStatDflt[SPOT_VO_DEV].s32SingleIndex];
				//printf("MSG_ID_PRV_MCC_SPOT_PREVIEW_NEXT_RSP:%d\n", ch);
				PRV_start_pciv(ch);
				PRV_RefreshSpotOsd(ch);
			}
				break;				
#endif	
			case MSG_ID_PRV_MCC_UPDATE_OSD_RSP: //从片OSD更新消息回复
		    {
		                //TODO
		    }
	            break;
					
			case MSG_ID_VAM_RCD_CFG_ADC_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_VAM_RCD_CFG_ADC_REQ\n", __LINE__);
			//	VO_CHN chn=0;
				RecAdcParamReq * req = (RecAdcParamReq *)msg_req->para;
				PRV_Set_AudioMap(req->ch_num, req->RecCfg.AudioRecord);
			//	PRV_GetFirstChn(s_VoDevCtrlDflt,&chn);
			//	PRV_AudioPreviewCtrl((const unsigned char *)&chn, 1);			
			//	printf("MSG_ID_VAM_RCD_CFG_ADC_REQ ch == %d, record=%d\n",req->ch_num, req->RecCfg.AudioRecord);
			
				PRV_PlayAudio(s_VoDevCtrlDflt);
			}
				break;
				
            case MSG_ID_FWK_SYSTEM_INITOK_IND:
            {
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_FWK_SYSTEM_INITOK_IND\n", __LINE__);
				s_bIsSysInit = HI_TRUE;
#if defined(SN9234H1)
				PRV_SetDevCsc(HD);
				if(DEV_SPOT_NUM > 0)
				{
					//static HI_S32 PRV_PreviewInit(HI_VOID);				
					PRV_PreviewInit();
		
					//初始化SPOT口
					int chn = 0, index = 0;
					chn = s_astVoDevStatDflt[SPOT_VO_DEV].as32ChnOrder[SingleScene][s_astVoDevStatDflt[SPOT_VO_DEV].s32SingleIndex];
					index = PRV_GetVoChnIndex(chn);
					if(index < 0)
						break;
					//printf("s_astVoDevStatDflt[SPOT_VO_DEV]ch = %d\n", ch);
					//if(ch >= PRV_CHAN_NUM)
					if(VochnInfo[index].SlaveId > 0)
					{
						PRV_InitSpotVo();
						PRV_start_pciv(chn);
						PRV_RefreshSpotOsd(chn);
					}
					else
					{
						PRV_PrevInitSpotVo(chn);
					}	
				}
#else				
				PRV_SetDevCsc(DHD0);
#endif
				
            }
                break;
				
			case MSG_ID_NTRANS_DELUSER_RSP:
			case MSG_ID_NTRANS_ONCEOVER_IND:
			{
				PRV_MSG_OverLinkReq(msg_req);
			}	
				break;
				
			case MSG_ID_PRV_MCC_DESVDEC_RSP:
			{
				PRV_MSG_MCC_DesVdecRsp(msg_req);
			}
				break;
				
			case MSG_ID_PRV_MCC_CREATEVDEC_RSP:
			{
				
				PRV_MSG_MCC_CreateVdecRsp(msg_req);	
			}			
				break;
				
			case MSG_ID_NTRANS_ADDUSER_RSP:
			case MSG_ID_NTRANS_BEGINLINK_IND:
			{
				PRV_MSG_NTRANSLinkReq(msg_req);				
			}			
				break;
			case  MSG_ID_FWK_UPDATE_PARAM_IND:
			{
				stParamUpdateNotify* pstNotify = (stParamUpdateNotify*)msg_req->para;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------line: %d, Receive Message: MSG_ID_FWK_UPDATE_PARAM_IND, pstNotify->prm_id:%d\n", __LINE__, pstNotify->prm_id);
				if(pstNotify->prm_id == PRM_ID_PREVIEW_CFG_EX)
				{
					PRM_PREVIEW_CFG_EX stNewPreviewInfo;
					set_TimeOsd_xy();
#if defined(SN9234H1)
					Prv_Disp_OSD(HD);
#else
					Prv_Disp_OSD(DHD0);
#endif
					if (PARAM_OK == GetParameter(PRM_ID_PREVIEW_CFG_EX, NULL, &stNewPreviewInfo, sizeof(PRM_PREVIEW_CFG_EX), 0, SUPER_USER_ID, NULL))
					{
						if(stNewPreviewInfo.reserve[0]==1)
						{
							OSD_Ctl(0, 1, OSD_TIME_TYPE);
						}
					}
				}
				else if(pstNotify->prm_id == PRM_ID_LINKAGE_GROUP_CFG)
				{
					PRV_MSG_SetGroupNameCfg(msg_req);
				}
			}
				break;
			case MSG_ID_SCM_SERIALNO_CHANGE_IND:
			{
				PRV_MSG_SerialNoChange_Ind(msg_req);
			}
				break;
			case MSG_ID_PRV_ENTER_SETTIMEOSD_IND:
			{
				 OSD_Ctl(0, 0, OSD_TIME_TYPE);
			}
				break;
			case MSG_ID_PRV_EXIT_SETTIMEOSD_IND:
			{
				PRM_PREVIEW_CFG_EX stPreviewInfo;
				if (PARAM_OK == GetParameter(PRM_ID_PREVIEW_CFG_EX, NULL, &stPreviewInfo, sizeof(PRM_PREVIEW_CFG_EX), 0, SUPER_USER_ID, NULL))
				{
					if(stPreviewInfo.reserve[0]==1)
					{
						OSD_Ctl(0, 1, OSD_TIME_TYPE);
					}
				}
				
			}
				break;
			#if 0	
			case MSG_ID_PRV_RECREATEVDEC_IND:
			{
				PRV_MSG_ReCreateVdecIND(msg_req);
			}
				break;
			case MSG_ID_PRV_MCC_RECREATEVDEC_RSP:

			{
				PRV_MCC_RecreateVdecRsp(msg_req);
			}
				break;
			#endif
			case MSG_ID_PRV_RECREATEADEC_IND:
			{
				PRV_MSG_ReCreateAdecIND(msg_req);
			}
				break;
				
			case MSG_ID_PRV_MCC_RECREATEVDEC_RSP:
			{
				PRV_MCC_RecreateVdecRsp(msg_req);
			}
				break;			
			case MSG_ID_PRV_AUDIO_OUTPUTCHANGE_IND:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Receive message: MSG_ID_PRV_AUDIO_OUTPUTCHANGE_IND\n");
				HI_S32 s32Ret = 0;
				PRV_AUDIO_OUTPUTCHANGE_IND *AUDIO_OUTPUT = (PRV_AUDIO_OUTPUTCHANGE_IND *)msg_req->para;				
				
				s32Ret = PRV_GetAudioState(AUDIO_OUTPUT->chn);
				if(s32Ret == HI_FAILURE)
				{
					break;
				}
				s32Ret = PRV_AudioOutputChange(AUDIO_OUTPUT->chn);
				if(s32Ret == HI_SUCCESS )
				{
					IsChoosePlayAudio = 1;
					PreAudioChn = AUDIO_OUTPUT->chn;
				}
			}
				break;

			case MSG_ID_PRV_SET_AUDIOOUTPUT_IND:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Receive message: MSG_ID_PRV_SET_AUDIOOUTPUT_IND\n");
				Achn = -1;
				IsChoosePlayAudio = 0;
				PreAudioChn = -1;
				CurAudioChn = -1;
				PRV_SetVoPreview(msg_req);
				PRV_PlayAudio(s_VoDevCtrlDflt);
			}
				break;
			case MSG_ID_PRV_DESALLVDEC_IND:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Receive message: MSG_ID_PRV_DESALLVDEC_IND\n");				
				DESALLVDEC_Req *DesReq = (DESALLVDEC_Req *)msg_req->para;
				if(PRV_CurDecodeMode != DesReq->DecMode  || 
					s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode != DesReq->mode || 
					DesReq->flag == LayOut_KeyBoard || DesReq->flag == ParamUpdate_Switch)
				{
					PRV_MSG_DestroyAllVdec(DesReq->flag);
				}

			}
				break;
			case MSG_ID_PRV_MCC_DESALLVDEC_RSP:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Receive message: MSG_ID_PRV_MCC_DESALLVDEC_RSP\n");				
				int *flag = (int *)msg_req->para;
				PRV_MSG_MCC_DesAllVdecRsp(*flag);

			}
				break;
			
			case MSG_ID_PRV_CTRVDEC_IND:
			{
				PRV_MSG_CtrlVdec(msg_req);
			}
				break;

			case MSG_ID_PRV_SETVOMODE_IND:
			{
				PRV_ReSetVoMode *ReSetVoMode = (PRV_ReSetVoMode *)msg_req->para;
#if defined(SN_SLAVE_ON)
				//if(s_State_Info.bIsSlaveConfig == HI_FALSE)
				{					
					SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_RESETVOMODE_REQ, ReSetVoMode, sizeof(PRV_ReSetVoMode));
				}

#else
				OutPutMode = ReSetVoMode->NewVoMode;				
				if(ReSetVoMode->IsRefreshVo)
				{
					int mode = s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode;
					PRV_RefreshVoDevScreen(s_VoDevCtrlDflt, DISP_DOUBLE_DISP, s_astVoDevStatDflt[s_VoDevCtrlDflt].as32ChnOrder[mode]);
				}
#endif
			}
				break;
				
			case MSG_ID_PRV_MCC_RESETVOMODE_RSP:
			{
				PRV_ReSetVoMode *ReSetVoMode = (PRV_ReSetVoMode *)msg_req->para;
				OutPutMode = ReSetVoMode->NewVoMode;	
				
				if(ReSetVoMode->IsRefreshVo)
				{
					int mode = s_astVoDevStatDflt[s_VoDevCtrlDflt].enPreviewMode;
					PRV_RefreshVoDevScreen(s_VoDevCtrlDflt, DISP_DOUBLE_DISP, s_astVoDevStatDflt[s_VoDevCtrlDflt].as32ChnOrder[mode]);
				}

			}
				break;
#if defined(Hi3531)||defined(Hi3535)				
			case MSG_ID_MCC_PRV_QUERYVDEC_RSP:
			{
				PRV_MccQueryVdecRsp *Rsp = (PRV_MccQueryVdecRsp *)msg_req->para;
				g_DecodeState[Rsp->VdecChn].DecodeVideoStreamFrames = Rsp->DecodeStreamFrames;
				if(Rsp->LeftStreamFrames > 20)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave===VdecChn: %d, Rsp->LeftStreamFrames: %u\n", Rsp->VdecChn, Rsp->LeftStreamFrames);
					VoChnState.IsStopGetVideoData[Rsp->VdecChn] = 1;
				}
				else if(Rsp->LeftStreamFrames < 10)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave===VdecChn: %d Begin Get Data Again\n", Rsp->VdecChn);
					VoChnState.IsStopGetVideoData[Rsp->VdecChn] = 0;	
				}
			}
				break;
#endif				
			case MSG_ID_PRV_MCC_GETVDECVOINFO_RSP:
			{
				PlayBack_MccGetVdecVoInfoRsp *Rsp = (PlayBack_MccGetVdecVoInfoRsp *)msg_req->para;
				g_DecodeState[Rsp->VoChn].DecodeVideoStreamFrames = Rsp->DecodeStreamFrames;
				g_ChnPlayStateInfo stPlayStateInfo;
				UINT8 MaxSteamFrames = 0;
				HI_S32 s32Ret = 0, count = 0, cidx = 0, TotalSize = 0;
				
				TRACE(SCI_TRACE_NORMAL, MOD_DEC, "=====MSG_ID_PRV_MCC_GETVDECVOINFO_RSP=====Rsp->Result: %d=====u64CurPts:%lld====LeftStreamFrames:%lu\n", Rsp->Result, Rsp->u64CurPts, Rsp->LeftStreamFrames);
				if(Rsp->Result != -1)
				{
					PtsInfo[Rsp->VoChn].CurVoChnPts = Rsp->u64CurPts;
				}
				PRV_GetVoChnPlayStateInfo(Rsp->VoChn, &stPlayStateInfo);
				if(stPlayStateInfo.RealType == DEC_TYPE_NOREAL)
					MaxSteamFrames = 5;
				else
					MaxSteamFrames = 10;
				if((int)(Rsp->LeftStreamFrames) >= MaxSteamFrames)
				{
					if(VoChnState.bIsPBStat_StopWriteData[Rsp->VoChn] == 0)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_DEC, "Slave===LeftStreamFrames: %d=======ChnStopWriteInd->Chn: %d=====1\n", Rsp->LeftStreamFrames, Rsp->VoChn);
						//VoChnState.IsStopGetVideoData[Rsp->VdecChn] = 1;
						Ftpc_ChnStopWrite(Rsp->VoChn, 1);					
						VoChnState.bIsPBStat_StopWriteData[Rsp->VoChn] = 1;
					}
					VoChnState.IsStopGetVideoData[Rsp->VoChn] = 1;
				}
				else
				{
					if(VoChnState.bIsPBStat_StopWriteData[Rsp->VoChn] == 1)
					{						
						s32Ret = BufferState(Rsp->VoChn + PRV_VIDEOBUFFER, &count, &TotalSize, &cidx);
						if(s32Ret == 0 && cidx <= 10)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_DEC, "Slave===LeftStreamFrames: %d=======ChnStopWriteInd->Chn: %d=====0\n", Rsp->LeftStreamFrames, Rsp->VoChn);
							//VoChnState.IsStopGetVideoData[Rsp->VdecChn] = 0;	
							Ftpc_ChnStopWrite(Rsp->VoChn, 0);					
							VoChnState.bIsPBStat_StopWriteData[Rsp->VoChn] = 0;
						}
					}
					VoChnState.IsStopGetVideoData[Rsp->VoChn] = 0;
				}
			}
				break;

			case MSG_ID_FTPC_PROSBAR_REQ://web和设备端按设备回放下拖动进度条，以及设备端即时回放拖动进度条
			{
				g_PlayInfo	stPlayInfo;
				PRV_GetPlayInfo(&stPlayInfo);
				Ftpc_Prosbar_Req *req = (Ftpc_Prosbar_Req *)msg_req->para;
				UINT8 chn = req->channel;
				time_t NewStartPts = 0;
				g_PRVPtsinfo stPlayPtsInfo;
				
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "=================MSG_ID_FTPC_PROSBAR_REQ===chn: %d\n", chn);
				
				if(chn <= 0)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d chn=%d\n", __LINE__, chn);
					break;
				}
				else
				{
					chn = chn - 1;
				}

				if(req->time >= 100)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d req->time: %lld======\n", __LINE__, req->time);
					req->time = 99;
				}
				else if(req->time < 0)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d req->time: %lld======\n", __LINE__, req->time);
					req->time = 0;
				}
				PRV_GetVoChnPtsInfo(chn, &stPlayPtsInfo);
				stPlayPtsInfo.FirstVideoPts = 0;
				stPlayPtsInfo.FirstAudioPts = 0;
				PRV_SetVoChnPtsInfo(chn, &stPlayPtsInfo);
				
				if(stPlayInfo.PlayBackState == PLAY_EXIT
					|| (stPlayInfo.PlayBackState == PLAY_INSTANT && chn != stPlayInfo.InstantPbChn))
				{
					Ftpc_Prosbar_Rsp Rsp;
					Rsp.result = SN_ERR_FTPC_PBISEXIT_ERROR;
					SendMessageEx(msg_req->user, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, MSG_ID_FTPC_PROSBAR_RSP, &Rsp, sizeof(Ftpc_Prosbar_Rsp));
					break;
				}
				else if(stPlayInfo.PlayBackState == PLAY_INSTANT)
				{
					time_t QueryStartPts = PlayBack_PrmTime_To_Sec(&PtsInfo[chn].QueryStartTime);
					time_t QueryFinalPts = PlayBack_PrmTime_To_Sec(&PtsInfo[chn].QueryFinalTime);
					NewStartPts = QueryStartPts + (int)(QueryFinalPts - QueryStartPts)*(req->time)/100;
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "req->time: %lld============QueryStartPts: %ld, NewStartPts: %ld\n", req->time, QueryStartPts, NewStartPts);
					Probar_time[chn] = NewStartPts;
					PtsInfo[chn].CurShowPts = (HI_U64)Probar_time[chn]*1000000;
					PlayBack_Sec_To_PrmTime(NewStartPts, &req->starttime);
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "QueryStarTime: %d-%d-%d, %d.%d.%d\n", PtsInfo[chn].QueryStartTime.Year, PtsInfo[chn].QueryStartTime.Month, PtsInfo[chn].QueryStartTime.Day, PtsInfo[chn].QueryStartTime.Hour, PtsInfo[chn].QueryStartTime.Minute, PtsInfo[chn].QueryStartTime.Second);
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "starttime: %d-%d-%d, %d.%d.%d\n", req->starttime.Year, req->starttime.Month, req->starttime.Day, req->starttime.Hour, req->starttime.Minute, req->starttime.Second);
					SendMessageEx(msg_req->user,msg_req->source,MOD_FTPC,msg_req->xid,msg_req->thread, msg_req->msgId, req, msg_req->size);
					BufferSet(chn + 1, MAX_ARRAY_NODE); 			
				}
				else
				{
					//chn = chn - 1;//设备回放通道号起始值为1
					NewStartPts = PlayBack_PrmTime_To_Sec(&(req->starttime));
					Probar_time[chn] = NewStartPts;
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "starttime: %d-%d-%d, %d.%d.%d, Probar_time[chn]: %ld\n", req->starttime.Year, req->starttime.Month, req->starttime.Day, req->starttime.Hour, req->starttime.Minute, req->starttime.Second, Probar_time[chn]);
					SendMessageEx(msg_req->user,msg_req->source,MOD_FTPC,msg_req->xid,msg_req->thread, msg_req->msgId, msg_req->para, msg_req->size);
					BufferSet(chn + 1, MAX_ARRAY_NODE); 			
				}
				Ftpc_ChnStopWrite(chn, 1);					
				VoChnState.bIsPBStat_StopWriteData[chn] = 1;
				BufferSet(chn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);			
				BufferSet(chn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
				if(VochnInfo[chn].SlaveId == PRV_MASTER)
				{
					PRV_ReStarVdec(VochnInfo[chn].VdecChn);
				}
				else
				{
					PlayBack_MccProsBarReq ProsBar;
					ProsBar.SlaveId = VochnInfo[chn].SlaveId;
					ProsBar.VoChn = chn;
					SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_PBPROSBAR_REQ, &ProsBar, sizeof(PlayBack_MccProsBarReq));
				}	

				VoChnState.VideoDataTimeLag[chn] = 0;
				VoChnState.AudioDataTimeLag[chn] = 0;
				VoChnState.FirstHaveVideoData[chn] = 0;
				VoChnState.FirstHaveAudioData[chn] = 0;
				
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VochnInfo[chn].VdecChn: %d, SlaveId: %d\n", VochnInfo[chn].VdecChn, VochnInfo[chn].SlaveId);
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d============bIsPBStat_StopWriteData---StopWrite==1\n", __LINE__);

			}
				break;

			case MSG_ID_FTPC_SETCHN_PBSTATE_POS_REQ://web即时回放拖动进度条
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "=================MSG_ID_FTPC_SETCHN_PBSTATE_POS_REQ\n");
				g_PlayInfo	stPlayInfo;
				PRV_GetPlayInfo(&stPlayInfo);
				Ftpc_SetChnPBStatePos_Req *SetChnPBPosReq = (Ftpc_SetChnPBStatePos_Req *)msg_req->para;
				UINT8 chn = SetChnPBPosReq->channel;

				if(chn <= 0)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d chn=%d\n", __LINE__, chn);
					break;
				}
				else
				{
					chn = chn - 1;
				}
				
				if(stPlayInfo.PlayBackState == PLAY_INSTANT && VochnInfo[chn].bIsPBStat)
				{					 
					Ftpc_Prosbar_Req req;
					time_t QueryStartPts = PlayBack_PrmTime_To_Sec(&PtsInfo[chn].QueryStartTime);
					time_t QueryFinalPts = PlayBack_PrmTime_To_Sec(&PtsInfo[chn].QueryFinalTime);
					time_t NewStartPts = QueryStartPts + (int)(QueryFinalPts - QueryStartPts)*(SetChnPBPosReq->position)/100;
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "============QueryStartPts: %ld, NewStartPts: %ld\n", QueryStartPts, NewStartPts);
					Probar_time[chn] = NewStartPts;
					
					PtsInfo[chn].CurShowPts = (HI_U64)Probar_time[chn]*1000000;
					req.channel = SetChnPBPosReq->channel;
					req.time = SetChnPBPosReq->position;
					PlayBack_Sec_To_PrmTime(NewStartPts, &req.starttime);
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "QueryStartTime: %d-%d-%d, %d.%d.%d\n", PtsInfo[chn].QueryStartTime.Year, PtsInfo[chn].QueryStartTime.Month, PtsInfo[chn].QueryStartTime.Day, PtsInfo[chn].QueryStartTime.Hour, PtsInfo[chn].QueryStartTime.Minute, PtsInfo[chn].QueryStartTime.Second);
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "starttime: %d-%d-%d, %d.%d.%d\n", req.starttime.Year, req.starttime.Month, req.starttime.Day, req.starttime.Hour, req.starttime.Minute, req.starttime.Second);
					SendMessageEx(msg_req->user,msg_req->source,MOD_FTPC,msg_req->xid,msg_req->thread, MSG_ID_FTPC_PROSBAR_REQ, &req, sizeof(Ftpc_Prosbar_Req));
					
					Ftpc_ChnStopWrite(chn, 1);					
					VoChnState.bIsPBStat_StopWriteData[chn] = 1;
					BufferSet(chn + 1, MAX_ARRAY_NODE); 			
					BufferSet(chn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);			
					BufferSet(chn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d============bIsPBStat_StopWriteData---StopWrite==1\n", __LINE__);
				}
				else
				{
					Ftpc_SetChnPBStatePos_Rsp SetChnPbPosRsp;
					SetChnPbPosRsp.channel = SetChnPBPosReq->channel;
					SetChnPbPosRsp.position = SetChnPBPosReq->position;
					if(stPlayInfo.PlayBackState == PLAY_EXIT)
						SetChnPbPosRsp.PbState = 0;
					else
						SetChnPbPosRsp.PbState = 2;
					SendMessageEx(msg_req->user,msg_req->source,MOD_FWK,msg_req->xid,msg_req->thread, MSG_ID_FTPC_GETCHN_PBSTATE_POS_RSP, &SetChnPbPosRsp, sizeof(Ftpc_SetChnPBStatePos_Rsp));
				}

			}
				break;
			
				
			case MSG_ID_PRV_CHNENTERPB_IND:
			{
				Prv_Chn_EnterPB_Ind *ChnEnterPB = (Prv_Chn_EnterPB_Ind *)msg_req->para;
				UINT8 chn = ChnEnterPB->Chn;

				if(chn <= 0)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d chn=%d\n", __LINE__, chn);
					break;
				}
				else
				{
					chn = chn - 1;
				}
				
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Chn: %d============MSG_ID_PRV_CHNENTERPB_IND========\n", ChnEnterPB->Chn);

				PRV_VoChnStateInit(chn);
				VochnInfo[chn].bIsPBStat = 1;
#if defined(SN_SLAVE_ON)
				SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_PBCHNENTER_IND, ChnEnterPB, sizeof(Prv_Chn_EnterPB_Ind));
#endif			
				g_ChnPlayStateInfo stPlayStateInfo;
				g_PlayInfo	stPlayInfo;
				PRV_GetPlayInfo(&stPlayInfo);
				PRV_GetVoChnPlayStateInfo(chn, &stPlayStateInfo);
				stPlayStateInfo.CurPlayState = DEC_STATE_NORMAL;
				stPlayStateInfo.CurSpeedState = DEC_SPEED_NORMAL;
				PRV_SetVoChnPlayStateInfo(chn, &stPlayStateInfo);
				if(stPlayInfo.PlayBackState == PLAY_EXIT)
				{
					stPlayInfo.PlayBackState = PLAY_INSTANT;
					stPlayInfo.InstantPbChn = chn;
					BufferSet(chn + 1, MAX_ARRAY_NODE); 		
					BufferSet(chn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE); 		
					BufferSet(chn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
					if(Achn >= 0)
					{
#if defined(SN9234H1)	

						CHECK(HI_MPI_AO_ClearChnBuf(0, 0));	
#else
						CHECK(HI_MPI_AO_ClearChnBuf(4, 0));	
#endif
						CHECK(HI_MPI_ADEC_ClearChnBuf(DecAdec));	
					}
					stPlayInfo.FullScreenId=PB_Full_id;
					PRV_SetPlayInfo(&stPlayInfo);
					sem_post(&sem_PBGetData);
					//sem_post(&sem_PBSendData);
					PreAudioChn = Achn;
					Achn = chn;
					CurAudioChn = chn;
				}
				sem_post(&sem_VoPtsQuery);
			}				
				break;
				
			case  MSG_ID_FTPC_END_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===================MSG_ID_FTPC_END_REQ=================\n");				
				
#if 1
				Ftpc_End_Req *EndPlay = (Ftpc_End_Req *)msg_req->para;
				HI_S32 chn = EndPlay->channel;

				if(chn <= 0)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d chn=%d\n", __LINE__, chn);
					break;
				}
				else
				{
					chn = chn - 1;
				}
				
				if(EndPlay->PlayType == 2)
				{
					BufferSet(chn + 1, MAX_ARRAY_NODE); 			
					PlayBack_Stop(chn);					
					PRV_PtsInfoInit(chn);
					PRV_InitVochnInfo(chn);
					PRV_VoChnStateInit(chn);
					VochnInfo[chn].bIsPBStat = 1;
				}				
				BufferSet(chn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);			
				BufferSet(chn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
#endif
			}
				break;
				
			case  MSG_ID_FTPC_QUERYFILE_REQ:
			{
				Ftpc_QueryFile_Req *QueFileReq = (Ftpc_QueryFile_Req *)msg_req->para;
				//即时回放下，需要获取通道5分钟的起始时间，用于更新进度条
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========MSG_ID_FTPC_QUERYFILE_REQ===playtype: %d\n", QueFileReq->PlayType);
				if(QueFileReq->PlayType == 1)
				{
					int i = 0;
					for(i = 0; i < DEV_CHANNEL_NUM; i++)
					{
						if(QueFileReq->RemoteChn[i] != 0)
							break;
					}
					if(i < DEV_CHANNEL_NUM)
					{
						PtsInfo[i].QueryStartTime = QueFileReq->StartTime;
						PtsInfo[i].QueryFinalTime = QueFileReq->FinalTime;
						
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "i: %d==========PtsInfo[chn].QueryStartTime: %d-%d-%d,%d.%d.%d\n", i, PtsInfo[i].QueryStartTime.Year, PtsInfo[i].QueryStartTime.Month, PtsInfo[i].QueryStartTime.Day, PtsInfo[i].QueryStartTime.Hour, PtsInfo[i].QueryStartTime.Minute, PtsInfo[i].QueryStartTime.Second);
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "i: %d==========PtsInfo[chn].QueryFinalTime: %d-%d-%d,%d.%d.%d\n", i, PtsInfo[i].QueryFinalTime.Year, PtsInfo[i].QueryFinalTime.Month, PtsInfo[i].QueryFinalTime.Day, PtsInfo[i].QueryFinalTime.Hour, PtsInfo[i].QueryFinalTime.Minute, PtsInfo[i].QueryFinalTime.Second);
					}
				}
				else
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----QueFileReq->PlaybackMode=%d\n", QueFileReq->PlaybackMode);
					if(QueFileReq->PlaybackMode == 0)
					{
						PRV_SetPreviewVoDevInMode(1);
					}
					else
					{
						PRV_SetPreviewVoDevInMode(4);
					}
					
				}
				
			}
				break;
				
			case  MSG_ID_FTPC_STOP_RSP:
			{
				Ftpc_Stop_Rsp *StopPlayRsp = (Ftpc_Stop_Rsp *)msg_req->para;
				HI_S32 i = 0, channel = 0;
				channel = StopPlayRsp->channel;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV, source: %d============MSG_ID_FTPC_STOP_RSP: %d===%d\n", msg_req->source, StopPlayRsp->PlayType, StopPlayRsp->channel);
				g_ChnPlayStateInfo stPlayStateInfo;
				g_PlayInfo	stPlayInfo;
				PRV_GetPlayInfo(&stPlayInfo);

				if(StopPlayRsp->PlayType == 1)
				{
					if(channel <= 0)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d chn=%d\n", __LINE__, channel);
						break;
					}
					else
					{
						channel = channel - 1;
					}
				}
				
				PRV_GetVoChnPlayStateInfo(channel, &stPlayStateInfo);
				if(StopPlayRsp->PlayType == 1)
				{
					VochnInfo[channel].bIsPBStat = 0;
					VoChnState.bIsPBStat_StopWriteData[channel] = 0;
#if defined(SN_SLAVE_ON)
					SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_PBCHNEXIT_IND, &channel, sizeof(HI_S32));
#endif		
				
					if(stPlayStateInfo.CurPlayState == DEC_STATE_NORMALPAUSE)
					{
#if defined(Hi3535)
						HI_MPI_VO_ResumeChn(HD, channel);
#else
						HI_MPI_VO_ChnResume(HD, channel); 
#endif
					}
						
					stPlayStateInfo.CurPlayState = DEC_STATE_EXIT;

					stPlayInfo.PlayBackState = PLAY_EXIT;
					stPlayInfo.InstantPbChn = 0;
					BufferSet(channel + 1, MAX_ARRAY_NODE);			
					BufferSet(channel + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);			
					BufferSet(channel + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);

					CHECK(PRV_StopAdec());
					CHECK(PRV_StartAdecAo(VochnInfo[channel]));
					IsCreateAdec = 1;
					HI_MPI_ADEC_ClearChnBuf(DecAdec);
					Achn = PreAudioChn;
					CurAudioChn = PreAudioChn;
				}
				else if(StopPlayRsp->PlayType == 2)
				{
#if defined(SN_SLAVE_ON)
					SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_PBSTOP_REQ, NULL, 0);
#endif		
					for(i = 0; i < stPlayInfo.ImagCount; i++)
					{
						BufferSet(i + 1, MAX_ARRAY_NODE); 			
						BufferSet(i + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);			
						BufferSet(i + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
						PlayBack_Stop(i);					
						PRV_PtsInfoInit(i);
						PRV_InitVochnInfo(i);	
						PRV_VoChnStateInit(i);
						VochnInfo[i].bIsPBStat = 1;
					}
					
					if(PRV_CurIndex > 0)
					{
						for(i = 0; i < PRV_CurIndex; i++)
						{
							NTRANS_FreeMediaData(PRV_OldVideoData[i]);
							PRV_OldVideoData[i] = NULL;
						}
						PRV_CurIndex = 0;
						PRV_SendDataLen = 0;
					}
					if(stPlayInfo.IsPause && stPlayInfo.PlayBackState > PLAY_INSTANT)
						sem_post(&sem_PlayPause);
					stPlayInfo.PlayBackState = PLAY_STOP;
					stPlayInfo.IsPause = 0;
#if defined(Hi3531)		
					PlayBack_StopAdec(4, AOCHN, ADECHN);
#else
					PlayBack_StopAdec(0, AOCHN, ADECHN);
#endif
				}
				PRV_SetPlayInfo(&stPlayInfo);
				PRV_SetVoChnPlayStateInfo(channel, &stPlayStateInfo);
			}
				break;
				
			case MSG_ID_FTPC_CLOSE_RSP:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "============MSG_ID_FTPC_CLOSE_RSP==========\n");
				#if defined(Hi3531)
					AUDIO_DEV AoDev = 4;
				#else
					AUDIO_DEV AoDev = 0;
				#endif
				g_PlayInfo	stPlayInfo;
				PRV_GetPlayInfo(&stPlayInfo);
				UINT8 i = 0;
				if(stPlayInfo.PlayBackState > PLAY_INSTANT)
				{
#if defined(SN_SLAVE_ON)
					SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_PBSTOP_REQ, NULL, 0);
#endif		
					for(i = 0; i < stPlayInfo.ImagCount; i++)
					{
						BufferSet(i + 1, MAX_ARRAY_NODE); 			
						BufferSet(i + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);			
						BufferSet(i + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
						PlayBack_Stop(i);						
						PRV_PtsInfoInit(i);
						PRV_InitVochnInfo(i);
						PRV_VoChnStateInit(i);
					}
				}
				PlayBack_StopAdec(AoDev, AOCHN, ADECHN);

				if(stPlayInfo.IsPause && stPlayInfo.PlayBackState > PLAY_INSTANT)
					sem_post(&sem_PlayPause);
				PRV_PBPlayInfoInit();
				PB_Full_id = 0;
#if defined(SN9234H1)
				PRV_MccPBCtlReq Mcc_req;
	            Mcc_req.FullScreenId=PB_Full_id;
				Mcc_req.flag=3;
				SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_PBFULLSCREEN_REQ, &Mcc_req, sizeof(PRV_MccPBCtlReq));	
#endif
			}
				break;
				
			case MSG_ID_PRV_PREPLAYBACK_IND:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "source: %d============MSG_ID_PRV_PREPLAYBACK_IND========\n", msg_req->source);
				#if defined(Hi3535)
					AUDIO_DEV AoDev = 0;
				#elif defined(Hi3531)
					AUDIO_DEV AoDev = 4;
				#endif
				PRV_PrePlayBack_Ind *PrePlayBack = (PRV_PrePlayBack_Ind *)msg_req->para;
				HI_S32 i = 0, s32ChnCount = 1;
				switch(PrePlayBack->PlaybackMode)
				{
					case PB_SingleScene:
						s32ChnCount = 1;
						break;
					case PB_FourScene:
						s32ChnCount = 4;
						break;
					case PB_NineScene:
						s32ChnCount = 9;
						break;
					case PB_SixteenScene:
						s32ChnCount = 16;						
						break;
					default:
						s32ChnCount = 1;						
						break;
				}
				g_PlayInfo PlayState;
				SN_MEMSET(&PlayState, 0, sizeof(g_PlayInfo));
				PlayState.FullScreenId = PB_Full_id;
				PlayState.bISDB=0;
				PlayState.DBClickChn=0;
				PlayState.IsZoom=0;
				PlayState.ImagCount = s32ChnCount;
				PlayState.IsSingle = (s32ChnCount == 1) ? 1 : 0;
				PlayState.PlayBackState = PLAY_ENTER;
				PlayState.IsPlaySound = 1;
				MMI_GetReplaySize(&PlayState.SubWidth, &PlayState.SubHeight);	
				int u32Width, u32Height;
				PlayBack_GetPlaySize((HI_U32 *)&u32Width, (HI_U32 *)&u32Height);
				PRV_SetPlayInfo(&PlayState);
				for(i = 0; i < CHANNEL_NUM; i++)
					VochnInfo[i].bIsPBStat = 1;
#if defined(SN_SLAVE_ON)
				PlayBack_MccOpenReq MccOpenReq;
				MccOpenReq.SlaveId = PRV_SLAVE_1;
				MccOpenReq.IsSingle = PlayState.IsSingle;
				MccOpenReq.ImageCount = s32ChnCount;
				MccOpenReq.subwidth  = PlayState.SubWidth;
				MccOpenReq.subheight = PlayState.SubHeight;
				MccOpenReq.StreamChnIDs = MasterToSlaveChnId;
				SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, msg_req->xid, msg_req->thread, MSG_ID_PRV_MCC_PBOPEN_REQ, &MccOpenReq, sizeof(MccOpenReq));
#endif
				sem_post(&sem_PBGetData);

#if defined(Hi3531)||defined(Hi3535)
				Achn = ADECHN;
				PlayBack_StartVo();
				PlayBack_StartAdec(AoDev, AOCHN, ADECHN, PT_G711A);
				if(PlayState.IsSingle==1)
					Playback_VoDevSingle(s_VoDevCtrlDflt,0);
				else
				    Playback_VoDevMul(s_VoDevCtrlDflt,s32ChnCount);
#endif
				PRV_GetPlayInfo(&PlayState);
			}

				break;
				
			case MSG_ID_FTPC_PLAY_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "=======================MSG_ID_FTPC_PLAY_REQ\n");
				PlayBack_Pro_PlayReq(msg_req);
			}
				break;
		
			case MSG_ID_PRV_PAUSE_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "=======================MSG_ID_PRV_PAUSE_REQ\n");

				PlayBack_Pro_PauseReq(msg_req);
			}
				break;
				
			case MSG_ID_PRV_FORWARDFAST_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===============MSG_ID_PRV_FORWARDFAST_REQ\n");
				PlayBack_ForwardFast_Req *Req = (PlayBack_ForwardFast_Req *)msg_req->para;
				PlayBack_ForwardFast_Req fastReq;
				PlayBack_ForwardFast_Rsp Rsp;
				g_PlayInfo stPlayInfo;
				g_ChnPlayStateInfo stPlayStateInfo;
				g_ChnPlayStateInfo playStatTmp;
				SN_MEMSET(&playStatTmp, 0, sizeof(playStatTmp));
				PRV_GetPlayInfo(&stPlayInfo);
				//不在设备回放下拖动进度条返回失败
				if(stPlayInfo.PlayBackState <= PLAY_INSTANT)
				{
					Rsp.result = SN_ERR_FTPC_PBISEXIT_ERROR;
					SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, MSG_ID_PRV_FORWARDFAST_RSP, &Rsp, sizeof(Rsp));					
					break;					
				}
				int i = 0;//, VoChn = (int)Req->channel;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===============VoChn==0=====ch=%d==========Req->speedstate: %d= ImagCount=%d\n", (int)Req->channel, Req->speedstate, stPlayInfo.ImagCount);
				//if(VoChn == 0)
				{
					for(i = 0; i < stPlayInfo.ImagCount; i++)
					{
						PRV_GetVoChnPlayStateInfo(i, &stPlayStateInfo);
						
						if(stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALFAST8)
							stPlayStateInfo.CurSpeedState = DEC_SPEED_NORMALFAST8;
						else
							stPlayStateInfo.CurSpeedState = stPlayStateInfo.CurSpeedState + 1;

						if(stPlayStateInfo.CurPlayState == DEC_STATE_NORMAL)
						{
							SN_MEMCPY(&playStatTmp, sizeof(playStatTmp),
								&stPlayStateInfo, sizeof(stPlayStateInfo),
								sizeof(stPlayStateInfo));
						}
						PRV_SetVoChnPlayStateInfo(i, &stPlayStateInfo);
					}
				}
				#if 0
				else
				{
					PRV_GetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);
					if(stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALFAST8)
						stPlayStateInfo.CurSpeedState = DEC_SPEED_NORMAL;
					else
						stPlayStateInfo.CurSpeedState = stPlayStateInfo.CurSpeedState+1;
					PRV_SetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);
				}
				#endif
				Rsp.result = 0;
				Rsp.channel = Req->channel;

				if(playStatTmp.CurPlayState == DEC_STATE_NORMAL)
				{
					Rsp.playstate = playStatTmp.CurPlayState;
					Rsp.speedstate = playStatTmp.CurSpeedState;	
				}
				else
				{
					Rsp.playstate = stPlayStateInfo.CurPlayState;
					Rsp.speedstate = stPlayStateInfo.CurSpeedState;	
				}

				PlayBack_AdaptRealType();
				fastReq.channel = Rsp.channel;
				fastReq.speedstate = Rsp.speedstate;

				SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_FTPC, msg_req->xid, msg_req->thread, MSG_ID_PRV_FORWARDFAST_REQ, &fastReq, sizeof(PlayBack_ForwardFast_Req));					
				SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, MSG_ID_PRV_FORWARDFAST_RSP, &Rsp, sizeof(Rsp));					
			}
				break;
				
			case MSG_ID_PRV_FORWARDSLOW_REQ:
			{
				PlayBack_ForwardSlow_Req *Req = (PlayBack_ForwardSlow_Req *)msg_req->para;
				PlayBack_ForwardSlow_Req slowReq;
				PlayBack_ForwardSlow_Rsp Rsp;
				int i = 0;//, VoChn = Req->channel;
				g_PlayInfo stPlayInfo;
				g_ChnPlayStateInfo stPlayStateInfo;
				g_ChnPlayStateInfo playStatTmp;
				SN_MEMSET(&playStatTmp, 0, sizeof(playStatTmp));
				PRV_GetPlayInfo(&stPlayInfo);
				//不在设备回放下拖动进度条返回失败
				if(stPlayInfo.PlayBackState <= PLAY_INSTANT)
				{
					Rsp.result = SN_ERR_FTPC_PBISEXIT_ERROR;
					SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, MSG_ID_PRV_FORWARDSLOW_REQ, &Rsp, sizeof(Rsp));					
					break;					
				}
				//if(VoChn == 0)
				{
					for(i = 0; i < stPlayInfo.ImagCount; i++)
					{
						PRV_GetVoChnPlayStateInfo(i, &stPlayStateInfo);
						if(stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALSLOW8)
							stPlayStateInfo.CurSpeedState = DEC_SPEED_NORMALSLOW8;
						else
							stPlayStateInfo.CurSpeedState = stPlayStateInfo.CurSpeedState - 1;

						if(stPlayStateInfo.CurPlayState == DEC_STATE_NORMAL)
						{
							SN_MEMCPY(&playStatTmp, sizeof(playStatTmp),
								&stPlayStateInfo, sizeof(stPlayStateInfo),
								sizeof(stPlayStateInfo));
						}
						
						PRV_SetVoChnPlayStateInfo(i, &stPlayStateInfo);
					}
				}
				#if 0
				else
				{
					PRV_GetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);
					if(stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALSLOW8)
						stPlayStateInfo.CurSpeedState = DEC_SPEED_NORMAL;
					else
						stPlayStateInfo.CurSpeedState = stPlayStateInfo.CurSpeedState-1;
					PRV_SetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);
				}
				#endif
				Rsp.result = 0;
				Rsp.channel = Req->channel;

				if(playStatTmp.CurPlayState == DEC_STATE_NORMAL)
				{
					Rsp.playstate = playStatTmp.CurPlayState;
					Rsp.speedstate = playStatTmp.CurSpeedState;	
				}
				else
				{
					Rsp.playstate = stPlayStateInfo.CurPlayState;
					Rsp.speedstate = stPlayStateInfo.CurSpeedState;	
				}
					
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===============i:%d===============stPlayStateInfo.CurSpeedState: %d= CurPlayState=%d\n",
					i, playStatTmp.CurSpeedState, playStatTmp.CurPlayState);
				PlayBack_AdaptRealType();

				slowReq.channel = Rsp.channel;
				slowReq.speedstate = Rsp.speedstate;
				
				SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_FTPC, msg_req->xid, msg_req->thread, MSG_ID_PRV_FORWARDSLOW_REQ, &slowReq, sizeof(PlayBack_ForwardSlow_Req));					
				SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, MSG_ID_PRV_FORWARDSLOW_RSP, &Rsp, sizeof(Rsp));					
			}
				break;
				
			case MSG_ID_PRV_PLAYSOUND_REQ:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===========MSG_ID_PRV_PLAYSOUND_REQ===\n");

				PlayBack_PlaySound_Req *req;

				req = (PlayBack_PlaySound_Req *)msg_req->para;

				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "msg_req->para = %d, msg_req->thread = %d\n", req->enable, msg_req->thread);
				
				PlayBack_PlaySound_Rsp Rsp;
				g_PlayInfo	stPlayInfo;
				
				PRV_GetPlayInfo(&stPlayInfo);
				if(PRV_GetVoiceTalkState() == HI_TRUE)
				{
					Rsp.result = SN_ERR_PRV_VOICETALK_ON;
				}
				else if(stPlayInfo.PlayBackState <= PLAY_INSTANT)
				{
					Rsp.result = SN_ERR_FTPC_PBISEXIT_ERROR;
				}
				else
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "111 stPlayInfo.IsPlaySound=%d \n", stPlayInfo.IsPlaySound);

					if(msg_req->thread > 0)
					{
						stPlayInfo.IsPlaySound = req->enable;
					}
					else
					{
						if(stPlayInfo.IsPlaySound == 0)
							stPlayInfo.IsPlaySound = 1;
						else
							stPlayInfo.IsPlaySound = 0;
					}
					
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "222 stPlayInfo.IsPlaySound=%d \n", stPlayInfo.IsPlaySound);
					
					Rsp.result = stPlayInfo.IsPlaySound;
				}
				SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, msg_req->source, msg_req->xid, msg_req->thread, MSG_ID_PRV_PLAYSOUND_RSP, &Rsp, sizeof(Rsp));					
				PRV_SetPlayInfo(&stPlayInfo);
			}
				break;

			case MSG_ID_PRV_ENTER_ZOOMIN_REQ:
			{
                Playback_ZoomEnter(msg_req);
			}
				break;
			case MSG_ID_PRV_SET_ZOOMINCHN_REQ:
			{
                 Playback_ZoomChn(msg_req);
				 
			}
				break;
				
			case MSG_ID_PRV_EXIT_ZOOMIN_REQ:
			{
                 Playback_ExitZoomChn(msg_req);
			}
				break;
				
			case MSG_ID_PRV_SET_ZOOMINRATIO_REQ:
			{
				Playback_MSG_ChnZoomIn(msg_req);
			}
				break;
				
			case MSG_ID_PRV_PBDBCLICK_REQ:
			{
                
				Playback_DB(msg_req);
               
			}
				break;

			
				
			case MSG_ID_PRV_FULLSCREEN_REQ:
			{
                Playback_FullScr(msg_req);
				
			}
				break;
				
			case MSG_ID_PRV_MCC_PBOPEN_RSP:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===========MSG_ID_PRV_MCC_PBOPEN_RSP===\n");
                g_PlayInfo PlayState;
				
				PRV_GetPlayInfo(&PlayState);
				Achn = ADECHN;
				PlayBack_StartVo();
				PlayBack_StartAdec(0, AOCHN, ADECHN, PT_G711A);
				if(PlayState.IsSingle==1)
				   Playback_VoDevSingle(s_VoDevCtrlDflt,0);
				else
				   Playback_VoDevMul(s_VoDevCtrlDflt,PlayState.ImagCount);
				sem_post(&sem_PBGetData);
				//sem_post(&sem_PBSendData);
			}
				break;
				
			case MSG_ID_PRV_MCC_PBPAUSE_RSP:
			{

			}
				break;
				
            case MSG_ID_PRV_MCC_PBZOOM_RSP:
			{
		#if defined(SN9234H1)
		
                Playback_MCC_ZoomRSP(msg_req);
		#endif
			}
			break;
				
			case MSG_ID_PRV_MCC_PBDBCLICK_RSP:
			{
        #if defined(SN9234H1)
				Playback_MCC_DBRSP(msg_req);
		#endif
			}
				break;	
				
				
			case MSG_ID_PRV_MCC_PBFULLSCREEN_RSP:
			{
		#if defined(SN9234H1)
                Playback_MCC_FullScrRSP(msg_req);
		#endif
			}
				break;	
				
			case MSG_ID_PRV_MCC_PBCLEANVOCHN_RSP:
			{

			}
				break;	
								
			case MSG_ID_PRV_MCC_PBCREATEVDEC_RSP:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "============MSG_ID_PRV_MCC_PBCREATEVDEC_RSP\n");
				PlayBack_MccCreatVdec(msg_req);

			}
				break;	
				
			case MSG_ID_PRV_MCC_PBDESVDEC_RSP:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "============MSG_ID_PRV_MCC_PBDESVDEC_RSP\n");
				PlayBack_MccDestroyVdec(msg_req);
			}
				break;	
			
			case MSG_ID_PRV_MCC_PBRECREATEVDEC_RSP:
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "============MSG_ID_PRV_MCC_PBRECREATEVDEC_RSP\n");
				PlayBack_MccReCreatVdec(msg_req);

			}
				break;	
				
			case MSG_ID_PRV_MCC_PBQUERYSTATE_RSP: /* 查询从片通道状态RSP */
			{
				PlayBack_MccQueryStateRsp *QueryRsp = (PlayBack_MccQueryStateRsp *)(msg_req->para);
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "=====================receive MSG_ID_MCC_DEC_QUERYSTATE_RSP---chn=%d",QueryRsp->VoChn);
				PlayBack_GetSlaveQueryRsp(QueryRsp);
			}
				break;
			default:
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s Get unknown or unused message: %#x\n", __FUNCTION__, msg_req->msgId);
				break;
		}
		pthread_mutex_unlock(&send_data_mutex);
		
		SN_FreeMessage(&msg_req);
	}

	return NULL;
}

#if defined(Hi3535)
static HI_VOID SAMPLE_COMM_VO_HdmiConvertSync(VO_INTF_SYNC_E enIntfSync,
    HI_HDMI_VIDEO_FMT_E *penVideoFmt)
{
    switch (enIntfSync)
    {
        case VO_OUTPUT_PAL:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_PAL;
            break;
        case VO_OUTPUT_NTSC:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_NTSC;
            break;
        case VO_OUTPUT_1080P24:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_1080P_24;
            break;
        case VO_OUTPUT_1080P25:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_1080P_25;
            break;
        case VO_OUTPUT_1080P30:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_1080P_30;
            break;
        case VO_OUTPUT_720P50:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_720P_50;
            break;
        case VO_OUTPUT_720P60:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_720P_60;
            break;
        case VO_OUTPUT_1080I50:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_1080i_50;
            break;
        case VO_OUTPUT_1080I60:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_1080i_60;
            break;
        case VO_OUTPUT_1080P50:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_1080P_50;
            break;
        case VO_OUTPUT_1080P60:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_1080P_60;
            break;
        case VO_OUTPUT_576P50:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_576P_50;
            break;
        case VO_OUTPUT_480P60:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_480P_60;
            break;
        case VO_OUTPUT_800x600_60:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_VESA_800X600_60;
            break;
        case VO_OUTPUT_1024x768_60:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_VESA_1024X768_60;
            break;
        case VO_OUTPUT_1280x1024_60:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_VESA_1280X1024_60;
            break;
        case VO_OUTPUT_1366x768_60:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_VESA_1366X768_60;
            break;
        case VO_OUTPUT_1440x900_60:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_VESA_1440X900_60;
            break;
        case VO_OUTPUT_1280x800_60:
            *penVideoFmt = HI_HDMI_VIDEO_FMT_VESA_1280X800_60;
            break;
        default :
            TRACE(SCI_TRACE_NORMAL, MOD_PRV,"Unkonw VO_INTF_SYNC_E value!\n");
            break;
    }

    return;
}

HI_S32 SAMPLE_COMM_VO_HdmiStart(VO_INTF_SYNC_E enIntfSync)
{
    HI_HDMI_INIT_PARA_S stHdmiPara;
    HI_HDMI_ATTR_S      stAttr;
    HI_HDMI_VIDEO_FMT_E enVideoFmt = 0;

    SAMPLE_COMM_VO_HdmiConvertSync(enIntfSync, &enVideoFmt);

    stHdmiPara.enForceMode = HI_HDMI_FORCE_HDMI;
    stHdmiPara.pCallBackArgs = NULL;
    stHdmiPara.pfnHdmiEventCallback = NULL;
    HI_MPI_HDMI_Init(&stHdmiPara);

    HI_MPI_HDMI_Open(HI_HDMI_ID_0);

    HI_MPI_HDMI_GetAttr(HI_HDMI_ID_0, &stAttr);

    stAttr.bEnableHdmi = HI_TRUE;
    
    stAttr.bEnableVideo = HI_TRUE;
    stAttr.enVideoFmt = enVideoFmt;

    stAttr.enVidOutMode = HI_HDMI_VIDEO_MODE_YCBCR444;
    stAttr.enDeepColorMode = HI_HDMI_DEEP_COLOR_OFF;
    stAttr.bxvYCCMode = HI_FALSE;

    stAttr.bEnableAudio = HI_FALSE;
    stAttr.enSoundIntf = HI_HDMI_SND_INTERFACE_I2S;
    stAttr.bIsMultiChannel = HI_FALSE;

    stAttr.enBitDepth = HI_HDMI_BIT_DEPTH_16;

    stAttr.bEnableAviInfoFrame = HI_TRUE;
    stAttr.bEnableAudInfoFrame = HI_TRUE;
    stAttr.bEnableSpdInfoFrame = HI_FALSE;
    stAttr.bEnableMpegInfoFrame = HI_FALSE;

    stAttr.bDebugFlag = HI_FALSE;          
    stAttr.bHDCPEnable = HI_FALSE;

    stAttr.b3DEnable = HI_FALSE;
    
    HI_MPI_HDMI_SetAttr(HI_HDMI_ID_0, &stAttr);

    HI_MPI_HDMI_Start(HI_HDMI_ID_0);
    
    printf("HDMI start success.\n");
    return HI_SUCCESS;
}

HI_S32 SAMPLE_COMM_VO_HdmiStop(HI_VOID)
{
    HI_MPI_HDMI_Stop(HI_HDMI_ID_0);
    HI_MPI_HDMI_Close(HI_HDMI_ID_0);
    HI_MPI_HDMI_DeInit();

    return HI_SUCCESS;
}
#endif
	
/************************************************************************/
/* 初始预览显示。
                                                                     */
/************************************************************************/
HI_S32 PRV_PreviewInit(HI_VOID)
{	
#if defined(SN9234H1)
	HI_S32 i=0;
	for(i = 0; i < PRV_VO_DEV_NUM-1; i++)//HD,AD
	{
		//if(i == SPOT_VO_DEV || i == AD)
		//	continue;
		PRV_RefreshVoDevScreen(i, (SD == i) ? DISP_NOT_DOUBLE_DISP : DISP_DOUBLE_DISP, s_astVoDevStatDflt[i].as32ChnOrder[s_astVoDevStatDflt[i].enPreviewMode]);
	}
#else
	HI_S32 i=0;
	for(i = 0; i < PRV_VO_MAX_DEV; i++)//VGA-DHD0;CVBS-DSD0;DHD1暂不用
	{
		if(i > DHD0)
			continue;
		PRV_RefreshVoDevScreen(i, DISP_NOT_DOUBLE_DISP, s_astVoDevStatDflt[i].as32ChnOrder[s_astVoDevStatDflt[i].enPreviewMode]);
	}
#endif
	RET_SUCCESS("");
}


/************************************************************************/
/* PRV模块初始入口。
                                                                     */
/************************************************************************/

int Preview_Init(void)
{
	/*create thread*/
	pthread_t comtid, sendpic_id, osd_id, dec_id_1, PB_id1, PB_id2, PB_VoPtsQuery, file_id/*, dec_id, test_id*/;
	HI_S32 err = 0;
#if defined(Hi3531)||defined(Hi3535)	
	HI_S32 i = 0, s32Ret = 0;
#endif	
	MPP_VERSION_S stVersion;
	Prv_Audiobuf.length = 0;
	SN_MEMSET((char*)Prv_Audiobuf.databuf,0x80,sizeof(Prv_Audiobuf.databuf));
	CHECK_RET(HI_MPI_SYS_GetVersion(&stVersion));
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "mpp version is "TEXT_COLOR_PURPLE("%s\n"), stVersion.aVersion);
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "MOD_PRV Last Build in "TEXT_COLOR_YELLOW("%s %s\n"), __DATE__, __TIME__);
	{
		PRM_GENERAL_CFG_BASIC preview_gneral;

		//获取NP制式
		if (PARAM_OK == GetParameter(PRM_ID_GENERAL_CFG_BASIC,NULL,&preview_gneral,sizeof(preview_gneral),1,SUPER_USER_ID,NULL))
		{
			/*NP制式*/
			s_s32NPFlagDflt = (0 == preview_gneral.CVBSOutputType) ? VIDEO_ENCODING_MODE_NTSC : VIDEO_ENCODING_MODE_PAL;
			s_s32VGAResolution = preview_gneral.VGAResolution;
			//printf("s_s32VGAResolution : %d\n", s_s32VGAResolution);
			//printf("preview_gneral.VGAResolution : %d\n", preview_gneral.VGAResolution);

		}
	}
#if defined(SN9234H1)	
	PRV_Init_M240();
#endif	
	//打开图形层
	Fb_clear_step1();
	//PRV_SysInit();
#if defined(SN9234H1)
	g_Max_Vo_Num = detect_video_input_num();
	//printf("##########g_Max_Vo_Num = %d##################\n",g_Max_Vo_Num);
	PRV_ViInit();


	
	PRV_VoInit();
#else	
	//g_Max_Vo_Num = detect_video_input_num();
	g_Max_Vo_Num = DEV_CHANNEL_NUM;
	//printf("##########g_Max_Vo_Num = %d##################\n",g_Max_Vo_Num);
	//PRV_ViInit();
	//额外1个VPSS(17)是为画中画(显示管理和电子放大，俩个功能互斥)
	for(i = 0; i < PRV_VO_CHN_NUM + 2; i++)
		CHECK_RET(PRV_VPSS_Start(i));
	
	CHECK_RET(PRV_VPSS_Start(DetVLoss_VdecChn));
	CHECK_RET(PRV_VPSS_Start(NoConfig_VdecChn));
	
	PRV_VoInit();
	if (HI_SUCCESS != SAMPLE_COMM_VO_HdmiStart(s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfSync))
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Start SAMPLE_COMM_VO_HdmiStart failed!\n");
        return -1;
    }
	for(i = 0; i < PRV_VO_CHN_NUM; i++)
	{
		s32Ret = PRV_VO_BindVpss(DHD0, i, i, VPSS_PRE0_CHN);
	    if (HI_SUCCESS != s32Ret)
	    {
	        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"SAMPLE_COMM_VO_BindVpss failed!\n");
	        return -1;
	    }
		#if 0
		s32Ret = PRV_VO_BindVpss(DSD0, i, i, VPSS_BYPASS_CHN);
	    if (HI_SUCCESS != s32Ret)
	    {
	        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"SAMPLE_COMM_VO_BindVpss failed!\n");
	        return -1;
	    }
		#endif
	}
#endif
	//PRV_ViVoBind();
#if !defined(Hi3535)	
	tw2865Init(0);
#endif
	PRV_DECInfoInit();

	//遮盖初始化
	OSD_Mask_init(&s_slaveVoStat);

	get_chn_param_init();
	get_OSD_param_init(&s_slaveVoStat);
	//PRV_PreviewInit();
	
	//printf("##########Preview_Init s_VoDevCtrlDflt = %d################\n",s_VoDevCtrlDflt);

	//PRV_BindHifbVo((HD==s_VoDevCtrlDflt)?AD:HD, G1);
#if defined(Hi3520)
	PRV_BindHifbVo(HD, G1);
#elif defined(Hi3535)
	PRV_BindHifbVo(DHD0, 0);
#else	
	PRV_BindHifbVo(DHD0, GRAPHICS_LAYER_G4);
#endif	
	//PRV_BindHifbVo(AD, G4);
	//SetSlaveTime();

	if(MAX_IPC_CHNNUM > 0)
		PRV_CreateVdecChn(JPEGENC, NOVIDEO_VDECHEIGHT, NOVIDEO_VDECWIDTH, RefFrameNum, DetVLoss_VdecChn);//创建解码通道存放"无网络视频"图片

	PRV_CreateVdecChn(JPEGENC, NOVIDEO_VDECHEIGHT, NOVIDEO_VDECWIDTH, RefFrameNum, NoConfig_VdecChn);//创建解码通道存放"未配置"图片

	PRV_PreviewInit();
	SN_MEMSET(&g_DecodeState, 0, PRV_VO_CHN_NUM * sizeof(PRV_DecodeState));
#if defined(SN_SLAVE_ON)
	PRV_InitHostToSlaveStream();
#endif
	err = sem_init(&OSD_time_Sem, 0, 0);
	if(err != 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Error in sem_init OSD_time_Sem\n");
		return -1;
	}
	err = sem_init(&sem_SendNoVideoPic, 0, 0);
	if(err != 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Error in sem_init sem_SendNoVideoPic\n");
		return -1;
	}
	err = sem_init(&sem_VoPtsQuery, 0, 0);
	if(err != 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Error in sem_init sem_VoPtsQuery\n");
		return -1;
	}
	err = sem_init(&sem_PlayPause, 0, 0);
	if(err != 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Error in sem_init sem_PlayPause\n");
		return -1;
	}
	
	err = sem_init(&sem_PrvGetData, 0, 0);
	if(err != 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Error in sem_init sem_PrvGetData\n");
		return -1;
	}
	err = sem_init(&sem_PrvSendData, 0, 0);
	if(err != 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Error in sem_init sem_PrvSendData\n");
		return -1;
	}
	
	err = sem_init(&sem_PBGetData, 0, 0);
	if(err != 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Error in sem_init sem_NoPBState\n");
		return -1;
	}
	
	err = sem_init(&sem_PBSendData, 0, 0);
	if(err != 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Error in sem_init sem_PBSendData\n");
		return -1;
	}

	err = pthread_create(&osd_id, 0, Set_OSD_TimeProc,NULL);
	if(err != 0) /* create thread fail if returned not 0 */
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "preview Set_OSD_TimeProc thread create fail\n");
		return -1; /*can not create*/
	}
	if(MAX_IPC_CHNNUM > 0)
	{
		err = pthread_create(&sendpic_id, 0, SendNvrNoVideoPicThread,NULL);
		if(err != 0) /* create thread fail if returned not 0 */
		{
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "preview SendNvrNoVideoPicThread thread create fail\n");
			return -1; /*can not create*/
		}
	}

	/*从client获取数据*/
	if(0 != pthread_create(&dec_id_1, NULL, PRV_GetPrvDataThread, NULL))
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "preview PRV_GetPrvDataThread thread create fail\n");
		return -1; 
	}
	/*
	if(0 != pthread_create(&dec_id_2, NULL, PRV_SendPrvDataThread, NULL))
	//{
	//	TRACE(SCI_TRACE_HIGH, MOD_PRV, "preview PRV_SendPrvDataThread thread create fail\n");
	//	return -1; 
	}
	*/
	if(0 != pthread_create(&PB_id1, NULL, PRV_GetPBDataThread, NULL))
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "preview PRV_GetPBDataThread thread create fail\n");
		return -1; 
	}

	/*向海思解码器送帧数据*/
	if(0 != pthread_create(&PB_id2, NULL, PRV_SendDataThread, NULL))
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "preview PRV_SendPBDataThread thread create fail\n");
		return -1; 
	}
	if(0 != pthread_create(&PB_VoPtsQuery, NULL, PlayBack_VdecThread, NULL))
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "preview PlayBack_VdecThread thread create fail\n");
		return -1; 
	}
	
	err = pthread_create(&comtid, 0, PRV_ParseMsgProc, NULL);
	if(err != 0) /* create thread fail if returned not 0 */
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "preview PRV_ParseMsgProc thread create fail\n");
		return -1; /*can not create*/
	}

	err = pthread_create(&file_id, 0, PRV_FileThread, NULL);
	if(err != 0) /* create thread fail if returned not 0 */
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "preview PRV_TestThread thread create fail\n");
		return -1; /*can not create*/
	}
	
	#if 0
	err = pthread_create(&test_id, 0, PRV_TestThread, NULL);
	if(err != 0) /* create thread fail if returned not 0 */
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "preview PRV_TestThread thread create fail\n");
		return -1; /*can not create*/
	}
	#endif

//	err = pthread_create(&comtid, NULL, VOA_ParseMsgProc, NULL);
	
//	atexit(exit_mpp_sys);
	//printf("-------------Preview OK!------------------\n");
	return OK;/*init success*/	
}

#if defined(USE_UI_OSD)
void PRV_Refresh_UiOsd()
{
	Prv_Disp_OSD(DHD0);
	return;
}
#endif

int Prv_Query_NP_Suc(void)
{
	if(s_State_Info.bIsNpfinish)
	{
		return s_s32NPFlagDflt;
	}
	return HI_FAILURE;
}

//---------------------------------------------------
//---------------------------------------------------
//---------------------------------------------------
#if 0
//画面切换相关测试线程
static char PRV_getch_t(void)
{
	int n = 1;
	unsigned char ch;
	struct timeval tv;
	fd_set rfds;

	FD_ZERO(&rfds);
	FD_SET(0, &rfds);
	tv.tv_sec = 10;
	tv.tv_usec = 0;
	n = select(1, &rfds, NULL, NULL, &tv);
	if (n > 0) {
			n = read(0, &ch, 1);
			if (n == 1)
				return ch;

			return n;
	}
	return -1;
}

void *PRV_TestThread(void *param)
{
	SCM_StopSwitch	stStopSwitch;
	Enter_chn_ctrl_Req StEnterReq;
	Layout_crtl_Req LayoutReq;
	Exit_chn_ctrl_Req StExitReq;
	printf("=-------=-==-=--==--==--==Begin PRV_TestThread\n");
	sleep(20);
	while(1)
	{	
		printf("=-------=-==-=--==--==--==Begin PRV_TestThread=============TEST\n");
		#if 0
		//进入报警弹出
		SN_MEMSET(&stStopSwitch, 0, sizeof(stStopSwitch));
		stStopSwitch.AlmInChn = 0;
		stStopSwitch.chn = 1;
		stStopSwitch.SerialNo = 0;
		SendMessageEx(SUPER_USER_ID, MOD_ALM, MOD_SCM, 0, 0, MSG_ID_SCM_POPALM_IND, &stStopSwitch, sizeof(SCM_StopSwitch));
		usleep(400 * 1000);
		//切换到九画面
		printf("=------PRV_TestThread=============TEST: MSG_ID_PRV_LAYOUT_CTRL_REQ to NineScene\n");
		SN_MEMSET(&LayoutReq, 0, sizeof(LayoutReq));
		LayoutReq.mode = NineScene;
		LayoutReq.chn = 0;
		LayoutReq.flag = SwitchDecode;
		SendMessageEx(SUPER_USER_ID, MOD_SCM, MOD_PRV, 0, 0, MSG_ID_PRV_LAYOUT_CTRL_REQ, &LayoutReq, sizeof(Layout_crtl_Req));			
		usleep(300 * 1000);

		//九画面下双击
		printf("=------PRV_TestThread=============TEST: MSG_ID_PRV_ENTER_CHN_CTRL_REQ---DoubleClick\n");
		SN_MEMSET(&StEnterReq, 0, sizeof(StEnterReq));
		StEnterReq.chn = 0;
		StEnterReq.flag = 6;
		StEnterReq.mouse_pos.x = 500;
		StEnterReq.mouse_pos.y = 150;
		SendMessageEx(SUPER_USER_ID, MOD_ALM, MOD_PRV, 0, 0, MSG_ID_PRV_ENTER_CHN_CTRL_REQ, &StEnterReq, sizeof(StEnterReq));
		usleep(300 * 1000);	

		//报警弹出
		printf("=------PRV_TestThread=============TEST: MSG_ID_SCM_POPALM_IND\n");
		SN_MEMSET(&stStopSwitch, 0, sizeof(stStopSwitch));
		stStopSwitch.AlmInChn = 0;
		stStopSwitch.chn = 3;
		stStopSwitch.SerialNo = 0;
		SendMessageEx(SUPER_USER_ID, MOD_ALM, MOD_SCM, 0, 0, MSG_ID_SCM_POPALM_IND, &stStopSwitch, sizeof(SCM_StopSwitch));
		usleep(300 * 1000);

		//报警弹出下双击
		printf("=------PRV_TestThread=============TEST: MSG_ID_PRV_ENTER_CHN_CTRL_REQ---PopAlarm DoubleClick\n");
		SN_MEMSET(&StEnterReq, 0, sizeof(StEnterReq));
		StEnterReq.chn = 0;
		StEnterReq.flag = 6;
		StEnterReq.mouse_pos.x = 500;
		StEnterReq.mouse_pos.y = 150;
		SendMessageEx(SUPER_USER_ID, MOD_ALM, MOD_PRV, 0, 0, MSG_ID_PRV_ENTER_CHN_CTRL_REQ, &StEnterReq, sizeof(StEnterReq));
		usleep(300 * 1000);

		//退出报警
		printf("=------PRV_TestThread=============TEST: MSG_ID_PRV_EXIT_CHN_CTRL_REQ---Exit PopAlarm\n");
		SN_MEMSET(&StExitReq, 0, sizeof(StExitReq));
		StExitReq.flag = 5;
		SendMessageEx(SUPER_USER_ID, MOD_ALM, MOD_PRV, 0, 0, MSG_ID_PRV_EXIT_CHN_CTRL_REQ, &StExitReq, sizeof(StExitReq));
		usleep(300 * 1000);
		#endif
		
		//切换到九画面
		//printf("=------PRV_TestThread=============TEST: MSG_ID_PRV_EXIT_CHN_CTRL_REQ---Layout To NineScene\n");
		SN_MEMSET(&LayoutReq, 0, sizeof(LayoutReq));
		LayoutReq.mode = NineScene;
		LayoutReq.chn = 0;
		LayoutReq.flag = SwitchDecode;
		SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_SCM, 0, 0, MSG_ID_PRV_LAYOUT_CTRL_REQ, &LayoutReq, sizeof(Layout_crtl_Req));			
		usleep(3000 * 1000);
		
		SN_MEMSET(&LayoutReq, 0, sizeof(LayoutReq));
		LayoutReq.mode = SixteenScene;
		LayoutReq.chn = 0;
		LayoutReq.flag = SwitchDecode;
		SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_SCM, 0, 0, MSG_ID_PRV_LAYOUT_CTRL_REQ, &LayoutReq, sizeof(Layout_crtl_Req));			
		usleep(3000 * 1000);

		//切换到单画面
		//printf("=------PRV_TestThread=============TEST: MSG_ID_PRV_EXIT_CHN_CTRL_REQ---Layout To NineScene\n");
		SN_MEMSET(&LayoutReq, 0, sizeof(LayoutReq));
		LayoutReq.mode = SingleScene;
		LayoutReq.chn = 0;
		LayoutReq.flag = SwitchDecode;
		SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_SCM, 0, 0, MSG_ID_PRV_LAYOUT_CTRL_REQ, &LayoutReq, sizeof(Layout_crtl_Req));			
		usleep(3000 * 1000);
		
		//切换到十六画面
		//printf("=------PRV_TestThread=============TEST: MSG_ID_PRV_EXIT_CHN_CTRL_REQ---Layout To SixteenScene\n");
		SN_MEMSET(&LayoutReq, 0, sizeof(LayoutReq));
		LayoutReq.mode = SixteenScene;
		LayoutReq.chn = 0;
		LayoutReq.flag = SwitchDecode;
		SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_SCM, 0, 0, MSG_ID_PRV_LAYOUT_CTRL_REQ, &LayoutReq, sizeof(Layout_crtl_Req));			
		usleep(3000 * 1000);

		SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_SCM, 0, 0, MSG_ID_PRV_LAYOUT_CTRL_REQ, &LayoutReq, sizeof(Layout_crtl_Req));			
		sleep(3);
	}
}
#endif

//生产测试
sem_t sem_TestSendData;

void PRV_TestInitVochnInfo(VI_CHN chn)
{
	int i = 0;
	if(chn < 0 || chn > DEV_CHANNEL_NUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "---------invalid channel!!line: %d\n", chn, __LINE__);
		return;
	}
	if(chn < LOCALVEDIONUM)
	{
		VochnInfo[chn].IsLocalVideo = 1;
		VochnInfo[chn].VdecChn = -1;
		if(chn >= DEV_CHANNEL_NUM/PRV_CHIP_NUM)
			VochnInfo[chn].SlaveId = 1;
		else
			VochnInfo[chn].SlaveId = 0;			
	}
	else if(chn >= LOCALVEDIONUM && chn < DEV_CHANNEL_NUM/PRV_CHIP_NUM)
	{
		VochnInfo[chn].IsLocalVideo = 0;
		VochnInfo[chn].VdecChn = DetVLoss_VdecChn;			
		VochnInfo[chn].SlaveId = PRV_MASTER;
	}
	else
	{
		#if defined(SN9234H1)		
		VochnInfo[chn].IsLocalVideo = 0;
		VochnInfo[chn].VdecChn = DetVLoss_VdecChn;			
		VochnInfo[chn].SlaveId = PRV_MASTER;
		#endif
	}

	VochnInfo[chn].VoChn = chn;
	VochnInfo[chn].CurChnIndex = VochnInfo[chn].VoChn - LOCALVEDIONUM;
	VochnInfo[chn].VideoInfo.vdoType= JPEGENC;
	VochnInfo[chn].VideoInfo.framerate = 0;
	VochnInfo[chn].VideoInfo.height = 0;
	VochnInfo[chn].VideoInfo.width = 0;
	VochnInfo[chn].AudioInfo.adoType = -1;
//	VochnInfo[chn].AudioInfo.bitwide = 0;
	VochnInfo[chn].AudioInfo.samrate = 0;
	VochnInfo[chn].AudioInfo.soundchannel = 0;
	VochnInfo[chn].IsHaveVdec = 0;
	VochnInfo[chn].IsConnect = 0;
#if defined(SN9234H1)
	for(i = 0; i < PRV_VO_DEV_NUM; i++)
		VochnInfo[chn].IsBindVdec[i] = -1;
#else	
	for(i = 0; i < PRV_VO_MAX_DEV; i++)
		VochnInfo[chn].IsBindVdec[i] = -1;
#endif		
	VochnInfo[chn].PrvType = 0;		
	VochnInfo[chn].bIsStopGetVideoData = 0;
	VochnInfo[chn].VdecCap = 0;
	VochnInfo[chn].bIsWaitIFrame = 0;
	VochnInfo[chn].bIsWaitGetIFrame = 0;
	

}

STATIC void* PRV_TestNVR_VLossDet()
{
	//printf("------NVR VLossDetProc\n");

	unsigned char PicBuff1[TESTPICBUFFSIZE], PicBuff2[TESTPICBUFFSIZE];
	int dataLen1 = 0, dataLen2 = 0;
	//int s32DataLen1 = 0, s32DataLen2 = 0;
	VDEC_STREAM_S stVstream1, stVstream2;
	
	SN_MEMSET(PicBuff1, 0, TESTPICBUFFSIZE);	
	SN_MEMSET(PicBuff2, 0, TESTPICBUFFSIZE);
	
	dataLen1 = PRV_ReadNvrNoVideoPic(NVR_NOVIDEO_FILE_TEST_1, PicBuff1);
	if(dataLen1 <= 0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "--------Read Test VideoFile: %s---fail!\n", NVR_NOVIDEO_FILE_TEST_1);
		return (void*)(-1);
	}
	stVstream1.pu8Addr = PicBuff1;
	stVstream1.u32Len = dataLen1; 
	stVstream1.u64PTS = 0;
	dataLen2 = PRV_ReadNvrNoVideoPic(NVR_NOVIDEO_FILE_TEST_2, PicBuff2);
	if(dataLen2 <= 0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "--------Read Test VideoFile: %s---fail!\n", NVR_NOVIDEO_FILE_TEST_2);
		return (void*)(-1);
	}
	stVstream2.pu8Addr = PicBuff2;
	stVstream2.u32Len = dataLen2; 
	stVstream2.u64PTS = 0;
	//printf("------------dataLen1: %d\n", dataLen1);
	//printf("------------dataLen2: %d\n", dataLen2);

#if defined(SN9234H1)
	int i = 0, j = 0;

	for(i = 0; i < PRV_VO_DEV_NUM; i++)
	{
		if(i == SPOT_VO_DEV || i == AD)
			continue;
		for(j = LOCALVEDIONUM; j < PRV_CHAN_NUM; j++)
		{			
			HI_MPI_VDEC_BindOutput(DetVLoss_VdecChn, i, VochnInfo[j].VoChn);			
		}
	}
	int count = 0, s32Ret = 0;
#else
	int i = 0, count = 0, s32Ret = 0;
	for(i = LOCALVEDIONUM; i < PRV_CHAN_NUM; i++)
	{
		if(VochnInfo[i].IsBindVdec[DHD0] == -1 && VochnInfo[i].IsBindVdec[DSD0] == -1)
		{
			if(HI_SUCCESS == PRV_VDEC_BindVpss(DetVLoss_VdecChn, VochnInfo[i].VoChn))
			{
				VochnInfo[i].IsBindVdec[DHD0] = 0;
				VochnInfo[i].IsBindVdec[DSD0] = 0;
			}
		}
	}
#endif
	//printf("------------Begin NVR Video Loss Detect!!!\n");
#if defined(SN_SLAVE_ON)
	sem_wait(&sem_TestSendData);
#endif
	while(1)
	{
		if((count % 2) == 0)
		{
			//printf("------Send Pic11111\n");
			s32Ret = HI_MPI_VDEC_SendStream(DetVLoss_VdecChn, &stVstream1, HI_IO_BLOCK);  /* 送至视频解码器 */		
			if (s32Ret != HI_SUCCESS)
			{
				//printf("send vdec chn %d stream error%#x!\n", DetVLoss_VdecChn, s32Ret);
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "send vdec chn %d stream error%#x!\n", DetVLoss_VdecChn, s32Ret);
			}
			count++;
		}
		else
		{
			//printf("------Send Pic222222\n");
			s32Ret = HI_MPI_VDEC_SendStream(DetVLoss_VdecChn, &stVstream2, HI_IO_BLOCK);  /* 送至视频解码器 */		
			if (s32Ret != HI_SUCCESS)
			{
				//printf("send vdec chn %d stream error%#x!\n", DetVLoss_VdecChn, s32Ret);
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "send vdec chn %d stream error%#x!\n", DetVLoss_VdecChn, s32Ret);
			}
			count = 0;
		}		
		usleep(500 * 1000);
	}

	return NULL;
}
#if defined(SN9234H1)	
STATIC void* PRV_Test_ParseMsgProc (void *param)
{

	SN_MSG *msg_req = NULL;

	int queue, ret;
	
	queue = CreatQueque(MOD_PRV);
	if (queue <= 0)
	{
		//printf("PRV_PRV:  PRV_PRV: CreateQueue Failed: queue = %d", queue);
		return NULL;
	}
	for (;;)
	{
		msg_req = SN_GetMessage(queue, MSG_GET_WAIT_ROREVER, &ret);
		if (ret < 0)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_PRV: SN_GetMessage Failed: %#x", ret);
			continue;
		}
		if (NULL == msg_req)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_PRV: SN_GetMessage return Null Pointer!");
			continue;
		}
		
		switch(msg_req->msgId)
		{
			case MSG_ID_PRV_MCC_PREVIEW_TEST_START_RSP:
			{
				//printf("-----------Receive Message: MSG_ID_PRV_MCC_PREVIEW_TEST_START_RSP\n");
				sem_post(&sem_TestSendData);
			}
				break;
				
			case MSG_ID_PRV_MCC_LAYOUT_CTRL_RSP: //主片画面切换请求
			{
				//printf("-----------Receive Message: MSG_ID_PRV_MCC_LAYOUT_CTRL_RSP\n");
				//Msg_id_prv_Rsp rsp;
				Prv_Slave_Layout_crtl_Rsp *slave_rsp = (Prv_Slave_Layout_crtl_Rsp *)msg_req->para;
				//printf("slave_rsp->enPreviewMode: %d, slave_rsp->chn: %d\n", slave_rsp->enPreviewMode, slave_rsp->chn);

				if (slave_rsp->enPreviewMode == SingleScene)
				{
					PRV_SingleChn(DHD0, slave_rsp->chn);
					//PRV_SingleChn(SD, slave_rsp->chn);
				} 
				else
				{
					PRV_MultiChn(DHD0, slave_rsp->enPreviewMode, slave_rsp->chn);
					//PRV_MultiChn(SD, slave_rsp->enPreviewMode, slave_rsp->chn);					
					//PRV_SingleChn(SD, slave_rsp->chn);
				}
			}
				break;
				
			default:
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s Get unknown or unused message: %#x\n", __FUNCTION__, msg_req->msgId);
			break;
		}
		
		
		SN_FreeMessage(&msg_req);
	}
	
	return NULL;
}	
#endif
int Preview_Test_Init_Param(void)
{
	HI_S32 i=0,j=0,val=0;
	//预览配置
	for(i = 0;i < PRV_VO_MAX_DEV; i++)
	{
		/*配置预览通道顺序*/
		for (j = 0; j < SEVENINDEX; j++)
		{
			if(j < PRV_VO_CHN_NUM)
			{
				val = j;
			}
			else
			{
				val = -1;
			}
			//配置预览单画面顺序
			s_astVoDevStatDflt[i].as32ChnOrder[SingleScene][j] = val;
			//配置预览3画面顺序
			s_astVoDevStatDflt[i].as32ChnOrder[ThreeScene][j] = val;
			//配置预览5画面顺序
			s_astVoDevStatDflt[i].as32ChnOrder[FiveScene][j] = val;
			//配置预览7画面顺序
			s_astVoDevStatDflt[i].as32ChnOrder[SevenScene][j] = val;
			//配置预览4画面顺序
			s_astVoDevStatDflt[i].as32ChnOrder[FourScene][j] = val;
			//配置预览6画面顺序
			s_astVoDevStatDflt[i].as32ChnOrder[SixScene][j] = val;
			//配置预览8画面顺序
			s_astVoDevStatDflt[i].as32ChnOrder[EightScene][j] = val;
			//配置预览9画面顺序
			s_astVoDevStatDflt[i].as32ChnOrder[NineScene][j] = val;
			//配置预览16画面顺序
			s_astVoDevStatDflt[i].as32ChnOrder[SixteenScene][j] = val;
			
			//printf("#############preview_info.ChannelReset[j] = %d#################### preview_info.PreviewMode = %d\n",s_astVoDevStatDflt[i].as32ChnOrder[NineScene][j],s_astVoDevStatDflt[i].enPreviewMode);
		}
		
	}
	return 0;
}
int Preview_Test_Init(void)
{	

#if defined(SN9234H1)
	pthread_t detVLoss_id, NVR_detVLoss_id;
	int err;
	int i = 0;
	
	IsTest = 1;
	g_Max_Vo_Num = detect_video_input_num();
	s_bIsSysInit = HI_TRUE;
	
	PRV_Init_M240();

	Fb_clear_step1();
	Fb_clear_step2();
	PRV_SysInit();
	PRV_ViInit();
	PRV_VoInit();

#else

	pthread_t NVR_detVLoss_id;
	int err = 0, s32Ret = 0, i = 0;
	IsTest = 1;
	g_Max_Vo_Num = DEV_CHANNEL_NUM;
	s_bIsSysInit = HI_TRUE;
	PRV_SysInit();
	
	Fb_clear_step1();
	Fb_clear_step2();
	
	for(i = 0; i < PRV_VO_CHN_NUM; i++)
		CHECK(PRV_VPSS_Start(i));
	PRV_VoInit();
#if defined(Hi3531)
	tw2865Init(0);
#endif
	if (HI_SUCCESS != SAMPLE_COMM_VO_HdmiStart(s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfSync))
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Start SAMPLE_COMM_VO_HdmiStart failed!\n");
        return -1;
    }
	for(i = 0; i < DEV_CHANNEL_NUM; i++)
	{
		s32Ret = PRV_VO_BindVpss(DHD0, i, i, VPSS_PRE0_CHN);
	    if (HI_SUCCESS != s32Ret)
	    {
	        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"SAMPLE_COMM_VO_BindVpss failed!\n");
	        return -1;
	    }
	}
#endif	
	Preview_Test_Init_Param();
	
	if(MAX_IPC_CHNNUM > 0)
	{
#if defined(SN9234H1)
		PRV_CreateVdecChn(JPEGENC, 576, 704, RefFrameNum, DetVLoss_VdecChn);
#else
		PRV_CreateVdecChn(JPEGENC, NOVIDEO_VDECHEIGHT*2, NOVIDEO_VDECWIDTH*2, RefFrameNum, DetVLoss_VdecChn);
#endif
	}
	for(i = 0; i < DEV_CHANNEL_NUM; i++)
	{
		PRV_TestInitVochnInfo(i);
	}
	
	PRV_PreviewInit();
	
	err = sem_init(&sem_TestSendData, 0, 0);
	if(err != 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Error in sem_init OSD_time_Sem\n");
		return -1;
	}


	err = pthread_create(&NVR_detVLoss_id, NULL, PRV_TestNVR_VLossDet, NULL);
	if(err != 0) 
	{	
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "preview PRV_TestNVR_VLossDet thread create fail\n");
		return -1; 
	}
#if defined(SN9234H1)
	err = pthread_create(&detVLoss_id, NULL, PRV_VLossDetProc, NULL);
	if(err != 0) 
	{	
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "preview PRV_VLossDetProc thread create fail\n");
		return -1; 
	}

	pthread_t prv_id;
	err = pthread_create(&prv_id, NULL, PRV_Test_ParseMsgProc, NULL);
	if(err != 0) 
	{
		perror("pthread_create: PRV_Test_ParseMsgProc");
		return -1; 
	}

	SN_SendMccMessageEx(PRV_SLAVE_1,SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_PREVIEW_TEST_START_REQ, NULL, 0);
#endif
	
	PRV_TEST_PreviewMode(0);
	sem_post(&sem_TestSendData);
	return 0;
}

int Preview_Test_Vo_Select(unsigned char vo_type)
{	
#if defined(SN9234H1)
	PRV_VoInit();
	PRV_RefreshVoDevScreen(HD,DISP_DOUBLE_DISP,s_astVoDevStatDflt[HD].as32ChnOrder[s_astVoDevStatDflt[HD].enPreviewMode]);
	PRV_RefreshVoDevScreen(SD,DISP_NOT_DOUBLE_DISP,s_astVoDevStatDflt[HD].as32ChnOrder[s_astVoDevStatDflt[HD].enPreviewMode]);
#else
	PRV_VoInit();
	if((s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfType & VO_INTF_HDMI) == VO_INTF_HDMI)
	{
		if (HI_SUCCESS != SAMPLE_COMM_VO_HdmiStart(s_astVoDevStatDflt[DHD0].stVoPubAttr.enIntfSync))
		{
		   TRACE(SCI_TRACE_NORMAL, MOD_PRV, "TEST Start SAMPLE_COMM_VO_HdmiStart failed!\n");
		    return -1;
		}
	}
	PRV_RefreshVoDevScreen(DHD0, DISP_DOUBLE_DISP, s_astVoDevStatDflt[DHD0].as32ChnOrder[s_astVoDevStatDflt[DHD0].enPreviewMode]);
#endif
	return 0;
}

int PRV_TEST_PreviewMode(int chn)
{
	PRV_PREVIEW_MODE_E enPreviewMode = SingleScene;
	HI_U32 u32Index = 0;
	HI_S32 i = 0;

	if (chn<0 || chn > PRV_VO_CHN_NUM)
	{
		RET_FAILURE("bad input parameter: chn");
	}

	if (0 == chn)
	{
		u32Index = 0;
		enPreviewMode = PRV_VO_MAX_MOD;
	}
	else
	{
		for(i = 0; i < PRV_VO_CHN_NUM; i++)
		{
#if defined(SN9234H1)
			if (s_astVoDevStatDflt[HD].as32ChnOrder[s_astVoDevStatDflt[HD].enPreviewMode][i] == chn-1)
#else
			if (s_astVoDevStatDflt[DHD0].as32ChnOrder[s_astVoDevStatDflt[DHD0].enPreviewMode][i] == chn - 1)
#endif				
			{
				u32Index = i;
				break;
			}
		}
		if (i == PRV_VO_CHN_NUM)
		{
			RET_FAILURE("the chn is hiden!!");
		}
		enPreviewMode = SingleScene;
	}
#if defined(SN9234H1)
	PRV_PreviewVoDevInMode(HD, enPreviewMode, u32Index, s_astVoDevStatDflt[HD].as32ChnOrder[s_astVoDevStatDflt[HD].enPreviewMode]);
	//PRV_PreviewVoDevInMode(AD, enPreviewMode, u32Index, s_astVoDevStatDflt[AD].as32ChnOrder[s_astVoDevStatDflt[AD].enPreviewMode]);
#else
	PRV_PreviewVoDevInMode(DHD0, enPreviewMode, u32Index, s_astVoDevStatDflt[DHD0].as32ChnOrder[s_astVoDevStatDflt[DHD0].enPreviewMode]);
	PRV_PreviewVoDevInMode(DSD0, enPreviewMode, u32Index, s_astVoDevStatDflt[DSD0].as32ChnOrder[s_astVoDevStatDflt[DSD0].enPreviewMode]);
#endif
	RET_SUCCESS("");
}


/***************************** END HEAR *****************************/

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

