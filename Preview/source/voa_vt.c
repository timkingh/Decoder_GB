/******************************************************************************

  Copyright (C), Star-Net

 ******************************************************************************
  File Name     : voa_vt.c
  Version       : Initial Draft
  Author        : 
  Created       : 2010/06/29
  Description   : voice talk module implement file
  History       :
  1.Date        : 
    Author      : caorh
    Modification: Created file

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

#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

//#include <stdio.h>
//#include <sys/types.h>
#include <net/if.h>
//#include <netinet/in.h>
//#include <sys/socket.h>
#include <linux/sockios.h>
//#include <sys/ioctl.h>
//#include <arpa/inet.h>


#include "hi_comm_aio.h"
#include "hi_comm_adec.h"
#include "hi_comm_aenc.h"

#include "mpi_ai.h"
#include "mpi_ao.h"
#include "mpi_aenc.h"
#include "mpi_adec.h"

#include "disp_api.h"
#include "global_str.h"

/************************ MACROS HEAR *************************/

//#define MOD_VOA MOD_VOA //输出消息转为MOD_VOA模块的。

#define STATIC static		//控制本文件内函数是否允许外部引用

#define PRV_AI_DEV_NUM SIO_MAX_NUM					//AI设备数量
#define PRV_AO_DEV_NUM SIO_MAX_NUM					//AO设备数量

#define PRV_AI_CHN_NUM VIU_MAX_CHN_NUM				//每个AI设备的通道数量
#define PRV_AO_CHN_NUM 2							//每个AO设备的通道数量

#define PRV_AUDIO_OFF		255		//无音频

//在此配置音频有关属性
#define AUDIO_PTNUMPERFRM	160//160//320//480
#define AUDIO_SAMPLERATE	AUDIO_SAMPLE_RATE_8000
#define AUDIO_BITWIDTH		AUDIO_BIT_WIDTH_16//AUDIO_BIT_WIDTH_16
#define AUDIO_TYPE			PT_G711A

#define AUDIO_ADPCM_TYPE	ADPCM_TYPE_DVI4/* ADPCM_TYPE_IMA, ADPCM_TYPE_DVI4*/
#define AUDIO_AAC_TYPE		AAC_TYPE_AACLC   /* AAC_TYPE_AACLC, AAC_TYPE_EAAC, AAC_TYPE_EAACPLUS*/
#define G726_BPS			MEDIA_G726_16K         /* MEDIA_G726_16K, MEDIA_G726_24K ... */
#define AMR_FORMAT			AMR_FORMAT_MMS       /* AMR_FORMAT_MMS, AMR_FORMAT_IF1, AMR_FORMAT_IF2*/
#define AMR_MODE			AMR_MODE_MR74         /* AMR_MODE_MR122, AMR_MODE_MR102 ... */
#define AMR_DTX				0

#define VIDEO_TALK_SIZE_MAX 1472
#define VIDEO_HEAD_SIZE_MAX (sizeof(VoiceHeader) + sizeof(Soket_DATA_HEADER))

/************************ DATA TYPE HEAR *************************/

#define	RPC_DATA_TYPE	0		/*数据负载是RPC调用数据*/
#define	ALARM_DATA_TYPE	1		/*数据负载是报警数据*/
#define TALKBACK_DATA_TYPE	2	/*数据负载是对讲数据*/
#define FILE_TRANSMISSION_TYPE	3	/*数据负载是文件下载协议数据*/
#define RPC_DEVICE_INFO				4	/*设备信息类型用于初始登录返回设备信息*/
#define RPC_MCAST_DATA				5	/*设备广播类型用于设备广播，客户端被动接收消息*/

typedef struct _Soket_DATA_HEADER
{
	unsigned int  tag;			/*STAR_TAG */
	unsigned int payloadlen;	/*负载数据长度*/
	unsigned int type;		/*负载数据类型*/
	unsigned long long  Cseq;				/*数据包序列*/
}__attribute__ ((packed)) Soket_DATA_HEADER, *PSoket_DATA_HEADER;

typedef struct _VoiceHeader 
{
	int Cseq;		/*从0开始递增*/
	unsigned short	datalen;	/*数据长度*/
	unsigned short  flags;		/*标志 1(第0位)表示这是最后一个包 2(第1位)表示这是第一个包，一个包可以同时具有这两个属性 4(第2位)表示对丢包不关注*/
}VoiceHeader, *PVoiceHeader;


/************************ GLOBALS HEAR *************************/


static HI_BOOL s_bAioReSample = HI_FALSE;


static AENC_CHN_ATTR_S s_stAencAttrDflt = {
    .enType = AUDIO_TYPE,//AUDIO_Type,
    .u32BufSize = 5,
    .pValue = NULL, 
};

static ADEC_CHN_ATTR_S s_stAdecAttrDflt = {
	.enType = AUDIO_TYPE,//AUDIO_Type,
	.u32BufSize = 20,
	.enMode = ADEC_MODE_PACK,
	.pValue = NULL,
};

static AUDIO_RESAMPLE_ATTR_S s_stAiReSmpAttr = {
	.u32InPointNum = AUDIO_PTNUMPERFRM*4,
	.enInSampleRate = AUDIO_SAMPLERATE*4,//AUDIO_Samplerate*4,
	.enReSampleType = AUDIO_RESAMPLE_4X1,
};

static AUDIO_RESAMPLE_ATTR_S s_stAoReSmpAttr = {
	.u32InPointNum = AUDIO_PTNUMPERFRM,
	.enInSampleRate = AUDIO_SAMPLERATE,//AUDIO_Samplerate,
	.enReSampleType = AUDIO_RESAMPLE_1X4,
};

#if defined(Hi3520)

static AIO_ATTR_S s_stAioAttrDflt = {
	.enWorkmode = AIO_MODE_I2S_SLAVE,
	.enBitwidth = AUDIO_BITWIDTH,//AUDIO_Bitwidth,
	.enSamplerate = AUDIO_SAMPLERATE,//AUDIO_Samplerate,
	.enSoundmode = AUDIO_SOUND_MODE_MOMO,
	.u32EXFlag = 0,
	.u32FrmNum = 5,
	.u32PtNumPerFrm = AUDIO_PTNUMPERFRM,
    .u32ClkSel = 0,
};

#elif defined(Hi3531)

static AIO_ATTR_S s_stAioAttrDflt = {
	.enWorkmode = I2S_WORK_MODE	,
	.enBitwidth = AUDIO_BITWIDTH,//AUDIO_Bitwidth,
	.enSamplerate = AUDIO_SAMPLERATE,//AUDIO_Samplerate,
	.enSoundmode = AUDIO_SOUND_MODE_MONO,
	.u32EXFlag = 0,
	.u32FrmNum = 5,
	.u32PtNumPerFrm = AUDIO_PTNUMPERFRM,
    .u32ClkSel = 0,
    .u32ChnCnt = 2,
};

#elif defined(Hi3535)

static AIO_ATTR_S s_stAioAttrDflt = {
	.enWorkmode = I2S_WORK_MODE	,
	.enBitwidth = AUDIO_BIT_WIDTH_16,//AUDIO_Bitwidth,
	.enSamplerate = AUDIO_SAMPLERATE,//AUDIO_Samplerate,
	.enSoundmode = AUDIO_SOUND_MODE_MONO,
	.u32EXFlag = 1,
	.u32FrmNum = 30,
	.u32PtNumPerFrm = AUDIO_PTNUMPERFRM,
    .u32ClkSel = 0,
    .u32ChnCnt = 2,
};

#endif

static volatile HI_BOOL s_bVoiceTalkOn = HI_FALSE;
//static volatile HI_BOOL s_bExitVoiceTalk = HI_FALSE;

static AENC_CHN s_VoiceTalkAeChnDflt = 16;	//语音对讲默认占用的编码通道号
static ADEC_CHN s_VoiceTalkAdChnDflt = 16;	//语音对讲默认占用的解码通道号

static struct sockaddr_in udpclient_addr;
static struct sockaddr_in udpserver_addr;

static int s_s32VoiceTalkSocketFd = -1;

static pthread_mutex_t s_vt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_send_tid = -1, s_recv_tid = -1;

static BOOLEAN s_abAudioPreview[CHANNEL_NUM];		//当前使用音频
static BOOLEAN s_abAudioMap[CHANNEL_NUM];		//音频映射表
static BOOLEAN s_abAudioEnable[CHANNEL_NUM];		//音频使能数组

static unsigned short s_VtPort;
static int s_s32CurrentUserId = SUPER_USER_ID;
extern HI_U32 g_Max_Vo_Num;

HI_S32 PRV_AUDIO_AencBindAi(AUDIO_DEV AiDev, AI_CHN AiChn, AENC_CHN AeChn);

/************************ FUNCTIONS HEAR *************************/
static const char * gethostip(void)
{
	int s;
	struct ifconf conf;
	struct ifreq *ifr;
	char buff[BUFSIZ];
	int num;
	int i;
	
	s = socket(PF_INET, SOCK_DGRAM, 0);
	conf.ifc_len = BUFSIZ;
	conf.ifc_buf = buff;
	
	ioctl(s, SIOCGIFCONF, &conf);
	num = conf.ifc_len / sizeof(struct ifreq);
	ifr = conf.ifc_req;
	
	for(i=0;i < num;i++)
	{
		struct sockaddr_in *sin = (struct sockaddr_in *)(&ifr->ifr_addr);
		
		ioctl(s, SIOCGIFFLAGS, ifr);
		if(((ifr->ifr_flags & IFF_LOOPBACK) == 0) && (ifr->ifr_flags & IFF_UP))
		{
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s (%s)\n",ifr->ifr_name,inet_ntoa(sin->sin_addr));
			return inet_ntoa(sin->sin_addr);
		}
		ifr++;
	}
	return "NULL";
}

/************************************************************************/
/* 写入语音对讲日志。
                                                                     */
/************************************************************************/
static void VOA_WriteVoiceTalkLog(int type, const char *msg, int userId)
{
    ST_FMG_ADD_LOG_REQ stLog;
    struct tm  newtime;
    time_t ltime;
	UserLogInfo usrInfo;

	if (NULL == msg)
	{
		return;
	}

	GetUserLogInfo(userId, &usrInfo);
	
    time(&ltime);
    localtime_r(&ltime, &newtime); 

	bzero(&stLog, sizeof(stLog));
    SN_STRNCPY(stLog.FileName, sizeof(stLog.FileName), "", 1);
	SN_STRNCPY(stLog.LogInfo, sizeof(stLog.LogInfo), msg, sizeof(stLog.LogInfo));
	if (usrInfo.blocal)
	{
		SN_STRNCPY(stLog.IpAddr, sizeof(stLog.IpAddr), gethostip(), sizeof(stLog.IpAddr));
		SN_STRNCPY(stLog.LocalUser, sizeof(stLog.LocalUser), ADMIN_NAME, sizeof(stLog.LocalUser));
	}
	else
	{
		SN_STRNCPY(stLog.IpAddr, sizeof(stLog.IpAddr), usrInfo.IPAddress, sizeof(stLog.IpAddr));
		SN_STRNCPY(stLog.RemoteUser, sizeof(stLog.RemoteUser), usrInfo.UserName, sizeof(stLog.RemoteUser));
	}
	
	stLog.Channel = 0;
	stLog.MajorType = PRM_ID_LOG_MAJOR_OPERARATION;
    stLog.MinorType = type;
    stLog.LogTime.Year= newtime.tm_year + 1900;
    stLog.LogTime.Month = newtime.tm_mon + 1;
    stLog.LogTime.Day = newtime.tm_mday;
    stLog.LogTime.Hour = newtime.tm_hour;
    stLog.LogTime.Minute = newtime.tm_min;
    stLog.LogTime.Second = newtime.tm_sec;

    SendMessageEx(SUPER_USER_ID, MOD_FWK,MOD_WLOG, 0, 0, MSG_ID_FMG_ADD_LOG_REQ,&stLog, sizeof(ST_FMG_ADD_LOG_REQ));
}

/************************************************************************/
/* 初始化音频：启动AI,AO，启动AI通道0，AO通道0，创建编码通道0，解码通道0，绑定通道。
                                                                     */
/************************************************************************/
HI_S32 PRV_AiInit(HI_VOID)
{
	AUDIO_DEV AiDev = PRV_VOA_AUDIO_DEV;
	AI_CHN AiChn = 0;
	AENC_CHN AeChn = s_VoiceTalkAeChnDflt;

	s_stAioAttrDflt.enSamplerate = s_bAioReSample ? s_stAiReSmpAttr.enInSampleRate : s_stAoReSmpAttr.enInSampleRate;
	//s_stAioAttrDflt.u32PtNumPerFrm = s_bAioReSample ? s_stAiReSmpAttr.u32InPointNum : s_stAoReSmpAttr.u32InPointNum;
	s_stAioAttrDflt.u32PtNumPerFrm = AUDIO_PTNUMPERFRM*2;
	PRV_TW2865_CfgAudio(s_stAioAttrDflt.enSamplerate);
	// AI
	s_stAioAttrDflt.u32ChnCnt = 2;	/*PRV_AI_CHN_NUM*/
	
    CHECK_RET(HI_MPI_AI_SetPubAttr(AiDev, &s_stAioAttrDflt));
	
    CHECK_RET(HI_MPI_AI_Enable(AiDev));

    CHECK_RET(HI_MPI_AI_EnableChn(AiDev, AiChn));
	
	if (HI_TRUE == s_bAioReSample)
	{
		CHECK_RET(HI_MPI_AI_EnableReSmp(AiDev, AiChn, &s_stAiReSmpAttr));
	}
	// AENC
	switch(s_stAencAttrDflt.enType)
	{
	case PT_ADPCMA:
		{
			AENC_ATTR_ADPCM_S stAdpcmAenc;
			s_stAencAttrDflt.pValue       = &stAdpcmAenc;
			stAdpcmAenc.enADPCMType = AUDIO_ADPCM_TYPE;
		}
		break;
	case PT_G711A:
	case PT_G711U:
		{
			AENC_ATTR_G711_S stAencG711;
			s_stAencAttrDflt.pValue       = &stAencG711;
		}
		break;
	case PT_G726:
		{
			AENC_ATTR_G726_S stAencG726;
			s_stAencAttrDflt.pValue       = &stAencG726;
			stAencG726.enG726bps    = G726_BPS;
		}
		break;
#if defined(SN9234H1)
	case PT_AMR:
		{
			AENC_ATTR_AMR_S stAencAmr;
			s_stAencAttrDflt.pValue       = &stAencAmr;
			stAencAmr.enFormat      = AMR_FORMAT ;
			stAencAmr.enMode        = AMR_MODE ;
			stAencAmr.s32Dtx        = AMR_DTX ;
		}
		break;
	case PT_AAC:
		{
			AENC_ATTR_AAC_S stAencAac;
			s_stAencAttrDflt.pValue       = &stAencAac;
			stAencAac.enAACType     = AUDIO_AAC_TYPE;        
			stAencAac.enBitRate     = AAC_BPS_128K;
			stAencAac.enBitWidth    = AUDIO_BIT_WIDTH_16;
			stAencAac.enSmpRate     = AUDIO_SAMPLE_RATE_16000;
			stAencAac.enSoundMode   = AUDIO_SOUND_MODE_MOMO;
		}
		break;

#endif
	case PT_LPCM:
		{
			AENC_ATTR_LPCM_S stAencLpcm;
			s_stAencAttrDflt.pValue = &stAencLpcm;
		}
		break;
	default :
		RET_FAILURE("invalid aenc payload type!");
	}

	CHECK_RET(HI_MPI_AENC_CreateChn(AeChn, &s_stAencAttrDflt));

	// Bind AI-AO or AI-AENC ADEC-AO
#if defined(SN9234H1)
	CHECK_RET(HI_MPI_AENC_BindAi(AeChn, AiDev, AiChn));

#else
	CHECK_RET(PRV_AUDIO_AencBindAi(AiDev, AiChn,AeChn));
#endif

    RET_SUCCESS("");
}

#if defined(Hi3531)||defined(Hi3535)
HI_S32 AUDIO_AoBindAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn)
{
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_ADEC;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = AdChn;
    stDestChn.enModId = HI_ID_AO;
    stDestChn.s32DevId = AoDev;
    stDestChn.s32ChnId = AoChn;
    HI_S32 s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
	if(s32Ret != HI_SUCCESS)
	{
		printf("AUDIO_AoBindAdec Fail %x\n",s32Ret);
		return HI_FAILURE;
	}
	return HI_SUCCESS;
   // return HI_MPI_SYS_Bind(&stSrcChn, &stDestChn); 
}
HI_S32 PRV_AUDIO_AoUnbindAi(AUDIO_DEV AiDev, AI_CHN AiChn, AUDIO_DEV AoDev, AO_CHN AoChn)
{
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_AI;
    stSrcChn.s32ChnId = AiChn;
    stSrcChn.s32DevId = AiDev;
    stDestChn.enModId = HI_ID_AO;
    stDestChn.s32DevId = AoDev;
    stDestChn.s32ChnId = AoChn;
    
    HI_S32 s32Ret = HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn); 
	if(s32Ret != HI_SUCCESS)
	{
		printf("PRV_AUDIO_AoUnbindAi Fail %x\n",s32Ret);
		return HI_FAILURE;
	}
	return HI_SUCCESS;
}
HI_S32 PRV_AUDIO_AoBindAi(AUDIO_DEV AiDev, AI_CHN AiChn, AUDIO_DEV AoDev, AO_CHN AoChn)
{
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_AI;
    stSrcChn.s32ChnId = AiChn;
    stSrcChn.s32DevId = AiDev;
    stDestChn.enModId = HI_ID_AO;
    stDestChn.s32DevId = AoDev;
    stDestChn.s32ChnId = AoChn;
    
    HI_S32 s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn); 
	if(s32Ret != HI_SUCCESS)
	{
		printf("PRV_AUDIO_AoBindAi Fail %x\n",s32Ret);
		return HI_FAILURE;
	}
	return HI_SUCCESS;
}

/******************************************************************************
* function : Ao unbind Adec
******************************************************************************/
HI_S32 AUDIO_AoUnbindAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn)
{
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_ADEC;
    stSrcChn.s32ChnId = AdChn;
    stSrcChn.s32DevId = 0;
    stDestChn.enModId = HI_ID_AO;
    stDestChn.s32DevId = AoDev;
    stDestChn.s32ChnId = AoChn;
    
    HI_S32 s32Ret = HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn); 
	if(s32Ret != HI_SUCCESS)
	{
		printf("PRV_AUDIO_AoBindAi Fail %x\n",s32Ret);
		return HI_FAILURE;
	}
	return HI_SUCCESS;
}
HI_S32 PRV_AUDIO_AencUnbindAi(AUDIO_DEV AiDev, AI_CHN AiChn, AENC_CHN AeChn)
{
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_AI;
    stSrcChn.s32DevId = AiDev;
    stSrcChn.s32ChnId = AiChn;
    stDestChn.enModId = HI_ID_AENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = AeChn;
    
    return HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn);      
}

/******************************************************************************
* function : Aenc bind Ai
******************************************************************************/
HI_S32 PRV_AUDIO_AencBindAi(AUDIO_DEV AiDev, AI_CHN AiChn, AENC_CHN AeChn)
{
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_AI;
    stSrcChn.s32DevId = AiDev;
    stSrcChn.s32ChnId = AiChn;
    stDestChn.enModId = HI_ID_AENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = AeChn;
    
    return HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
}
#endif

int PRV_AoInitTest(void)
{
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif

	AO_CHN AoChn = 0;
	ADEC_CHN AdChn = s_VoiceTalkAdChnDflt;

	s_stAioAttrDflt.enSamplerate = s_bAioReSample ? s_stAiReSmpAttr.enInSampleRate : s_stAoReSmpAttr.enInSampleRate;
	s_stAioAttrDflt.u32PtNumPerFrm = s_bAioReSample ? s_stAiReSmpAttr.u32InPointNum : s_stAoReSmpAttr.u32InPointNum;

	PRV_TW2865_CfgAudio(s_stAioAttrDflt.enSamplerate);
	// AO
	s_stAioAttrDflt.u32ChnCnt = 2;	/*PRV_AO_CHN_NUM*/
	s_stAioAttrDflt.enBitwidth = AUDIO_BIT_WIDTH_16;
	s_stAioAttrDflt.u32EXFlag = 1;
    CHECK_RET(HI_MPI_AO_SetPubAttr(AoDev, &s_stAioAttrDflt));
	
    CHECK_RET(HI_MPI_AO_Enable(AoDev));
	
    CHECK_RET(HI_MPI_AO_EnableChn(AoDev, AoChn));
	
    if (HI_TRUE == s_bAioReSample)
    {
        CHECK_RET(HI_MPI_AO_EnableReSmp(AoDev, AoChn, &s_stAoReSmpAttr));
    }

	s_stAdecAttrDflt.enType = PT_LPCM;
	// ADEC
	switch(s_stAdecAttrDflt.enType)
	{
	case PT_ADPCMA:
		{
			ADEC_ATTR_ADPCM_S stAdpcm;
			s_stAdecAttrDflt.pValue = &stAdpcm;
			stAdpcm.enADPCMType = AUDIO_ADPCM_TYPE ;			
		}
		break;
	case PT_G711A:
	case PT_G711U:
		{
			ADEC_ATTR_G711_S stAdecG711;
			s_stAdecAttrDflt.pValue = &stAdecG711;	
		}
		break;
	case PT_G726:
		{
			ADEC_ATTR_G726_S stAdecG726;
			s_stAdecAttrDflt.pValue = &stAdecG726;
			stAdecG726.enG726bps = G726_BPS ;     	
		}
		break;
	case PT_LPCM:
		{
			ADEC_ATTR_LPCM_S stAdecLpcm;
			s_stAdecAttrDflt.pValue = &stAdecLpcm;
			s_stAdecAttrDflt.enMode = ADEC_MODE_PACK;/* lpcm must use pack mode */			
		}
		break;
	default :
		RET_FAILURE("invalid aenc payload type!");
	}
	
	CHECK_RET(HI_MPI_ADEC_CreateChn(AdChn, &s_stAdecAttrDflt));

	// Bind AI-AO or AI-AENC ADEC-AO
#if 0
	CHECK_RET(HI_MPI_AO_BindAi(AoDev, AoChn, AiDev, AiChn));
#else
	//CHECK_RET(HI_MPI_AO_BindAdec(AoDev, AoChn, AdChn));
#endif

    RET_SUCCESS("");
}


HI_S32 PRV_AoInit(HI_VOID)
{
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif
	AO_CHN AoChn = 0;
	ADEC_CHN AdChn = s_VoiceTalkAdChnDflt;

	s_stAioAttrDflt.enSamplerate = s_bAioReSample ? s_stAiReSmpAttr.enInSampleRate : s_stAoReSmpAttr.enInSampleRate;
	s_stAioAttrDflt.u32PtNumPerFrm = s_bAioReSample ? s_stAiReSmpAttr.u32InPointNum : s_stAoReSmpAttr.u32InPointNum;

	PRV_TW2865_CfgAudio(s_stAioAttrDflt.enSamplerate);
	// AO
	s_stAioAttrDflt.u32ChnCnt = 2;	/*PRV_AO_CHN_NUM*/
	s_stAioAttrDflt.enBitwidth = AUDIO_BIT_WIDTH_16;
	s_stAioAttrDflt.u32EXFlag = 1;
    CHECK_RET(HI_MPI_AO_SetPubAttr(AoDev, &s_stAioAttrDflt));
	
    CHECK_RET(HI_MPI_AO_Enable(AoDev));
	
    CHECK_RET(HI_MPI_AO_EnableChn(AoDev, AoChn));
	
    if (HI_TRUE == s_bAioReSample)
    {
        CHECK_RET(HI_MPI_AO_EnableReSmp(AoDev, AoChn, &s_stAoReSmpAttr));
    }
	// ADEC
	switch(s_stAdecAttrDflt.enType)
	{
	case PT_ADPCMA:
		{
			ADEC_ATTR_ADPCM_S stAdpcm;
			s_stAdecAttrDflt.pValue = &stAdpcm;
			stAdpcm.enADPCMType = AUDIO_ADPCM_TYPE ;			
		}
		break;
	case PT_G711A:
	case PT_G711U:
		{
			ADEC_ATTR_G711_S stAdecG711;
			s_stAdecAttrDflt.pValue = &stAdecG711;	
		}
		break;
	case PT_G726:
		{
			ADEC_ATTR_G726_S stAdecG726;
			s_stAdecAttrDflt.pValue = &stAdecG726;
			stAdecG726.enG726bps = G726_BPS ;     	
		}
		break;
#if defined(SN9234H1)
	case PT_AMR:
		{
			ADEC_ATTR_AMR_S stAdecAmr;
			s_stAdecAttrDflt.pValue = &stAdecAmr;
			stAdecAmr.enFormat = AMR_FORMAT;	
		}
		break;
	case PT_AAC:
		{
			ADEC_ATTR_AAC_S stAdecAac;
			s_stAdecAttrDflt.pValue = &stAdecAac;
			s_stAdecAttrDflt.enMode = ADEC_MODE_STREAM;/* aac now only support stream mode */	
		}
		break;

#endif
	case PT_LPCM:
		{
			ADEC_ATTR_LPCM_S stAdecLpcm;
			s_stAdecAttrDflt.pValue = &stAdecLpcm;
			s_stAdecAttrDflt.enMode = ADEC_MODE_PACK;/* lpcm must use pack mode */			
		}
		break;
	default :
		RET_FAILURE("invalid aenc payload type!");
	}
	
	CHECK_RET(HI_MPI_ADEC_CreateChn(AdChn, &s_stAdecAttrDflt));

	// Bind AI-AO or AI-AENC ADEC-AO
#if 0
	CHECK_RET(HI_MPI_AO_BindAi(AoDev, AoChn, AiDev, AiChn));
#else
	//CHECK_RET(HI_MPI_AO_BindAdec(AoDev, AoChn, AdChn));
#endif

    RET_SUCCESS("");
}
int PRV_StopAo()
{
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif

	CHECK_RET(HI_MPI_ADEC_ClearChnBuf(s_VoiceTalkAdChnDflt));
	CHECK_RET(HI_MPI_AO_DisableChn(AoDev, 0));
    CHECK_RET(HI_MPI_AO_Disable(AoDev));
	CHECK_RET(HI_MPI_ADEC_DestroyChn(s_VoiceTalkAdChnDflt));
	return 0;
}
int PRV_StopAi()
{
	CHECK_RET(HI_MPI_AENC_DestroyChn(s_VoiceTalkAeChnDflt));
    CHECK_RET(HI_MPI_AI_DisableChn(0,0));
    CHECK_RET(HI_MPI_AI_Disable(0));
	return 0;
}
//重置音频映射表
int PRV_Set_AudioMap(int ch, int pAudiomap)
{
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_Set_AudioMap: ch =%d, pAudiomap=%d\n", ch, pAudiomap);
	if(ch < CHANNEL_NUM && pAudiomap < (DEV_AUDIO_IN_NUM+1))
	{
		s_abAudioMap[ch] = pAudiomap;
	}
	return 0;
}
//设置音频使能
int PRV_Set_AudioPreview_Enable(const BOOLEAN *pbAudioPreview)
{
	int i=0;
	if(pbAudioPreview == NULL)
	{
		RET_FAILURE("null point!");
	}
	for(i=0;i<CHANNEL_NUM;i++)
	{
		//s_abAudioEnable[i] = pbAudioPreview[i];
		s_abAudioEnable[i] = pbAudioPreview[0];
	}
	return 0;
}

//查找当前通道对应音频
int  PRV_ChnAudio_Search(unsigned char chn,BOOLEAN *pbAudio)
{

	//int i=0;
	unsigned char audio_ch=0;
	if(pbAudio == NULL)
	{
		RET_FAILURE("null point!");
	}
	if(chn >= g_Max_Vo_Num)
	{
		RET_FAILURE("chn out range!");
	}
	
	//if(s_abAudioMap[chn] != PRV_AUDIO_OFF)
	{//如果窜在音频
		if(s_abAudioEnable[chn] && s_abAudioMap[chn])
		{//如果音频使能
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "!!!!!!!s_abAudioMap[chn] = %d!!!!!chn =%d!!!!!!!!!!!!!\n",s_abAudioMap[chn] ,chn);
			//audio_ch = s_abAudioMap[chn]?s_abAudioMap[chn] -1 : s_abAudioMap[chn];
			audio_ch = s_abAudioMap[chn] -1;
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------GetAudioChan ch=%d map=%d------\n", chn, audio_ch);
			pbAudio[audio_ch] = 1;
		}
	}
		
	return 0;
}
//放音通路控制： isEnable: 0-Mute 1-On
int PRV_PlayAudioCtrl(int isEnable)
{
	tw2865_master_pb_cfg(isEnable);
	RET_SUCCESS("");
}

//设置通道预览音频
HI_S32 PRV_SetAudioPreview(const BOOLEAN *pbAudioPreview, HI_U32 u32ChnNum)
{
	if (NULL == pbAudioPreview)
	{
		RET_FAILURE("bad parameter!");
	}
	if (u32ChnNum > CHANNEL_NUM)
	{
		TRACE(SCI_TRACE_HIGH, MOD_VOA, TEXT_COLOR_RED("%s, u32ChnNum = %d"), __FUNCTION__, u32ChnNum);
	}

	const HI_U8 au8ChnOrder[AENC_MAX_CHN_NUM] = {0,8,1,9,2,10,3,11,4,12,5,13,6,14,7,15};

	HI_S32 i;
#if defined(Hi3520)
	AUDIO_DEV AiDev = 1;
	static AI_CHN AiChn = -1;/*-1 represent audio preview is disabled*/
	AUDIO_DEV AoDev = 0;
#elif defined(Hi3531)
	AUDIO_DEV AiDev = 4;
	static AI_CHN AiChn = -1;/*-1 represent audio preview is disabled*/
	AUDIO_DEV AoDev = 4;
#elif defined(Hi3535)
	AUDIO_DEV AiDev = 0;
	static AI_CHN AiChn = -1;/*-1 represent audio preview is disabled*/
	AUDIO_DEV AoDev = 0;
#endif	
	AO_CHN AoChn = 0;

	for (i = 0; i < u32ChnNum; i++)
	{
		if (pbAudioPreview[i])
		{
			break;
		}
	}

	if (i != u32ChnNum && AiChn != au8ChnOrder[i])
	{
		if (AiChn != -1)
		{
#if defined(SN9234H1)
			CHECK(HI_MPI_AO_UnBindAi(AoDev, AoChn, AiDev, AiChn));
#else
			CHECK_RET( PRV_AUDIO_AoUnbindAi(AiDev, AiChn, AoDev, AoChn));
#endif
		}
		AiChn = au8ChnOrder[i];
#if defined(SN9234H1)
		CHECK(HI_MPI_AO_BindAi(AoDev, AoChn, AiDev, AiChn));
#else
		CHECK_RET(PRV_AUDIO_AoBindAi(AiDev, AiChn, AoDev, AoChn));
#endif
	}
	else if (i == u32ChnNum)/*disable audio preview*/
	{
		if (AiChn != -1)
		{
#if defined(SN9234H1)
			CHECK(HI_MPI_AO_UnBindAi(AoDev, AoChn, AiDev, AiChn));
#else			
			CHECK_RET(PRV_AUDIO_AoUnbindAi(AiDev, AiChn, AoDev, AoChn));
#endif
		}
		AiChn = -1;
	}

	RET_SUCCESS("");
}

//开启通道预览音频
HI_S32 PRV_EnableAudioPreview(HI_VOID)
{
	if(s_bVoiceTalkOn)
	{
		RET_SUCCESS("");
	}

	PRV_SetAudioPreview(s_abAudioPreview, CHANNEL_NUM);

	RET_SUCCESS("");
}

//根据视频通道号，获取对应的音频状态
int PRV_GetLocalAudioState(int chn)
{
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----PRV_GetAudioState, chn: %d\n", chn);
	if(!s_abAudioEnable[chn])//音频总开关如果是关闭
		return HI_FAILURE;
	
	if(!s_abAudioMap[chn] || !s_abAudioPreview[s_abAudioMap[chn] - 1])//视频通道没有绑定音频通道
		return HI_FAILURE;

	return HI_SUCCESS;
}

HI_S32 PRV_LocalAudioOutputChange(int chn)
{
	int i = 0;
	if(chn >= LOCALVEDIONUM)
		RET_FAILURE("Digital Channel!!!");

	if(chn < 0 || chn >= CHANNEL_NUM)
		RET_FAILURE("Invalid Chn!!!");
		
	if(!s_abAudioMap[chn])
		RET_FAILURE("no choose corresponding Audio channel!");
	for(i = 0; i < LOCALVEDIONUM; i++)
	{
		if(s_abAudioMap[i] > 0)//指定了音频输入
		{
			if(s_abAudioMap[i] != s_abAudioMap[chn])//不是需要播放的通道中的音频输入
				s_abAudioPreview[s_abAudioMap[i] - 1] = 0;//关闭音频通道
		}
		//printf("----------i: %d, s_abAudioPreview[i]: %d\n", i, s_abAudioPreview[i]);
	}
	
	s_abAudioPreview[s_abAudioMap[chn] - 1] = (~s_abAudioPreview[s_abAudioMap[chn] - 1]) & 0x01;//开启/关闭指定通道音频

	if(!s_bVoiceTalkOn)
	{//语音对讲时，不进行配置
		PRV_SetAudioPreview(s_abAudioPreview, CHANNEL_NUM);
	}
	RET_SUCCESS("");
	
}

//关闭通道预览音频
HI_S32 PRV_DisableAudioPreview(HI_VOID)
{
	BOOLEAN abAudioPreview[CHANNEL_NUM] = {0};
	if(s_bVoiceTalkOn)
	{
		RET_SUCCESS("");
	}

	PRV_SetAudioPreview(abAudioPreview, CHANNEL_NUM);

	RET_SUCCESS("");
}
#if defined(SN6104) || defined(SN8604D) || defined(SN8604M)
static HI_S32 PRV_EnableAudioVOA_Dev(HI_VOID)
{
	AUDIO_DEV AiDev = PRV_VOA_AUDIO_DEV;
	AI_CHN AiChn = 0;
	AIO_ATTR_S stAttr;
	
//关闭关闭通道0	
	CHECK_RET(HI_MPI_AI_DisableChn(AiDev, AiChn));
//关闭音频设备PRV_VOA_AUDIO_DEV	
	CHECK_RET(HI_MPI_AI_Disable(AiDev));
//设置音频设备
	stAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	stAttr.enSamplerate = AUDIO_SAMPLE_RATE_8000;
#if defined(SN9234H1)
	stAttr.enSoundmode = AUDIO_SOUND_MODE_MOMO;
#else	
	stAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
#endif
	stAttr.enWorkmode = I2S_WORK_MODE;
	stAttr.u32EXFlag = 0;
	stAttr.u32FrmNum = 30;
	stAttr.u32PtNumPerFrm = AUDIO_PTNUMPERFRM;
	stAttr.u32ChnCnt = 2;
	stAttr.u32ClkSel = 0;
	
	PRV_TW2865_CfgAudio(stAttr.enSamplerate);
	
	CHECK_RET(HI_MPI_AI_SetPubAttr(AiDev, &stAttr));
	
	CHECK_RET(HI_MPI_AI_Enable(AiDev));
	
	CHECK_RET(HI_MPI_AI_EnableChn(AiDev, AiChn));
	
	RET_SUCCESS("");
}
static HI_S32 PRV_DisableAudioVOA_Dev(HI_VOID)
{
	AIO_ATTR_S stAttr;
	AUDIO_DEV AiDev = PRV_AUDIO_DEV;
	AI_CHN AiChn = 0;
	
	stAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	stAttr.enSamplerate = AUDIO_SAMPLE_RATE_8000;
#if defined(SN9234H1)
	stAttr.enSoundmode = AUDIO_SOUND_MODE_MOMO;
#else	
	stAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
#endif
	stAttr.enWorkmode = I2S_WORK_MODE;
	stAttr.u32EXFlag = 0;
	stAttr.u32FrmNum = 5;
	stAttr.u32PtNumPerFrm = AUDIO_PTNUMPERFRM;
	stAttr.u32ChnCnt = 2;
	stAttr.u32ClkSel = 0;
	
//关闭关闭通道0	
	CHECK_RET(HI_MPI_AI_DisableChn(AiDev, AiChn));
//关闭音频设备PRV_VOA_AUDIO_DEV	
	CHECK_RET(HI_MPI_AI_Disable(AiDev));
//设置音频设备
	PRV_TW2865_CfgAudio(stAttr.enSamplerate);
	CHECK_RET(HI_MPI_AI_SetPubAttr(AiDev, &stAttr));
	
	CHECK_RET(HI_MPI_AI_Enable(AiDev));
	
	CHECK_RET(HI_MPI_AI_EnableChn(AiDev, AiChn));
	RET_SUCCESS("");
	
}
#endif
//开启4路对讲音频
static HI_S32 PRV_EnableAudioVOA(HI_VOID)
{
	
#if defined(SN6104) || defined(SN8604D) || defined(SN8604M) || defined(SN2008LE)
	BOOLEAN abAudioPreview[CHANNEL_NUM] = {0};

	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@@@@@@@@@@@PRV_EnableAudioVOA	6104\n");

	//开启语音对讲设备
	PRV_EnableAudioVOA_Dev();
	abAudioPreview[CHANNEL_NUM-1] = 1;
	s_abAudioPreview[CHANNEL_NUM-1] = 1;
	PRV_SetAudioPreview(abAudioPreview, CHANNEL_NUM);
#endif	
	

	RET_SUCCESS("");
}
//关闭通道预览音频
static HI_S32 PRV_DisableAudioVOA(HI_VOID)
{
	
#if defined(SN6104) || defined(SN8604D) || defined(SN8604M) || defined(SN2008LE)
	BOOLEAN abAudioPreview[CHANNEL_NUM] = {0};
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@@@@@@@@@@@PRV_DisableAudioVOA	6104\n");
	s_abAudioPreview[CHANNEL_NUM-1] = 0;
	PRV_SetAudioPreview(abAudioPreview, CHANNEL_NUM);
	PRV_DisableAudioVOA_Dev();
#endif

	RET_SUCCESS("");
}
#if 0
//通道预览音频配置
HI_S32 PRV_AudioPreviewCtrl(const BOOLEAN *pbAudioPreview, HI_U32 u32ChnNum)
{
	HI_S32 i;

	if (NULL == pbAudioPreview)
	{
		RET_FAILURE("Invalid Parameter: NULL Pointer!");
	}
	
	for (i = 0; i < u32ChnNum && i < DEV_AUDIO_IN_NUM; i++)
	{
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@@@@@@@@@@@pbAudioPreview[i] = %d   \n",pbAudioPreview[i]);
		s_abAudioPreview[i] = pbAudioPreview[i];
	}
	if(!s_bVoiceTalkOn)
	{//语音对讲时，不进行配置
		PRV_SetAudioPreview(s_abAudioPreview, CHANNEL_NUM);
	}
	RET_SUCCESS("");
}
#else
//通道预览音频配置
HI_S32 PRV_AudioPreviewCtrl(const unsigned char *pchn, HI_U32 u32ChnNum)
{
	HI_S32 i,chn=0;

	if (NULL == pchn)
	{
		RET_FAILURE("Invalid Parameter: NULL Pointer!");
	}
	SN_MEMSET(s_abAudioPreview,0,CHANNEL_NUM);
	for (i = 0; i < u32ChnNum && i < DEV_AUDIO_IN_NUM; i++)
	{
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "@@@@@@@@@@@pbAudioPreview[i] = %d   \n",pbAudioPreview[i]);
		chn = pchn[i];
		if(s_abAudioEnable[chn])
		{//如果音频使能
			//查找当前画面通道对应的音频通道
			PRV_ChnAudio_Search(chn,s_abAudioPreview);
		}
	}
	if(!s_bVoiceTalkOn)
	{//语音对讲时，不进行配置
		PRV_SetAudioPreview(s_abAudioPreview, CHANNEL_NUM);
	}
	RET_SUCCESS("");
}

#endif
HI_S32 PRV_SetAoAttrAgain()
{
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif
	AO_CHN AoChn = 0;
	AIO_ATTR_S stAioAttr;
	
	CHECK_RET(HI_MPI_AO_GetPubAttr(AoDev, &stAioAttr));
	stAioAttr.u32PtNumPerFrm = AUDIO_PTNUMPERFRM;
#if defined(Hi3531)||defined(Hi3535)	
	PtNumPerFrm = AUDIO_PTNUMPERFRM;
#endif

	CHECK_RET(HI_MPI_AO_DisableChn(AoDev, AoChn));

	CHECK_RET(HI_MPI_AO_Disable(AoDev));

	CHECK_RET(HI_MPI_AO_SetPubAttr(AoDev, &stAioAttr));

	CHECK_RET(HI_MPI_AO_Enable(AoDev));

	CHECK_RET(HI_MPI_AO_EnableChn(AoDev, AoChn));

	return HI_SUCCESS;

}
HI_BOOL PRV_GetVoiceTalkState()
{
	return s_bVoiceTalkOn;
}
//语音对讲socket初始化
static int socket_set_rwbuf(int socket, int wmem, int rmem)
{
	if(setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &rmem, sizeof(rmem)) != 0) {
		TRACE(SCI_TRACE_NORMAL, MOD_NET, "Set Recv Buf Failed!\n");
	}
	if(setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &wmem, sizeof(wmem)) != 0) {
		TRACE(SCI_TRACE_NORMAL, MOD_NET, "Set Send Buf Failed!\n");
	}

	return 0;
}

static HI_S32 PRV_VoiceTalkSocketInit(HI_VOID)
{
	int flag, len;
	PRM_Net_PORT_INFO stNetPortInfo;
	
	if (PARAM_OK != GetParameter(PRM_ID_NET_PORT_INFO,NULL,&stNetPortInfo,sizeof(PRM_Net_PORT_INFO),1,SUPER_USER_ID,NULL))
	{
		s_VtPort = VT_PORT;
		TRACE(SCI_TRACE_HIGH, MOD_VOA, TEXT_COLOR_RED("get parameter PRM_ID_NET_PORT_INFO fail! Voice Talk using default port: %d"), VT_PORT);
	}
	else
	{
		s_VtPort = stNetPortInfo.dwReserved1;
		TRACE(SCI_TRACE_HIGH, MOD_VOA, TEXT_COLOR_GREEN("get parameter PRM_ID_NET_PORT_INFO success! Voice Talk port: %d"), s_VtPort);
	}
	
	udpserver_addr.sin_family = PF_INET;
	udpserver_addr.sin_port = htons(s_VtPort);
	udpserver_addr.sin_addr.s_addr = INADDR_ANY;

	if (s_s32VoiceTalkSocketFd != -1)
	{
		TRACE(SCI_TRACE_HIGH, MOD_VOA,"Voice talk socket already exist? socket=%d", s_s32VoiceTalkSocketFd);
		RET_FAILURE("");
		//RET_SUCCESS("");
	}
	if ((s_s32VoiceTalkSocketFd = socket(PF_INET, SOCK_DGRAM, 0)) == -1)
	{
		TRACE(SCI_TRACE_HIGH, MOD_VOA,"Create voice talk udp socket failed! socket() fail: %s", strerror(errno));
		RET_FAILURE("");
	}
#if defined(IPTOS_LOWDELAY)
	setsocket_tos(s_s32VoiceTalkSocketFd, IPTOS_LOWDELAY);
#endif
#if defined(SO_REUSEADDR)
	flag = 1;
	len = sizeof(flag);
	if( setsockopt(s_s32VoiceTalkSocketFd, SOL_SOCKET, SO_REUSEADDR, &flag, len) == -1)
	{
		perror("setsockopt SO_REUSEADDR error");
	}
#endif
#if defined(SO_REUSEPORT)
	flag = 1;
	len = sizeof(flag);
	if( setsockopt(s_s32VoiceTalkSocketFd, SOL_SOCKET, SO_REUSEPORT, &flag, len) == -1)
	{
		perror("setsockopt SO_REUSEPORT error");
	}
#endif
	socket_set_rwbuf(s_s32VoiceTalkSocketFd,2048,2048);
	if (bind(s_s32VoiceTalkSocketFd, (struct sockaddr *) &udpserver_addr, sizeof(udpserver_addr))== -1)
	{
		TRACE(SCI_TRACE_HIGH, MOD_VOA,"Socket Bind failed! bind() fail: %s", strerror(errno));
		close(s_s32VoiceTalkSocketFd);
		s_s32VoiceTalkSocketFd = -1;
		RET_FAILURE("");
	}

	RET_SUCCESS("");
}

/************************************************************************/
/* 语音对讲接收。
                                                                     */
/************************************************************************/
HI_VOID *PRV_VoiceTalk_RecvProc(HI_VOID *param)
{
	HI_S32 s32Ret;
	ADEC_CHN AdChn = s_VoiceTalkAdChnDflt;
	AUDIO_STREAM_S stAudioStream;
	HI_U8 buf[VIDEO_TALK_SIZE_MAX];
	HI_U8 buf_temp[VIDEO_TALK_SIZE_MAX*2];
	
	unsigned int audio_offset=0;
	HI_S32 s32Len,s32cnt=0,s32offset=0;
	struct sockaddr_in rin;
	int i;
	
	int ret;
	fd_set rset;
	struct timeval tv;
	int sock_fd = (int)param;
	int isfirst = 1;
	socklen_t rlen;
	
	Log_pid(__FUNCTION__);

	/*检测对讲锁是否已获得，如果已获得则进行对讲！*/
	while( pthread_mutex_trylock(&s_vt_mutex) != 0 )
	{
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		
		FD_ZERO(&rset);
		FD_SET(sock_fd, &rset);
		
		ret = select(sock_fd+1, &rset, NULL, NULL, &tv);
		if (ret<0)
		{
			perror(TEXT_COLOR_RED("select tk socket error"));
			sleep(1);
			continue;
		}
		else if (ret == 0)
		{
			TRACE(SCI_TRACE_HIGH, MOD_VOA, TEXT_COLOR_YELLOW("RecvProc: select time out for 1 sec! sock_fd=%d"), sock_fd);
			continue;
		}

		rlen = sizeof(rin);
		s32Ret = recvfrom(sock_fd, buf, VIDEO_TALK_SIZE_MAX, 0, (struct sockaddr *)&rin, &rlen);
		if (s32Ret <= VIDEO_HEAD_SIZE_MAX)
		{
			TRACE(SCI_TRACE_HIGH, MOD_VOA,"error: recvfrom() return %d",s32Ret);
			continue;
		}
		else
		{
			Soket_DATA_HEADER *pheader = (Soket_DATA_HEADER *)buf;
			VoiceHeader *pudpheader = (VoiceHeader *)&buf[sizeof(Soket_DATA_HEADER)];

			if (STAR_TAG != pheader->tag || TALKBACK_DATA_TYPE != pheader->type)
			{
				TRACE(SCI_TRACE_HIGH, MOD_VOA, TEXT_COLOR_YELLOW("Invalid VoiceTalk package recieved from ip:%s, port:%d!"), inet_ntoa(rin.sin_addr), ntohs(rin.sin_port));
				continue;
			}
			else if (pudpheader->datalen != pheader->payloadlen - sizeof(VoiceHeader))
			{
				TRACE(SCI_TRACE_HIGH, MOD_VOA, TEXT_COLOR_YELLOW("VoiceTalk package payloadlen unmatch! recieved from ip:%s,port:%d!"), inet_ntoa(rin.sin_addr), ntohs(rin.sin_port));
				continue;
			}
			else if (isfirst)
			{
				if(rin.sin_addr.s_addr != udpclient_addr.sin_addr.s_addr)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_VOA, TEXT_COLOR_RED("内网用户? %s:%d"), inet_ntoa(udpclient_addr.sin_addr), ntohs(udpclient_addr.sin_port));
				}
				udpclient_addr = rin;
				TRACE(SCI_TRACE_NORMAL, MOD_VOA, TEXT_COLOR_PURPLE("Begin VoiceTalk: %s:%d"), inet_ntoa(rin.sin_addr), ntohs(rin.sin_port));
				isfirst = 0;
			}
			else if (rin.sin_addr.s_addr != udpclient_addr.sin_addr.s_addr)
			{
				TRACE(SCI_TRACE_HIGH, MOD_VOA, "udp package recieved from %s:%d ,discarded!", inet_ntoa(rin.sin_addr), ntohs(rin.sin_port));
				continue;
			}
			else
			{
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "recvfrom: %s:%d: datalen:%d\n", inet_ntoa(rin.sin_addr), ntohs(rin.sin_port), s32Ret);
			}
		}

		s32Len = s32Ret - VIDEO_HEAD_SIZE_MAX + audio_offset;
		s32cnt = AUDIO_PTNUMPERFRM - audio_offset;
		s32offset = VIDEO_HEAD_SIZE_MAX;
		while(s32Len >= AUDIO_PTNUMPERFRM)
		{
			buf_temp[0] = 0x00;
			buf_temp[1] = 0x01;
			buf_temp[2] = 0x50;
			buf_temp[3] = 0x00;			        
            for(i=0; i < s32cnt; i++)
            {
                buf_temp[4+i+audio_offset] = buf[s32offset+i];
            }
			stAudioStream.pStream = buf_temp;
			stAudioStream.u32Len = AUDIO_PTNUMPERFRM+4;
			HI_MPI_ADEC_SendStream(AdChn, &stAudioStream, HI_IO_NOBLOCK);

			s32Len -= AUDIO_PTNUMPERFRM;
			audio_offset = 0;
			s32offset += s32cnt;
			s32cnt = AUDIO_PTNUMPERFRM;
		}
		if(s32Len)
		{
			for(i=0;i<s32Len;i++)
			{
				buf_temp[4+i] = buf[s32offset+i];
			}
			audio_offset = s32Len;
		}
	}

	pthread_mutex_unlock(&s_vt_mutex);
	return NULL;
}


void VOA_CodeInit(void)
{
#if defined(Hi3535)	
	int ret = -1;
	unsigned int value = 0;
	ret = testmod_reg_rw(0, 0x20120000, 0xd8, &value);
	if(ret<0)
		return;
	//关闭CODE的高通滤波
	value &= 0xffff3fff;
	ret = testmod_reg_rw(1, 0x20120000, 0xd8, &value);
	return;
#endif
}
/************************************************************************/
/* 语音对讲发送。
                                                                     */
/************************************************************************/
HI_VOID *PRV_VoiceTalk_SendProc(HI_VOID *param)
{
	HI_S32 s32Ret;
	AENC_CHN AeChn=s_VoiceTalkAeChnDflt;
	AUDIO_STREAM_S stAudioStream;
	fd_set read_fds;
	int fd;	
	struct timeval timeout;
	int sock_fd = (int)param;
	
	Log_pid(__FUNCTION__);

	
	//海思系统函数会调整code, 对code初始化放入采集数据线程，海思API之后
	VOA_CodeInit();

	fd = HI_MPI_AENC_GetFd(AeChn);
	if (fd < 0)
	{
		TRACE(SCI_TRACE_HIGH, MOD_VOA, "get aenc fd fail!");
		return NULL;
	}
#if 0 
	FILE *pf = fopen("aduio.dat", "a");
#endif

	/*检测对讲锁是否已获得，如果已获得则进行对讲！*/
	while( pthread_mutex_trylock(&s_vt_mutex) != 0 )
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

		s32Ret = select(fd + 1, &read_fds, NULL, NULL, &timeout);
		if(s32Ret < 0)
		{
			perror(TEXT_COLOR_RED("select aenc fd error"));
			sleep(1);
			continue;
		}
		else if(s32Ret == 0)
		{
			TRACE(SCI_TRACE_HIGH, MOD_VOA, TEXT_COLOR_YELLOW("SendProc: select time out for 1 sec! aenc_fd=%d"), fd);
			continue;
		}

		if (FD_ISSET(fd, &read_fds)) 
		{
			CHECK2(HI_MPI_AENC_GetStream(AeChn, &stAudioStream, HI_IO_BLOCK), s32Ret);
			if (HI_SUCCESS == s32Ret)
			{
				Soket_DATA_HEADER header;
				VoiceHeader udpheader;
				unsigned char tmp_send[VIDEO_TALK_SIZE_MAX];
				unsigned int len;
				int send_len=0;
				int cnt = 0;
				int offset = 0;
#if defined(SN6104)	 || defined(SN8604D) || defined(SN8604M) || defined(SN2008LE)			
				len = stAudioStream.u32Len/2;
#else
				len = stAudioStream.u32Len-4;
#endif
#if 0
				{
					int ii=0;
					unsigned char *dat = stAudioStream.pStream;
					if(NULL!=pf)
					{
						for(ii=0;ii<len/2;ii++)
						{
							fwrite(&dat[ii*2+1],1,1,pf);
						}
					}
				}
#endif						
				for(cnt = 0; len > 0; cnt++, offset += udpheader.datalen, len -= udpheader.datalen)
				{
					header.Cseq = 0;
					header.tag = STAR_TAG;
					header.type = TALKBACK_DATA_TYPE;
					header.payloadlen = (len > (VIDEO_TALK_SIZE_MAX - VIDEO_HEAD_SIZE_MAX)) 
						? VIDEO_TALK_SIZE_MAX - sizeof(Soket_DATA_HEADER)
						: len + sizeof(VoiceHeader);
					
					udpheader.flags = 0x03;
					udpheader.Cseq = cnt;
					udpheader.datalen = header.payloadlen - sizeof(VoiceHeader);
					
					send_len = sizeof(Soket_DATA_HEADER) + header.payloadlen;
					
					bzero(tmp_send, sizeof(tmp_send));
					SN_MEMCPY(tmp_send,sizeof(tmp_send),&header,sizeof(Soket_DATA_HEADER),sizeof(Soket_DATA_HEADER));
					SN_MEMCPY(&tmp_send[sizeof(Soket_DATA_HEADER)],(sizeof(tmp_send)-sizeof(Soket_DATA_HEADER)), &udpheader,sizeof(VoiceHeader),sizeof(VoiceHeader));
#if defined(SN6104)	 || defined(SN8604D) || defined(SN8604M) || defined(SN2008LE)
					int i =0;
					for(i=0;i<udpheader.datalen;i++)
					{
						tmp_send[VIDEO_HEAD_SIZE_MAX + i] = stAudioStream.pStream[4+offset+i];
					}	
#else
	
					SN_MEMCPY(&tmp_send[VIDEO_HEAD_SIZE_MAX],(sizeof(tmp_send)-VIDEO_HEAD_SIZE_MAX),&stAudioStream.pStream[4+offset],udpheader.datalen,udpheader.datalen);
#endif					
								
					s32Ret = sendto(sock_fd, tmp_send, send_len, 0, (struct sockaddr *)&udpclient_addr, sizeof(udpclient_addr));
					if (s32Ret <= 0)
					{
						TRACE(SCI_TRACE_HIGH, MOD_VOA,"sendto fail:%s! sock_fd=%d", strerror(errno), sock_fd);
						usleep(100*1000);
					}
					else
					{
						//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "sendto: %s:%d: datalen:%d\n", inet_ntoa(udpclient_addr.sin_addr), ntohs(udpclient_addr.sin_port), s32Ret);
					}
				}
				CHECK(HI_MPI_AENC_ReleaseStream(AeChn, &stAudioStream));
			}
		}
	}
#if 0 
	fclose(pf);
#endif	
	pthread_mutex_unlock(&s_vt_mutex);
	return NULL;
}

/************************************************************************/
/* 开启语音对讲。
                                                                     */
/************************************************************************/
static HI_S32 VOA_VoiceTalkStart(const struct sockaddr_in *pclient_addr)
{
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif
	AO_CHN AoChn = 0;
	ADEC_CHN AdChn = s_VoiceTalkAdChnDflt;
	int ret;

	if (NULL == pclient_addr)
	{
		RET_FAILURE("NULL ptr!");
	}
	
	if (s_bVoiceTalkOn)
	{
		RET_FAILURE("Voice talk already opened!");
	}
	else
	{
		ret = pthread_mutex_trylock(&s_vt_mutex);
		if (ret == 0)
		{
			if (HI_SUCCESS != PRV_VoiceTalkSocketInit())
			{
				pthread_mutex_unlock(&s_vt_mutex);
				close(s_s32VoiceTalkSocketFd);
				s_s32VoiceTalkSocketFd = -1;
				RET_FAILURE("PRV_VoiceTalkSocketInit failed!");
			}
			
			if(LOCALVEDIONUM > 0)
				PRV_DisableAudioPreview();
			CHECK_RET(PRV_DisableDigChnAudio());
			PRV_SetAoAttrAgain();
			PRV_EnableAudioVOA();
#if defined(SN9234H1)	
			ret = HI_MPI_AO_BindAdec(AoDev, AoChn, AdChn);
#else
			ret = PRV_AUDIO_AoBindAdec(AoDev, AoChn, AdChn);
#endif
			if (HI_SUCCESS != ret)
			{
				pthread_mutex_unlock(&s_vt_mutex);
				close(s_s32VoiceTalkSocketFd);
				s_s32VoiceTalkSocketFd = -1;
				puts(PRV_GetErrMsg(ret));
				RET_FAILURE("PRV_AUDIO_AoBindAdec failed!");
			}

			udpclient_addr = *pclient_addr;
			
			if (pthread_create(&s_recv_tid, NULL, PRV_VoiceTalk_RecvProc, (void*)s_s32VoiceTalkSocketFd) != 0)
			{
				pthread_mutex_unlock(&s_vt_mutex);
				close(s_s32VoiceTalkSocketFd);
				s_s32VoiceTalkSocketFd = -1;
				RET_FAILURE("create voice talk recv thread failed!");
			}
			
			if (pthread_create(&s_send_tid, NULL, PRV_VoiceTalk_SendProc, (void*)s_s32VoiceTalkSocketFd) != 0)
			{
				pthread_mutex_unlock(&s_vt_mutex);
				close(s_s32VoiceTalkSocketFd);
				s_s32VoiceTalkSocketFd = -1;
				RET_FAILURE("create voice talk send thread failed!");
			}
			
			s_bVoiceTalkOn = HI_TRUE;
		}
		else
		{
			RET_FAILURE("VoiceTalkStart failed! vt has locked!");
		}
	}

	RET_SUCCESS("");
}

/************************************************************************/
/* 停止语音对讲。
                                                                     */
/************************************************************************/
static HI_S32 VOA_VoiceTalkStop(HI_VOID)
{
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif
	AO_CHN AoChn = 0;
	ADEC_CHN AdChn = s_VoiceTalkAdChnDflt;
	
	if (s_bVoiceTalkOn)
	{
		PRV_PREVIEW_MODE_E enPreviewMode;

		pthread_mutex_unlock(&s_vt_mutex);
		pthread_join(s_send_tid,0);
		pthread_join(s_recv_tid,0);

		s_bVoiceTalkOn = HI_FALSE;
		
		PRV_DisableAudioVOA();

		/*判断是否正在回放,不是则打开音频预览*/
		if (HI_SUCCESS == PRV_GetPrvMode(&enPreviewMode) && ScmGetChnPBState() != 1)
		{
#if defined(SN9234H1)
			CHECK_RET(HI_MPI_AO_UnBindAdec(AoDev, AoChn, AdChn));
#else
			CHECK_RET(PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, AdChn));
#endif
			if(LOCALVEDIONUM > 0)
				PRV_EnableAudioPreview();
			else
				PRV_EnableDigChnAudio();
		}

		/*关闭对讲SOCKET*/
		close(s_s32VoiceTalkSocketFd);
		s_s32VoiceTalkSocketFd = -1;
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_VOA,"Voice talk already closed!");
	}

	RET_SUCCESS("");
}

static HI_S32 VOA_MSG_VoiceTalk(const VoiceTalkReq *param, VoiceTalkRsp *rsp, int userId)
{
	PRV_PREVIEW_MODE_E enPreviewMode;
	char str[256];
	UserLogInfo usrInfo;
	struct sockaddr_in client_addr;
	
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	
	rsp->flag = 0;
	rsp->audiochannelnum = 1;
	rsp->audiocompressflag = 1;
	rsp->audiosamplerate = 8000;
	rsp->audiosamplesize = 8;
	if(!detect_audio_input_num())
	{//如果无音频输出接口
		rsp->flag = MOD_VOA_TK_ERR_NO_AUDIO;
		RET_FAILURE("VoiceTalk Open fail: No Audio Input");
	}
	GetUserLogInfo(userId, &usrInfo);
	
	client_addr.sin_family = AF_INET;
	client_addr.sin_addr.s_addr = param->clientip;
	client_addr.sin_port = param->clientport;
	
	if (param->flag)
	{
		char string[128];
		SN_GetString (L_VOA_0, string, sizeof (string));	
		
		SN_SPRINTF(str,sizeof(str), "IP:%s %s:%s:%d", usrInfo.IPAddress, string, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
		TRACE(SCI_TRACE_NORMAL, MOD_VOA, "\033[0;35m%s\033[0;39m", str);
		VOA_WriteVoiceTalkLog(PRM_ID_LOG_MINOR_OPERARATION_START_VT, str, userId);
		
		/*判断是否正在回放,是则返回失败:MOD_VOA_TK_ERR_PB*/
		if (HI_SUCCESS != PRV_GetPrvMode(&enPreviewMode) || ScmGetChnPBState())
		{
			rsp->flag = MOD_VOA_TK_ERR_PB;
			RET_FAILURE("VoiceTalk Open Fail!In PB Or Pic Now");
		}

		if (s_s32CurrentUserId == SUPER_USER_ID || s_s32CurrentUserId == userId)
		{
			CHECK_RET(VOA_VoiceTalkStart(&client_addr));
			s_s32CurrentUserId = userId;
		}
		else
		{
			RET_FAILURE("UserId: Operation Forbidden");
		}
	}
	else
	{
		char string[128];
		SN_GetString (L_VOA_1, string, sizeof(string));	
		SN_SPRINTF(str,sizeof(str), "IP:%s %s:%s:%d", usrInfo.IPAddress, string, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
		TRACE(SCI_TRACE_NORMAL, MOD_VOA, "\033[0;33m%s\033[0;39m", str);
		VOA_WriteVoiceTalkLog(PRM_ID_LOG_MINOR_OPERARATION_STOP_VT, str, userId);
		
		if (s_s32CurrentUserId == SUPER_USER_ID || s_s32CurrentUserId == userId)
		{
			CHECK_RET(VOA_VoiceTalkStop());
			s_s32CurrentUserId = SUPER_USER_ID;
		}
		else
		{
			RET_FAILURE("UserId: Operation Forbidden");
		}
	}
	{//添加端口映射
		Net_PORT_INFO mapPort;
		UserLogInfo pInfo;
		int flag = 0;
		char clientIP[100];
		char localIp[100];

		SN_MEMSET(clientIP, 0, sizeof(clientIP));
		SN_MEMSET(localIp, 0, sizeof(localIp));
		  
		GetUserLogInfo(userId, &pInfo);
		GetLocalIPaddr(localIp);
		SN_SPRINTF(clientIP, sizeof(clientIP),"%s", pInfo.IPAddress);
		flag = IS_Port_Map(clientIP, localIp); 
		
		Get_Port_Map(&mapPort, flag);
		
		rsp->voicetalkport = mapPort.dwReserved1; 

		TRACE(SCI_TRACE_NORMAL, MOD_VOA, "talkRsp->voicetalkport = %d   client= %s\n", rsp->voicetalkport, clientIP);
	}
	//rsp->voicetalkport = s_VtPort;
	rsp->flag = 1;

	RET_SUCCESS("");
}

/************************************************************************/
/*//语音对讲状态查询  */
/************************************************************************/
int PRV_TkOrPb(int *pFlag)
{
	PRV_PREVIEW_MODE_E enPreviewMode;
	if(pFlag == NULL)
	{
		RET_FAILURE("");
	}
	
	if (s_bVoiceTalkOn)
	{
		*pFlag = VOA_STATE;//1表示正在对讲
	}
	else if (HI_FAILURE == PRV_GetPrvMode(&enPreviewMode))
	{
		/*switch(*pFlag)
		{
			case SLC_CTL_FLAG:
			case PIC_CTL_FLAG:
				PRV_DisableAudioPreview();//如果处于空闲状态，那么需要关闭音频
				break;
			default:
				break;
		}*/
		*pFlag = PB_STATE;//0表示正在回放
	}
	else
	{
		*pFlag = PRV_STATE;//2表示空闲或音频预览
	}	

	//tw2865_master_tk_pb_switch(s32Flag);
	RET_SUCCESS("");
}

static HI_S32 VOA_MSG_VoiceTalkIsPb(const VoiceTalk_Is_pb_Req *param, VoiceTalk_Is_pb_Rsp *rsp)
{
	
	if (NULL == param || NULL == rsp)
	{
		RET_FAILURE("NULL pointer!!!");
	}
	
	PRV_TkOrPb(&rsp->flag);
	
	RET_SUCCESS("");
}

static HI_S32 VOA_MSG_UserLogout(int userId)
{
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif
	AO_CHN AoChn = 0;
	ADEC_CHN AdChn = s_VoiceTalkAdChnDflt;
	char string[128];
	if (s_bVoiceTalkOn)
	{
		pthread_mutex_unlock(&s_vt_mutex);
		pthread_join(s_send_tid,0);
		pthread_join(s_recv_tid,0);
		
		s_bVoiceTalkOn = HI_FALSE;
		s_s32CurrentUserId = SUPER_USER_ID;
#if defined(SN9234H1)
		CHECK_RET(HI_MPI_AO_UnBindAdec(AoDev, AoChn, AdChn));
#else
		CHECK_RET(PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, AdChn));
#endif
		PRV_EnableAudioPreview();
		
		/*关闭对讲SOCKET*/
		close(s_s32VoiceTalkSocketFd);
		s_s32VoiceTalkSocketFd = -1;

		/*写关日志*/
		
		SN_GetString (L_VOA_2, string, sizeof(string));			
		VOA_WriteVoiceTalkLog(PRM_ID_LOG_MINOR_OPERARATION_STOP_VT, string, userId);
	}

	TRACE(SCI_TRACE_NORMAL, MOD_VOA, TEXT_COLOR_RED("对讲连接断开! userId=%d"), userId);
	
	RET_SUCCESS("");
}

/************************************************************************/
/* 声音预览线程，TLV320AIC3014+TVP5157方案专用。
                                                                     */
/************************************************************************/
STATIC HI_S32 PRV_AudioPreviewInit(HI_VOID)
{
#if defined(SN2016) || defined(SN6108) || defined(SN8608D) || defined(SN8608M)
	AIO_ATTR_S stAttr;
	AUDIO_DEV AiDev = PRV_AUDIO_DEV;
	AI_CHN AiChn = 0;
	
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif
	AO_CHN AoChn = 0;
	
	stAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	stAttr.enSamplerate = AUDIO_SAMPLE_RATE_8000;
	stAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
	stAttr.enWorkmode = I2S_WORK_MODE;
	stAttr.u32EXFlag = 0;
	stAttr.u32FrmNum = 5;
	stAttr.u32PtNumPerFrm = AUDIO_PTNUMPERFRM;
	stAttr.u32ChnCnt = 2;
	stAttr.u32ClkSel = 0;
	
	CHECK_RET(HI_MPI_AI_SetPubAttr(AiDev, &stAttr));
	
	CHECK_RET(HI_MPI_AI_Enable(AiDev));
	
	CHECK_RET(HI_MPI_AI_EnableChn(AiDev, 0));
#if defined(SN9234H1)
	CHECK_RET(HI_MPI_AO_BindAi(AoDev, AoChn, AiDev, AiChn));
#else
	CHECK_RET(SAMPLE_COMM_AUDIO_AoBindAi(AiDev, AiChn, AoDev, AoChn));
#endif
   
#elif defined(SN2016HS) || defined(SN6104)  || defined(SN8604D) || defined(SN8604M) || defined(SN2008LE)
	PRV_DisableAudioVOA_Dev();
	PRV_EnableAudioPreview();
#endif
	RET_SUCCESS("");
}

/************************************************************************/
/* 对讲与回放切换，AODEV 0, AOCHN 0 音频输出唯一通道。
                                                                     */
/************************************************************************/
HI_S32 PRV_TkPbSwitch(HI_S32 s32Flag)
{
#if 0
	AUDIO_DEV AoDev = 0;
	AO_CHN AoChn = 0;
	ADEC_CHN AdChn = s_VoiceTalkAdChnDflt;
	
	if (s_bVoiceTalkOn)
	{
		if (s32Flag) /*进入回放关闭对讲输出*/
		{
			CHECK_RET(HI_MPI_AO_UnBindAdec(AoDev, AoChn, AdChn));
		}
		else /*退出回放开启对讲输出*/
		{
			CHECK_RET(HI_MPI_AO_BindAdec(AoDev, AoChn, AdChn));
		}
	}
#endif
	//tw2865_master_tk_pb_switch(s32Flag);
	RET_SUCCESS("");
}

/************************************************************************/
/* VOA模块消息线程函数。
                                                                     */
/************************************************************************/
HI_VOID *VOA_ParseMsgProc(HI_VOID *param)
{

	SN_MSG *msg_req = NULL;

	int queue, ret;

	queue = CreatQueque(MOD_VOA);
	if (queue <= 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_VOA, "CreateQueue Failed: queue = %d", queue);
		return NULL;
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_VOA, "VOA CreateQueue success: queue = %d", queue);
	}
	for (;;)
	{
		msg_req = SN_GetMessage(queue, MSG_GET_WAIT_ROREVER, &ret);
		if (ret < 0)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_VOA, "SN_GetMessage Failed: %#x", ret);
			sleep(1);
			continue;
		}
		if (NULL == msg_req)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_VOA, "SN_GetMessage return Null Pointer!");
			sleep(1);
			continue;
		}
		switch(msg_req->msgId)
		{
		
		case MSG_ID_VOA_VOICE_REQ:
			{
				VoiceTalkRsp rsp;
				VOA_MSG_VoiceTalk((VoiceTalkReq *)msg_req->para, &rsp, msg_req->user);
				SN_SendMessageEx(msg_req->user, MOD_VOA, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_VOA_VOICE_RSP, &rsp, sizeof(rsp));
			}
			break;
		case MSG_ID_VOA_IS_PB_REQ:
			{
				VoiceTalk_Is_pb_Rsp rsp;
				VOA_MSG_VoiceTalkIsPb((VoiceTalk_Is_pb_Req *)msg_req->para, &rsp);
				SN_SendMessageEx(msg_req->user, MOD_VOA, msg_req->source, msg_req->xid, msg_req->thread, 
					MSG_ID_VOA_IS_PB_RSP, &rsp, sizeof(rsp));
			}
			break;
		case MSG_ID_FWK_USER_LOGOUT_RSP:
			{
				VOA_MSG_UserLogout(msg_req->user);
			}
			break;
#if defined(Hi3531)||defined(Hi3535)			
		case MSG_ID_FWK_REBOOT_REQ://重启消息
		case MSG_ID_FWK_POWER_OFF_REQ://关机消息
			{
				if (s_bVoiceTalkOn)
				{
					VOA_VoiceTalkStop();
					sleep(5);
				}
				SendMessageEx(SUPER_USER_ID, MOD_VAM, MOD_PRV, 0, 0, msg_req->msgId, 0, 0);
			}
			break;
#endif			
		default:
			TRACE(SCI_TRACE_NORMAL, MOD_VOA, "%s Get unknown or unused message: %#x", __FUNCTION__, msg_req->msgId);
			break;
		}
		
		SN_FreeMessage(&msg_req);
	}

	return NULL;
}

/************************************************************************/
/* PRV模块初始入口。
                                                                     */
/************************************************************************/
int VOA_Init(void)
{
	pthread_t comtid;

	PRV_AiInit();

	PRV_AoInit();

	PRV_PlayAudioCtrl(1);
	if(LOCALVEDIONUM > 0)
		PRV_AudioPreviewInit();

	if(pthread_create(&comtid, NULL, VOA_ParseMsgProc, NULL) != 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_VOA, "voice talk thread create failed! %s", strerror(errno));
		return -1;
	}

	TRACE(SCI_TRACE_NORMAL, MOD_VOA, "voa threads created! comtid = %d!", comtid);
	return OK;
}

/************************************************************************/
//生产测试
/************************************************************************/
int g_play_flag = 1;

#if defined(Hi3531)||defined(Hi3535)
pthread_t g_aenc_tid = 0; /* 音频输入监听线程ID */
#endif

int PRV_TEST_AudioOut(unsigned char *pstr,unsigned int file_len)
{
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif
	AO_CHN AoChn = 0;
	ADEC_CHN AdChn = s_VoiceTalkAdChnDflt;

	HI_S32 s32ret;
    AUDIO_STREAM_S stAudioStream;
    HI_U32 u32ReadLen = AUDIO_PTNUMPERFRM;
    HI_U32 offset = 0;
	unsigned char temp_buf[2048];
	int i = 0;
	//PRV_AoInit();
	PRV_AoInitTest();
#if defined(SN9234H1)	
	CHECK_RET(HI_MPI_AO_BindAdec(AoDev, AoChn, AdChn));
#endif
    PRV_PlayAudioCtrl(1);
	SN_MEMSET(temp_buf,0,sizeof(temp_buf));

	stAudioStream.pStream = temp_buf;
	stAudioStream.u32Len = u32ReadLen*2;
#if defined(Hi3531)||defined(Hi3535)		
	CHECK_RET(AUDIO_AoBindAdec(AoDev,AoChn,AdChn));
#endif
	while (g_play_flag)
	{
		if(offset >= file_len)
		{
			break;
		}
		for(i=0;i<u32ReadLen;i++)
		{
			temp_buf[i*2+1] = pstr[offset +i];
		}
		s32ret = HI_MPI_ADEC_SendStream(AdChn, &stAudioStream, HI_IO_BLOCK);
		if (HI_SUCCESS != s32ret)
		{
			break;
		}
		offset += u32ReadLen;
    }
#if defined(SN9234H1)
	CHECK(HI_MPI_AO_UnBindAdec(AoDev, AoChn, AdChn));
#else
	CHECK(PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, AdChn));
#endif
	PRV_StopAo();

	return 0;
}


/************************************************************************/
/*              SN2016 SN2016H 音频口生产自检实现方法。                 */
#if defined(SN2016) || defined(SN2016HS) || defined(SN2016HE) || defined(SN8608D) || defined(SN8608M)|| defined(SN6000) || defined(SN8600)


static void * Sample_AencProc(void* param)
{
	HI_S32 u32ChnCnt = (HI_S32)param;
    HI_S32 s32ret, i, j, max=0;
    HI_S32 AencFd[AENC_MAX_CHN_NUM] = {0};
    HI_S32 AencData[AENC_MAX_CHN_NUM * 2] = {0};
    AENC_CHN AeChn;
    AUDIO_STREAM_S stStream; 
    fd_set read_fds;
    struct timeval TimeoutVal;
	HI_U16 *pAudioData;
	HI_U32 u32DataLen;
	HI_S16 s16Tmp;
#if 0
#if (DEV_AI_CODEC_TYPE==1)
#if (DEV_AUDIO_IN_NUM==4) 
	const HI_U8 au8ChnOrder[AENC_MAX_CHN_NUM] = {0,2,1,3,4};
#elif (DEV_AUDIO_IN_NUM==3)
	const HI_U8 au8ChnOrder[AENC_MAX_CHN_NUM] = {0,1,2,3};
#elif (DEV_AUDIO_IN_NUM==8)
	const HI_U8 au8ChnOrder[AENC_MAX_CHN_NUM] = {0,2,4,6,1,3,5,7};
#elif (DEV_AUDIO_IN_NUM==16)
	const HI_U8 au8ChnOrder[AENC_MAX_CHN_NUM] = {0,8,1,9,2,10,3,11,4,12,5,13,6,14,7,15,16};//{0,2,4,6,8,10,12,14,1,3,5,7,9,11,13,15,16};
#endif
#endif
#endif
	const HI_U8 au8ChnOrder[AENC_MAX_CHN_NUM] = {0};

	if (u32ChnCnt > AENC_MAX_CHN_NUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "error: u32ChnCnt is %d \n", u32ChnCnt);
		return 0;
	}
    
    FD_ZERO(&read_fds);
    for (i=0; i<u32ChnCnt; i++)
    {
        AencFd[i] = HI_MPI_AENC_GetFd(i);
		FD_SET(AencFd[i],&read_fds);
		if(max < AencFd[i])
		{
			max = AencFd[i];
		}
    }
    
    while (g_play_flag)
    {     
        TimeoutVal.tv_sec = 1;
		TimeoutVal.tv_usec = 0;
        
        FD_ZERO(&read_fds);
        for (i=0; i<u32ChnCnt; i++)
        {
            FD_SET(AencFd[i], &read_fds);
        }
        
        s32ret = select(max+1, &read_fds, NULL, NULL, &TimeoutVal);
		if (s32ret < 0) 
		{			
			perror("select");
            break;
		}
		else if (0 == s32ret) 
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "get aenc stream select time out\n");
            break;
		}
        for (i=0; i<u32ChnCnt; i++)
        {
            AeChn = i;
            if (FD_ISSET(AencFd[AeChn], &read_fds))
            {
                /* get stream from aenc chn */
                s32ret = HI_MPI_AENC_GetStream(AeChn, &stStream, HI_IO_NOBLOCK);
                if (HI_SUCCESS != s32ret )
                {
                    TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_AENC_GetStream chn:%d, fail: %#x\n",AeChn, s32ret);
                    return NULL;
                }
		
				/*TRACE(SCI_TRACE_NORMAL, MOD_PRV, "aechn:%d got data\n", AeChn);*/
				u32DataLen = stStream.u32Len/2;
				pAudioData = (HI_U16 *)stStream.pStream;
				AencData[AeChn] += u32DataLen;/*总音频数据量*/
				for (j=0; j<u32DataLen-1; j++)
				{
					s16Tmp = pAudioData[j+1] - pAudioData[j];
					if (s16Tmp>100 || s16Tmp<-100)
					{
						AencData[AENC_MAX_CHN_NUM + AeChn]++;/*有效音频数据量*/
					}
				}
				
                /* finally you must release the stream */
                HI_MPI_AENC_ReleaseStream(AeChn, &stStream);
            }            
        }
    }

	s32ret = 0;
	for (i=0; i<u32ChnCnt; i++)
	{
		HI_U8 idx = au8ChnOrder[i];/*转换音频口实际顺序*/
		if (0 == AencData[idx])
		{
			continue;
		}
		if (AencData[idx+AENC_MAX_CHN_NUM] * 100 / AencData[idx] > 10)
		{
			s32ret |= (0x1<<i);/*超过10%即认为有音频*/
		}
	}

    return (void*)s32ret;
}


static HI_S32 SAMPLE_StartAi(AUDIO_DEV AiDevId, HI_S32 s32AiChnCnt,AIO_ATTR_S *pstAioAttr)
{
    HI_S32 j, s32Ret;
    
    s32Ret = HI_MPI_AI_SetPubAttr(AiDevId, pstAioAttr);
    if (s32Ret)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_AI_SetPubAttr aidev:%d err, %x\n", AiDevId, s32Ret);
        return HI_FAILURE;
    }
    if (HI_MPI_AI_Enable(AiDevId))
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV, "enable ai dev:%d fail\n", AiDevId);
        return HI_FAILURE;
    }                
    for (j=0; j<s32AiChnCnt; j++)
    {
        if (HI_MPI_AI_EnableChn(AiDevId, j))
        {
            TRACE(SCI_TRACE_NORMAL, MOD_PRV, "enable ai(%d,%d) fail\n", AiDevId, j);
            return HI_FAILURE;
        }
    }

    
    return HI_SUCCESS;
}

static HI_S32 SAMPLE_StopAi(AUDIO_DEV AiDevId, HI_S32 s32AiChnCnt)
{
    HI_S32 i;    
    for (i=0; i<s32AiChnCnt; i++)
    {
        HI_MPI_AI_DisableChn(AiDevId, i);
    }  
    HI_MPI_AI_Disable(AiDevId);
    return HI_SUCCESS;
}

static HI_S32 SAMPLE_StartAenc(HI_S32 s32AencChnCnt)
{
    AENC_CHN AeChn;
    HI_S32 s32Ret, j;
    AENC_CHN_ATTR_S stAencAttr;
	AENC_ATTR_LPCM_S stAencLpcm;
   
    /* set AENC chn attr */
    stAencAttr.enType = PT_LPCM;
    stAencAttr.u32BufSize = 30;
	stAencAttr.pValue = &stAencLpcm;
	
    for (j=0; j<s32AencChnCnt; j++)
    {
        AeChn = j;
        
        /* create aenc chn*/
        s32Ret = HI_MPI_AENC_CreateChn(AeChn, &stAencAttr);
        if (s32Ret != HI_SUCCESS)
        {
            TRACE(SCI_TRACE_NORMAL, MOD_PRV, "create aenc chn %d err:0x%x\n", AeChn, s32Ret);
            return s32Ret;
        }        
    }
    
    return HI_SUCCESS;
}

static HI_S32 SAMPLE_StopAenc(HI_S32 s32AencChnCnt)
{
    HI_S32 i;
    for (i=0; i<s32AencChnCnt; i++)
    {
        HI_MPI_AENC_DestroyChn(i);
    }
    return HI_SUCCESS;
}

static HI_S32 SAMPLE_AiAenc(void)
{
    HI_S32 i, s32Ret;
    AUDIO_DEV AiDev;
    AI_CHN AiChn;
    AIO_ATTR_S stAioAttr;
    HI_S32 s32AiChnCnt;
    HI_S32 s32AencChnCnt;
    AENC_CHN AeChn;
	pthread_t aenc_tid, sndplay_tid;
	extern void playsound_main(void *data);
	typedef void *(*START_ROUTINE)(void*);
	//HI_S32  Audio_max_num=DEV_AUDIO_VAM_NUM;
	HI_S32	Audio_max_num=0;

	//if(Audio_max_num < 4)
	//	Audio_max_num = 4;
	
    stAioAttr.enWorkmode = I2S_WORK_MODE;
    stAioAttr.u32ChnCnt = 2;
    stAioAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
    stAioAttr.enSamplerate = AUDIO_SAMPLE_RATE_8000;
#if defined(SN9234H1)
    stAioAttr.enSoundmode = AUDIO_SOUND_MODE_MOMO;
#else
    stAioAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
#endif
    stAioAttr.u32EXFlag = 1;
    stAioAttr.u32FrmNum = 5;
    stAioAttr.u32PtNumPerFrm = AUDIO_PTNUMPERFRM;
    stAioAttr.u32ClkSel = 0;
    

    /* config audio codec */
	PRV_TW2865_CfgAudio(stAioAttr.enSamplerate);
    
    /* enable AiDev 0 and channel */
#if defined(Hi3531)
	AiDev = 4;
#else
	AiDev = 0;
#endif	
	
   	//s32AiChnCnt = 2; 
   	s32AiChnCnt = 1;
  	//stAioAttr.u32ChnCnt = s32AiChnCnt;
    s32Ret = SAMPLE_StartAi(AiDev, s32AiChnCnt, &stAioAttr);
    if (s32Ret != HI_SUCCESS)
    {
        return HI_FAILURE;
    }
	
    /* enable AiDev 1 and channel */
	#if 0
	AiDev = 1;
    s32AiChnCnt = Audio_max_num;
	if (s32AiChnCnt>0)
	{
		stAioAttr.u32ChnCnt = s32AiChnCnt;
		s32Ret = SAMPLE_StartAi(AiDev, s32AiChnCnt, &stAioAttr);
		if (s32Ret != HI_SUCCESS)
		{
			return HI_FAILURE;
		}
	}
	#endif
    /* create AENC channel */
    //s32AencChnCnt = 2 + Audio_max_num;
	s32AencChnCnt = 1;
    s32Ret = SAMPLE_StartAenc(s32AencChnCnt);
    if (s32Ret != HI_SUCCESS)
    {
        return HI_FAILURE;
    }    

    /* bind AENC to AI channel  */
    for (i=0; i<s32AencChnCnt; i++)
    {
        AeChn = i;
		if (i>=Audio_max_num)
		{
#if defined(Hi3531)
			AiDev = 4;
#else
			AiDev = 0;
#endif	
			AiChn = i - Audio_max_num;
			PRV_DisableAudioVOA();
			PRV_EnableAudioVOA();
		}
		else
		{
#if defined(Hi3520)
			AiDev = 1;
#elif defined(Hi3531)
			AiDev = 4;
#elif defined(Hi3535)
			AiDev = 0;
#endif
			//AiDev = 0;
			AiChn = i;
		}
#if defined(SN9234H1)		
        s32Ret = HI_MPI_AENC_BindAi(AeChn, AiDev, AiChn);
#else		
      	PRV_AUDIO_AencBindAi(AiDev,AiChn,AeChn);
#endif
        if (s32Ret != HI_SUCCESS)
        {
            return s32Ret;
        }
    }

    /* create pthread to get aenc stream */
	g_play_flag = 1;
	if (pthread_create(&aenc_tid, NULL, Sample_AencProc, (void*)s32AencChnCnt) != 0)
	{
		perror("pthread_create Sample_AencProc fail\n");
		return 0;
	}
	if(pthread_create(&sndplay_tid,NULL,(START_ROUTINE)playsound_main,NULL) != 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "pthread_create playsound_main fail\n");
		return 0;
	}

	sleep(1);
	g_play_flag = 0;

    pthread_join(sndplay_tid, NULL);
    pthread_join(aenc_tid, (void*)&s32Ret);
	
	g_play_flag = 1;
	SAMPLE_StopAenc(s32AencChnCnt);
    //SAMPLE_StopAi(0, 2);
    //SAMPLE_StopAi(1, Audio_max_num);
#if defined(Hi3531)
   SAMPLE_StopAi(4, s32AiChnCnt);
#else
   SAMPLE_StopAi(0, s32AiChnCnt);
#endif

    return s32Ret;
}

#if defined(Hi3531)||defined(Hi3535)
int SAMPLE_Unite_AudioOut(void)
{
    HI_S32 i, s32Ret;
    AUDIO_DEV AiDev;
    AI_CHN AiChn;
    AIO_ATTR_S stAioAttr;
    HI_S32 s32AiChnCnt;
    HI_S32 s32AencChnCnt;
    AENC_CHN AeChn;
	pthread_t sndplay_tid;
	extern void playsound_main(void *data);
	typedef void *(*START_ROUTINE)(void*);
	//HI_S32  Audio_max_num=DEV_AUDIO_VAM_NUM;
	HI_S32	Audio_max_num=0;

	//if(Audio_max_num < 4)
	//	Audio_max_num = 4;
	
    stAioAttr.enWorkmode = I2S_WORK_MODE;
    stAioAttr.u32ChnCnt = 2;
    stAioAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
    stAioAttr.enSamplerate = AUDIO_SAMPLE_RATE_8000;
    stAioAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
    stAioAttr.u32EXFlag = 1;
    stAioAttr.u32FrmNum = 30;
    stAioAttr.u32PtNumPerFrm = AUDIO_PTNUMPERFRM;
    stAioAttr.u32ClkSel = 0;
    

    /* config audio codec */
	PRV_TW2865_CfgAudio(stAioAttr.enSamplerate);
    
    /* enable AiDev 0 and channel */
#if defined(Hi3531)
	AiDev = 4;
#else
	AiDev = 0;
#endif
   	//s32AiChnCnt = 2; 
   	s32AiChnCnt = 1;
  	//stAioAttr.u32ChnCnt = s32AiChnCnt;
    s32Ret = SAMPLE_StartAi(AiDev, s32AiChnCnt, &stAioAttr);
    if (s32Ret != HI_SUCCESS)
    {
        return HI_FAILURE;
    }
	
    /* enable AiDev 1 and channel */

    /* create AENC channel */
    //s32AencChnCnt = 2 + Audio_max_num;
	s32AencChnCnt = 1;
    s32Ret = SAMPLE_StartAenc(s32AencChnCnt);
    if (s32Ret != HI_SUCCESS)
    {
        return HI_FAILURE;
    }    

    /* bind AENC to AI channel  */
    for (i=0; i<s32AencChnCnt; i++)
    {
        AeChn = i;
		if (i>=Audio_max_num)
		{
			#if defined(Hi3531)
				AiDev = 4;
			#else
				AiDev = 0;
			#endif
			AiChn = i - Audio_max_num;
			PRV_DisableAudioVOA();
			PRV_EnableAudioVOA();
		}
		else
		{
			#if defined(Hi3531)
				AiDev = 4;
			#else
				AiDev = 0;
			#endif
			//AiDev = 0;
			AiChn = i;
		}
		PRV_AUDIO_AencBindAi(AiDev,AiChn,AeChn);
      //  s32Ret = HI_MPI_AENC_BindAi(AeChn, AiDev, AiChn);
        if (s32Ret != HI_SUCCESS)
        {
            return s32Ret;
        }
    }

    /* create pthread to get aenc stream */
	g_play_flag = 1;
	if(pthread_create(&sndplay_tid,NULL,(START_ROUTINE)playsound_main,NULL) != 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "pthread_create playsound_main fail\n");
		return ERROR;
	}

	sleep(2);
	g_play_flag = 0;

    pthread_join(sndplay_tid, NULL);
	g_play_flag = 1;
	SAMPLE_StopAenc(s32AencChnCnt);
    //SAMPLE_StopAi(0, 2);
    //SAMPLE_StopAi(1, Audio_max_num);
   SAMPLE_StopAi(AiDev, s32AiChnCnt);
    
    return OK;
}

int SAMPLE_Unite_AudioInDete(void)
{
    HI_S32 i, s32Ret;
    AUDIO_DEV AiDev;
    AI_CHN AiChn;
    AIO_ATTR_S stAioAttr;
    HI_S32 s32AiChnCnt;
    HI_S32 s32AencChnCnt;
    AENC_CHN AeChn;
	extern void playsound_main(void *data);
	typedef void *(*START_ROUTINE)(void*);
	//HI_S32  Audio_max_num=DEV_AUDIO_VAM_NUM;
	HI_S32	Audio_max_num=0;

	//if(Audio_max_num < 4)
	//	Audio_max_num = 4;
	
    stAioAttr.enWorkmode = I2S_WORK_MODE;
    stAioAttr.u32ChnCnt = 2;
    stAioAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
    stAioAttr.enSamplerate = AUDIO_SAMPLE_RATE_8000;
    stAioAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
    stAioAttr.u32EXFlag = 1;
    stAioAttr.u32FrmNum = 30;
    stAioAttr.u32PtNumPerFrm = AUDIO_PTNUMPERFRM;
    stAioAttr.u32ClkSel = 0;
    

    /* config audio codec */
	PRV_TW2865_CfgAudio(stAioAttr.enSamplerate);
    
    /* enable AiDev 0 and channel */
	#if defined(Hi3531)
		AiDev = 4;
	#else
		AiDev = 0;
	#endif
   	//s32AiChnCnt = 2; 
   	s32AiChnCnt = 1;
  	//stAioAttr.u32ChnCnt = s32AiChnCnt;
    s32Ret = SAMPLE_StartAi(AiDev, s32AiChnCnt, &stAioAttr);
    if (s32Ret != HI_SUCCESS)
    {
        return HI_FAILURE;
    }
	
    /* enable AiDev 1 and channel */

    /* create AENC channel */
    //s32AencChnCnt = 2 + Audio_max_num;
	s32AencChnCnt = 1;
    s32Ret = SAMPLE_StartAenc(s32AencChnCnt);
    if (s32Ret != HI_SUCCESS)
    {
        return HI_FAILURE;
    }    

    /* bind AENC to AI channel  */
    for (i=0; i<s32AencChnCnt; i++)
    {
        AeChn = i;
		if (i>=Audio_max_num)
		{
			#if defined(Hi3531)
				AiDev = 4;
			#else
				AiDev = 0;
			#endif
			AiChn = i - Audio_max_num;
			PRV_DisableAudioVOA();
			PRV_EnableAudioVOA();
		}
		else
		{
			#if defined(Hi3531)
				AiDev = 4;
			#else
				AiDev = 0;
			#endif
			//AiDev = 0;
			AiChn = i;
		}
		PRV_AUDIO_AencBindAi(AiDev,AiChn,AeChn);
      //  s32Ret = HI_MPI_AENC_BindAi(AeChn, AiDev, AiChn);
        if (s32Ret != HI_SUCCESS)
        {
            return s32Ret;
        }
    }

    /* create pthread to get aenc stream */
	g_play_flag = 1;
	if (pthread_create(&g_aenc_tid, NULL, Sample_AencProc, (void*)s32AencChnCnt) != 0)
	{
		perror("pthread_create Sample_AencProc fail\n");
		return 0;
	}
    return OK;
}

int SAMPLE_Unite_Wait_AudioInDete_Out(void)
{
    int s32Ret = 0;
	
	g_play_flag = 0;
	pthread_join(g_aenc_tid, (void*)&s32Ret);
	g_play_flag = 1;
	SAMPLE_StopAenc(1);
	#if defined(Hi3531)
		SAMPLE_StopAi(4, 1);
	#else
		SAMPLE_StopAi(0, 1);
	#endif
	return s32Ret;
}
#endif

int PRV_TestAi(void)
{
	return SAMPLE_AiAenc();
}

#endif
/************************************************************************/
