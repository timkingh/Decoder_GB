
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
#include <hi_comm_vo.h>

#include "disp_api.h"
#include "dec_api.h"
#include "PlayBack_api.h"

static UINT8 PlayBack_OutPutMode;
extern UINT8 PB_Full_id;
int bIsQueryVdec[DEV_DEC_TOTALVOCHN] = {0};
int bIsStopReadData[DEV_DEC_TOTALVOCHN] = {0};
int TimerHandle = -1;
HI_U64 ChIndex_CurReadVideoPts = 0;//当前读的基准通道视频时间戳
ST_FMG_QUERY_FILE_RSP *st_file = NULL;
VIDEO_FRAME_INFO_S stUserFrameInfo;
PRM_ID_TIME PlayStartTime, PlayEndTime;
HI_U32 ReadLen[DEV_DEC_TOTALVOCHN]={0};
char *PstDataAddr[DEV_DEC_TOTALVOCHN] = {NULL};
char *SlaveDataAddr = NULL;
int SlaveDataAddrOffset = 0;
int bIsSizeEnough = 1;//足够
int IsVdecBufFull[DEV_DEC_TOTALVOCHN] = {0};
HI_U64 g_PreAudioPts=0;
HI_U32 Dec_PtNumPerFrm = 160;
int AudioDataAddr[160]={0};
int ImagNum = 0;
sem_t Play_FileRead, Play_Pause, semt, Play_probtime, Play_VExit, sem_StepPlay;  /* 与各线程相关的信号量 */
pthread_mutex_t mutex_play, mutex_time;  /* 用于回放控制、回放进度时间控制的互斥锁 */
#if defined(Hi3531)||defined(Hi3535)
extern HI_S32 PRV_VDEC_BindVpss(VDEC_CHN VdChn, VPSS_GRP VpssGrp);
extern HI_S32 PRV_VDEC_UnBindVpss(VDEC_CHN VdChn, VPSS_GRP VpssGrp);
#endif
time_t PlayBack_PrmTime_To_Sec(PRM_ID_TIME *pTime)
{
	struct tm stTime;
	SN_MEMSET(&stTime, 0, sizeof(stTime));
	if(pTime != NULL)
	{
		stTime.tm_year = pTime->Year - 1900;
		stTime.tm_mon  = pTime->Month - 1;
		stTime.tm_mday = pTime->Day;
		stTime.tm_hour = pTime->Hour;
		stTime.tm_min  = pTime->Minute;
		stTime.tm_sec  = pTime->Second;
		return mktime(&stTime);
	}
	return 0;
}

void   PlayBack_Sec_To_PrmTime(time_t pts, PRM_ID_TIME *pTime)
{
	struct tm stTime; 

	localtime_r(&pts, &stTime);
	if (pTime != NULL)
	{
		pTime->Day = stTime.tm_mday;
		pTime->Hour = stTime.tm_hour;
		pTime->Minute = stTime.tm_min;
		pTime->Second = stTime.tm_sec;
		pTime->Month = stTime.tm_mon + 1;
		pTime->Year = stTime.tm_year + 1900;
	}
}
void   PlayBack_GetCurPlayTime(UINT8 VoChn, PRM_ID_TIME *pTime)
{
	time_t locTime = 0;
	g_PRVPtsinfo stPlayPtsInfo;

	PRV_GetVoChnPtsInfo(VoChn, &stPlayPtsInfo);
	locTime = (time_t)(stPlayPtsInfo.CurShowPts/1000000); /* 获取当前进度条显示时间 */
	if (stPlayPtsInfo.CurShowPts % 1000000 >= 500000)
	{
		locTime += 1;
	}
	PlayBack_Sec_To_PrmTime(locTime, pTime); /* 将对应时间转换成时间结构体 */
	TRACE(SCI_TRACE_NORMAL, MOD_MMI, "============localTime: %d\n", locTime);
	TRACE(SCI_TRACE_NORMAL, MOD_MMI, "VoChn: %d========pTime  %d-%d-%d, %d.%d.%d\n", VoChn, pTime->Year, pTime->Month, pTime->Day, pTime->Hour, pTime->Minute, pTime->Second);

}

//即时回放下获取进度条时间百分比(5分钟之内)
int   PlayBack_GetCurPlayTime_Instant(UINT8 VoChn)
{
	time_t locTime = 0, beginQueryTime = 0, finalQueryTime = 0;
	g_PRVPtsinfo stPlayPtsInfo;
	int Pos = 0;
	PRV_GetVoChnPtsInfo(VoChn, &stPlayPtsInfo);
	locTime = (time_t)(stPlayPtsInfo.CurShowPts/1000000); /* 获取当前进度条显示时间 */
	if (stPlayPtsInfo.CurShowPts % 1000000 >= 500000)
	{
		locTime += 1;
	}
	TRACE(SCI_TRACE_NORMAL, MOD_MMI, "VoChn: %d========QueryStartTime  %d-%d-%d, %d.%d.%d\n", VoChn, stPlayPtsInfo.QueryStartTime.Year, stPlayPtsInfo.QueryStartTime.Month, stPlayPtsInfo.QueryStartTime.Day, stPlayPtsInfo.QueryStartTime.Hour, stPlayPtsInfo.QueryStartTime.Minute, stPlayPtsInfo.QueryStartTime.Second);
	TRACE(SCI_TRACE_NORMAL, MOD_MMI, "VoChn: %d========QueryFinalTime  %d-%d-%d, %d.%d.%d\n", VoChn, stPlayPtsInfo.QueryFinalTime.Year, stPlayPtsInfo.QueryFinalTime.Month, stPlayPtsInfo.QueryFinalTime.Day, stPlayPtsInfo.QueryFinalTime.Hour, stPlayPtsInfo.QueryFinalTime.Minute, stPlayPtsInfo.QueryFinalTime.Second);
	beginQueryTime = PlayBack_PrmTime_To_Sec(&stPlayPtsInfo.QueryStartTime);
	finalQueryTime = PlayBack_PrmTime_To_Sec(&stPlayPtsInfo.QueryFinalTime);

	TRACE(SCI_TRACE_NORMAL, MOD_MMI, "VoChn: %d, locTime=%u, beginQueryTime=%u, finalQueryTime=%u\n", VoChn, locTime, beginQueryTime, finalQueryTime);

	if(finalQueryTime - beginQueryTime == 0)
		return 0;
	if(locTime > beginQueryTime)
		Pos = (UINT32)(locTime - beginQueryTime)*100/(finalQueryTime - beginQueryTime);
	else
		Pos = 0;
	TRACE(SCI_TRACE_NORMAL, MOD_MMI, "Pos: %d========%d============%d===%lld\n", Pos, locTime - beginQueryTime, finalQueryTime - beginQueryTime, stPlayPtsInfo.CurShowPts);
	return Pos;
}


HI_S32 Playback_GetSpeedCprToNormal(UINT8 VoChn)
{
	HI_S32 j = 0, index = 1, count = 0;
	g_ChnPlayStateInfo stPlayStateInfo;
	PRV_GetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);
	if (stPlayStateInfo.CurSpeedState < DEC_SPEED_NORMAL)  /*状态不同，各个通道同步处理一次性送入数据的总帧数不同 */
	{
		count = DEC_SPEED_NORMAL - stPlayStateInfo.CurSpeedState;
	}
	else
	{
		count = stPlayStateInfo.CurSpeedState - DEC_SPEED_NORMAL;
	}

	if(count)	
	{
		for(j=0; j<count; j++)
		{
			index = index * 2;
		}
	}

	if (index < 1)
	{
		index = 1;
	}
	
	return index;
}

HI_S32 ConvertFrameRatio(VO_CHN VoChn)
{
	HI_S32 FrameRate = 0;
	g_ChnPlayStateInfo stPlayStateInfo;
	PRV_GetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);

	if (stPlayStateInfo.CurPlayState <= DEC_STATE_NORMALPAUSE)	/* 计算各种状态下应设置的帧率 */
	{
		if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALSLOW2)
		{
			FrameRate = (HI_S32)(25 / 2);
		}
		if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALSLOW4)
		{
			FrameRate = (HI_S32)(25 / 4);
		}
		if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALSLOW8)
		{
			FrameRate = (HI_S32)(25 / 8);
		}
		if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMAL)
		{
			FrameRate = 25;
		}
		if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALFAST2)
		{
			FrameRate = 2*(25);
		}
		if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALFAST4)
		{
			FrameRate = 4*(25);
		}
		if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALFAST8)
		{
			FrameRate = 8*(25);
		}
		if (FrameRate < 1)
		{
			FrameRate = 1;
		}
		return FrameRate;
	}
	return FrameRate;
}

float ConvertVdecSizeRatio(VO_CHN VoChn)
{
	float ConVertCnt = 0, WidthCnt = 0, HeightCnt = 0;
	
	g_ChnPlayStateInfo stPlayStateInfo;
	PRV_GetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);
	WidthCnt = (float)VochnInfo[VoChn].VideoInfo.width / Pal_QCifVdecWidth;
	HeightCnt = (float)VochnInfo[VoChn].VideoInfo.height/ Pal_QCifVdecHeight;
	ConVertCnt = WidthCnt * HeightCnt;
	return ConVertCnt;
}


void   PlayBack_AdaptRealType()
{
	HI_S32 i = 0, j = 0, index = 0, DecodeCnt[DEV_DEC_SLAVECNT] = {0}, FrameRate = 0, IsNoRealType = 0;
	HI_S32 ChnAbility[DEV_DEC_TOTALVOCHN] = {0}, ChnOrder[DEV_DEC_TOTALVOCHN] = {0};
	float ConvertRatio = 1.0;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Enter==========PlayBack_AdaptRealType\n");
	g_ChnPlayStateInfo stPlayStateInfo;

	for (j = 0; j < DEV_DEC_SLAVECNT; j++)
	{
		for (i = 0; i < DEV_DEC_TOTALVOCHN; i++)	/* 计算每片所属通道全部解码能力 */
		{
			PRV_GetVoChnPlayStateInfo(i, &stPlayStateInfo);
			if ((stPlayStateInfo.SynState == SYN_PLAYING) && (VochnInfo[i].SlaveId == j))
			{
				ConvertRatio = ConvertVdecSizeRatio(i);
				FrameRate = ConvertFrameRatio(i);
				ChnAbility[index] = (HI_S32)(ConvertRatio * FrameRate);
				ChnOrder[index] = i;
				DecodeCnt[j] += ChnAbility[index];
				index++;
			}
		}
		
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave %d all chn real play--DecodeCnt=%d---DEV_DEC_MAX_DECODECNT = %d", j, DecodeCnt[j],DEV_DEC_MAX_DECODECNT);
	}
	for (j = 0; j < DEV_DEC_SLAVECNT; j++)
	{
		if (DecodeCnt[j] > DEV_DEC_MAX_DECODECNT)
		{
			IsNoRealType = 1;
			break;
		}
	}
	//只要存在一片性能不满足，则所有都是非实时
	if(IsNoRealType == 1)
	{
		for (j = 0; j < DEV_DEC_SLAVECNT; j++)
		{
			for (i = 0; i < DEV_DEC_TOTALVOCHN; i++)
			{
				PRV_GetVoChnPlayStateInfo(i, &stPlayStateInfo);
				if ((stPlayStateInfo.SynState == SYN_PLAYING) 
					&& (VochnInfo[i].SlaveId == j))
				{
					stPlayStateInfo.RealType = DEC_TYPE_NOREAL;
					PRV_SetVoChnPlayStateInfo(i, &stPlayStateInfo);
					//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "VoChn=%d---RealType=%d",i, stPlayChnInfo.RealType);
				}
			}	
		}
	}
	else
	{
		for (j = 0; j < DEV_DEC_SLAVECNT; j++)
		{
			for (i = 0; i < DEV_DEC_TOTALVOCHN; i++)
			{
				PRV_GetVoChnPlayStateInfo(i, &stPlayStateInfo);
				if ((stPlayStateInfo.SynState == SYN_PLAYING) 
					&& (VochnInfo[i].SlaveId == j))
				{
					stPlayStateInfo.RealType = DEC_TYPE_REAL;
					PRV_SetVoChnPlayStateInfo(i, &stPlayStateInfo);
					//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "SlaveId: %d, VoChn=%d---RealType=%d",j, i, stPlayChnInfo.RealType);
				}
			}		
		}

	}
#if 0
	g_PlayInfo stPlayInfo;
	PRV_GetPlayInfo(&stPlayInfo);
	printf("==============stPlayInfo.IsSingle: %d\n", stPlayInfo.IsSingle);
	if(stPlayInfo.IsSingle)
	{
		for(i = 0; i < DEV_DEC_TOTALVOCHN; i++)
		{
			PRV_GetVoChnPlayStateInfo(i, &stPlayStateInfo);
			if(stPlayStateInfo.SynState == SYN_PLAYING)
			{
				break;
			}
		}
		if(i == DEV_DEC_TOTALVOCHN)
		{
			printf("============No Single Chn\n");
		}
		PRV_GetVoChnPlayStateInfo(i, &stPlayStateInfo);
		printf("i: %d, line: %d==========stPlayStateInfo.CurSpeedState: %d\n", i, __LINE__, stPlayStateInfo.CurSpeedState);
		if(stPlayStateInfo.CurSpeedState > DEC_SPEED_NORMALFAST2)
		{
			stPlayStateInfo.RealType = DEC_TYPE_NOREAL;
		}
		PRV_SetVoChnPlayStateInfo(i, &stPlayStateInfo);
	}
#endif	
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "out Playback_AdaptRealType");

}

void   PlayBack_ChooseSlaveId(VO_CHN VoChn, HI_S32 width, HI_S32 heigth, HI_S32 framerate)
{
	HI_S32 i = 0, FrameRate=0, ChnAbility = 0, newChnAbility = 0, MasterTmpDecDecod = 0, SlaveTmpDecDecod = 0;
	float ConvertRatio = 1.0;
	g_ChnPlayStateInfo stPlayStateInfo;

	if(framerate <= 0)
	{
		framerate = 25;
	}

	if(width <= 0 || heigth <= 0)
	{
		width = Pal_D1VdecWidth;
		heigth = Pal_D1VdecHeight;
	}

	newChnAbility = width * heigth * framerate / QCIF;
	
	for (i = 0; i < PRV_VO_CHN_NUM; i++)
	{
		if(i == VoChn)
			continue;
		PRV_GetVoChnPlayStateInfo(i, &stPlayStateInfo);
		
		if(stPlayStateInfo.SynState == SYN_PLAYING)
		{
			ConvertRatio = ConvertVdecSizeRatio(i);
			FrameRate = ConvertFrameRatio(i);
			ChnAbility = (HI_S32)(ConvertRatio * FrameRate);
			if(VochnInfo[i].SlaveId == PRV_MASTER)
			{
				MasterTmpDecDecod += ChnAbility;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV,"chn=%d-----ChnAbility=%d", i, ChnAbility);
			}
			else
			{
				SlaveTmpDecDecod += ChnAbility;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV,"chn=%d-----ChnAbility=%d",i, ChnAbility);
			}	
		}
	}
#if defined(SN9234H1)
	if(MasterTmpDecDecod == 0)//主片优先
	{
		VochnInfo[VoChn].SlaveId = PRV_MASTER;
	}
	else if(SlaveTmpDecDecod == 0)
	{
		VochnInfo[VoChn].SlaveId = PRV_SLAVE_1;
	}
	else
	{
		if((MasterTmpDecDecod + 2 * newChnAbility <= DEV_DEC_MAX_DECODECNT) && (SlaveTmpDecDecod + 2 * newChnAbility <= DEV_DEC_MAX_DECODECNT))
		{
			if(MasterTmpDecDecod < SlaveTmpDecDecod)
			{
				VochnInfo[VoChn].SlaveId = PRV_MASTER;
			}
			else
			{
				VochnInfo[VoChn].SlaveId = PRV_SLAVE_1;
			}
		}
		else if((MasterTmpDecDecod + 2 * newChnAbility <= DEV_DEC_MAX_DECODECNT) || (SlaveTmpDecDecod + 2 * newChnAbility <= DEV_DEC_MAX_DECODECNT))
		{
			if(MasterTmpDecDecod + 2 * newChnAbility <= DEV_DEC_MAX_DECODECNT)
			{
				VochnInfo[VoChn].SlaveId = PRV_MASTER;
			}
			else
			{
				VochnInfo[VoChn].SlaveId = PRV_SLAVE_1;
			}
		}
		else if((MasterTmpDecDecod + newChnAbility <= DEV_DEC_MAX_DECODECNT) && (SlaveTmpDecDecod + newChnAbility <= DEV_DEC_MAX_DECODECNT))
		{
			if(MasterTmpDecDecod > SlaveTmpDecDecod)
			{
				VochnInfo[VoChn].SlaveId = PRV_MASTER;
			}
			else
			{
				VochnInfo[VoChn].SlaveId = PRV_SLAVE_1;
			}
		}
		else if((MasterTmpDecDecod + newChnAbility <= DEV_DEC_MAX_DECODECNT) || (SlaveTmpDecDecod + newChnAbility <= DEV_DEC_MAX_DECODECNT))
		{
			if(MasterTmpDecDecod + newChnAbility <= DEV_DEC_MAX_DECODECNT)
			{
				VochnInfo[VoChn].SlaveId = PRV_MASTER;
			}
			else
			{
				VochnInfo[VoChn].SlaveId = PRV_SLAVE_1;
			}
		}
		else
		{
			if(MasterTmpDecDecod < SlaveTmpDecDecod)
			{
				VochnInfo[VoChn].SlaveId = PRV_MASTER;
			}
			else
			{
				VochnInfo[VoChn].SlaveId = PRV_SLAVE_1;
			}
		}
		
	}

#if 0
	if(MasterTmpDecDecod <= SlaveTmpDecDecod)
		VochnInfo[VoChn].SlaveId = PRV_MASTER;
	else
		VochnInfo[VoChn].SlaveId = PRV_SLAVE_1;
#endif	

#else
	VochnInfo[VoChn].SlaveId = PRV_MASTER;
#endif
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Choose Slave ID for channel: %d===SlaveId: %d!", VoChn, VochnInfo[VoChn].SlaveId );
	
	VochnInfo[VoChn].CurChnIndex = VoChn - LOCALVEDIONUM;
	VochnInfo[VoChn].VoChn = VoChn; 
	VochnInfo[VoChn].VdecChn = VoChn; 
		
}
HI_S32 PlayBack_SetVoChnScreen(HI_S32 VoDev, HI_S32 s32ChnCnt, HI_U32 u32Width, HI_U32 u32Height)
{
	HI_S32 i=0, div = 0, s32Ret = 0;
	HI_S32 width = 0, height = 0,u32Width_s=0,u32Hight_s=0;
	VO_CHN_ATTR_S stChnAttr;
	u32Width_s = u32Width;
	u32Hight_s = u32Height;
	if(s32ChnCnt==9)
    {
        while(u32Width%6 != 0)
			u32Width++;
		while(u32Height%6 != 0)
			u32Height++;    
	}
	div = sqrt(s32ChnCnt);		/* 计算每个通道的宽度和高度 */
	width = (HI_S32)(u32Width/div);//每个输出通道的宽高
	height = (HI_S32)(u32Height/div);

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

    
	
	for (i = 0; i < s32ChnCnt; i++)
	{
		s32Ret = HI_MPI_VO_GetChnAttr(VoDev, i, &stChnAttr);
		stChnAttr.stRect.s32X = width*(i%div);/* 其它画面显示时通道号从小到大依次排列 */
		stChnAttr.stRect.s32Y = height*(i/div);
		stChnAttr.stRect.u32Width= width;
		stChnAttr.stRect.u32Height = height;
		if( s32ChnCnt==9 )
		{ 
			if((i + 1) % 3 == 0)//最后一列
				stChnAttr.stRect.u32Width = u32Width_s- stChnAttr.stRect.s32X;
			if(i > 5 && i < 9)//最后一行
				stChnAttr.stRect.u32Height = u32Hight_s- stChnAttr.stRect.s32Y;
		}
		stChnAttr.stRect.s32X 		&= (~0x01);
		stChnAttr.stRect.s32Y		&= (~0x01);
		stChnAttr.stRect.u32Width   &= (~0x01);
		stChnAttr.stRect.u32Height  &= (~0x01);
		s32Ret = HI_MPI_VO_SetChnAttr(VoDev, i, &stChnAttr);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV,"VoDevID:%d--In set channel %d attr failed with %x! ",VoDev, i, s32Ret);
		}
		HI_MPI_VO_SetChnField(VoDev, i, VO_FIELD_BOTH);
		
		HI_MPI_VO_EnableChn(VoDev, i);
#if defined(Hi3535)
		HI_MPI_VO_ShowChn(VoDev, i);
#else
		HI_MPI_VO_ChnShow(VoDev, i);
#endif
	}
	return HI_SUCCESS;
}
void PlayBack_GetPlaySize(HI_U32 *Width, HI_U32 *Height)
{
	HI_S32 s32Ret = 0, GuiVoDev = 0;
	HI_S32 SubWidth, SubHeight;
	HI_U32 u32Width = 0, u32Height = 0;
	float ConvertRatio = 1.0;
    HI_U8 FullScreenId;
    g_PlayInfo stPlayInfo;
	PRV_GetPlayInfo(&stPlayInfo);
	FullScreenId=stPlayInfo.FullScreenId;
	
	VO_VIDEO_LAYER_ATTR_S pstLayerAttr;
	PRV_GetGuiVo(&GuiVoDev);
	s32Ret = HI_MPI_VO_GetVideoLayerAttr(GuiVoDev, &pstLayerAttr);
	u32Width = pstLayerAttr.stImageSize.u32Width;
	u32Height = pstLayerAttr.stImageSize.u32Height;
	MMI_GetReplaySize(&SubWidth, &SubHeight);
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-2222SubWidth=%d---SubHeight=%d---\n", SubWidth, SubHeight);
	ConvertRatio = (float)pstLayerAttr.stImageSize.u32Width/VOWIDTH;
	SubWidth = (HI_U32) (SubWidth * ConvertRatio);
	ConvertRatio = (float)pstLayerAttr.stImageSize.u32Height/VOHEIGH;
	SubHeight = (HI_U32)(SubHeight * ConvertRatio); 
	if(FullScreenId==1 ||stPlayInfo.IsZoom==1|| PB_Full_id==1)
	{
       *Width = u32Width;
	   *Height = u32Height;
	}
	else
	{
       *Width = u32Width - SubWidth;
	   *Height = u32Height - SubHeight;
	}
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-333*Width=%d---*Height=%d---\n", *Width, *Height);
}


/***********************************************************************************************************
函数名:		Playback_SetVoZoomInWindow
功能:     	设置视频输出通道属性
输入参数:  	HI_S32 FirstId      首次绑定
			HI_S32 s32ChnCnt    画面号
			HI_U32 u32Width     图像层宽度
			HI_U32 u32Height    图像层高度
输出:		HI_FAILURE:失败， HI_SUCCESS:成功
************************************************************************************************************/
HI_S32 PlayBack_SetVoZoomInWindow(VO_DEV VoDev, VO_CHN VoChn)
{
	HI_S32 s32Ret = 0, div = 0, width = 0, height = 0,s32ChnCount = 0;
	HI_U32 u32Width = 0, u32Height = 0,u32width_s=0,u32Height_s=0;
	VO_CHN_ATTR_S stChnAttr;
	VO_ZOOM_ATTR_S stZoomAttr;	
	g_ChnPlayStateInfo stPlayStateInfo;
	g_PlayInfo stPlayInfo;
	PRV_GetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);	
	PRV_GetPlayInfo(&stPlayInfo);
	s32ChnCount = stPlayInfo.ImagCount;
	PlayBack_GetPlaySize(&u32Width, &u32Height);
    u32width_s=u32Width;
    u32Height_s=u32Height;
	if(s32ChnCount==9)
	{
 
        while(u32Width%6!= 0)
		    u32Width++;
		while(u32Height%6 != 0)
		    u32Height++;
	}

	div = sqrt(s32ChnCount);		/* 计算每个通道的宽度和高度 */
	width = (HI_S32)(u32Width/div);
	height = (HI_S32)(u32Height/div);
	s32Ret = HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stChnAttr);
	stChnAttr.stRect.s32X = width*(VoChn%div);/* 其它画面显示时通道号从小到大依次排列 */
	stChnAttr.stRect.s32Y = height*(VoChn/div);
	stChnAttr.stRect.u32Width= width;
	stChnAttr.stRect.u32Height = height;
	if(s32ChnCount==9)
    {
      
	    if((VoChn+ 1) % 3 == 0)//最后一列
			stChnAttr.stRect.u32Width = u32width_s- stChnAttr.stRect.s32X;
		if(VoChn > 5 && VoChn < 9)//最后一行
			stChnAttr.stRect.u32Height = u32Height_s- stChnAttr.stRect.s32Y;
		
	}
	stChnAttr.stRect = PRV_ReSetVoRect(PlayBack_OutPutMode, VochnInfo[VoChn].VideoInfo.width, VochnInfo[VoChn].VideoInfo.height, stChnAttr.stRect);
    
	stChnAttr.stRect.s32X		&= (~0x01);
	stChnAttr.stRect.s32Y		&= (~0x01);
	stChnAttr.stRect.u32Width	&= (~0x01);
	stChnAttr.stRect.u32Height	&= (~0x01);
	s32Ret = HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stChnAttr);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VoDevID:%d--In set channel %d attr failed with %x! ",VoDev, VoChn, s32Ret);
	}
#if defined(SN9234H1)
	stZoomAttr.enField = VIDEO_FIELD_INTERLACED;	
#else
	stZoomAttr.enZoomType = VOU_ZOOM_IN_RECT;
#endif	
	stZoomAttr.stZoomRect = stChnAttr.stRect;
	s32Ret = HI_MPI_VO_SetZoomInWindow(VoDev, VoChn, &stZoomAttr);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VoDevID:%d--Set zoom %d area on cascade picture failed--%x!\n", VoDev, VoChn, s32Ret);
	}
#if defined(SN9234H1)	
	s32Ret = HI_MPI_VI_BindOutput(0, 0, VoDev, VoChn);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_DEC,"HI_MPI_VI_BindOutput %x", s32Ret);
	}
#endif	
	return HI_SUCCESS;
}

/*****************************************************************************
函数名:		PlaybackStartAdec
功能:     	创建音频解码通道
输入参数:  	ADEC_CHN AdChn   通道号
			HI_S32 AdecEntype  音频解码类型
输出:		HI_FAILURE:失败， HI_SUCCESS:成功
*****************************************************************************/
HI_S32 PlayBack_StartAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn, HI_S32 AdecEntype)
{
	HI_S32 s32Ret=0;
	ADEC_CHN_ATTR_S stAdecAttr;
	AIO_ATTR_S stAioAttr;

	CHECK(HI_MPI_AO_DisableChn(AoDev, AoChn));	
    CHECK(HI_MPI_AO_Disable(AoDev));

	if(AdecEntype == PT_PCMU)
	{
		stAdecAttr.enType = PT_G711U;
	}
	else if(AdecEntype == PT_PCMA)
	{
		stAdecAttr.enType = PT_G711A;
	}
	else
	{
		stAdecAttr.enType = AdecEntype;
	}
	
	stAdecAttr.u32BufSize = 20;
	stAdecAttr.enMode = ADEC_MODE_PACK;/* propose use pack mode in your app */

	CHECK(HI_MPI_AO_GetPubAttr(AoDev, &stAioAttr));
	if(stAioAttr.u32PtNumPerFrm != 160 || stAioAttr.u32PtNumPerFrm != 320)
	{
		stAioAttr.u32PtNumPerFrm = 320;
	}

	stAioAttr.u32ClkSel = 0;

	CHECK(HI_MPI_AO_SetPubAttr(AoDev, &stAioAttr));
	
	CHECK(HI_MPI_AO_Enable(AoDev));

	CHECK(HI_MPI_AO_EnableChn(AoDev, AoChn));

	if (PT_ADPCMA == stAdecAttr.enType)
	{
		ADEC_ATTR_ADPCM_S stAdpcm;
		stAdecAttr.pValue = &stAdpcm;
		stAdpcm.enADPCMType = AUDIO_ADPCM_TYPE ;
	}
	else if (PT_G711A == stAdecAttr.enType || PT_G711U == stAdecAttr.enType)
	{
		ADEC_ATTR_G711_S stAdecG711;
		stAdecAttr.pValue = &stAdecG711;
	}
	else if (PT_G726 == stAdecAttr.enType)
	{
		ADEC_ATTR_G726_S stAdecG726;
		stAdecAttr.pValue = &stAdecG726;
		stAdecG726.enG726bps = G726_BPS ;      
	}
#if defined(SN9234H1)	
	else if (PT_AMR == stAdecAttr.enType)
	{
		ADEC_ATTR_AMR_S stAdecAmr;
		stAdecAttr.pValue = &stAdecAmr;
		stAdecAmr.enFormat = AMR_FORMAT;
	}
	else if (PT_AAC == stAdecAttr.enType)
	{
		ADEC_ATTR_AAC_S stAdecAac;
		stAdecAttr.pValue = &stAdecAac;
		stAdecAttr.enMode = ADEC_MODE_STREAM;
	}
#endif	
	else if (PT_LPCM == stAdecAttr.enType)
	{
		ADEC_ATTR_LPCM_S stAdecLpcm;
		stAdecAttr.pValue = &stAdecLpcm;
		stAdecAttr.enMode = ADEC_MODE_PACK;
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "invalid aenc payload type:%d ", stAdecAttr.enType);
		return HI_FAILURE;
	}      
	s32Ret = HI_MPI_ADEC_CreateChn(AdChn, &stAdecAttr);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"create adec chn %d err:0x%x ", AdChn,s32Ret);
		return HI_FAILURE;
	}
#if defined(SN9234H1)
	s32Ret = HI_MPI_AO_BindAdec(AoDev, AoChn, AdChn);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_DEC, "HI_MPI_AO_BindAdec fail---%x!", s32Ret);
	}
#else
	if(!IsAdecBindAo)
	{
		CHECK(PRV_AUDIO_AoBindAdec(AoDev, AoChn, AdChn));
		IsAdecBindAo = 1;
	}
#endif

	PtNumPerFrm = stAioAttr.u32PtNumPerFrm;
	Prv_Audiobuf.length = 0;

	return HI_SUCCESS;
}

HI_S32 PlayBack_StopAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn)
{
	HI_S32 s32Ret = 0;
	HI_MPI_AO_ResumeChn(AoDev, AoChn);
#if defined(SN9234H1)	
	s32Ret = HI_MPI_AO_UnBindAdec(AoDev, AoChn, AdChn);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_AO_UnBindAdec error--%#x!",s32Ret);
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_AO_UnBindAdec success ");
	}
#else
	if(IsAdecBindAo)
	{
		CHECK(PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, DecAdec));
		IsAdecBindAo = 0;
	}
#endif

	s32Ret = HI_MPI_ADEC_DestroyChn(AdChn);
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_DEC,"HI_MPI_ADEC_DestroyChn ERROR---%#x!!",s32Ret);
		return s32Ret;
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_DEC,"HI_MPI_ADEC_DestroyChn SUCCESS");
	}

	Prv_Audiobuf.length = 0;
	
	return HI_SUCCESS;

}
HI_S32 PlayBack_StartVo()
{
	HI_S32 i = 0, s32Ret = 0, GuiVoDev = 0, s32ChnCount = 0;
	HI_S32 subwidth = 0, subheight = 0;
	HI_U32 u32Width = 0, u32Height = 0;
	float ConvertRatio = 1.0;
	g_PlayInfo stPlayInfo;
	PRV_GetPlayInfo(&stPlayInfo);
	VO_VIDEO_LAYER_ATTR_S pstLayerAttr;
	//g_chnplayinfo stPlayChnInfo;
	PRM_PREVIEW_CFG_EX preview_info;
	if (PARAM_OK != GetParameter(PRM_ID_PREVIEW_CFG_EX, NULL, &preview_info, sizeof(preview_info), 0, SUPER_USER_ID, NULL))
	{
		RET_FAILURE("get parameter PRM_PREVIEW_CFG_EX fail!");
	}
	
	if (preview_info.reserve[1] > IntelligentMode)
	{
		preview_info.reserve[1] = IntelligentMode;
	}
	
	PlayBack_OutPutMode = preview_info.reserve[1];	

	for (i = 0; i < DEV_DEC_TOTALVOCHN; i++)
	{
		HI_MPI_VO_DisableChn(0, i);
	}
	s32ChnCount = stPlayInfo.ImagCount;

	PRV_GetGuiVo(&GuiVoDev);
	s32Ret = HI_MPI_VO_GetVideoLayerAttr(GuiVoDev, &pstLayerAttr);
	u32Width = pstLayerAttr.stImageSize.u32Width;
	u32Height = pstLayerAttr.stImageSize.u32Height;
	if (GuiVoDev == 0)
	{
		MMI_GetReplaySize(&subwidth, &subheight);
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"000000000000000000000*subwidth :%d	*subheight:%d", subwidth, subheight);
		stPlayInfo.SubWidth  = subwidth;
		stPlayInfo.SubHeight = subheight;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"u32Width=%d, pstLayerAttr.stImageSize.u32Width:%d,  VOWIDTH:%d ", u32Width, pstLayerAttr.stImageSize.u32Width,VOWIDTH);
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"u32Height=%d pstLayerAttr.stImageSize.u32Height:%d	VOHEIGH:%d ", u32Height, pstLayerAttr.stImageSize.u32Height,VOHEIGH);
		ConvertRatio = (float)pstLayerAttr.stImageSize.u32Width/VOWIDTH;
		subwidth = (HI_U32) (subwidth * ConvertRatio);
		ConvertRatio = (float)pstLayerAttr.stImageSize.u32Height/VOHEIGH;
		subheight = (HI_U32)(subheight* ConvertRatio); 
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"*subwidth :%d	*subheight:%d", subwidth, subheight);
		if(stPlayInfo.FullScreenId==1)
		{}
		else
		{
            u32Width = u32Width - subwidth;
		    u32Height = u32Height - subheight;
		}
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"u32Width=%d, u32Height=%d", u32Width, u32Height);
	}
	s32Ret = PlayBack_SetVoChnScreen(GuiVoDev, s32ChnCount, u32Width, u32Height);/* 设置视频输出通道属性 */
	if (s32Ret != HI_SUCCESS)
	{
		return HI_FAILURE;
	}
	stPlayInfo.bISDB=0;
	stPlayInfo.IsZoom=0;
	PRV_SetPlayInfo(&stPlayInfo);
	//Playback_SetVoZoomInWindow(1, s32ChnCount, SlaveWidth, SlaveHeight); /* 放大从片对应的VO通道 */
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Enable vo channelsuccess");
	return HI_SUCCESS;
}


HI_S32 PlayBack_CreatVdec(VO_DEV VoDev, VO_CHN VoChn)
{
	HI_S32 s32Ret = 0, div = 0, width = 0, height = 0, index = 0;
	HI_U32 u32Width = 0, u32Height = 0,u32width_s=0,u32Height_s=0;
	
	VO_CHN_ATTR_S stChnAttr;
	
	g_ChnPlayStateInfo stPlayStateInfo;
	g_PlayInfo stPlayInfo;
	PRV_GetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);
	PRV_GetPlayInfo(&stPlayInfo);
	div = sqrt(stPlayInfo.ImagCount);

	index = PRV_GetVoChnIndex(VoChn);
	if(index < 0)
		RET_FAILURE("Valid index!!");
	
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "+++++++++Enter into PlaybackStartVdec()==VoChn: %d\n", VoChn);
	if(VochnInfo[VoChn].SlaveId == PRV_MASTER
		&& stPlayStateInfo.SynState == SYN_PLAYING
		&& VochnInfo[VoChn].IsHaveVdec == 0)
	{
		if ((VochnInfo[VoChn].VideoInfo.height > 0) && (VochnInfo[VoChn].VideoInfo.width > 0))
		{
			s32Ret = PRV_CreateVdecChn(VochnInfo[VoChn].VideoInfo.vdoType, VochnInfo[VoChn].VideoInfo.height, VochnInfo[VoChn].VideoInfo.width, VochnInfo[VoChn].u32RefFrameNum, VoChn);
			if(s32Ret != HI_SUCCESS)
			{
				printf("line: %d, PRV_CreateVdecChn fail: 0x%x\n", __LINE__, s32Ret);
				return HI_FAILURE;
			}
			VochnInfo[VoChn].IsHaveVdec = 1;
		}
		else
		{
			return HI_FAILURE;
		}
#if defined(Hi3531)
		//1.解绑VO通道
		CHECK(HI_MPI_VO_ChnHide(VoDev, VoChn));
		//if(VochnInfo[index].IsBindVdec[VoDev] == 0 || VochnInfo[index].IsBindVdec[VoDev] == 1)
		{
 			CHECK(PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn, VoChn));
			VochnInfo[index].IsBindVdec[VoDev] = -1;
		}
		CHECK(PRV_VO_UnBindVpss(VoDev, VoChn, VoChn, VPSS_PRE0_CHN));

		//2.关闭VO通道
		CHECK(HI_MPI_VO_DisableChn(VoDev ,VoChn));
#elif defined(Hi3535)
		//1.解绑VO通道
		CHECK(HI_MPI_VO_HideChn(VoDev, VoChn));
		//if(VochnInfo[index].IsBindVdec[VoDev] == 0 || VochnInfo[index].IsBindVdec[VoDev] == 1)
		{
 			CHECK(PRV_VDEC_UnBindVpss(VochnInfo[index].VdecChn, VoChn));
			VochnInfo[index].IsBindVdec[VoDev] = -1;
		}
		CHECK(PRV_VO_UnBindVpss(VoDev, VoChn, VoChn, VPSS_BSTR_CHN));

		//2.关闭VO通道
		CHECK(HI_MPI_VO_DisableChn(VoDev ,VoChn));
#endif
		//3.设置VO通道
		PlayBack_GetPlaySize(&u32Width, &u32Height);
		u32width_s=u32Width;
		u32Height_s=u32Height;
		if(stPlayInfo.ImagCount==9)
		{
					  
			while(u32Width%6!= 0)
				u32Width++;
			while(u32Height%6 != 0)
			    u32Height++;
		}

		width = (HI_S32)(u32Width/div);
		height = (HI_S32)(u32Height/div);
		
		s32Ret = HI_MPI_VO_GetChnAttr(VoDev, VoChn, &stChnAttr);
		stChnAttr.stRect.s32X = width*(VoChn%div);/* 其它画面显示时通道号从小到大依次排列 */
		stChnAttr.stRect.s32Y = height*(VoChn/div);
		stChnAttr.stRect.u32Width= width;
		stChnAttr.stRect.u32Height = height;
		if(stPlayInfo.ImagCount==9)
        {
	       if((VoChn+ 1) % 3 == 0)//最后一列
			   stChnAttr.stRect.u32Width = u32width_s- stChnAttr.stRect.s32X;
		   if(VoChn > 5 && VoChn < 9)//最后一行
			   stChnAttr.stRect.u32Height = u32Height_s- stChnAttr.stRect.s32Y;
	    }
		stChnAttr.stRect = PRV_ReSetVoRect(PlayBack_OutPutMode, VochnInfo[VoChn].VideoInfo.width, VochnInfo[VoChn].VideoInfo.height, stChnAttr.stRect);
        
		stChnAttr.stRect.s32X 		&= (~0x01);
		stChnAttr.stRect.s32Y 		&= (~0x01);
		stChnAttr.stRect.u32Width   &= (~0x01);
		stChnAttr.stRect.u32Height  &= (~0x01);
		s32Ret = HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stChnAttr);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VoDevID:%d--In set channel %d attr failed with %x! ",VoDev, VoChn, s32Ret);
		}
#if defined(SN9234H1)	
		s32Ret = HI_MPI_VDEC_BindOutput(VoChn, VoDev, VoChn);
		if (s32Ret != HI_SUCCESS)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VDEC_BindOutput VoChn=%d error  %#x !",VoChn, s32Ret);
		}
#else		
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VoChn=%d, VochnInfo[%d].VdecChn=%d, VoDev=%d\n", VoChn, index, VochnInfo[index].VdecChn, VoDev);

		{		
			CHECK_RET(PRV_VO_BindVpss(VoDev, VoChn, VoChn, VPSS_PRE0_CHN));		
			CHECK(HI_MPI_VO_ClearChnBuffer(VoDev, VochnInfo[index].VoChn, HI_TRUE));

			//4.绑定VO通道
			//if(VochnInfo[index].IsBindVdec[VoDev] == -1)
			{	
				PRV_VPSS_ResetWH(VoChn,VoChn,VochnInfo[VoChn].VideoInfo.width,VochnInfo[VoChn].VideoInfo.height);
				CHECK(PRV_VDEC_BindVpss(VoChn, VoChn));
				VochnInfo[index].IsBindVdec[VoDev] = 1;
			}
		}
		
		//CHECK_RET(HI_MPI_VO_ChnShow(VoDev,VoChn));
		
		//5.开启VO通道
		CHECK_RET(HI_MPI_VO_EnableChn(VoDev,VoChn));
		//PRV_VdecBindAllVpss(VoDev);
#endif

	}
	
	PRV_SetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);
	return HI_SUCCESS;
}


HI_S32 PlayBack_DestroyVdec(VO_DEV VoDev, VDEC_CHN VdecChn)
{
	HI_S32 s32Ret = 0;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===============PlayBack_DestroyVdec==============\n");
	s32Ret = HI_MPI_VDEC_StopRecvStream(VdecChn); 
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VdecChn=%d HI_MPI_VDEC_StopRecvStream failed errno 0x%x", VdecChn, s32Ret);  
	}
	s32Ret = HI_MPI_VDEC_ResetChn(VdecChn); 
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VdecChn=%d HI_MPI_VDEC_ResetChn error 0x%x", VdecChn, s32Ret);  
	}
	s32Ret = HI_MPI_VO_ClearChnBuffer(VoDev, VdecChn, 1); 
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"VdecChn=%d HI_MPI_VO_ClearChnBuffer error 0x%x  ", VdecChn, s32Ret);	
	}
#if defined(Hi3520)		
	s32Ret = HI_MPI_VDEC_UnbindOutputChn(VdecChn, VoDev, VdecChn);
#elif defined(Hi3535)
	CHECK(HI_MPI_VO_HideChn(VoDev, VdecChn));
	PRV_VDEC_UnBindVpss(VdecChn, VdecChn);
	VochnInfo[VdecChn].IsBindVdec[VoDev] = -1;
#else
	CHECK(HI_MPI_VO_ChnHide(VoDev, VdecChn));
	PRV_VDEC_UnBindVpss(VdecChn, VdecChn);
	VochnInfo[VdecChn].IsBindVdec[VoDev] = -1;
#endif
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VDEC_UnbindOutputChn fail!");
	}	
	s32Ret = HI_MPI_VDEC_DestroyChn(VdecChn); 
	if (s32Ret != HI_SUCCESS)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"VdecChn = %d HI_MPI_VDEC_DestroyChn failed errno 0x%x  ", VdecChn, s32Ret);
	}
	g_ChnPlayStateInfo stPlayStateInfo;
	PRV_GetVoChnPlayStateInfo(VdecChn, &stPlayStateInfo);	
	VochnInfo[VdecChn].IsHaveVdec = 0;
	PRV_SetVoChnPlayStateInfo(VdecChn, &stPlayStateInfo);
	return HI_SUCCESS;
}

HI_S32 PlayBack_CheckIntervalPtsEx(HI_S32 VoChn)
{
	HI_U64 IntervalPts=0;
	g_PRVPtsinfo stPlayPtsInfo;
	g_ChnPlayStateInfo stPlayStateInfo;
	PRV_GetVoChnPtsInfo(VoChn, &stPlayPtsInfo);
	PRV_GetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);	

	if (stPlayPtsInfo.PreVideoPts == 0)
	{
		IntervalPts = PalDec_IntervalPts;
		stPlayPtsInfo.pFrameOffectPts = IntervalPts;
		stPlayPtsInfo.IntervalPts = IntervalPts;
		PRV_SetVoChnPtsInfo(VoChn, &stPlayPtsInfo);
		return 0;
	}
	if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMAL)
	{
		if (stPlayStateInfo.CurPlayState <= DEC_STATE_NORMAL)
		{
			if (stPlayPtsInfo.CurVideoPts >= stPlayPtsInfo.PreVideoPts)
			{
				IntervalPts = (stPlayPtsInfo.CurVideoPts - stPlayPtsInfo.PreVideoPts) * 1000000/90000;
			}
			stPlayPtsInfo.pFrameOffectPts = IntervalPts;
		}
		else
		{
			if (stPlayPtsInfo.PreVideoPts >= stPlayPtsInfo.CurVideoPts)
			{
				IntervalPts = (stPlayPtsInfo.PreVideoPts - stPlayPtsInfo.CurVideoPts) * 1000000/90000;
			}
		}
	}
	else if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALFAST2)
	{
		if (stPlayStateInfo.CurPlayState <= DEC_STATE_NORMAL)
		{
			if (stPlayPtsInfo.CurVideoPts >= stPlayPtsInfo.PreVideoPts)
			{
				IntervalPts = (stPlayPtsInfo.CurVideoPts - stPlayPtsInfo.PreVideoPts) * 1000000/90000;
				IntervalPts = IntervalPts/2;
			}
		}
		else
		{
			if (stPlayPtsInfo.PreVideoPts >= stPlayPtsInfo.CurVideoPts)
			{
				IntervalPts = (stPlayPtsInfo.PreVideoPts - stPlayPtsInfo.CurVideoPts) * 1000000/90000;
				IntervalPts = IntervalPts/2;
			}
		}
	}
	else if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALFAST4)
	{
		if (stPlayStateInfo.CurPlayState <= DEC_STATE_NORMAL)
		{
			if (stPlayPtsInfo.CurVideoPts >= stPlayPtsInfo.PreVideoPts)
			{
				IntervalPts = (stPlayPtsInfo.CurVideoPts - stPlayPtsInfo.PreVideoPts) * 1000000/90000;
				IntervalPts = IntervalPts/4;
			}
		}
		else
		{
			if (stPlayPtsInfo.PreVideoPts >= stPlayPtsInfo.CurVideoPts)
			{
				IntervalPts = (stPlayPtsInfo.PreVideoPts - stPlayPtsInfo.CurVideoPts) * 1000000/90000;
				IntervalPts = IntervalPts/4;
			}
		}
	}
	else if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALFAST8)
	{
		if (stPlayStateInfo.CurPlayState <= DEC_STATE_NORMAL)
		{
			if (stPlayPtsInfo.CurVideoPts >= stPlayPtsInfo.PreVideoPts)
			{
				IntervalPts = (stPlayPtsInfo.CurVideoPts - stPlayPtsInfo.PreVideoPts) * 1000000/90000;
				IntervalPts = IntervalPts/8;
			}
		}
		else
		{
			if (stPlayPtsInfo.PreVideoPts >= stPlayPtsInfo.CurVideoPts)
			{
				IntervalPts = (stPlayPtsInfo.PreVideoPts - stPlayPtsInfo.CurVideoPts) * 1000000/90000;
				IntervalPts = IntervalPts/8;
			}
		}
	}
	else if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALSLOW2)
	{
		if (stPlayPtsInfo.CurVideoPts >= stPlayPtsInfo.PreVideoPts)
		{
			IntervalPts = (stPlayPtsInfo.CurVideoPts - stPlayPtsInfo.PreVideoPts) * 1000000/90000;
			IntervalPts = IntervalPts*2;
			stPlayPtsInfo.pFrameOffectPts = (stPlayPtsInfo.CurVideoPts - stPlayPtsInfo.PreVideoPts) * 1000000/90000;
		}
	}
	else if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALSLOW4)
	{
		if (stPlayPtsInfo.CurVideoPts >= stPlayPtsInfo.PreVideoPts)
		{
			IntervalPts = (stPlayPtsInfo.CurVideoPts - stPlayPtsInfo.PreVideoPts) * 1000000/90000;
			IntervalPts = IntervalPts*4;
			stPlayPtsInfo.pFrameOffectPts = (stPlayPtsInfo.CurVideoPts - stPlayPtsInfo.PreVideoPts) * 1000000/90000;
		}
	}
	else if (stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMALSLOW8)
	{
		if (stPlayPtsInfo.CurVideoPts >= stPlayPtsInfo.PreVideoPts)
		{
			IntervalPts = (stPlayPtsInfo.CurVideoPts - stPlayPtsInfo.PreVideoPts) * 1000000/90000;
			IntervalPts = IntervalPts*8;
			stPlayPtsInfo.pFrameOffectPts = (stPlayPtsInfo.CurVideoPts - stPlayPtsInfo.PreVideoPts) * 1000000/90000;
		}
	}
	//if(ImagNum > 9 && stPlayStateInfo.CurSpeedState == DEC_SPEED_NORMAL)
	//	IntervalPts = IntervalPts * 4 / 3;
	
	//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "1111111VoChn: %d, stPlayChnInfo.IntervalPts=%lld, IntervalPts=%lld", VoChn, stPlayPtsInfo.IntervalPts,IntervalPts);
	PRV_SetVoChnPtsInfo(VoChn, &stPlayPtsInfo);
	if (IntervalPts != stPlayPtsInfo.IntervalPts)
	{
		if(abs(IntervalPts - stPlayPtsInfo.IntervalPts) >= 4000 && IntervalPts > 0)
		{
			//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "1111111CurReadPts=%lld, PreReadPts=%lld", stPlayChnInfo.CurReadPts, stPlayChnInfo.PreReadPts);
			//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "1111111VoChn: %d, stPlayChnInfo.IntervalPts=%lld, IntervalPts=%lld", VoChn, stPlayChnInfo.IntervalPts,IntervalPts);
		}
		if(IntervalPts >= 0)
		{
			stPlayPtsInfo.IntervalPts = IntervalPts;
		}
		if(IntervalPts > 10000000)
		{
			stPlayPtsInfo.IntervalPts = 10000000;
		}
#if defined(Hi3531)||defined(Hi3535)		
		CHECK_RET(HI_MPI_VO_SetPlayToleration (DHD0, 1000));
#endif		
		PRV_SetVoChnPtsInfo(VoChn, &stPlayPtsInfo);
		return 1;
	}
	return 0;
}


void  PlayBack_Pro_PlayReq(SN_MSG *msg)
{
	Ftpc_Play_Req *PlayReq = (Ftpc_Play_Req *)msg->para;
	int VoChn = PlayReq->ImageID;
	g_ChnPlayStateInfo stPlayStateInfo;
	g_PlayInfo stPlayInfo;
	g_PRVPtsinfo stPlayPtsInfo;

	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s %d: PlayReq->ImageID = %d\n", __FUNCTION__, __LINE__, VoChn);
	
	if(VoChn > 0)
	{
		VoChn -= 1;
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s %d: PlayReq->ImageID = %d error\n", __FUNCTION__, __LINE__, VoChn);
		return;
	}
	
	PRV_GetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);	
	PRV_GetPlayInfo(&stPlayInfo);	
	PRV_GetVoChnPtsInfo(VoChn, &stPlayPtsInfo);
	stPlayPtsInfo.FirstVideoPts = 0;
	stPlayPtsInfo.FirstAudioPts = 0;
	PRV_SetVoChnPtsInfo(VoChn, &stPlayPtsInfo);
	
	if(PlayReq->PlayType == 2)
	{
		stPlayStateInfo.CurPlayState = DEC_STATE_NORMAL;
		if(stPlayInfo.PlayBackState != PLAY_PROCESS)
		{
			stPlayInfo.PlayBackState = PLAY_PROCESS;
			sem_post(&sem_VoPtsQuery);
			
		}
		//stPlayStateInfo.CurPlayState = PlayReq->CurPlayState;
		stPlayStateInfo.CurSpeedState = PlayReq->CurSpeedState;
		PRV_SetVoChnPlayStateInfo(VoChn, &stPlayStateInfo); 
		Probar_time[VoChn] = PlayBack_PrmTime_To_Sec(&(PlayReq->SetTime));
		PtsInfo[VoChn].CurShowPts = (HI_U64)Probar_time[VoChn]*1000000;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PlayBack_Pro_PlayReq SetTime: %d-%d-%d, %d.%d.%d, Probar_time[VoChn]: %d\n", PlayReq->SetTime.Year, PlayReq->SetTime.Month, PlayReq->SetTime.Day, PlayReq->SetTime.Hour, PlayReq->SetTime.Minute, PlayReq->SetTime.Second, (int)Probar_time[VoChn]);
	}
	
	PRV_SetPlayInfo(&stPlayInfo);	

}

void PlayBack_GetSlaveQueryRsp(PlayBack_MccQueryStateRsp *QueryRsp)
{
	HI_S32 VoChn = QueryRsp->VoChn;
	g_ChnPlayStateInfo stPlayStateInfo;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===============PlayBack_GetSlaveQueryRsp===============VoChn: %d\n", VoChn);
	if (VoChn < DEV_DEC_TOTALVOCHN)
	{
		PRV_GetVoChnPlayStateInfo(VoChn, &stPlayStateInfo); 
		stPlayStateInfo.QuerySlaveId = 0;
		PRV_SetVoChnPlayStateInfo(VoChn, &stPlayStateInfo); 
	}
}

HI_S32 PlayBack_MccCreatVdec(SN_MSG *msg)
{
	PlayBack_MccPBCreateVdecRsp *Rsp = (PlayBack_MccPBCreateVdecRsp *)msg->para;
	int VoChn = Rsp->VoChn;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PlayBack_MccCreatVdec===result: %d\n", Rsp->Result);
	if(Rsp->Result != 0)
	{
#if defined(SN9234H1)
		(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, HD, VochnInfo[VoChn].VoChn));					
		CHECK(HI_MPI_VO_ClearChnBuffer(HD, VochnInfo[VoChn].VoChn, 1));
#else
		//(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, DHD0, VochnInfo[index].VoChn));					
		CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, VochnInfo[VoChn].VoChn, 1));
#endif			
		VochnInfo[VoChn].bIsWaitIFrame = 1;
		return HI_FAILURE;
	}
	PlayBack_SetVoZoomInWindow(HD, VoChn);
	VochnInfo[VoChn].SlaveId = Rsp->SlaveId;
	VochnInfo[VoChn].VoChn = Rsp->VoChn;
	VochnInfo[VoChn].VdecChn = Rsp->VdecChn;
	VochnInfo[VoChn].IsHaveVdec = 1;
	VochnInfo[VoChn].IsConnect = 1;
	VochnInfo[VoChn].MccCreateingVdec = 0;
	VochnInfo[VoChn].MccReCreateingVdec = 0;
	SlaveState.SlaveCreatingVdec[VoChn] = 0; 
	
	return 0;
}

HI_S32 PlayBack_MccDestroyVdec(SN_MSG *msg)
{
	PlayBack_MccPBDestroyVdecRsp *Rsp = (PlayBack_MccPBDestroyVdecRsp *)msg->para;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PlayBack_MccDestroyVdec===result: %d\n", Rsp->Result);
	int VoChn = Rsp->VdecChn;
#if defined(SN9234H1)	
	CHECK(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, HD, VochnInfo[VoChn].VoChn));	
#else

#endif
	CHECK(HI_MPI_VO_ClearChnBuffer(HD, VochnInfo[VoChn].VoChn, 1));
	//CHECK(HI_MPI_VO_DisableChn(i, VochnInfo[index].VoChn));
	VochnInfo[VoChn].IsBindVdec[HD] = -1;
#if defined(Hi3535)	
	HI_MPI_VO_ResumeChn(HD, VoChn);
#else
	HI_MPI_VO_ChnResume(HD, VoChn);
#endif
	BufferSet(VochnInfo[VoChn].VoChn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);			
	BufferSet(VochnInfo[VoChn].VoChn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);			
	PRV_VoChnStateInit(VochnInfo[VoChn].CurChnIndex);
	PRV_PtsInfoInit(VochnInfo[VoChn].CurChnIndex);					
	PRV_InitVochnInfo(VochnInfo[VoChn].VoChn);
	
	g_PlayInfo	stPlayInfo;
	PRV_GetPlayInfo(&stPlayInfo);
	if(stPlayInfo.PlayBackState > PLAY_EXIT)
		VochnInfo[VoChn].bIsPBStat = 1;
	return 0;
}

HI_S32 PlayBack_MccReCreatVdec(SN_MSG *msg)
{
	int VoChn = 0;
	PlayBack_MccPBReCreateVdecRsp *Rsp = (PlayBack_MccPBReCreateVdecRsp *)msg->para;
	VoChn = Rsp->VdecChn;
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PlayBack_MccReCreatVdec===VoChn: %d, result: %d\n", VoChn, Rsp->Result);
	if(Rsp->Result != 0)
	{
#if defined(SN9234H1)
			(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, HD, VochnInfo[VoChn].VoChn));					
			CHECK(HI_MPI_VO_ClearChnBuffer(HD, VochnInfo[VoChn].VoChn, 1));
#else
			//(HI_MPI_VI_UnBindOutput(PRV_HD_DEV, 0, DHD0, VochnInfo[index].VoChn));					
			CHECK(HI_MPI_VO_ClearChnBuffer(DHD0, VochnInfo[VoChn].VoChn, 1));
#endif			
		VochnInfo[VoChn].MccCreateingVdec = 0;
		VochnInfo[VoChn].MccReCreateingVdec = 0;
		return HI_FAILURE;
	}
	
	PlayBack_SetVoZoomInWindow(HD, VoChn);
	VochnInfo[VoChn].IsHaveVdec = 1;
	VochnInfo[VoChn].MccReCreateingVdec = 0;	
	VochnInfo[VoChn].MccCreateingVdec = 0;
	SlaveState.SlaveCreatingVdec[VoChn] = 0; 
	return 0;
}

void   PlayBack_Pro_DoubleClickReq(SN_MSG *msg)
{

}


HI_S32 PlayBack_Pro_PauseReq(SN_MSG *msg)
{
	PlayBack_Pause_Req *PauseReq = (PlayBack_Pause_Req *)msg->para;
	PlayBack_Pause_Rsp Rsp;
	int i = 0;
	g_PlayInfo stPlayInfo;
	g_ChnPlayStateInfo stPlayStateInfo;
	PRV_GetPlayInfo(&stPlayInfo);
	Rsp.result = 0;
	Rsp.channel = PauseReq->channel;
	printf("==================Rsp.channel: %d, stPlayInfo.PlayBackState: %d\n", Rsp.channel, stPlayInfo.PlayBackState);
	if(stPlayInfo.PlayBackState == PLAY_INSTANT
		|| (stPlayInfo.PlayBackState > PLAY_INSTANT && Rsp.channel > 0))
	{
		i = PauseReq->channel;
		//if(stPlayInfo.PlayBackState > PLAY_INSTANT)
		if(i>0)
		{
			i = i-1;
		}
		else
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "chn=%d error\n", i);
			return SN_ERR_FTPC_PARAM_ERROR;
		}
		
		PRV_GetVoChnPlayStateInfo(i, &stPlayStateInfo);
		printf("i: %d, stPlayInfo.PlayBackState: %d, stPlayStateInfo.CurPlayState: %d\n", i, stPlayInfo.PlayBackState, stPlayStateInfo.CurPlayState);
		if((stPlayInfo.PlayBackState > PLAY_INSTANT && stPlayStateInfo.CurPlayState == DEC_STATE_STOP)||
			(stPlayInfo.PlayBackState == PLAY_EXIT && !VochnInfo[i].bIsPBStat))
		{
			
		}
		else
		{
			if (stPlayStateInfo.CurPlayState == DEC_STATE_NORMAL)	/* 当前状态为播放，则切换为暂停状态 */
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "chn: %d playPro:MSG_ID_DEC_PAUSE_REQ:-------pause\n", i);
				if(stPlayStateInfo.SynState == SYN_PLAYING)
				{
#if defined(Hi3535)
					HI_MPI_VO_PauseChn(0, i);
#else
					HI_MPI_VO_ChnPause(0, i);
#endif
					if(Achn == i)
					{
#if defined(Hi3531)
						HI_MPI_AO_PauseChn(4, AOCHN);
#else
						HI_MPI_AO_PauseChn(0, AOCHN);
#endif
						HI_MPI_ADEC_ClearChnBuf(DecAdec);
					}

				}
				stPlayStateInfo.CurPlayState = DEC_STATE_NORMALPAUSE;
				Ftpc_ChnStopWrite(i, 1);					
				VoChnState.bIsPBStat_StopWriteData[i] = 1;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d============bIsPBStat_StopWriteData---StopWrite==1\n", __LINE__);
			}
			else if (stPlayStateInfo.CurPlayState == DEC_STATE_NORMALPAUSE)	/* 当前状态为暂停，则切换为播放状态 */
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "chn: %d playPro:MSG_ID_DEC_PAUSE_REQ:-------resume\n", i);
				if(stPlayStateInfo.SynState == SYN_PLAYING)
				{
#if defined(Hi3535)
					HI_MPI_VO_ResumeChn(0, i);
#else
					HI_MPI_VO_ChnResume(0, i);
#endif
					if(Achn == i)
					{
#if defined(Hi3531)
						HI_MPI_AO_ResumeChn(4, AOCHN);
#else
						HI_MPI_AO_ResumeChn(0, AOCHN);
#endif
					}
						
				}
				stPlayStateInfo.CurPlayState = DEC_STATE_NORMAL;
				Ftpc_ChnStopWrite(i, 0);					
				VoChnState.bIsPBStat_StopWriteData[i] = 0;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d============bIsPBStat_StopWriteData---StopWrite==0\n", __LINE__);
				sem_post(&sem_VoPtsQuery);
			}
			PRV_SetVoChnPlayStateInfo(i, &stPlayStateInfo);
		}
		Rsp.playstate = stPlayStateInfo.CurPlayState;
		Rsp.speedstate = stPlayStateInfo.CurSpeedState;

	}
	else if(stPlayInfo.PlayBackState > PLAY_INSTANT)
	{
		for(i = 0; i < stPlayInfo.ImagCount; i++)
		{
			PRV_GetVoChnPlayStateInfo(i, &stPlayStateInfo);
			if((stPlayInfo.PlayBackState > PLAY_INSTANT && stPlayStateInfo.CurPlayState == DEC_STATE_STOP)||
				(stPlayInfo.PlayBackState == PLAY_EXIT && !VochnInfo[i].bIsPBStat))
			{
				continue;
			}
			if (stPlayStateInfo.CurPlayState == DEC_STATE_NORMAL)	/* 当前状态为播放，则切换为暂停状态 */
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "chn: %d playPro:MSG_ID_DEC_PAUSE_REQ:-------pause\n", i);
				if(stPlayStateInfo.SynState == SYN_PLAYING)
				{
#if defined(Hi3535)
					HI_MPI_VO_ResumeChn(0, i);
#else
					HI_MPI_VO_ChnPause(0, i);
#endif
					if(i == 0)
					{
#if defined(Hi3531)
						HI_MPI_AO_PauseChn(4, AOCHN);
#else
						HI_MPI_AO_PauseChn(0, AOCHN);
#endif
					}
				}
				stPlayStateInfo.CurPlayState = DEC_STATE_NORMALPAUSE;
				Ftpc_ChnStopWrite(i, 1);					
				VoChnState.bIsPBStat_StopWriteData[i] = 1;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d============bIsPBStat_StopWriteData---StopWrite==1\n", __LINE__);
				
			}
			else if (stPlayStateInfo.CurPlayState == DEC_STATE_NORMALPAUSE)	/* 当前状态为暂停，则切换为播放状态 */
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "chn: %d playPro:MSG_ID_DEC_PAUSE_REQ:-------resume\n", i);
				if(stPlayStateInfo.SynState == SYN_PLAYING)
				{
#if defined(Hi3535)
					HI_MPI_VO_ResumeChn(0, i);
#else
					HI_MPI_VO_ChnResume(0, i);
#endif
					if(i == 0)
					{
#if defined(Hi3531)
						HI_MPI_AO_ResumeChn(4, AOCHN);
#else
						HI_MPI_AO_ResumeChn(0, AOCHN);
#endif
					}
				}
				stPlayStateInfo.CurPlayState = DEC_STATE_NORMAL;			
				Ftpc_ChnStopWrite(i, 0);					
				VoChnState.bIsPBStat_StopWriteData[i] = 0;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d============bIsPBStat_StopWriteData---StopWrite==0\n", __LINE__);
			}
			Rsp.speedstate = stPlayStateInfo.CurSpeedState;
			PRV_SetVoChnPlayStateInfo(i, &stPlayStateInfo);

		}
		PRV_GetPlayInfo(&stPlayInfo);
		if(stPlayInfo.IsPause == 0)
			stPlayInfo.IsPause = 1;
		else
			stPlayInfo.IsPause = 0;
		PRV_SetPlayInfo(&stPlayInfo);
		if(stPlayInfo.IsPause == 0)
			sem_post(&sem_PlayPause);
		printf("stPlayInfo.PlayBackState > PLAY_INSTANT=========stPlayInfo.IsPause: %d\n", stPlayInfo.IsPause);
		Rsp.playstate = (stPlayInfo.IsPause == 0) ? DEC_STATE_NORMAL : DEC_STATE_NORMALPAUSE;
		
	}
	SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_PBPAUSE_REQ, NULL, 0);					
	usleep(100 * 1000);
	SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, msg->source, msg->xid, msg->thread, MSG_ID_PRV_PAUSE_RSP, &Rsp, sizeof(Rsp));					
	
	return 0;

}


void PlayBack_Stop(UINT8 channel)
{
	HI_S32 i = 0;
	g_PlayInfo stPlayInfo;
	g_ChnPlayStateInfo stPlayStateInfo;
	PlayBack_MccPBDestroyVdecReq MccDestroyVdecReq;
	PRV_GetPlayInfo(&stPlayInfo);
	//for(i = 0; i < stPlayInfo.ImagCount; i++)
	i = channel;
	{
		PRV_GetVoChnPlayStateInfo(i, &stPlayStateInfo);
		#if 1
		if(VochnInfo[channel].IsHaveVdec)
		{
			if(VochnInfo[i].SlaveId > PRV_MASTER)
			{
				MccDestroyVdecReq.VdecChn = i;
				SN_SendMccMessageEx(VochnInfo[channel].SlaveId, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_PBDESVDEC_REQ, &MccDestroyVdecReq, sizeof(MccDestroyVdecReq));					

			}
			else
			{
				if(stPlayStateInfo.CurPlayState == DEC_STATE_NORMALPAUSE)
				{
#if defined(Hi3535)
					HI_MPI_VO_ResumeChn(0, i);
#else
					HI_MPI_VO_ChnResume(HD, i);	
#endif
				}
				PlayBack_DestroyVdec(HD, i);
			}

		}
		#endif
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===============PlayBack_Stop==i: %d\n", i);
		stPlayStateInfo.SynState = SYN_OVER;
		stPlayStateInfo.CurPlayState = DEC_STATE_STOP;
		stPlayStateInfo.CurSpeedState = DEC_SPEED_NORMAL;		
		stPlayStateInfo.SendDataType = TYPE_NORMAL;	
		stPlayStateInfo.RealType = DEC_TYPE_REAL; 
		PRV_SetVoChnPlayStateInfo(i, &stPlayStateInfo);
	}

}


void   PlayBack_Pro_MccFullScreenRsp()
{

}
#if(IS_SUPPORT_PLAYBACKOSD == 1)
int DEC_Get_CurChanState(int chn)
{
	if(chn < 0 || chn >= DEV_CHANNEL_NUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_DEC, "DEC_Get_CurChanState chn:%d error\n",chn);
		return -1;
	}
    g_ChnPlayStateInfo stPlayStateInfo;
    PRV_GetVoChnPlayStateInfo(chn, &stPlayStateInfo);
    if(stPlayStateInfo.CurPlayState==DEC_STATE_EXIT||stPlayStateInfo.CurPlayState==DEC_STATE_STOP)
    {
        return -1;
    }
    return 0;
}
#endif

int DEC_Get_CurPlayTime(int VoChn,PRM_ID_TIME* prm_time)
{
	time_t curtime = 0;
	unsigned long long IntervalPts = 0,pu64ChnPts = 0; 
	VDEC_CHN_STAT_S pstStat;
	g_PlayInfo stPlayInfo;
	g_ChnPlayStateInfo stPlayStateInfo;
	time_t StartTime = 0, EndTime = 0;
	if(VoChn < 0 || VoChn >= DEV_CHANNEL_NUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_DEC, "DEC_Get_CurPlayTime chn:%d error\n",VoChn);
		return -1;
	}
	
	SN_MEMSET(&stPlayInfo,0,sizeof(g_PlayInfo));
	PRV_GetPlayInfo(&stPlayInfo);
	if(stPlayInfo.PlayBackState == PLAY_EXIT || stPlayInfo.PlayBackState == PLAY_STOP)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_DEC, "DEC_Get_CurPlayTime chn:%d PlayBackState:%d\n",VoChn,stPlayInfo.PlayBackState);
		return -1;
	}
	SN_MEMSET(&stPlayStateInfo,0,sizeof(g_ChnPlayStateInfo));
	PRV_GetVoChnPlayStateInfo(VoChn, &stPlayStateInfo);
	if(stPlayStateInfo.CurPlayState == DEC_STATE_EXIT||Ftpc_getChnIsPlay(VoChn)==0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_DEC, "DEC_Get_CurPlayTime chn:%d CurPlayState:%d\n",VoChn,stPlayStateInfo.CurPlayState);
		return -1;
	}
	SN_MEMSET(&pstStat,0,sizeof(VDEC_CHN_STAT_S));
	HI_MPI_VO_GetChnPts(0, VochnInfo[VoChn].VoChn, &pu64ChnPts);
	switch(stPlayStateInfo.CurSpeedState)
	{
		case DEC_SPEED_NORMALSTEP:
			IntervalPts = PtsInfo[VoChn].PreSendPts - pu64ChnPts;
			break;
   		case DEC_SPEED_NORMALSLOW8:
			IntervalPts= (PtsInfo[VoChn].PreSendPts - pu64ChnPts)/8;
			break;
   		case DEC_SPEED_NORMALSLOW4:
			IntervalPts = (PtsInfo[VoChn].PreSendPts - pu64ChnPts)/4;
			break;
	   	case DEC_SPEED_NORMALSLOW2:
			IntervalPts = (PtsInfo[VoChn].PreSendPts - pu64ChnPts)/2;
			break;
	   	case DEC_SPEED_NORMAL:
			IntervalPts = PtsInfo[VoChn].PreSendPts - pu64ChnPts;
			break;
	   	case DEC_SPEED_NORMALFAST2:
			IntervalPts = (PtsInfo[VoChn].PreSendPts - pu64ChnPts)*2;
			break;
	   	case DEC_SPEED_NORMALFAST4:
			IntervalPts = (PtsInfo[VoChn].PreSendPts - pu64ChnPts)*4;
			break;
	   	case DEC_SPEED_NORMALFAST8:
			IntervalPts = (PtsInfo[VoChn].PreSendPts - pu64ChnPts)*8;
			break;
		default:
			IntervalPts = PtsInfo[VoChn].IntervalPts;
			break;
	}
	
	
	//printf("chn:%d,PlayStateInfo.playchnmapping[chn]%d,IntervalPts:%llu,pu64ChnPts:%llu\n",chn,PlayStateInfo.playchnmapping[chn],IntervalPts,pu64ChnPts);
	if(IntervalPts % 1000000 >= 500000)
	{
		IntervalPts = (IntervalPts/1000000 + 1)*1000000;
	}
	
	Ftpc_PlayFileCurTime_Sec(VoChn, &StartTime, &EndTime);
	if(PtsInfo[VoChn].CurTime >= StartTime && PtsInfo[VoChn].CurTime <= EndTime)
	{
		if(stPlayStateInfo.CurPlayState == DEC_STATE_BACKPLAY || stPlayStateInfo.CurPlayState == DEC_STATE_BACKPAUSE)
		{
			curtime = PtsInfo[VoChn].CurTime + IntervalPts/1000000;
		}
		else
		{
			curtime =PtsInfo[VoChn].CurTime - IntervalPts/1000000;;
		}
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_DEC, "DEC_Get_CurPlayTime chn:%d CurTime error,CurTime:%u,StartPts:%u,EndPts:%u\n",VoChn,PtsInfo[VoChn].CurTime,StartTime,EndTime);
		return -1;
	}
	if(curtime > EndTime)
		curtime = EndTime;
	if(curtime < StartTime)
		curtime = StartTime;
	PlayBack_Sec_To_PrmTime(curtime,prm_time);
	
	TRACE(SCI_TRACE_NORMAL, MOD_VAM, "DEC_Get_CurPlayTime,ch:%d,PlayChnInfo[chn].IntervalPts:%llu,time:%d-%d-%d-%d-%d-%d\n", VoChn,PtsInfo[VoChn].IntervalPts,prm_time->Year ,prm_time->Month , prm_time->Day, prm_time->Hour, prm_time->Minute, prm_time->Second);
	return 0;
}

void* PlayBack_VdecThread()
{
	UINT8 i = 0, index = 0, PlayBackChnCount = 0, MaxStreamFrames = 0;
	HI_U64 pu64ChnPts = 0, IntervalPts = 0;
	HI_S32 s32Ret = 0, buffercount = 0, cidx = 0, TotalSize = 0, count = 0, LeftSteamFrames = 0;
	PlayBack_MccGetVdecVoInfoReq GetVdecVoInfo;
	//g_PRVPtsinfo stPlayPtsInfo;
	g_ChnPlayStateInfo stPlayStateInfo;
	g_PlayInfo stPlayInfo;
	VDEC_CHN_STAT_S pstStat;	
	
	Log_pid(__FUNCTION__);
	pthread_detach(pthread_self());
	while (1)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s line:%d sem_wait(&sem_VoPtsQuery)", __FUNCTION__, __LINE__);
		sem_wait(&sem_VoPtsQuery);
		PlayBackChnCount = 1;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "============Playback_VdecThread===============");
		while (PlayBackChnCount > 0)	/* 当前处于回放状态，则实时更新各个通道进度条时间 */
		{			
			PlayBackChnCount = 0;
			
			//pthread_mutex_lock(&send_data_mutex);
			PRV_GetPlayInfo(&stPlayInfo);	
			//printf("stPlayInfo.PlayBackState: %d\n", stPlayInfo.PlayBackState);
			if(stPlayInfo.PlayBackState == PLAY_EXIT || stPlayInfo.PlayBackState == PLAY_STOP)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "stPlayInfo.PlayBackState == PLAY_EXIT || stPlayInfo.PlayBackState == PLAY_STOP: %d", stPlayInfo.PlayBackState);
				//pthread_mutex_unlock(&send_data_mutex);
				usleep(300 * 1000);
				continue;
			}
			
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "stPlayInfo.IsPause: %d,stPlayInfo.PlayBackState: %d", stPlayInfo.IsPause, stPlayInfo.PlayBackState);
			if(stPlayInfo.IsPause && stPlayInfo.PlayBackState > PLAY_INSTANT)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "stPlayInfo.IsPause===sem_wait(&sem_PlayPause)");
				//pthread_mutex_unlock(&send_data_mutex);
				sem_wait(&sem_PlayPause);
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "After stPlayInfo.IsPause===sem_wait(&sem_PlayPause)");
			}
			else
			{
				//pthread_mutex_unlock(&send_data_mutex);
			}
			usleep(50 * 1000);
			for(i = 0; i < DEV_CHANNEL_NUM; i++)
			{	
				//预览下通道非及时回放状态，不需要计算进度条
				//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "i: %d===================stPlayInfo.PlayBackState: %d\n", i, stPlayInfo.PlayBackState);
				if(stPlayInfo.PlayBackState == PLAY_INSTANT && !VochnInfo[i].bIsPBStat)
				{					
					continue;
				}
				//按设备回放状态下，大于最大画面数的通道没有进度条
				if(stPlayInfo.PlayBackState == PLAY_PROCESS && i >= stPlayInfo.ImagCount)
				{					
					continue;
				}

				//PRV_GetVoChnPtsInfo(i, &stPlayPtsInfo);
				PRV_GetVoChnPlayStateInfo(i, &stPlayStateInfo);
				if(stPlayInfo.PlayBackState == PLAY_INSTANT && VochnInfo[i].bIsPBStat && stPlayStateInfo.CurPlayState == DEC_STATE_NORMALPAUSE)
				{
					SN_MEMSET(&pstStat, 0, sizeof(VDEC_CHN_STAT_S));
					if(HI_MPI_VDEC_Query(VochnInfo[i].VdecChn, &pstStat) != HI_SUCCESS)
						pstStat.u32LeftStreamFrames = 0;
					LeftSteamFrames = (HI_S32)pstStat.u32LeftStreamFrames;
					TRACE(SCI_TRACE_NORMAL, MOD_DEC, "i: %d===================stPlayStateInfo.CurPlayState== DEC_STATE_NORMALPAUSE, LeftSteamFrames=%d\n", i, LeftSteamFrames);				
					if(LeftSteamFrames > 5)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_DEC, "i: %d===", i);					
						usleep(500 * 1000);
						break;
					}
				}
				if (stPlayStateInfo.CurPlayState != DEC_STATE_STOP && stPlayStateInfo.CurPlayState != DEC_STATE_EXIT)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_DEC, "i: %d===================stPlayStateInfo.CurPlayState: %d\n", i, stPlayStateInfo.CurPlayState);					
					PlayBackChnCount++;
				}
				else					
				{					
					continue;
				}
				TRACE(SCI_TRACE_NORMAL, MOD_DEC, "i: %d===================VochnInfo[i].SlaveId: %d, VochnInfo[i].IsHaveVdec: %d\n", i, VochnInfo[i].SlaveId, VochnInfo[i].IsHaveVdec);
				if(PtsInfo[i].CurShowPts == 0)
					continue;
				index = Playback_GetSpeedCprToNormal(i);
				if (VochnInfo[i].SlaveId == PRV_MASTER) /* 如果基准通道在主片，直接调用接口获取时间戳 */
				{
					HI_MPI_VO_GetChnPts(0,	i, &PtsInfo[i].CurVoChnPts);  /* 获取基准通道的时间戳 */
					if(VochnInfo[i].IsHaveVdec)
					{	
						TRACE(SCI_TRACE_NORMAL, MOD_DEC, "============i: %d===================VochnInfo[i].SlaveId: %d\n", i, VochnInfo[i].SlaveId);
						SN_MEMSET(&pstStat, 0, sizeof(VDEC_CHN_STAT_S));
						if(HI_MPI_VDEC_Query(VochnInfo[i].VdecChn, &pstStat) != HI_SUCCESS)
							pstStat.u32LeftStreamFrames = 0;
						LeftSteamFrames = (HI_S32)pstStat.u32LeftStreamFrames;
						
						TRACE(SCI_TRACE_NORMAL, MOD_DEC, "===========VdecChn: %d, LeftSteamFrames: %d\n",
								VochnInfo[i].VdecChn, LeftSteamFrames);
						if(stPlayStateInfo.RealType == DEC_TYPE_NOREAL)
							MaxStreamFrames = 5;
						else
							MaxStreamFrames = 10;
						if(LeftSteamFrames >= MaxStreamFrames)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_DEC, "===========VdecChn: %d, pstStat.u32LeftStreamFrames: %d\n",
									VochnInfo[i].VdecChn, LeftSteamFrames);
							Ftpc_ChnStopWrite(i, 1);					
							VoChnState.bIsPBStat_StopWriteData[i] = 1;
							TRACE(SCI_TRACE_NORMAL, MOD_DEC, "line: %d============bIsPBStat_StopWriteData---StopWrite==1\n", __LINE__);
							if(LeftSteamFrames > 20)
							{
								TRACE(SCI_TRACE_NORMAL, MOD_DEC, "line: %d======LeftSteamFrames > 20---i: %d, LeftSteamFrames: %d\n", __LINE__, i, LeftSteamFrames);
								VoChnState.IsStopGetVideoData[i] = 1;
							}
						}
						else
						{	
							if(VoChnState.bIsPBStat_StopWriteData[i] == 1)
							{
								s32Ret = BufferState(i + PRV_VIDEOBUFFER, &buffercount, &TotalSize, &cidx);
								if(s32Ret == 0 && cidx <= PRV_GetBufferSize(i))
								{
									TRACE(SCI_TRACE_NORMAL, MOD_DEC, "===========u32LeftStreamFrames < 10=== VdecChn: %d, pstStat.u32LeftStreamFrames: %d\n",
											VochnInfo[i].VdecChn, LeftSteamFrames);
									Ftpc_ChnStopWrite(i, 0);					
									VoChnState.bIsPBStat_StopWriteData[i] = 0;
									TRACE(SCI_TRACE_NORMAL, MOD_DEC, "line: %d============bIsPBStat_StopWriteData---StopWrite==0\n", __LINE__);					
								}
							}
							TRACE(SCI_TRACE_NORMAL, MOD_DEC, "line: %d======LeftSteamFrames < 5---i: %d, LeftSteamFrames: %d\n", __LINE__, i, LeftSteamFrames);
							VoChnState.IsStopGetVideoData[i] = 0;
						}
					}
				}
				else			/* 发送消息给从片，获取相应输出通道的时间戳 */
				{
					GetVdecVoInfo.VoChn = i;
					SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_GETVDECVOINFO_REQ, &GetVdecVoInfo, sizeof(PlayBack_MccGetVdecVoInfoReq)); 
					pu64ChnPts = PtsInfo[i].CurVoChnPts; /* 从片消息RSP的时间戳保存在全局结构体中 */
				}
				//printf("===============stPlayPtsInfo.CurVoChnPts: %lld, stPlayPtsInfo.PreVoChnPts: %lld, stPlayPtsInfo.CurSendPts: %lld\n",  stPlayPtsInfo.CurVoChnPts, stPlayPtsInfo.PreVoChnPts, stPlayPtsInfo.CurSendPts);
				if ((stPlayStateInfo.CurPlayState == DEC_STATE_NORMALPAUSE)  /* 如果当前状态处于暂停状态，或者获取的时间戳小于等于之前获取的时间戳，则不更新进度条时间 */
					|| (stPlayStateInfo.CurPlayState == DEC_STATE_BACKPAUSE)
					|| (PtsInfo[i].CurVoChnPts > PtsInfo[i].CurSendPts)
					|| (PtsInfo[i].CurVoChnPts <= PtsInfo[i].PreVoChnPts))
				{					
					continue;
				}
				IntervalPts = PtsInfo[i].IntervalPts;
				if (IntervalPts <= 0)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_DEC, "IntervalPts illegal--%lld", IntervalPts);
					continue;
				}
				//pthread_mutex_lock(&send_data_mutex);
				
				if (stPlayStateInfo.SendDataType == TYPE_PREIFRAME)
				{
					if(stPlayStateInfo.CurSpeedState < DEC_SPEED_NORMAL)
					{
						PtsInfo[i].CurShowPts -= (PtsInfo[i].CurVoChnPts - PtsInfo[i].PreVoChnPts) / index; 
					}
					else
					{
						PtsInfo[i].CurShowPts -= (PtsInfo[i].CurVoChnPts - PtsInfo[i].PreVoChnPts)*index; 
					}
				}
				else
				{
					if(stPlayStateInfo.CurSpeedState < DEC_SPEED_NORMAL)
					{
						PtsInfo[i].CurShowPts += (PtsInfo[i].CurVoChnPts - PtsInfo[i].PreVoChnPts) / index; 
					}
					else
					{
						PtsInfo[i].CurShowPts +=  (PtsInfo[i].CurVoChnPts - PtsInfo[i].PreVoChnPts) * index;
					}
				}
				if (IntervalPts > 0)
				{
					count += (PtsInfo[i].CurVoChnPts - PtsInfo[i].PreVoChnPts)/IntervalPts;
				}
				TRACE(SCI_TRACE_NORMAL, MOD_DEC, "i: %d, count=%d, PlayPtsInfo->CurShowPts = %lld, PreVoChnPts=%lld", i, count, PtsInfo[i].CurShowPts, PtsInfo[i].PreVoChnPts);
				PtsInfo[i].PreVoChnPts = PtsInfo[i].CurVoChnPts;
				//PRV_SetVoChnPtsInfo(i, &stPlayPtsInfo);
				//pthread_mutex_unlock(&send_data_mutex);
								
			}
			//pthread_mutex_lock(&send_data_mutex);
			PRV_GetPlayInfo(&stPlayInfo);
			if( stPlayInfo.PlayBackState == PLAY_PROCESS && PlayBackChnCount == 0)
			{
				stPlayInfo.PlayBackState = PLAY_STOP;
				PRV_SetPlayInfo(&stPlayInfo);	
				//pthread_mutex_unlock(&send_data_mutex);
				break;				
			}
			if(stPlayInfo.PlayBackState == PLAY_EXIT && PlayBackChnCount == 0)
			{
				//pthread_mutex_unlock(&send_data_mutex);
				break;				
			}
			PRV_SetPlayInfo(&stPlayInfo);
			//pthread_mutex_unlock(&send_data_mutex);
		}/* VideoEnableDec为0时表示退出回放，应销毁通道 */
		TRACE(SCI_TRACE_NORMAL, MOD_DEC, "out VideoEnableDec");
	}
}

