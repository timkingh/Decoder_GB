//
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
#include "h264_stream.h"

#define PATH_STOP "/tmp/stop"
#define SIGN_MAX 	16

int MasterToSlaveChnId = 0;//主从片传输数据通道
g_PRVVoChnInfo VochnInfo[DEV_CHANNEL_NUM];//视频输出通道的通道信息
g_PRVSlaveState SlaveState;
g_PRVPtsinfo PtsInfo[MAX_IPC_CHNNUM];//每一个数字通道视频时间戳信息
g_PRVVoChnState VoChnState;
g_ChnPlayStateInfo  PlayStateInfo[PRV_VO_CHN_NUM];
g_PlayInfo		    PlayInfo;
g_PRVSendChnInfo    SendChnInfo;

AUDIO_DATABUF Prv_Audiobuf; 

int g_PrvType = Flow_Type;
int CurSlaveCap = 0;
int CurMasterCap = 0;	//主片当前已使用的性能
int CurSlaveChnCount = 0;
int CurCap = 0;//当前已使用的性能CurSlaveCap + CurMasterCap

//void* PRV_PstDataAddr[MAX_IPC_CHNNUM] = {NULL};//保存IPC视频数据地址
//void* PRV_PstAudioDataAddr = NULL;//保存音频数据地址
int CurIPCCount = 0;

int Achn = -1;//音频输出对应的通道下标
HI_U32 PtNumPerFrm = 160;//音频每帧采样点个数，值改变时需要重新创建音频解码通道
int IsAdecBindAo = 0;//Ao绑定Adec标识1:绑定，0:不绑定
int IsStopGetAudio = 0;
int IsCreatingAdec = 0;
int IsCreateAdec = 0;
int IsAudioOpen = 0;//音频开启/关闭标识  1:开启，0:关闭

int CurCheckChn = 0;//小于MAX_IPC_CHNNUM时，代表当前查询视频buffer；否则代表当前查询音频buffer
pthread_mutex_t send_data_mutex = PTHREAD_MUTEX_INITIALIZER;//用于销毁解码通道过程中，发送数据的互斥锁

AVPacket *PRV_OldVideoData[DEV_CHANNEL_NUM] = {NULL};
VDEC_CHN PRV_OldVdec[DEV_CHANNEL_NUM] = {0};
int PRV_SendDataLen = 0;
int PRV_CurIndex = 0;
int IsUpGrade = 0;
enum PreviewMode_enum g_stPreviewMode = SingleScene;

AVPacket *tmp_PRVGetVideoData[MAX_IPC_CHNNUM] = {NULL};//用于保存从片创建解码器过程中不能发送的I帧数据
AVPacket *Pb_FirstGetVideoData[MAX_IPC_CHNNUM] = {NULL};//回放下数据切换获取的第一帧数据

static h264_stream_t h_stream[MAX_IPC_CHNNUM];
static int g_beginsign = 0;								//表示trace开关是否获取录像文件
static int g_filesign[DEV_CHANNEL_NUM][SIGN_MAX] = {{-1}};		//用于表示是否获取文件的标志，[0]:表示获取刚获取到的预览数据，[1]:表示获取送到解码器前的数据，[2]:ftpc接收到的数据

extern	UINT8	PRV_CurDecodeMode;
extern int FWK_SetOverflowState (int chn, int state);


static void rtp_stream_testC(AVPacket * av_packet)
{
	AVPacket *pTmpPacket = NULL;
	AVPacket *NextPacket = NULL;
	int wlen=0;
	int src_file;
	char start_code[4] = {0x00, 0x00, 0x00, 0x01};

	pTmpPacket = av_packet;
	NextPacket = pTmpPacket->next;
	while(pTmpPacket != NULL)
	{
		//printf("\n%s Line %d:TIMKINGH is DEBUGGING!!! ====>DataSize:%d,BufOffset:%d,rtpOffset:%d\n\n",
				//__func__,__LINE__,pTmpPacket->DataSize,pTmpPacket->BufOffset,pTmpPacket->rtpOffset);
		int length = pTmpPacket->DataSize - pTmpPacket->BufOffset;		

		src_file = open("/var/tmp/stream_data_from_client.h264",O_RDWR | O_CREAT | O_APPEND);
		if(src_file > 0)
		{
			if(pTmpPacket->frame_type == 0 || pTmpPacket->frame_type == 1)
			{
				wlen = write(src_file,start_code,4);
				//printf("Line %d:add start code!!!\n",__LINE__);
			}
			wlen = write(src_file,(char*)pTmpPacket + pTmpPacket->BufOffset,length);
			//printf("%s Line %d ----> wlen:%d,pTmpPacket:%p,nalu_type:%d,frame_type:%d\n",__func__,__LINE__,
											//wlen,pTmpPacket,pTmpPacket->naluType,pTmpPacket->frame_type);
		}
		if(pTmpPacket->Extendnext == NULL)
		{
			pTmpPacket = (AVPacket *)NextPacket;
			if(pTmpPacket != NULL)
				NextPacket = pTmpPacket->next;
		}
		else
		{
			pTmpPacket = (AVPacket *)(pTmpPacket->Extendnext);
		}				
	}
	close(src_file);
}


static void rtp_stream_testD(AVPacket * av_packet)
{
	AVPacket *pTmpPacket = NULL;
	AVPacket *NextPacket = NULL;
	int wlen=0;
	int src_file;
	char start_code[4] = {0x00, 0x00, 0x00, 0x01};

	pTmpPacket = av_packet;
	NextPacket = pTmpPacket->next;
	while(pTmpPacket != NULL)
	{
		//printf("\n%s Line %d:TIMKINGH is DEBUGGING!!! ====>DataSize:%d,BufOffset:%d,rtpOffset:%d\n\n",
				//__func__,__LINE__,pTmpPacket->DataSize,pTmpPacket->BufOffset,pTmpPacket->rtpOffset);
		int length = pTmpPacket->DataSize - pTmpPacket->BufOffset;		

		src_file = open("/var/tmp/data_sent_to_decoder.h264",O_RDWR | O_CREAT | O_APPEND);
		if(src_file > 0)
		{
			if(pTmpPacket->frame_type == 0 || pTmpPacket->frame_type == 1)
			{
				wlen = write(src_file,start_code,4);
				//printf("Line %d:add start code!!!\n",__LINE__);
			}
			wlen = write(src_file,(char*)pTmpPacket + pTmpPacket->BufOffset,length);
			//printf("%s Line %d ----> wlen:%d,pTmpPacket:%p,nalu_type:%d,frame_type:%d\n",__func__,__LINE__,
											//wlen,pTmpPacket,pTmpPacket->naluType,pTmpPacket->frame_type);
		}
		if(pTmpPacket->Extendnext == NULL)
		{
			pTmpPacket = (AVPacket *)NextPacket;
			if(pTmpPacket != NULL)
				NextPacket = pTmpPacket->next;
		}
		else
		{
			pTmpPacket = (AVPacket *)(pTmpPacket->Extendnext);
		}				
	}
	close(src_file);
}


int PRV_SetBeginSign(int value)
{
	g_beginsign = value;

	return OK;
}

int PRV_GetFileSign(int ch, int pos)
{
	int sign = -1;
	
	if(ch < 0 || ch > DEV_CHANNEL_NUM || pos < 0 || pos > SIGN_MAX)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "param error, ch=%d, pos=%d!!", ch, pos);
		return sign;
	}

	sign = g_filesign[ch][pos];
	
	return sign;
}


int PRV_GetCurCap()
{
	return CurCap;
}
/********************************************************
函数名:PRV_WriteBuffer
功     能:向指定通道的预览缓冲区中写数据
参     数:[in]ch   指定通道号
		    [in]dataType 数据类型，音频数据或视频数据
		   [in]DataFromRTSP   指定数据

返回值:  0成功
		    -1失败
*********************************************************/
HI_S32 PRV_WriteBuffer(HI_S32 chn, int dataType, AVPacket *DataFromRTSP)
{
	HI_S32 s32Ret = 0, prvBufferChn = 0;
	//int offSet = sizeof(int) * 3 * DataFromRTSP->nal_units;
	if(DataFromRTSP == NULL)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "---------Invalid Pointer, NULL Pointer!!");
		return HI_FAILURE;
	}
	if(dataType == CODEC_ID_H264)
	{
		if(VochnInfo[chn].bIsPBStat)
		{
			if(!VoChnState.FirstHaveVideoData[chn] && DataFromRTSP->frame_type == 1)
			{
				BufferSet(chn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);			
				s32Ret = SetNode_Ex(chn + PRV_VIDEOBUFFER, PREVIEWARRAY);
				if(s32Ret != 0)
					TRACE(SCI_TRACE_HIGH, MOD_PRV, "--------------Set Node fail!!\n");
				VoChnState.FirstHaveVideoData[chn] = 1;
				VoChnState.VideoDataTimeLag[chn] = 0;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "DataFromRTSP->codec_id == CODEC_ID_H264-FirstVideoPts=%llu--DataFromRTSP->pts: %llu\n", PtsInfo[chn].FirstVideoPts, DataFromRTSP->pts);
				PtsInfo[chn].FirstVideoPts = DataFromRTSP->pts;
			}
			 				
		}
		else
		{
			if(!VoChnState.FirstHaveVideoData[chn])
			{
				BufferSet(chn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);			
				s32Ret = SetNode_Ex(chn + PRV_VIDEOBUFFER, PREVIEWARRAY);
				if(s32Ret != 0)
					TRACE(SCI_TRACE_HIGH, MOD_PRV, "--------------Set Node fail!!\n");
				VoChnState.FirstHaveVideoData[chn] = 1;
			}
		}
		
		if(!VochnInfo[chn + LOCALVEDIONUM].IsHaveVdec)//此时数据是不发解码通道。开始发送解码通道时才累加，确保获取的Vo的时间戳一致
		{
			PtsInfo[chn].CurSendPts  = 0;
			PtsInfo[chn].CurVoChnPts = 0;
			//PtsInfo[chn].PreGetVideoPts = 0;
		}
		VoChnState.VideoDataCount[chn]++;
			
		prvBufferChn = chn + PRV_VIDEOBUFFER;
		DataFromRTSP->prvpts = DataFromRTSP->pts * 1000000/90000;		
		//if(VochnInfo[chn].bIsPBStat)
			//printf("============PRV_WriteBuffer===DataFromRTSP->prvpts: %lld\n", DataFromRTSP->prvpts);
			
		s32Ret = BufferWrite(prvBufferChn, (char*)DataFromRTSP, DataFromRTSP->data_len + sizeof(AVPacket));
		if(0 != s32Ret)
		{				
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "------------BufferWrite failure---chn = %d, pts = %lld", chn, DataFromRTSP->pts);
			VochnInfo[chn + LOCALVEDIONUM].bIsWaitIFrame = 1;
			NTRANS_FreeMediaData(DataFromRTSP);
			return s32Ret;
		}

	}
	else if((dataType == CODEC_ID_PCMA || dataType == CODEC_ID_PCMU))
	{	
		if(VochnInfo[chn].bIsPBStat)
		{
			if(!VoChnState.FirstHaveAudioData[chn]  && VoChnState.FirstHaveVideoData[chn])
			{
				BufferSet(chn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);			
				s32Ret = SetNode_Ex(chn + PRV_AUDIOBUFFER, PREVIEWARRAY);
				
				PtsInfo[chn].PreGetAudioPts = 0;
				if(s32Ret != 0)
					TRACE(SCI_TRACE_HIGH, MOD_PRV, "--------------Set Node fail!!\n");
				VoChnState.FirstHaveAudioData[chn] = 1;
				VoChnState.AudioDataTimeLag[chn] = 0;
				
				if(PlayInfo.PlayBackState == PLAY_INSTANT)
				{
					PRV_StopAdec();
					VochnInfo[chn].AudioInfo.PtNumPerFrm = PtNumPerFrm;
					PRV_StartAdecAo(VochnInfo[chn]);	
					IsCreateAdec = 1;
					HI_MPI_ADEC_ClearChnBuf(DecAdec);
				}
				
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "DataFromRTSP->codec_id == CODEC_ID_PCMA-FirstAudioPts=%llu--DataFromRTSP->pts: %llu\n", PtsInfo[chn].FirstAudioPts, DataFromRTSP->pts);
				PtsInfo[chn].FirstAudioPts = DataFromRTSP->pts; 				
				
			}
			else if(!VoChnState.FirstHaveAudioData[chn])
			{
				NTRANS_FreeMediaData(DataFromRTSP);
				return HI_SUCCESS;
			}
			
		}
		else
		{
			if(!VoChnState.FirstHaveAudioData[chn])
			{
				BufferSet(chn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);			
				s32Ret = SetNode_Ex(chn + PRV_AUDIOBUFFER, PREVIEWARRAY);
				
				PtsInfo[chn].PreGetAudioPts = 0;
				if(s32Ret != 0)
					TRACE(SCI_TRACE_HIGH, MOD_PRV, "--------------Set Node fail!!\n");
				VoChnState.FirstHaveAudioData[chn] = 1;
			}
		}

		VoChnState.AudioDataCount[chn]++;

		if(PlayInfo.PlayBackState <= PLAY_INSTANT && Achn != chn)
		{
			NTRANS_FreeMediaData(DataFromRTSP);
			return HI_SUCCESS;
		}
		
		prvBufferChn = chn + PRV_AUDIOBUFFER;
		s32Ret = BufferWrite(prvBufferChn, (char*)DataFromRTSP, DataFromRTSP->data_len + sizeof(AVPacket));
		if(0 != s32Ret)
		{			
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "------------BufferWrite failure---chn = %d, pts = %lld", chn, DataFromRTSP->pts);
			NTRANS_FreeMediaData(DataFromRTSP);
			return s32Ret;
		}
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "------------Invalid DataType: %d", dataType);
		NTRANS_FreeMediaData(DataFromRTSP);
		return HI_FAILURE;
	}
		
	return HI_SUCCESS;
}
/********************************************************
函数名:PRV_CreateVdecChn
功     能:创建解码通道
参     数:[in]EncType  指定解码类型，H264或JPEG
		   [in]VdecChn    创建的解码通道号
返回值:  0成功
		    -1失败

*********************************************************/
HI_S32 PRV_CreateVdecChn(HI_S32 EncType, HI_S32 height, HI_S32 width, HI_U32 u32RefFrameNum, VDEC_CHN VdecChn)
{
	HI_S32 s32Ret = 0;
	VDEC_CHN_ATTR_S stAttr;
	HI_U32 refframenum = 0;

	if(u32RefFrameNum < 1 || u32RefFrameNum > 4)
	{
		if((height*width) > MAX_8D1)
		{
			refframenum = 2;
		}
		else
		{
			refframenum = RefFrameNum;
		}
		
	}
	else
	{
		refframenum = u32RefFrameNum;
	}
	
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s line:%d VdecChn=%d, refframenum=%u", __FUNCTION__, __LINE__, VdecChn, refframenum);
	
#if defined(SN9234H1)	
	VDEC_ATTR_H264_S stH264Attr;
	VDEC_ATTR_JPEG_S stJpegAttr;
	
	stH264Attr.u32Priority = 0;	
	stJpegAttr.u32Priority = 0;
	
	stH264Attr.u32PicHeight = height;
	stH264Attr.u32PicWidth = width;
	stJpegAttr.u32PicHeight = height;
	stJpegAttr.u32PicWidth = width;
	
	stH264Attr.u32RefFrameNum = refframenum;
	stH264Attr.enMode = H264D_MODE_FRAME;

	stAttr.u32BufSize = (height * width * 3 / 2);
	stAttr.enType = PT_H264;
	stAttr.pValue = (void*)&stH264Attr;
	if(H264ENC == EncType)
	{		
		stAttr.enType = PT_H264;
		stAttr.pValue = (void*)&stH264Attr;
	}
	else if(JPEGENC == EncType)
	{
		stAttr.enType = PT_JPEG;
		stAttr.pValue = (void*)&stJpegAttr;
	}

	s32Ret = HI_MPI_VDEC_CreateChn(VdecChn, &stAttr, NULL);	/* 创建视频解码通道 */

#else
    VDEC_PRTCL_PARAM_S stPrtclParam;

	 switch (EncType)
    {
        case H264ENC:
			stAttr.stVdecVideoAttr.u32RefFrameNum = refframenum;
		    stAttr.stVdecVideoAttr.enMode = VIDEO_MODE_FRAME;
		    stAttr.stVdecVideoAttr.s32SupportBFrame = 0;
			stAttr.enType = PT_H264;
            break;
        case JPEGENC:
            stAttr.stVdecJpegAttr.enMode = VIDEO_MODE_FRAME;
  //          stAttr.stVdecJpegAttr.u32Tmp = 0;
			stAttr.enType = PT_JPEG;
            break;
       
        default:
            TRACE(SCI_TRACE_NORMAL, MOD_PRV,"err type \n");
            return HI_FAILURE;
    }
	
    stAttr.u32Priority = 250;//此处必须大于0
    stAttr.u32PicWidth = width;
    stAttr.u32PicHeight = height;

	stAttr.u32BufSize = (stAttr.u32PicWidth * stAttr.u32PicHeight * 3/2);
	s32Ret = HI_MPI_VDEC_CreateChn(VdecChn, &stAttr);	// 创建视频解码通道 
#endif	
	if(s32Ret != HI_SUCCESS && s32Ret != HI_ERR_VDEC_EXIST)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "HI_MPI_VDEC_CreateChn fail 0x%x  width=%d height=%d\n", s32Ret, width, height);		
		return s32Ret;
	}

	if(s32Ret == HI_ERR_VDEC_EXIST)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Vdec: %d Allready Create, Destroy Vdec First\n", VdecChn);
		CHECK(HI_MPI_VDEC_StopRecvStream(VdecChn));//销毁通道前先停止接收数据		
		CHECK(HI_MPI_VDEC_ResetChn(VdecChn)); //解码器复位 
		CHECK(HI_MPI_VDEC_DestroyChn(VdecChn)); // 销毁视频通道 	
#if defined(SN9234H1)
		CHECK(HI_MPI_VDEC_CreateChn(VdecChn, &stAttr, NULL)); /* 创建视频解码通道 */
#else		
		CHECK(HI_MPI_VDEC_CreateChn(VdecChn, &stAttr)); // 创建视频解码通道
#endif		
	}
	
#if defined(Hi3531)

	s32Ret = HI_MPI_VDEC_GetPrtclParam(VdecChn, &stPrtclParam);
    if (HI_SUCCESS != s32Ret)
    {
       	TRACE(SCI_TRACE_NORMAL, MOD_PRV,"HI_MPI_VDEC_GetPrtclParam failed errno 0x%x \n", s32Ret);
        return s32Ret;
    }

  //  stPrtclParam.s32MaxSpsNum = 21;
  //  stPrtclParam.s32MaxPpsNum = 22;
  //  stPrtclParam.s32MaxSliceNum = 100;
//	stPrtclParam.s32DisplayFrameNum = 2;
    s32Ret = HI_MPI_VDEC_SetPrtclParam(VdecChn, &stPrtclParam);
    if (HI_SUCCESS != s32Ret)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"HI_MPI_VDEC_SetPrtclParam failed errno 0x%x \n", s32Ret);
        return s32Ret;
    }
#elif defined(Hi3535)
	s32Ret = HI_MPI_VDEC_GetProtocolParam(VdecChn, &stPrtclParam);
    if (HI_SUCCESS != s32Ret)
    {
       	TRACE(SCI_TRACE_NORMAL, MOD_PRV,"HI_MPI_VDEC_GetPrtclParam failed errno 0x%x \n", s32Ret);
        return s32Ret;
    }

  //  stPrtclParam.s32MaxSpsNum = 21;
  //  stPrtclParam.s32MaxPpsNum = 22;
  //  stPrtclParam.s32MaxSliceNum = 100;
//	stPrtclParam.s32DisplayFrameNum = 2;
    s32Ret = HI_MPI_VDEC_SetProtocolParam(VdecChn, &stPrtclParam);
    if (HI_SUCCESS != s32Ret)
    {
        TRACE(SCI_TRACE_NORMAL, MOD_PRV,"HI_MPI_VDEC_SetPrtclParam failed errno 0x%x \n", s32Ret);
        return s32Ret;
    }
	HI_MPI_VDEC_SetDisplayMode(VdecChn, VIDEO_DISPLAY_MODE_PLAYBACK);
#endif	
	CHECK_RET(HI_MPI_VDEC_StartRecvStream(VdecChn));

	return HI_SUCCESS;

}

void PRV_GetPlayInfo(g_PlayInfo *pPlayInfo)
{
	pPlayInfo->PlayBackState = PlayInfo.PlayBackState;
	pPlayInfo->IsSingle = PlayInfo.IsSingle;
   	pPlayInfo->DBClickChn = PlayInfo.DBClickChn;
   	pPlayInfo->FullScreenId = PlayInfo.FullScreenId;
    pPlayInfo->ImagCount = PlayInfo.ImagCount;
    pPlayInfo->IsPlaySound = PlayInfo.IsPlaySound;
    pPlayInfo->IsPause = PlayInfo.IsPause;
	pPlayInfo->InstantPbChn = PlayInfo.InstantPbChn;
	pPlayInfo->SubWidth = PlayInfo.SubWidth;
	pPlayInfo->SubHeight = PlayInfo.SubHeight; 
	pPlayInfo->bISDB=PlayInfo.bISDB;
	pPlayInfo->ZoomChn=PlayInfo.ZoomChn;
	pPlayInfo->IsZoom=PlayInfo.IsZoom;
}

void PRV_SetPlayInfo(g_PlayInfo *pPlayInfo)
{
	PlayInfo.PlayBackState = pPlayInfo->PlayBackState;
	PlayInfo.IsSingle = pPlayInfo->IsSingle;
   	PlayInfo.DBClickChn = pPlayInfo->DBClickChn;
   	PlayInfo.FullScreenId = pPlayInfo->FullScreenId;
    PlayInfo.ImagCount = pPlayInfo->ImagCount;
    PlayInfo.IsPlaySound = pPlayInfo->IsPlaySound;
    PlayInfo.IsPause = pPlayInfo->IsPause;
	PlayInfo.InstantPbChn = pPlayInfo->InstantPbChn;
	PlayInfo.SubWidth = pPlayInfo->SubWidth;
	PlayInfo.SubHeight = pPlayInfo->SubHeight; 
	PlayInfo.bISDB=pPlayInfo->bISDB;
	PlayInfo.ZoomChn=pPlayInfo->ZoomChn;
	PlayInfo.IsZoom=pPlayInfo->IsZoom;

}


void PRV_GetVoChnPtsInfo(HI_S32 VoChn, g_PRVPtsinfo *pPtsInfo)
{
	pPtsInfo->PreGetVideoPts = PtsInfo[VoChn].PreGetVideoPts;
	pPtsInfo->CurGetVideoPts = PtsInfo[VoChn].CurGetVideoPts;	
	pPtsInfo->PreVideoPts = PtsInfo[VoChn].PreVideoPts;
	pPtsInfo->CurVideoPts = PtsInfo[VoChn].CurVideoPts;	
	pPtsInfo->PreSendPts = PtsInfo[VoChn].PreSendPts;
	pPtsInfo->CurSendPts = PtsInfo[VoChn].CurSendPts;
	pPtsInfo->CurVoChnPts = PtsInfo[VoChn].CurVoChnPts;
	pPtsInfo->PreVoChnPts = PtsInfo[VoChn].PreVoChnPts;	
	pPtsInfo->DevicePts = PtsInfo[VoChn].DevicePts;
	pPtsInfo->DeviceIntervalPts = PtsInfo[VoChn].DeviceIntervalPts;
	pPtsInfo->IntervalPts = PtsInfo[VoChn].IntervalPts;
	pPtsInfo->ChangeIntervalPts = PtsInfo[VoChn].ChangeIntervalPts;
	pPtsInfo->FirstVideoPts = PtsInfo[VoChn].FirstVideoPts;
	pPtsInfo->FirstAudioPts = PtsInfo[VoChn].FirstAudioPts;
	pPtsInfo->PreGetAudioPts = PtsInfo[VoChn].PreGetAudioPts;
	pPtsInfo->BaseVideoPts = PtsInfo[VoChn].BaseVideoPts;
	pPtsInfo->IFrameOffectPts = PtsInfo[VoChn].IFrameOffectPts;
	pPtsInfo->pFrameOffectPts = PtsInfo[VoChn].pFrameOffectPts;
	pPtsInfo->CurShowPts = PtsInfo[VoChn].CurShowPts;            
	pPtsInfo->StartPts = PtsInfo[VoChn].StartPts; 
	pPtsInfo->QueryStartTime = PtsInfo[VoChn].QueryStartTime;
	pPtsInfo->QueryFinalTime = PtsInfo[VoChn].QueryFinalTime;
	pPtsInfo->StartPrm = PtsInfo[VoChn].StartPrm;
	pPtsInfo->EndPrm = PtsInfo[VoChn].EndPrm;
}

void PRV_SetVoChnPtsInfo(HI_S32 VoChn, g_PRVPtsinfo *pPtsInfo)
{
	PtsInfo[VoChn].PreGetVideoPts = pPtsInfo->PreGetVideoPts;
	PtsInfo[VoChn].CurGetVideoPts = pPtsInfo->CurGetVideoPts;	
	PtsInfo[VoChn].PreVideoPts = pPtsInfo->PreVideoPts;
	PtsInfo[VoChn].CurVideoPts = pPtsInfo->CurVideoPts;	
	PtsInfo[VoChn].PreSendPts = pPtsInfo->PreSendPts;
	PtsInfo[VoChn].CurSendPts = pPtsInfo->CurSendPts;
	PtsInfo[VoChn].CurVoChnPts = pPtsInfo->CurVoChnPts;
	PtsInfo[VoChn].PreVoChnPts = pPtsInfo->PreVoChnPts;	
	PtsInfo[VoChn].DevicePts = pPtsInfo->DevicePts;
	PtsInfo[VoChn].DeviceIntervalPts = pPtsInfo->DeviceIntervalPts;
	PtsInfo[VoChn].IntervalPts = pPtsInfo->IntervalPts;
	PtsInfo[VoChn].ChangeIntervalPts = pPtsInfo->ChangeIntervalPts;
	PtsInfo[VoChn].FirstVideoPts = pPtsInfo->FirstVideoPts;
	PtsInfo[VoChn].FirstAudioPts = pPtsInfo->FirstAudioPts;
	PtsInfo[VoChn].PreGetAudioPts = pPtsInfo->PreGetAudioPts;
	PtsInfo[VoChn].BaseVideoPts = pPtsInfo->BaseVideoPts;
	PtsInfo[VoChn].IFrameOffectPts = pPtsInfo->IFrameOffectPts;
	PtsInfo[VoChn].pFrameOffectPts = pPtsInfo->pFrameOffectPts;
	PtsInfo[VoChn].CurShowPts = pPtsInfo->CurShowPts;            
	PtsInfo[VoChn].StartPts = pPtsInfo->StartPts;  
	PtsInfo[VoChn].QueryStartTime = pPtsInfo->QueryStartTime;
	PtsInfo[VoChn].QueryFinalTime = pPtsInfo->QueryFinalTime;
	PtsInfo[VoChn].StartPrm = pPtsInfo->StartPrm;
	PtsInfo[VoChn].EndPrm = pPtsInfo->EndPrm;

}

void PRV_GetVoChnPlayStateInfo(HI_S32 VoChn, g_ChnPlayStateInfo *pPlayStateInfo)
{
	pPlayStateInfo->CurPlayState = PlayStateInfo[VoChn].CurPlayState;
	pPlayStateInfo->CurSpeedState = PlayStateInfo[VoChn].CurSpeedState;
	pPlayStateInfo->QuerySlaveId = PlayStateInfo[VoChn].QuerySlaveId;
	pPlayStateInfo->SendDataType = PlayStateInfo[VoChn].SendDataType;
	pPlayStateInfo->RealType = PlayStateInfo[VoChn].RealType;
	pPlayStateInfo->SynState = PlayStateInfo[VoChn].SynState;

}

void PRV_SetVoChnPlayStateInfo(HI_S32 VoChn, g_ChnPlayStateInfo *pPlayStateInfo)
{
	PlayStateInfo[VoChn].CurPlayState = pPlayStateInfo->CurPlayState;
	PlayStateInfo[VoChn].CurSpeedState = pPlayStateInfo->CurSpeedState;
	PlayStateInfo[VoChn].QuerySlaveId = pPlayStateInfo->QuerySlaveId;
	PlayStateInfo[VoChn].SendDataType = pPlayStateInfo->SendDataType;
	PlayStateInfo[VoChn].RealType = pPlayStateInfo->RealType;
	PlayStateInfo[VoChn].SynState = pPlayStateInfo->SynState;

}

void PRV_SetChnPlayPts(HI_S32 VoChn, AVPacket *PRV_DataFromRTSP)
{
	HI_S32 s32Ret = 0;
	PtsInfo[VoChn].CurVideoPts = PRV_DataFromRTSP->pts;
	
	if(VochnInfo[VoChn].bIsPBStat)
	{
		if(PlayInfo.PlayBackState >= PLAY_INSTANT)
		{
			s32Ret = PlayBack_CheckIntervalPtsEx(VoChn);
		}
		PtsInfo[VoChn].CurSendPts = PtsInfo[VoChn].PreSendPts + PtsInfo[VoChn].IntervalPts;
		PRV_DataFromRTSP->prvpts = PtsInfo[VoChn].CurSendPts;
		PtsInfo[VoChn].CurTime = (time_t)PRV_DataFromRTSP->vampts;
		if(PlayInfo.PlayBackState != PLAY_INSTANT && s32Ret == 1)
			PtsInfo[VoChn].ChangeIntervalPts = PtsInfo[VoChn].CurSendPts;
		//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "===========PtsInfo[chn].CurSendPts: %lld, PtsInfo[chn].PreSendPts: %lld\n", PtsInfo[VoChn].CurSendPts, PtsInfo[VoChn].PreSendPts);
		PtsInfo[VoChn].PreSendPts = PtsInfo[VoChn].CurSendPts;
	}
	
	PtsInfo[VoChn].PreVideoPts = PtsInfo[VoChn].CurVideoPts;
}

unsigned char VideoDataStream[MAXFRAMELEN] = {0};
HI_S32 PRV_SendData(HI_S32 chn, AVPacket *PRV_DataFromRTSP, HI_S32 datatype, HI_S32 s32StreamChnIDs, HI_S32 PRV_State)
{
	if(NULL == PRV_DataFromRTSP)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_DataFromRTSP: NULL Point!!!\n");
		return HI_FAILURE;
	}

	#if 0
	rtp_stream_testD(PRV_DataFromRTSP);
	printf("%s Line %d -----> chn:%d\n",__func__,__LINE__,chn);
	#endif
	
	unsigned char AudioData[MAXAUDIODATALEN] = {0};
	
	char VideoDataStarCode[] = {0x00, 0x00, 0x00, 0x01};
	char AudioDataStarCode[4];
	((char *)AudioDataStarCode)[0] = 0;
	((char *)AudioDataStarCode)[1] = 1;
	((char *)AudioDataStarCode)[2] = PtNumPerFrm/2;;
	((char *)AudioDataStarCode)[3] = 0;
	AVPacket *NextPacket = NULL;
	AVPacket *pTmp = NULL;
	HI_S32 s32Ret = 0, TotalLength = 0, datalength=0, length = 0, bIsHaveAudio = 0;
	HI_S32 VdecChn = 0, AdChn = 0, SlaveId = 0, index = 0, tmpChn = 0;
	AUDIO_STREAM_S	stAStream;
	VDEC_STREAM_S stVstream;
	int Aoffset = 0;
	unsigned char *p = NULL;
	int nal_start, nal_end, len, ret;	
	
	index = chn + LOCALVEDIONUM;
	VdecChn = VochnInfo[index].VdecChn;
	SlaveId = VochnInfo[index].SlaveId;

	if(PlayInfo.PlayBackState <= PLAY_INSTANT)
		AdChn = DecAdec;
	else
		AdChn = ADECHN;

	if(CODEC_ID_PCMA == datatype || CODEC_ID_PCMU == datatype)
	{
		bIsHaveAudio = detect_audio_input_num();  // 检测是否有音频输出，没有则不送音频 
		if (bIsHaveAudio == 0)
			return HI_SUCCESS;
		
		pTmp = (AVPacket *)PRV_DataFromRTSP;
		NextPacket = pTmp->next;
		while(pTmp != NULL)
		{
			length = pTmp->DataSize - pTmp->BufOffset;
			Aoffset = 0;
			if(Prv_Audiobuf.length == 0 )
			{
				while(length > 0)
				{	
					if(length >= PtNumPerFrm)
					{
						SN_MEMCPY(AudioData, MAXAUDIODATALEN, AudioDataStarCode, 4, 4);
						SN_MEMCPY(AudioData + 4, MAXAUDIODATALEN, (char*)pTmp + pTmp->BufOffset+Aoffset, PtNumPerFrm, PtNumPerFrm);
						stAStream.pStream = AudioData;//PRV_DataFromRTSP->data + offSet + sizeof(HI_U64); 
						stAStream.u32Len = PtNumPerFrm + 4;//dataLen  - sizeof(HI_U64);

						s32Ret = HI_MPI_ADEC_SendStream(AdChn, &stAStream, HI_IO_NOBLOCK);
						if(s32Ret != HI_SUCCESS)  /* 送至解码器 */
						{
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Send Audio Stream to Adec %d Fail: 0x%x\n", DecAdec, s32Ret);
							HI_MPI_ADEC_ClearChnBuf(AdChn);
							return s32Ret;
						}
						length = length - PtNumPerFrm;
						Aoffset = Aoffset + PtNumPerFrm;
						//printf("send 1 PtNumPerFrm:%d\n",PtNumPerFrm);
					}
					else
					{
						SN_MEMCPY((char*)Prv_Audiobuf.databuf, sizeof(Prv_Audiobuf.databuf), (char*)pTmp + pTmp->BufOffset+Aoffset, length, length);
						Prv_Audiobuf.length = length;
						length = 0;
						//printf("copy 1 Prv_Audiobuf.length:%d\n",Prv_Audiobuf.length);
					}
				}
			}
			else
			{
				while(length > 0)
				{	
					if(Prv_Audiobuf.length+length >= PtNumPerFrm)
					{
						SN_MEMCPY(AudioData, MAXAUDIODATALEN, AudioDataStarCode, 4, 4);
						if(Prv_Audiobuf.length != 0)
						{
							
							SN_MEMCPY(AudioData + 4, MAXAUDIODATALEN, (char*)Prv_Audiobuf.databuf, Prv_Audiobuf.length, Prv_Audiobuf.length);
							SN_MEMCPY(AudioData + 4 + Prv_Audiobuf.length, MAXAUDIODATALEN, (char*)pTmp + pTmp->BufOffset+Aoffset, PtNumPerFrm-Prv_Audiobuf.length, PtNumPerFrm-Prv_Audiobuf.length);
							stAStream.pStream = AudioData;//PRV_DataFromRTSP->data + offSet + sizeof(HI_U64); 
							stAStream.u32Len = PtNumPerFrm + 4;//dataLen  - sizeof(HI_U64);
							//printf("copy send  2 Prv_Audiobuf.length:%d\n",Prv_Audiobuf.length);
							length = length + Prv_Audiobuf.length - PtNumPerFrm;
							Aoffset = Aoffset + PtNumPerFrm - Prv_Audiobuf.length;
							Prv_Audiobuf.length = 0;

						}
						else
						{
							SN_MEMCPY(AudioData + 4, MAXAUDIODATALEN, (char*)pTmp + pTmp->BufOffset+Aoffset, PtNumPerFrm, PtNumPerFrm);
							stAStream.pStream = AudioData;//PRV_DataFromRTSP->data + offSet + sizeof(HI_U64); 
							stAStream.u32Len = PtNumPerFrm + 4;//dataLen  - sizeof(HI_U64);
							//printf("send  2 PtNumPerFrm:%d\n",PtNumPerFrm);
							length = length - PtNumPerFrm;
							Aoffset = Aoffset + PtNumPerFrm;
						}

						s32Ret = HI_MPI_ADEC_SendStream(AdChn, &stAStream, HI_IO_NOBLOCK);
						if(s32Ret != HI_SUCCESS)  /* 送至解码器 */
						{
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Send Audio Stream to Adec %d Fail: 0x%x\n", DecAdec, s32Ret); 
							return s32Ret;
						}

					}
					else
					{
						SN_MEMCPY((char*)Prv_Audiobuf.databuf+Prv_Audiobuf.length, sizeof(Prv_Audiobuf.databuf), (char*)pTmp + pTmp->BufOffset+Aoffset, length, length);
						Prv_Audiobuf.length += length;
						length = 0;
						//printf("copy 2 length:%d,Prv_Audiobuf.length:%d\n",length,Prv_Audiobuf.length);
					}
				}
			}
			if(pTmp->Extendnext == NULL)
			{
				pTmp = (AVPacket *)NextPacket;
				if(pTmp != NULL)
				NextPacket = pTmp->next;
			}
			else
			{
				pTmp = (AVPacket *)(pTmp->Extendnext);
			}
		}
	}
	else if(CODEC_ID_H264 == datatype && VochnInfo[index].IsHaveVdec)
	{
		if(PRV_MASTER == SlaveId)
		{
			pTmp = (AVPacket *)PRV_DataFromRTSP;
			NextPacket = pTmp->next;
			while(pTmp != NULL)
			{
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Send Stream\n");
				length = pTmp->DataSize - pTmp->BufOffset;
				if(pTmp->frame_type == 0 || pTmp->frame_type == 1)
				{					
					if(TotalLength + 4 <= MAXFRAMELEN)
					{
						SN_MEMCPY(VideoDataStream + TotalLength, MAXFRAMELEN, VideoDataStarCode, 4, 4);
						TotalLength += 4;
					}
					else
					{						
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "WARNING: TotalLength + 4 > MAXFRAMELEN\n"); 
						return HI_FAILURE;
					}
				}				
			
				if(TotalLength + length <= MAXFRAMELEN)
				{
					SN_MEMCPY(VideoDataStream + TotalLength, MAXFRAMELEN, (char*)pTmp + pTmp->BufOffset, length, length);
					TotalLength += length;
				}
				else
				{						
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "WARNING: TotalLength + length > MAXFRAMELEN\n"); 
					return HI_FAILURE;
				}
				
				if(pTmp->Extendnext == NULL)
				{
					pTmp = (AVPacket *)NextPacket;
					if(pTmp != NULL)
						NextPacket = pTmp->next;
				}
				else
				{
					pTmp = (AVPacket *)(pTmp->Extendnext);
				}
			}

			if(TotalLength + 4 <= MAXFRAMELEN)
			{
				SN_MEMCPY(VideoDataStream + TotalLength, MAXFRAMELEN, VideoDataStarCode, 4, 4);
			}
			else
			{						
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "WARNING: TotalLength + 4 > MAXFRAMELEN\n"); 
				return HI_FAILURE;
			}

			p = (unsigned char *)VideoDataStream;
			len = TotalLength + 4;
			while(find_nal_unit((uint8_t*)p, len, &nal_start, &nal_end) > 0)
			{
				p += nal_start; 
				ret = read_nal_unit((h264_stream_t*)&h_stream[VdecChn], (uint8_t*)p, nal_end - nal_start);
				
				p += (nal_end - nal_start);
            	len -= nal_end;
			}
			
			if (h_stream[VdecChn].nal.nal_unit_type == 1 || h_stream[VdecChn].nal.nal_unit_type == 5)
			{
				if (h_stream[VdecChn].sh.frame_num - h_stream[VdecChn].pre_num > 1)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_SLC, "line:%d ch=%d, nal_unit_type=%d, pre_num=%d, frame_num=%d", __LINE__, VdecChn, h_stream[VdecChn].nal.nal_unit_type, h_stream[VdecChn].pre_num, h_stream[VdecChn].sh.frame_num);
					h_stream[VdecChn].pre_num = h_stream[VdecChn].sh.frame_num;
					return HI_SUCCESS;
				}
				else
				{
#if 1
					if(SendChnInfo.is_disgard[VdecChn] && h_stream[VdecChn].nal.nal_unit_type == 1)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_SLC, "1 line:%d ch=%d, nal_unit_type=%d, pre_num=%d, frame_num=%d, first_mb_in_slice=%d", __LINE__, VdecChn, h_stream[VdecChn].nal.nal_unit_type, h_stream[VdecChn].pre_num, h_stream[VdecChn].sh.frame_num, h_stream[VdecChn].sh.first_mb_in_slice);
						SendChnInfo.is_first[VdecChn] = 0;
						return HI_SUCCESS;
					}

					if(h_stream[VdecChn].nal.nal_unit_type == 5 && h_stream[VdecChn].sh.first_mb_in_slice == 0)
					{
						SendChnInfo.is_disgard[VdecChn] = 0;
						SendChnInfo.is_first[VdecChn] = 1;
						h_stream[VdecChn].pre_num = h_stream[VdecChn].sh.frame_num;
					}
					
					if(SendChnInfo.is_first[VdecChn] && h_stream[VdecChn].nal.nal_unit_type == 5 && h_stream[VdecChn].sh.first_mb_in_slice !=0)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_SLC, "2 is_first line:%d ch=%d, nal_unit_type=%d, pre_num=%d, frame_num=%d, first_mb_in_slice=%d", __LINE__, VdecChn, h_stream[VdecChn].nal.nal_unit_type, h_stream[VdecChn].pre_num, h_stream[VdecChn].sh.frame_num, h_stream[VdecChn].sh.first_mb_in_slice);
						SendChnInfo.is_disgard[VdecChn] = 1;
						SendChnInfo.is_first[VdecChn] = 0;
						return HI_SUCCESS;
					}

					if(SendChnInfo.is_disgard[VdecChn] && SendChnInfo.is_first[VdecChn] == 0 && h_stream[VdecChn].nal.nal_unit_type == 5)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_SLC, "2 is_disgard line:%d ch=%d, nal_unit_type=%d, pre_num=%d, frame_num=%d, first_mb_in_slice=%d", __LINE__, VdecChn, h_stream[VdecChn].nal.nal_unit_type, h_stream[VdecChn].pre_num, h_stream[VdecChn].sh.frame_num, h_stream[VdecChn].sh.first_mb_in_slice);
						
						return HI_SUCCESS;
					}
#endif				
					if(!VochnInfo[index].bIsPBStat && SendChnInfo.is_first[VdecChn] == 0 && h_stream[VdecChn].pre_num == h_stream[VdecChn].sh.frame_num)
					{
						SendChnInfo.is_same[VdecChn] = 1;
					}
					else
					{
						SendChnInfo.is_same[VdecChn] = 0;
					}
					
					h_stream[VdecChn].pre_num = h_stream[VdecChn].sh.frame_num;
					
				}
			}

			if(!VochnInfo[index].bIsPBStat && SendChnInfo.is_first[VdecChn])
			{
				if(SendChnInfo.VideoDataStream1[VdecChn] != NULL)
				{
					SN_FREE(SendChnInfo.VideoDataStream1[VdecChn]);
					SendChnInfo.VideoDataStream1[VdecChn] = NULL;
				}

				SendChnInfo.VideoDataStream1[VdecChn] = (HI_U8*)SN_MALLOC(TotalLength);
				if(SendChnInfo.VideoDataStream1[VdecChn] == NULL)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s, line:%d VideoDataStream1[%d] is NULL\n", VdecChn, __FUNCTION__, __LINE__);
					return HI_SUCCESS;
				}
				
				SN_MEMCPY(SendChnInfo.VideoDataStream1[VdecChn], TotalLength, VideoDataStream , TotalLength, TotalLength);
				SendChnInfo.TotalLength1[VdecChn] = TotalLength;
				SendChnInfo.is_first[VdecChn] = 0;
				return HI_SUCCESS;
			}
			
			if(SendChnInfo.is_same[VdecChn])
			{
				if(SendChnInfo.VideoDataStream2[VdecChn] != NULL)
				{
					SN_FREE(SendChnInfo.VideoDataStream2[VdecChn]);
					datalength = 0;
				}

				if(SendChnInfo.VideoDataStream1[VdecChn] == NULL)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s, line:%d VideoDataStream1[%d] is NULL\n", VdecChn, __FUNCTION__, __LINE__);
					SendChnInfo.TotalLength1[VdecChn] = 0;
					SendChnInfo.is_first[VdecChn] = 0;
					return HI_SUCCESS;
				}

				datalength = TotalLength + SendChnInfo.TotalLength1[VdecChn];

				SendChnInfo.VideoDataStream2[VdecChn] = (HI_U8*)SN_MALLOC(datalength);

				if(SendChnInfo.VideoDataStream2[VdecChn] == NULL)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s, line:%d VideoDataStream2[%d] is NULL\n", VdecChn, __FUNCTION__, __LINE__);
					if(SendChnInfo.VideoDataStream1[VdecChn] != NULL)
					{
						SN_FREE(SendChnInfo.VideoDataStream1[VdecChn]);
						SendChnInfo.VideoDataStream1[VdecChn] = NULL;
					}
					SendChnInfo.TotalLength1[VdecChn] = TotalLength;
					SendChnInfo.is_first[VdecChn] = 0;
					SendChnInfo.is_same[VdecChn] = 0;
					return HI_SUCCESS;
				}

				SN_MEMCPY(SendChnInfo.VideoDataStream2[VdecChn], datalength, SendChnInfo.VideoDataStream1[VdecChn], SendChnInfo.TotalLength1[VdecChn], SendChnInfo.TotalLength1[VdecChn]);
				
				SN_MEMCPY(SendChnInfo.VideoDataStream2[VdecChn] + SendChnInfo.TotalLength1[VdecChn], datalength, VideoDataStream, TotalLength, TotalLength);
				
			}
			
			if(g_filesign[VdecChn][1] >= 0 && g_filesign[VdecChn][1] == VdecChn)
			{
				char name[32];
				SN_SPRINTF(name, sizeof(name), "/tmp/send_ch%d.h264", g_filesign[VdecChn][1]);
				
				FILE *pf = fopen(name, "a");
				if(NULL!=pf)
				{					
					if(SendChnInfo.is_same[VdecChn])
					{
						if(datalength)
						{
							fwrite(SendChnInfo.VideoDataStream2[VdecChn],datalength,1,pf);
						}
					}
					else
					{
						if(SendChnInfo.TotalLength1[VdecChn])
						{
							fwrite(SendChnInfo.VideoDataStream1[VdecChn],SendChnInfo.TotalLength1[VdecChn],1,pf);
						}
					}

					fclose(pf);
				}
			}

			if(SendChnInfo.is_same[VdecChn])
			{
				stVstream.pu8Addr = SendChnInfo.VideoDataStream2[VdecChn];
				stVstream.u32Len = datalength;
			}
			else
			{
				stVstream.pu8Addr = SendChnInfo.VideoDataStream1[VdecChn];
				stVstream.u32Len = SendChnInfo.TotalLength1[VdecChn];
			}

			if(!VochnInfo[index].bIsPBStat)
			{
				stVstream.u64PTS = PRV_DataFromRTSP->prvpts;
			}
			else
			{
				stVstream.u64PTS = PRV_DataFromRTSP->prvpts;
			}

			/*往解码器送视频流数据*/
			s32Ret = HI_MPI_VDEC_SendStream(VdecChn, &stVstream, HI_IO_NOBLOCK);
			
			SendChnInfo.TotalLength1[VdecChn] = 0;
			SendChnInfo.is_first[VdecChn] = 1;
			
			if(SendChnInfo.is_same[VdecChn] == 0)
			{
				SendChnInfo.is_first[VdecChn] = 0;
				
				if(SendChnInfo.VideoDataStream1[VdecChn] != NULL)
				{
					SN_FREE(SendChnInfo.VideoDataStream1[VdecChn]);
					SendChnInfo.VideoDataStream1[VdecChn] = NULL;
				}

				SendChnInfo.VideoDataStream1[VdecChn] = (HI_U8*)SN_MALLOC(TotalLength);
				SN_MEMCPY(SendChnInfo.VideoDataStream1[VdecChn], TotalLength, VideoDataStream, TotalLength, TotalLength);
				SendChnInfo.TotalLength1[VdecChn] = TotalLength;
			}
			else
			{
				if(SendChnInfo.VideoDataStream2[VdecChn] != NULL)
				{
					SN_FREE(SendChnInfo.VideoDataStream2[VdecChn]);
					SendChnInfo.VideoDataStream2[VdecChn] = NULL;
					datalength = 0;
				}
			}
		
			if(s32Ret != HI_SUCCESS)  /* 送至视频解码器 */
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Send Video Stream to Vdec %d Fail: 0x%x\n", VdecChn, s32Ret); 		
				return s32Ret;
			}
			
			return HI_SUCCESS;
		}
		else if(SlaveId > PRV_MASTER)
		{	
			unsigned char sdata[24] = {0};
			int sdata_len = 24;
			if(PRV_State == 1)//退出回放/切片时，如果在从片，每帧发送一次从片
			{				
				pTmp = (AVPacket *)PRV_DataFromRTSP;
				*(HI_S32 *)sdata = 1;
				*(HI_S32 *)(sdata + sizeof(HI_S32)) = PRV_DataFromRTSP->frame_type;
				*(HI_S32 *)(sdata + 2 * sizeof(HI_S32)) = VdecChn;
				//HostSendHostToSlaveBuffer(s32StreamChnIDs, sdata, sizeof(HI_S32), OldVdec[i]);
				*(HI_S32 *)(sdata + 3 * sizeof(HI_S32)) = PRV_DataFromRTSP->data_len;
				*(HI_U64 *)(sdata + 4 * sizeof(HI_S32)) = /*(PRV_DataFromRTSP->pts - PtsInfo[chn].FirstVideoPts) * 1000000/90000;//*/PRV_DataFromRTSP->prvpts;
				
				if(HostSendHostToSlaveReady(s32StreamChnIDs, PRV_DataFromRTSP->data_len + sdata_len, VdecChn) != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d, HostSendHostToSlaveStart fail\n", __LINE__);
					PRV_InitHostToSlaveStream();
					return HI_SUCCESS;
				}
				HostSendHostToSlaveBuffer(s32StreamChnIDs, sdata, sdata_len, VdecChn);	
				NextPacket = pTmp->next;
				while(pTmp != NULL)
				{
					length = pTmp->DataSize - pTmp->BufOffset;
					if(pTmp->frame_type == 0 || pTmp->frame_type == 1)
					{
						HostSendHostToSlaveBuffer(s32StreamChnIDs, VideoDataStarCode, 4, VdecChn);
					}
					
					HostSendHostToSlaveBuffer(s32StreamChnIDs, (char*)pTmp + pTmp->BufOffset, length, VdecChn);
					if(pTmp->Extendnext == NULL)
					{
						pTmp = (AVPacket *)NextPacket;
						if(pTmp != NULL)
						NextPacket = pTmp->next;
					}
					else
					{
						pTmp = (AVPacket *)(pTmp->Extendnext);
					}
				}
				if(HostSendHostToSlaveStart(s32StreamChnIDs) == HI_FAILURE)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d, HostSendHostToSlaveStart fail\n", __LINE__);
					PRV_InitHostToSlaveStream();
				}
				return HI_SUCCESS;
			}
			#if (IS_DECODER_DEVTYPE == 1)
			{
				#if 0
				if(fp == NULL)
				{
					fp = fopen("/tmp/video", "wb+");
					if(fp == NULL)
					{
						printf("============Open file fail!\n");
					}
						
				}
				#endif
				pTmp = (AVPacket *)PRV_DataFromRTSP;
				TotalLength = 0;
				NextPacket = pTmp->next;

				*(HI_S32 *)VideoDataStream = 1;//批量发送的总帧数
				*(HI_S32 *)(VideoDataStream + sizeof(HI_S32)) = PRV_DataFromRTSP->frame_type;
				*(HI_S32 *)(VideoDataStream + 2 * sizeof(HI_S32)) = VdecChn;
				*(HI_S32 *)(VideoDataStream + 3 * sizeof(HI_S32)) = PRV_DataFromRTSP->data_len;
				*(HI_U64 *)(VideoDataStream + 4 * sizeof(HI_S32)) = /*(PRV_DataFromRTSP->pts - PtsInfo[chn].FirstVideoPts) * 1000000/90000;//*/PRV_DataFromRTSP->prvpts;
				TotalLength += 24;
				//if(VochnInfo[index].bIsPBStat)
				
				//CHECK(HostSendHostToSlaveReady(s32StreamChnIDs, PRV_DataFromRTSP->data_len + sdata_len, VdecChn));
				//(HostSendHostToSlaveBuffer(s32StreamChnIDs, sdata, sdata_len, VdecChn));	
				#if 0
				if(PRV_DataFromRTSP->frame_type == 0 || PRV_DataFromRTSP->frame_type == 1)
				{
					CHECK(HostSendHostToSlaveBuffer(s32StreamChnIDs, VideoDataStarCode, 4, VdecChn));
					if(fp != NULL)
					{
						if(fwrite(VideoDataStarCode, 4, 1, fp) != 1)
						{
							printf("++++++++++++++Write Fail!\n");
						}

					}
				}
				#endif
				pTmp = (AVPacket *)PRV_DataFromRTSP;
				NextPacket = pTmp->next;
				while(pTmp != NULL)
				{
					length = pTmp->DataSize - pTmp->BufOffset;
					if(pTmp->frame_type == 0 || pTmp->frame_type == 1)
					{
						
						if(TotalLength + 4 <= MAXFRAMELEN)
						{
							SN_MEMCPY(VideoDataStream + TotalLength, MAXFRAMELEN, VideoDataStarCode, 4, 4);
							TotalLength += 4;
						}
						else
						{						
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "WARNING: TotalLength + 4 > MAXFRAMELEN\n"); 
							return HI_FAILURE;
						}
						//HostSendHostToSlaveBuffer(s32StreamChnIDs, VideoDataStarCode, 4, VdecChn);
					}
					if(TotalLength + length <= MAXFRAMELEN)
					{
						SN_MEMCPY(VideoDataStream + TotalLength, MAXFRAMELEN, (char*)pTmp + pTmp->BufOffset, length, length);
						TotalLength += length;
					}
					else
					{						
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "WARNING: slave===TotalLength + length > MAXFRAMELEN\n"); 
						return HI_FAILURE;
					}
					
					//HostSendHostToSlaveBuffer(s32StreamChnIDs, (char*)pTmp + pTmp->BufOffset, length, VdecChn);
					if(pTmp->Extendnext == NULL)
					{
						pTmp = (AVPacket *)NextPacket;
						if(pTmp != NULL)
							NextPacket = pTmp->next;
					}
					else
					{
						pTmp = (AVPacket *)(pTmp->Extendnext);
					}
				}
				#if 0
				length = PRV_DataFromRTSP->DataSize - PRV_DataFromRTSP->BufOffset;
				CHECK(HostSendHostToSlaveBuffer(s32StreamChnIDs, (char*)PRV_DataFromRTSP + PRV_DataFromRTSP->BufOffset, length, VdecChn));
				if(fp != NULL && WriteCount <= 100)
				{
					if(fwrite((char*)PRV_DataFromRTSP + PRV_DataFromRTSP->BufOffset, length, 1, fp) != 1)
					{
						printf("++++++++++++++Write Fail!\n");
					}
					else
						WriteCount++;
				}
				#endif
				int sendState = 0;
				s32Ret = HostSendHostToSlaveStream(s32StreamChnIDs, VideoDataStream, TotalLength, VdecChn, &sendState);	 /* 发送数据给从片 */
				if(s32Ret != HI_SUCCESS)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d, HostSendHostToSlaveStart fail\n", __LINE__);
					PRV_InitHostToSlaveStream();
					return HI_FAILURE;
				}
				//TRACE(SCI_TRACE_NORMAL, MOD_VAM, "slave: Vdec: %d, TotalLength: %d, data_len: %d, s32StreamChnIDs: %d, PRV_DataFromRTSP->prvpts: %lld\n", VdecChn, TotalLength, PRV_DataFromRTSP->data_len, s32StreamChnIDs, PRV_DataFromRTSP->prvpts);
				//CHECK(HostSendHostToSlaveStart(s32StreamChnIDs));
				tmpChn = VochnInfo[index].CurChnIndex;
				VoChnState.VideoDataTimeLag[tmpChn] = (HI_S64)(PRV_DataFromRTSP->pts - PtsInfo[tmpChn].FirstVideoPts) * 1000/90000;//当前视频时间戳与基准时间戳的差值(换算成ms)				
				return HI_SUCCESS;
			}
			#endif
			//固定大小发送SendSize1－SendSize2(打包发送)
			AVPacket *pTmp1 = NULL, *OldpTmp = NULL;
			int i = 0, count = 0, tmpSendDataLen = 0, tmpIndex = 0;
			int SendSizeMin = 0, SendSizeMax = 0, ToTalFramePerTime = 0;
#if defined(SN9234H1)
			SendSizeMin = BASE_SENDSIZE_MIN * 2;
			SendSizeMax = BASE_SENDSIZE_MAX * 2;
#else			
			SendSizeMin = BASE_SENDSIZE_MIN * CurSlaveChnCount;
			SendSizeMax = BASE_SENDSIZE_MAX * CurSlaveChnCount;
#endif			
			PRV_SendDataLen = PRV_SendDataLen + PRV_DataFromRTSP->data_len + sdata_len;
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_DataFromRTSP->data_len: %d, PRV_SendDataLen: %d", PRV_DataFromRTSP->data_len, PRV_SendDataLen);
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "SendDataLen: %d, CurIndex: %d\n", SendDataLen, CurIndex);
			//累积到一定帧数后，一起发从片，
			//相对每帧发送一次，可以降低预览5%CPU使用率
			//相对累积到一定大小后，一起发从片，从片显示的实时性更好
#if defined(SN9234H1)
			if(PRV_CurIndex < 2)
#else			
			if(PRV_CurIndex < CurSlaveChnCount)
#endif			
			{
				if(PRV_SendDataLen < SendSizeMax)
				{
					PRV_OldVideoData[PRV_CurIndex] = PRV_DataFromRTSP;
					PRV_OldVdec[PRV_CurIndex] = VdecChn;
					PRV_CurIndex++;
					
					if(PRV_SendDataLen < SendSizeMin)
					{
						return HI_SUCCESS;
					}
				}
				else
				{
					PRV_SendDataLen = PRV_SendDataLen - PRV_DataFromRTSP->data_len - sdata_len;
					pTmp = PRV_DataFromRTSP;
					pTmp1 = pTmp;
				}

			}
			//额外超出的数据额外单独发送
			else
			{
				PRV_SendDataLen = PRV_SendDataLen - PRV_DataFromRTSP->data_len - sdata_len;
				pTmp = PRV_DataFromRTSP;
				pTmp1 = pTmp;
			}
			if(s32StreamChnIDs <= 0)
			{
				for(i = 0; i < PRV_CurIndex; i++)
				{
					NTRANS_FreeMediaData(PRV_OldVideoData[i]);
					PRV_OldVideoData[i] = NULL;
				}
				PRV_CurIndex = 0;
				PRV_SendDataLen = 0;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "s32StreamChnIDs: %d\n", s32StreamChnIDs);
				return HI_FAILURE;
			}
			//先发送累积的数据
			if(HostSendHostToSlaveReady(s32StreamChnIDs, PRV_SendDataLen, VdecChn)!=HI_SUCCESS)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d, HostSendHostToSlaveStart fail\n", __LINE__);
				PRV_InitHostToSlaveStream();
				return HI_FAILURE;
			}
			ToTalFramePerTime = PRV_CurIndex;
			for(i = 0; i < PRV_CurIndex; i++)
			{
				//判断是否有数据被释放掉，如果有，则此次发送的总帧数-1
				if(PRV_OldVideoData[i] == NULL)
				{
					ToTalFramePerTime--;
				}
			}
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Ready Send Data: %d, CurIndex: %d\n", SendDataLen, CurIndex);
			for(i = 0; i < PRV_CurIndex; i++)
			{
				if(PRV_OldVideoData[i] == NULL)
				{
					continue;
				}
				*(HI_S32 *)sdata = ToTalFramePerTime;//批量发送的总帧数
				*(HI_S32 *)(sdata + sizeof(HI_S32)) = PRV_OldVideoData[i]->frame_type;
				*(HI_S32 *)(sdata + 2 * sizeof(HI_S32)) = PRV_OldVdec[i];
				//HostSendHostToSlaveBuffer(s32StreamChnIDs, sdata, sizeof(HI_S32), OldVdec[i]);
				*(HI_S32 *)(sdata + 3 * sizeof(HI_S32)) = PRV_OldVideoData[i]->data_len;
				*(HI_U64 *)(sdata + 4 * sizeof(HI_S32)) = PRV_OldVideoData[i]->prvpts;
				CHECK(HostSendHostToSlaveBuffer(s32StreamChnIDs, sdata, sdata_len, PRV_OldVdec[i]));	

				OldpTmp = PRV_OldVideoData[i];
				NextPacket = OldpTmp->next;
				while(OldpTmp != NULL)
				{
					length = OldpTmp->DataSize - OldpTmp->BufOffset;
					if(OldpTmp->frame_type == 0 || OldpTmp->frame_type == 1)
					{
						CHECK(HostSendHostToSlaveBuffer(s32StreamChnIDs, VideoDataStarCode, 4, PRV_OldVdec[i]));
					}
					
					CHECK(HostSendHostToSlaveBuffer(s32StreamChnIDs, (char*)OldpTmp + OldpTmp->BufOffset, length, PRV_OldVdec[i]));

					if(OldpTmp->Extendnext == NULL)
					{
						OldpTmp = (AVPacket *)NextPacket;
						if(OldpTmp != NULL)
						NextPacket = OldpTmp->next;
					}
					else
					{
						OldpTmp = (AVPacket *)(OldpTmp->Extendnext);
					}
				}
				tmpIndex = PRV_GetVoChnIndex(PRV_OldVdec[i]);
				tmpChn = VochnInfo[tmpIndex].CurChnIndex;
				VoChnState.VideoDataTimeLag[tmpChn] = (HI_S64)(PRV_OldVideoData[i]->pts - PtsInfo[tmpChn].FirstVideoPts) * 1000/90000;//当前视频时间戳与基准时间戳的差值(换算成ms)
				NTRANS_FreeMediaData(PRV_OldVideoData[i]);
				PRV_OldVideoData[i] = NULL;
				
			}
			if(HostSendHostToSlaveStart(s32StreamChnIDs) == HI_FAILURE)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d, HostSendHostToSlaveStart fail\n", __LINE__);
				PRV_InitHostToSlaveStream();
			}

			PRV_CurIndex = 0;
			PRV_SendDataLen = 0;
			//再发送额外的数据
			if(pTmp != NULL)
			{	
				//当前额外的数据大于PCI一次传输的大小时，此数据分多次发送给从片
				if(PRV_DataFromRTSP->data_len > PCIV_VDEC_STREAM_BUF_LEN/2)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "VdecChn: %d---PRV_DataFromRTSP->data_len---%d > 1024 *1024\n", VdecChn, PRV_DataFromRTSP->data_len);
					NextPacket = pTmp->next;
					while(pTmp != NULL)
					{
						count = 0;
						tmpSendDataLen = 0;
						//定位每次发送数据的结束包位置
						while(pTmp1 != NULL)
						{
							length = pTmp1->DataSize - pTmp1->BufOffset;
							tmpSendDataLen += length;
							//每次最多发送1M
							if(tmpSendDataLen >= (PCIV_VDEC_STREAM_BUF_LEN/2  - sdata_len))
							{
								tmpSendDataLen -= length;								
								break;
							}
							count++;
							if(pTmp1->Extendnext == NULL)
							{
								pTmp1 = (AVPacket *)NextPacket;
								if(pTmp1 != NULL)
								NextPacket = pTmp1->next;
							}
							else
							{
								pTmp1 = (AVPacket *)(pTmp1->Extendnext);
							}
						}
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "count---%d, tmpSendDataLen: %d\n", count, tmpSendDataLen);
						
						//定位到结束包后，发送此包之前的数据
						if(HostSendHostToSlaveReady(s32StreamChnIDs, tmpSendDataLen + sdata_len, VdecChn)!=HI_SUCCESS)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d, HostSendHostToSlaveStart fail\n", __LINE__);
							PRV_InitHostToSlaveStream();
							return HI_FAILURE;
						}
						*(HI_S32 *)sdata = 1;
						*(HI_S32 *)(sdata + sizeof(HI_S32)) = -1;//说明一帧数据采用分段发送，后续还有同一帧数据的其余部分

					
						if(pTmp1 == NULL)//最后一部分数据
						{
							*(HI_S32 *)(sdata + sizeof(HI_S32)) = PRV_DataFromRTSP->frame_type + 2;
						}

						*(HI_S32 *)(sdata + 2 * sizeof(HI_S32)) = VdecChn;
						//HostSendHostToSlaveBuffer(s32StreamChnIDs, sdata, sizeof(HI_S32), VdecChn);
						*(HI_S32 *)(sdata + 3 * sizeof(HI_S32)) = tmpSendDataLen;
						*(HI_U64 *)(sdata + 4 * sizeof(HI_S32)) = PRV_DataFromRTSP->prvpts;
						CHECK(HostSendHostToSlaveBuffer(s32StreamChnIDs, sdata, sdata_len, VdecChn));//优先送3个数据标识

						i = 0;
						NextPacket = pTmp->next;
						while(i++ < count )
						{
							length = pTmp->DataSize - pTmp->BufOffset;
							if(pTmp->frame_type == 0 || pTmp->frame_type == 1)
							{
								//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "i: %d, pTmp->frame_type: %d", i, pTmp->frame_type);
								CHECK(HostSendHostToSlaveBuffer(s32StreamChnIDs, VideoDataStarCode, 4, VdecChn));
							}							
							CHECK(HostSendHostToSlaveBuffer(s32StreamChnIDs, (char*)pTmp + pTmp->BufOffset, length, VdecChn));

							if(pTmp->Extendnext == NULL)
							{
								pTmp = (AVPacket *)NextPacket;
								if(pTmp != NULL)
								NextPacket = pTmp->next;
							}
							else
							{
								pTmp = (AVPacket *)(pTmp->Extendnext);
							}

						}
						if(HostSendHostToSlaveStart(s32StreamChnIDs) == HI_FAILURE)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d, HostSendHostToSlaveStart fail\n", __LINE__);
							PRV_InitHostToSlaveStream();
						}

					}
				}
				else//数据可以一次发送
				{						
					if(HostSendHostToSlaveReady(s32StreamChnIDs,  PRV_DataFromRTSP->data_len + sdata_len, VdecChn)!=HI_SUCCESS)	
					{
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d, HostSendHostToSlaveStart fail\n", __LINE__);
						PRV_InitHostToSlaveStream();
						return HI_FAILURE;
					}
					*(HI_S32 *)sdata = 1;
					*(HI_S32 *)(sdata + sizeof(HI_S32)) = PRV_DataFromRTSP->frame_type;
					*(HI_S32 *)(sdata + 2 * sizeof(HI_S32)) = VdecChn;
					//HostSendHostToSlaveBuffer(s32StreamChnIDs, sdata, sizeof(HI_S32), VdecChn);
					*(HI_S32 *)(sdata + 3 * sizeof(HI_S32)) = PRV_DataFromRTSP->data_len;
					*(HI_U64 *)(sdata + 4 * sizeof(HI_S32)) = PRV_DataFromRTSP->prvpts;
					CHECK(HostSendHostToSlaveBuffer(s32StreamChnIDs, sdata, sdata_len, VdecChn));	//优先送3个数据标识
					NextPacket = pTmp->next;
					while(pTmp != NULL)
					{
						length = pTmp->DataSize - pTmp->BufOffset;
						if(pTmp->frame_type == 0 || pTmp->frame_type == 1)
						{
							CHECK(HostSendHostToSlaveBuffer(s32StreamChnIDs, VideoDataStarCode, 4, VdecChn));
						}
						
						CHECK(HostSendHostToSlaveBuffer(s32StreamChnIDs, (char*)pTmp + pTmp->BufOffset, length, VdecChn));
						if(pTmp->Extendnext == NULL)
						{
							pTmp = (AVPacket *)NextPacket;
							if(pTmp != NULL)
							NextPacket = pTmp->next;
						}
						else
						{
							pTmp = (AVPacket *)(pTmp->Extendnext);
						}
					}
					if(HostSendHostToSlaveStart(s32StreamChnIDs) == HI_FAILURE)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d, HostSendHostToSlaveStart fail\n", __LINE__);
						PRV_InitHostToSlaveStream();
					}
				}					
				VoChnState.VideoDataTimeLag[chn] = (HI_S64)(PRV_DataFromRTSP->pts - PtsInfo[chn].FirstVideoPts) * 1000/90000;//当前视频时间戳与基准时间戳的差值(换算成ms)
			}			
		}
		else
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Error SlaveId---%d\n", SlaveId);
			return HI_FAILURE;
		}
	}
	else
	{
		return HI_FAILURE;
	}
	
	//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "End Send: %d, slaveid: %d\n", VdecChn, SlaveId);
	return HI_SUCCESS;

}


//重新设置音频输出的属性
HI_S32 PRV_SetAoAttr(HI_U32 u32PtNumPerFrm)
{
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif	
	AO_CHN AoChn = 0;
	AIO_ATTR_S stAioAttr;
	
	CHECK_RET(HI_MPI_AO_GetPubAttr(AoDev, &stAioAttr));
	stAioAttr.u32PtNumPerFrm = u32PtNumPerFrm;

	CHECK_RET(HI_MPI_AO_DisableChn(AoDev, AoChn));

	CHECK_RET(HI_MPI_AO_Disable(AoDev));

	CHECK_RET(HI_MPI_AO_SetPubAttr(AoDev, &stAioAttr));

	CHECK_RET(HI_MPI_AO_Enable(AoDev));

	CHECK_RET(HI_MPI_AO_EnableChn(AoDev, AoChn));
#if defined(SN9234H1)	
	HI_MPI_AO_BindAdec(AoDev, AoChn, DecAdec);
#endif
	return HI_SUCCESS;

}

//判断音频参数是否改变，如果改变了，需要重新设置音频输出属性
HI_S32 PRV_IsAudioParaChange(int chn, int length)
{
	//RET_SUCCESS("");
#if defined(Hi3531)	
	AUDIO_DEV AoDev = 4;
	AO_CHN AoChn = 0;
#elif defined(Hi3535)
	AUDIO_DEV AoDev = 0;
	AO_CHN AoChn = 0;
#endif	
	HI_U32 NewPtNumPerFrm = 0;

	if(chn < 0 || chn > DEV_CHANNEL_NUM)
	{
		RET_FAILURE("chn is error");
	}

	if(length != 160 && length != 320)
		length = 320;
	
	if(length != PtNumPerFrm)
	{
		if(IsCreateAdec == 0)
		{
			PRV_StartAdec(VochnInfo[chn].AudioInfo.adoType,DecAdec);
			IsCreateAdec = 1;
		}
		
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-----------PtNumPerFrm Is Change----Old: %d, New: %d\n", PtNumPerFrm, length);
		NewPtNumPerFrm = length;
#if defined(Hi3531)||defined(Hi3535)	
		if(IsAdecBindAo == 1)
		{
			PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, DecAdec);
			IsAdecBindAo = 0;
		}
#endif		
		CHECK_RET(PRV_SetAoAttr(NewPtNumPerFrm));
#if defined(Hi3531)||defined(Hi3535)	
		if(IsAdecBindAo == 0)
		{
			PRV_AUDIO_AoBindAdec(AoDev, AoChn, DecAdec);
			IsAdecBindAo = 1;
		}
#endif		
		PtNumPerFrm = NewPtNumPerFrm;
		Prv_Audiobuf.length = 0;
	}
	
	RET_SUCCESS("");
}


//解析数据的SPS信息，获取视频的分辨率
HI_S32 PRV_SearchSPS(int chn, AVPacket *PRV_DataFromRTSP, int *new_width, int *new_height)
{
	HI_S32 index = 0;	
	SPS SPSInfo;
	AVPacket *pTmp = PRV_DataFromRTSP;
	SN_MEMSET(&SPSInfo, 0, sizeof(SPS_DATA));
	index = chn + LOCALVEDIONUM;
	
	while(pTmp != NULL)
	{
		if(pTmp->frame_type == 1)
		{
			if((((char*)pTmp)[pTmp->BufOffset] & 0x1f) == 0x7)
			{
				/*跳过SPS帧的第一个字节(NALU头)*/
				ParseSPS_Ex((unsigned char*)pTmp + pTmp->BufOffset + 1, &SPSInfo);//根据I帧数据，解析SPS信息
				break;
			}
		}
		pTmp = pTmp->next;
	}
	*new_width = (SPSInfo.mb_width) * 16;
	*new_height = (SPSInfo.mb_height) * 16;

 	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s line: %d, chn: %d, ref_frame_count=%d\n", __FUNCTION__, __LINE__, chn, SPSInfo.ref_frame_count);

#if(DEV_TYPE == DEV_SN_9234_H_1)	 //3520需要做此判断，3531不需要
	if(SPSInfo.profile_idc > 66 && !VochnInfo[index].bIsPBStat)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s line: %d, chn: %d, new_width: %d, new_height: %d, profile_idc=%d\n",
			__FUNCTION__, __LINE__, chn, *new_width, *new_height, SPSInfo.profile_idc);

		FWK_SetOverflowState(chn, EXP_PROFILE_ERR);
		ALM_STATUS_CHG_ST status;
		status.AlmCnt = 1;
		SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_MMI, 0, 0, MSG_ID_ALM_STATUS_CHG, &status, sizeof(status));

		deluser_used use;
		use.channel = chn;
		SN_SendMessageEx(SUPER_USER_ID, MOD_NTRANS, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ,(void*)&use,sizeof(deluser_used));
		VochnInfo[chn].u32RefFrameNum = SPSInfo.ref_frame_count;
		return -2;
	}


	if(SPSInfo.profile_idc > 66 && !VoChnState.FirstHaveVideoData[chn] && VochnInfo[index].bIsPBStat && PlayInfo.PlayBackState >= PLAY_INSTANT)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s line: %d, chn: %d, new_width: %d, new_height: %d, profile_idc=%d\n",
			__FUNCTION__, __LINE__, chn, *new_width, *new_height, SPSInfo.profile_idc);

		Ftpc_End_Req endreq;
		endreq.result = SN_ERR_FTPC_VINOSUPORT_ERROR;
		endreq.channel = chn + 1;
		endreq.PlayType = PlayInfo.PlayBackState > PLAY_INSTANT ? 2 : 1;

		SendMessageEx(SUPER_USER_ID,MOD_PRV,MOD_FWK,0,0,MSG_ID_FTPC_END_REQ,&endreq,sizeof(Ftpc_End_Req));
		VochnInfo[chn].u32RefFrameNum = SPSInfo.ref_frame_count;
		return -2;
	}
#endif

	if(((*new_width) * (*new_height) < 176 * 144) && *new_width >= 0 && *new_height >= 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"%s line: %d, chn: %d, new_width: %d, new_height: %d, VochnInfo[index].VideoInfo.width: %d, VochnInfo[index].VideoInfo.height: %d\n",
			__FUNCTION__, __LINE__, chn, *new_width, *new_height, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height);

		*new_width = VochnInfo[index].VideoInfo.width;
		*new_height = VochnInfo[index].VideoInfo.height;
		VochnInfo[chn].u32RefFrameNum = SPSInfo.ref_frame_count;
		return HI_SUCCESS;
	}
	else
	{
		//数据分辨率与解码通道分辨率不一致
		if(*new_height != VochnInfo[index].VideoInfo.height || *new_width != VochnInfo[index].VideoInfo.width||VochnInfo[chn].u32RefFrameNum != SPSInfo.ref_frame_count)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s line: %d, chn: %d, new_width: %d, new_height: %d, VochnInfo[index].VideoInfo.width: %d, VochnInfo[index].VideoInfo.height: %d\n",
				__FUNCTION__, __LINE__, chn, *new_width, *new_height, VochnInfo[index].VideoInfo.width, VochnInfo[index].VideoInfo.height);
			VochnInfo[chn].u32RefFrameNum = SPSInfo.ref_frame_count;
			return HI_FAILURE;
		}
	}

	return HI_SUCCESS;
}

//重新启动解码器。
//创建的解码器中，太多数据未解时，导致视频延时太大，调用此接口清解码器中数据
HI_VOID PRV_ReStarVdec(VDEC_CHN VdecChn)
{
	CHECK(HI_MPI_VDEC_StopRecvStream(VdecChn));//销毁通道前先停止接收数据

	CHECK(HI_MPI_VDEC_ResetChn(VdecChn)); //解码器复位 
	
	CHECK(HI_MPI_VDEC_StartRecvStream(VdecChn)); 	
}

/********************************************************
函数名:PRV_StartAdec
功     能:创建音频解码通道
参     数:[in]AdecEntype 指定解码类型
		   [in]AdecChn 音频解码通道号
返回值:  0成功
		    -1失败

*********************************************************/
HI_S32 PRV_StartAdec(PAYLOAD_TYPE_E AdecEntype, ADEC_CHN AdecChn)
{
	ADEC_CHN_ATTR_S stAdecAttr;
	stAdecAttr.enType = AdecEntype;
	
	if(AdecEntype == PT_PCMA)
		stAdecAttr.enType = PT_G711A;
	else if(AdecEntype == PT_PCMU)
		stAdecAttr.enType = PT_G711U;
	
	stAdecAttr.u32BufSize = 20;
	stAdecAttr.enMode = ADEC_MODE_PACK;/* propose use pack mode in your app */
	switch(stAdecAttr.enType)
	{
		case PT_ADPCMA:
		{
			ADEC_ATTR_ADPCM_S stAdpcm;
			stAdecAttr.pValue = &stAdpcm;
			stAdpcm.enADPCMType = AUDIO_ADPCM_TYPE ;
		}
			break;
		case PT_G711A:
		case PT_G711U:
		{
			ADEC_ATTR_G711_S stAdecG711;
			stAdecAttr.pValue = &stAdecG711;		
		}
			break;
		case PT_G726:
		{
			ADEC_ATTR_G726_S stAdecG726;
			stAdecAttr.pValue = &stAdecG726;
			stAdecG726.enG726bps = G726_BPS ;      
		}
			break;
#if defined(SN9234H1)
		case PT_AMR:
		{
			ADEC_ATTR_AMR_S stAdecAmr;
			stAdecAttr.pValue = &stAdecAmr;
			stAdecAmr.enFormat = AMR_FORMAT;
		}
			break;
		case PT_AAC:
		{
			ADEC_ATTR_AAC_S stAdecAac;
			stAdecAttr.pValue = &stAdecAac;
			stAdecAttr.enMode = ADEC_MODE_STREAM;
		}
			break;
#endif
		case PT_LPCM:
		{
			ADEC_ATTR_LPCM_S stAdecLpcm;
			stAdecAttr.pValue = &stAdecLpcm;
			stAdecAttr.enMode = ADEC_MODE_PACK;
		}
			break;
		default:
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "invalid aenc payload type: %d\n", stAdecAttr.enType);
			return HI_FAILURE;
		} 
	}
	CHECK(HI_MPI_ADEC_CreateChn(AdecChn, &stAdecAttr));
	

	return HI_SUCCESS;

}

/********************************************************
函数名:PRV_StartAdecAo
功     能:开启音频解码通道
参     数:[in]AdecEntype 指定解码类型
		   [in]AdecChn 音频解码通道号
返回值:  0成功
		    -1失败

*********************************************************/
HI_S32 PRV_StartAdecAo(g_PRVVoChnInfo playInfo)
{
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif
	AO_CHN AoChn = 0;
	AIO_ATTR_S stAioAttr;
	PAYLOAD_TYPE_E AdecEntype;
	AdecEntype = playInfo.AudioInfo.adoType;
	//AdecEntype = PT_G711A;
	CHECK(HI_MPI_AO_DisableChn(AoDev, AoChn));	
    CHECK(HI_MPI_AO_Disable(AoDev));

	stAioAttr.enWorkmode = I2S_WORK_MODE;      /* 参数设置 */
	stAioAttr.u32ChnCnt = 2;
	//stAioAttr.enBitwidth = playInfo.AudioInfo.bitwide;
	stAioAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	//stAioAttr.enSamplerate = playInfo.AudioInfo.samrate;	
	stAioAttr.enSamplerate = AUDIO_SAMPLE_RATE_8000;
	//stAioAttr.enSoundmode = AUDIO_SOUND_MODE_MOMO;
	stAioAttr.enSoundmode = playInfo.AudioInfo.soundchannel;
	stAioAttr.u32EXFlag = 1;
	stAioAttr.u32FrmNum = 20;
	
	if(playInfo.AudioInfo.PtNumPerFrm == 160 || playInfo.AudioInfo.PtNumPerFrm == 320)
	{
		stAioAttr.u32PtNumPerFrm = playInfo.AudioInfo.PtNumPerFrm;
	}
	else
	{
		stAioAttr.u32PtNumPerFrm = 320;
		playInfo.AudioInfo.PtNumPerFrm = 320;
	}
	//stAioAttr.u32PtNumPerFrm = 160;
	stAioAttr.u32ClkSel = 0;

	CHECK(HI_MPI_AO_SetPubAttr(AoDev, &stAioAttr));
	
	CHECK(HI_MPI_AO_Enable(AoDev));

	CHECK(HI_MPI_AO_EnableChn(AoDev, AoChn));

	CHECK(PRV_StartAdec(AdecEntype, DecAdec));

	//CHECK_RET(PRV_StartAdec(PT_G711A, DecAdec));
	
#if defined(SN9234H1)
	CHECK(HI_MPI_AO_BindAdec(AoDev, AoChn, DecAdec));
#else	
	if(IsAdecBindAo == 0)
	{
		CHECK(PRV_AUDIO_AoBindAdec(AoDev, AoChn, DecAdec));
		IsAdecBindAo = 1;
	}
	
#endif	
	PtNumPerFrm = playInfo.AudioInfo.PtNumPerFrm;
	Prv_Audiobuf.length = 0;
	return HI_SUCCESS;

}

/********************************************************
函数名:PRV_StopAdec
功     能:停止音频解码通道
参     数:[in]AdecChn 音频解码通道号
返回值:  0成功
		    -1失败

*********************************************************/
HI_S32 PRV_StopAdec()
{	
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif
	AO_CHN AoChn = 0;
	
	//CHECK_RET(HI_MPI_ADEC_ClearChnBuf(DecAdec));

	CHECK(HI_MPI_AO_ClearChnBuf(AoDev, AoChn));
#if defined(SN9234H1)
	CHECK(HI_MPI_AO_UnBindAdec(AoDev, AoChn, DecAdec));
#else	
	if(IsAdecBindAo == 1)
	{
		CHECK(PRV_AUDIO_AoUnbindAdec(AoDev, AoChn, DecAdec));
		IsAdecBindAo = 0;
	}
#endif		
	CHECK(HI_MPI_AO_DisableChn(AoDev, AoChn));
	
    CHECK(HI_MPI_AO_Disable(AoDev));
	
	CHECK(HI_MPI_ADEC_DestroyChn(DecAdec));
	//PtNumPerFrm = 0;
	Prv_Audiobuf.length = 0;
	return HI_SUCCESS;
}


//初始化每个通道的通道信息
void PRV_InitVochnInfo(int chn)
{
	int i = 0;
	if(chn < 0 || chn >= DEV_CHANNEL_NUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_InitVochnInfo---------invalid channel: %d!!line: %d\n", chn, __LINE__);
		return;
	}
	if(chn < LOCALVEDIONUM)//本地模拟通道
	{
		VochnInfo[chn].IsLocalVideo = 1;
		VochnInfo[chn].VdecChn = -1;
		if(chn >= DEV_CHANNEL_NUM/PRV_CHIP_NUM)
			VochnInfo[chn].SlaveId = PRV_SLAVE_1;
		else
			VochnInfo[chn].SlaveId = PRV_MASTER;			
	}
	else//数字通道
	{
		VochnInfo[chn].IsLocalVideo = 0;
		if(SCM_ChnConfigState(VochnInfo[chn].VoChn) == 0)
		{
			VochnInfo[chn].VdecChn = NoConfig_VdecChn;	
		}
		else
		{
			if(1)
			{
				VochnInfo[chn].VdecChn = DetVLoss_VdecChn;	//数字通道，初始化默认"未配置"或"无网络视频"
			}
		}
		//被动解码和点位控制下，无连接的通道默认都是黑
		if(PRV_CurDecodeMode == PassiveDecode || ScmGetListCtlState() == 1)
		{
			VochnInfo[chn].VdecChn = NoConfig_VdecChn;
		}
		VochnInfo[chn].VdecChn = NoConfig_VdecChn;
		VochnInfo[chn].SlaveId = PRV_MASTER;
	}
	
	VochnInfo[chn].VoChn = chn;
	VochnInfo[chn].CurChnIndex = VochnInfo[chn].VoChn - LOCALVEDIONUM;
	VochnInfo[chn].VideoInfo.vdoType= H264ENC;
	VochnInfo[chn].VideoInfo.framerate = 0;
	VochnInfo[chn].VideoInfo.height= 0;
	VochnInfo[chn].VideoInfo.width = 0;
	VochnInfo[chn].AudioInfo.adoType = -1;
	VochnInfo[chn].AudioInfo.samrate = 0;
	VochnInfo[chn].AudioInfo.soundchannel = 0;
	VochnInfo[chn].AudioInfo.PtNumPerFrm = 160;
	//	VochnInfo[chn].AudioInfo.bitwide = 0;
	for(i = 0; i < PRV_VO_DEV_NUM; i++)
		VochnInfo[chn].IsBindVdec[i] = -1;

	VochnInfo[chn].IsHaveVdec = 0;
	VochnInfo[chn].IsConnect = 0;
	VochnInfo[chn].PrvType = 0;		
	VochnInfo[chn].bIsStopGetVideoData= 0;
	VochnInfo[chn].VdecCap = 0;
	VochnInfo[chn].bIsWaitIFrame = 0;
	VochnInfo[chn].bIsDouble = 0;
	VochnInfo[chn].bIsWaitGetIFrame = 0;
	VochnInfo[chn].MccCreateingVdec = 0;
	VochnInfo[chn].MccReCreateingVdec = 0;
	VochnInfo[chn].IsChooseSlaveId = 0;
	VochnInfo[chn].IsDiscard = 0;
	VochnInfo[chn].bIsPBStat = 0;
}

//初始化发送通道信息
void PRV_SendInfoInit(int chn)
{
	if(chn < 0 || chn >= MAX_IPC_CHNNUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_SendInfoInit---------invalid channel: %d!!line: %d\n", chn, __LINE__);
		return;
	}	
	SendChnInfo.pre_frame[chn] = 0;
	SendChnInfo.is_same[chn] = 1;
	SendChnInfo.is_first[chn] = 1;
	SendChnInfo.is_disgard[chn] = 0;
	SendChnInfo.TotalLength1[chn] = 0;
	SendChnInfo.VideoDataStream1[chn] = NULL;
	SendChnInfo.VideoDataStream2[chn] = NULL;
	
}

//初始化每个通道数据的时间戳信息
void PRV_PtsInfoInit(int chn)
{
	if(chn < 0 || chn >= MAX_IPC_CHNNUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_PtsInfoInit---------invalid channel: %d!!line: %d\n", chn, __LINE__);
		return;
	}	
	SN_MEMSET(&PtsInfo[chn], 0, sizeof(g_PRVPtsinfo));
	PtsInfo[chn].IntervalPts = PalDec_IntervalPts;
	PtsInfo[chn].FirstVideoPts = (~0x0);
	PtsInfo[chn].FirstAudioPts = (~0x0);
}

void PRV_VoChnStateInit(int chn)
{
	if(chn < 0 || chn >= MAX_IPC_CHNNUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_PtsInfoInit---------invalid channel: %d!!line: %d\n", chn, __LINE__);
		return;
	}	
	VoChnState.FirstHaveVideoData[chn] = 0;
	VoChnState.FirstHaveAudioData[chn] = 0;
	VoChnState.FirstGetData[chn] = 0;
	VoChnState.IsGetFirstData[chn] = 0;
	VoChnState.IsStopGetVideoData[chn] = 0;
	VoChnState.BeginSendData[chn] = 0;	
	VoChnState.bIsPBStat_StopWriteData[chn] = 0;
	VoChnState.bIsPBStat_BeyondCap[chn] = 0;
	VoChnState.VideoDataCount[chn] = 0;
	VoChnState.AudioDataCount[chn] = 0;
	VoChnState.SendAudioDataCount[chn] = 0;
	VoChnState.VideoDataTimeLag[chn] = 0;
	VoChnState.AudioDataTimeLag[chn] = 0;
}

void PRV_PBStateInfoInit(int chn)
{	
	SN_MEMSET(&PlayStateInfo[chn], 0, sizeof(g_ChnPlayStateInfo));	
	PlayStateInfo[chn].CurPlayState = DEC_STATE_EXIT;
	PlayStateInfo[chn].CurSpeedState = DEC_SPEED_NORMAL;		
	PlayStateInfo[chn].SendDataType = TYPE_NORMAL;	
	PlayStateInfo[chn].RealType = DEC_TYPE_REAL; 
	PlayStateInfo[chn].SynState = SYN_NOPLAY; 	
}
void PRV_PBPlayInfoInit()
{   
	SN_MEMSET(&PlayInfo, 0, sizeof(PlayInfo));
	PlayInfo.PlayBackState = PLAY_EXIT;
}


/********************************************************
函数名:PRV_DECInfoInit
功     能:初始化解码线程需要使用到的几个全局变量
参     数:无
返回值:  无

*********************************************************/
void PRV_DECInfoInit()
{
	HI_S32 i = 0;

	for(i = 0; i < DEV_CHANNEL_NUM; i++)
		PRV_InitVochnInfo(i);
	
	for(i = 0; i < MAX_IPC_CHNNUM; i++)
	{
		PRV_PtsInfoInit(i);
		PRV_VoChnStateInit(i);
		PRV_SendInfoInit(i);
	}
	for(i = 0; i < DEV_CHANNEL_NUM; i++)
	{
		SlaveState.SlaveCreatingVdec[i] = 0;
		SlaveState.SlaveIsDesingVdec[i] = 0;
		PRV_PBStateInfoInit(i);
	}
	PRV_PBPlayInfoInit();
}

//根据分辨率大小计算此分辨率的性能(相对D1)
//每个芯片最大支持同时解8个D1
HI_S32 PRV_ComPareToD1(HI_S32 Width, HI_S32 Height)
{
	HI_S32 MulD1 = -1, TmpPixel = 0;
	TmpPixel = Width * Height;
	
	if(TmpPixel <= QCIF)
	{
		MulD1 = QCIF;
	}
	else if(TmpPixel <= CIF)
	{
		MulD1 = CIF;
	}
	else if(TmpPixel <= D1)
	{
		MulD1 = D1;
	}
	else if(TmpPixel <= HIGH_SEVEN || TmpPixel == UXGA)
	{
		MulD1 = HIGH_SEVEN;
	}
	else if(TmpPixel <= HIGH_NINE)
	{
		MulD1 = HIGH_NINE;
	}
	else if(TmpPixel <= HIGH_TEN)
	{
		MulD1 = HIGH_TEN;
	}
	else
	{
		MulD1 = MAX_8D1;
	}
	
	return MulD1;
	

}

HI_S32 PRV_ComPare(HI_S32 TmpPixel)
{
	HI_S32 MulD1 = -1;
	
	if(TmpPixel <= QCIF)
	{
		MulD1 = QCIF;
	}
	else if(TmpPixel <= CIF)
	{
		MulD1 = CIF;
	}
	else if(TmpPixel <= D1)
	{
		MulD1 = D1;
	}
	else if(TmpPixel <= HIGH_SEVEN || TmpPixel == UXGA)
	{
		MulD1 = HIGH_SEVEN;
	}
	else if(TmpPixel <= MAX_3D1)
	{
		MulD1 = MAX_3D1;
	}
	else if(TmpPixel <= HIGH_NINE)
	{
		MulD1 = HIGH_NINE;
	}
	else if(TmpPixel <= HIGH_TEN)
	{
		MulD1 = HIGH_TEN;
	}
	else
	{
		MulD1 = MAX_8D1;
	}
	
	return MulD1;
	

}



/********************************************************
函数名:PRV_SetVochnInfo
功     能:根据获取的SDP信息设置解码通道的状态信息结构体
参     数:[out] 解码通道的状态信息结构体
		   [in]SDP信息
返回值:  0成功
		    -1失败

*********************************************************/
HI_S32 PRV_SetVochnInfo(g_PRVVoChnInfo *chnInfo, RTSP_C_SDPInfo *RTSP_SDP)
{
	chnInfo->VideoInfo.vdoType = RTSP_SDP->vdoType;
	chnInfo->VideoInfo.framerate = RTSP_SDP->framerate;
	
	chnInfo->AudioInfo.adoType = RTSP_SDP->adoType;
	//chnInfo->AudioInfo.bitwide = RTSP_SDP->bitwide;
	chnInfo->AudioInfo.samrate = RTSP_SDP->samrate;
	chnInfo->AudioInfo.soundchannel = RTSP_SDP->soundchannel;

	if(chnInfo->VideoInfo.height == 0 || chnInfo->VideoInfo.width == 0)
	{
		chnInfo->VideoInfo.height = RTSP_SDP->high == 0 ? 702 : RTSP_SDP->high;
		chnInfo->VideoInfo.width= RTSP_SDP->width == 0 ? 576 : RTSP_SDP->width;
	}
	
	//chnInfo->VdecCap = MulQcif;
#if(IS_DECODER_DEVTYPE == 0)
	HI_S32 MulD1 = PRV_ComPareToD1(RTSP_SDP->high, RTSP_SDP->width);
	if(MulD1 <= 0)
		return HI_FAILURE;
		
	chnInfo->VideoInfo.height = RTSP_SDP->high == 0 ? 702 : RTSP_SDP->high;
	chnInfo->VideoInfo.width= RTSP_SDP->width == 0 ? 576 : RTSP_SDP->width;
	
	if(RTSP_SDP->high <= 64 && RTSP_SDP->high > 0)
		chnInfo->VideoInfo.height = 64;
	
	if(RTSP_SDP->width <= 64 && RTSP_SDP->width > 0)
		chnInfo->VideoInfo.width = 64;
	//chnInfo->VdecCap = (RTSP_SDP->high * RTSP_SDP->width);
	chnInfo->VdecCap = MulD1;
	//如果某一通道性能比单片总性能大，则丢弃此通道
	if(chnInfo->VdecCap  > TOTALCAPPCHIP)
	{	
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "----------Discard the channel: VdecCap---%d\n", chnInfo->VdecCap);
		//chnInfo->CurChnIndex = -1;
		chnInfo->VdecCap = 0;
		return HI_FAILURE;
	}			
#endif
	return HI_SUCCESS;
}

/********************************************************
函数名:PRV_ChooseSlaveId
功     能:选择预览指定通道的IPC 视频的芯片(主片或从片)
参     数:[in] IPC通道
		   [out]
返回值:  0成功
		    -1失败

*********************************************************/
HI_S32 PRV_ChooseSlaveId(int chn, PRV_MccCreateVdecReq *SlaveCreateVdec)
{
	//性能合法性判断
	//if(VochnInfo[chn].VdecCap <= 0 || VochnInfo[chn].VdecCap > TOTALCAPPCHIP)
	//	return HI_FAILURE;
	
	//HI_S32 MulD1 = PRV_ComPareToD1(VochnInfo[chn].VideoInfo.height, VochnInfo[chn].VideoInfo.width);
	HI_S32 MulD1 = VochnInfo[chn].VideoInfo.height * VochnInfo[chn].VideoInfo.width;
	if(MulD1 <= 0)
		return HI_FAILURE;
	VochnInfo[chn].VdecCap = MulD1;
#if defined(SN9234H1)	
	CurCap = CurMasterCap + CurSlaveCap;
#else
	CurCap = CurMasterCap;
#endif
	CurCap += VochnInfo[chn].VdecCap;
	TRACE(SCI_TRACE_HIGH, MOD_PRV, "line:%d The current cap: %d, master cap: %d,  slave cap: %d, the new chn---%d, VdecCap: %d\n", __LINE__, CurCap, CurMasterCap, CurSlaveCap, chn, VochnInfo[chn].VdecCap);
#if 1
	//如果性能已超出总性能，对当前的通道不处理	
	if(CurCap > (TOTALCAPPCHIP * DEV_CHIP_NUM - LOCALVEDIONUM * 1))
	{
		TRACE(SCI_TRACE_HIGH, MOD_PRV,"The current capbility: %d is beyond the TOTALCAP, discard the newest channel---%d, VdecCap: %d, CurSlaveCap: %d, CurMasterCap: %d\n", CurCap, chn, VochnInfo[chn].VdecCap, CurSlaveCap, CurMasterCap);
		CurCap -= VochnInfo[chn].VdecCap;
		//VochnInfo[chn].CurChnIndex = -1;
		//VochnInfo[chn].VdecCap = 0;
		return HI_FAILURE;
	}
#endif	
	
#if 0	
	CurIPCCount++;
	if(CurIPCCount > MAX_IPC_CHNNUM)
	{
		//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "++++++++++++Link Too Many IPC!\n");	
		//return HI_FAILURE;
	}
	TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Choose master chip!\n");			
	VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
	VochnInfo[chn].SlaveId = PRV_MASTER;
	VochnInfo[chn].VoChn = chn; 
	//在此给这两个变量赋值是因为:如果当前在回放状态，有新的连接，暂时无法
	//创建解码通道，需要先保存信息，等退出回放后，会根据这些信息创建解码通道
	VochnInfo[chn].IsConnect = 1;
	VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;	
	
#else	

#if (DEV_TYPE == DEV_SN_9234_H_1)

#if 0
	if(CurMasterCap == 0 && DEV_CHIP_NUM > 1 && CurSlaveCap == 0) //还未存在通道时，优先主片
	{
		//优先放主片解码
		if((CurMasterCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP)
		{
			VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
			VochnInfo[chn].SlaveId = PRV_MASTER;
			VochnInfo[chn].VoChn = chn;	
			//在此给这两个变量赋值是因为:如果当前在回放状态，有新的连接，暂时无法
			//创建解码通道，需要先保存信息，等退出回放后，会根据这些信息创建解码通道
			VochnInfo[chn].IsConnect = 1;
			//VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;	
			SlaveCreateVdec->SlaveId = PRV_MASTER;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d Choose master chip!\n", __LINE__);			
		}	
		else
		{
			CurCap -= VochnInfo[chn].VdecCap;
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "line:%d The current cap: %d, master cap: %d,  slave cap: %d, the new chn---%d, VdecCap: %d\n", __LINE__, CurCap, CurMasterCap, CurSlaveCap, chn, VochnInfo[chn].VdecCap);
			//PRV_InitVochnInfo(chn);
			return HI_FAILURE;
			
		}
	}
	else if(CurMasterCap > 0 && DEV_CHIP_NUM > 1 && CurSlaveCap == 0) //主片已经有通道，再以从片优先
	{
		if(DEV_CHIP_NUM > 1 && CurSlaveCap <= TOTALCAPPCHIP && (CurSlaveCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP)
		{				
			VochnInfo[chn].SlaveId = PRV_SLAVE_1;
			VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
			VochnInfo[chn].VoChn = chn;
			VochnInfo[chn].IsConnect = 1;
			CurSlaveCap += VochnInfo[chn].VdecCap;
			CurSlaveChnCount++;
			//从片创建解码通道信息
			SlaveCreateVdec->SlaveId = PRV_SLAVE_1;	
			SlaveCreateVdec->s32StreamChnIDs = MasterToSlaveChnId;
			SlaveCreateVdec->EncType = VochnInfo[chn].VideoInfo.vdoType;
			SlaveCreateVdec->chn = VochnInfo[chn].CurChnIndex;
			SlaveCreateVdec->VoChn = VochnInfo[chn].VoChn;
			SlaveCreateVdec->VdecChn = VochnInfo[chn].VoChn;
			SlaveCreateVdec->VdecCap = VochnInfo[chn].VdecCap;	
			SlaveCreateVdec->height = VochnInfo[chn].VideoInfo.height;
			SlaveCreateVdec->width = VochnInfo[chn].VideoInfo.width;		
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d Choose Slave chip! chn: %d ,VdecCap: %d, CurSlaveCap: %d, CurSlaveChnCount: %d\n", __LINE__, chn, VochnInfo[chn].VdecCap, CurSlaveCap, CurSlaveChnCount);
		}
		else
		{
			CurCap -= VochnInfo[chn].VdecCap;
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "line:%d The current cap: %d, master cap: %d,  slave cap: %d, the new chn---%d, VdecCap: %d\n", __LINE__, CurCap, CurMasterCap, CurSlaveCap, chn, VochnInfo[chn].VdecCap);
			return HI_FAILURE;
			
		}
	}
	else
	{
		if(((CurMasterCap + VochnInfo[chn].VdecCap * 2) <= TOTALCAPPCHIP) && (DEV_CHIP_NUM > 1 && CurSlaveCap <= TOTALCAPPCHIP && (CurSlaveCap + VochnInfo[chn].VdecCap * 2) <= TOTALCAPPCHIP))
		{
			if(CurMasterCap < CurSlaveCap)
			{
				VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
				VochnInfo[chn].SlaveId = PRV_MASTER;
				VochnInfo[chn].VoChn = chn;	
				VochnInfo[chn].IsConnect = 1;
				SlaveCreateVdec->SlaveId = PRV_MASTER;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d Choose master chip!\n", __LINE__);	
			}
			else
			{
				VochnInfo[chn].SlaveId = PRV_SLAVE_1;
				VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
				VochnInfo[chn].VoChn = chn;
				VochnInfo[chn].IsConnect = 1;
				CurSlaveCap += VochnInfo[chn].VdecCap;
				CurSlaveChnCount++;
				//从片创建解码通道信息
				SlaveCreateVdec->SlaveId = PRV_SLAVE_1;	
				SlaveCreateVdec->s32StreamChnIDs = MasterToSlaveChnId;
				SlaveCreateVdec->EncType = VochnInfo[chn].VideoInfo.vdoType;
				SlaveCreateVdec->chn = VochnInfo[chn].CurChnIndex;
				SlaveCreateVdec->VoChn = VochnInfo[chn].VoChn;
				SlaveCreateVdec->VdecChn = VochnInfo[chn].VoChn;
				SlaveCreateVdec->VdecCap = VochnInfo[chn].VdecCap;	
				SlaveCreateVdec->height = VochnInfo[chn].VideoInfo.height;
				SlaveCreateVdec->width = VochnInfo[chn].VideoInfo.width;		
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d Choose Slave chip! chn: %d ,VdecCap: %d, CurSlaveCap: %d, CurSlaveChnCount: %d\n", __LINE__, chn, VochnInfo[chn].VdecCap, CurSlaveCap, CurSlaveChnCount);
			}
					
		}	
		else if(((CurMasterCap + VochnInfo[chn].VdecCap * 2) <= TOTALCAPPCHIP) || (DEV_CHIP_NUM > 1 && CurSlaveCap <= TOTALCAPPCHIP && (CurSlaveCap + VochnInfo[chn].VdecCap * 2) <= TOTALCAPPCHIP))
		{
			if((CurMasterCap + VochnInfo[chn].VdecCap * 2) <= TOTALCAPPCHIP)
			{
				VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
				VochnInfo[chn].SlaveId = PRV_MASTER;
				VochnInfo[chn].VoChn = chn;	
				VochnInfo[chn].IsConnect = 1;
				SlaveCreateVdec->SlaveId = PRV_MASTER;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d Choose master chip!\n", __LINE__);	
			}
			else
			{
				VochnInfo[chn].SlaveId = PRV_SLAVE_1;
				VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
				VochnInfo[chn].VoChn = chn;
				VochnInfo[chn].IsConnect = 1;
				CurSlaveCap += VochnInfo[chn].VdecCap;
				CurSlaveChnCount++;
				//从片创建解码通道信息
				SlaveCreateVdec->SlaveId = PRV_SLAVE_1;	
				SlaveCreateVdec->s32StreamChnIDs = MasterToSlaveChnId;
				SlaveCreateVdec->EncType = VochnInfo[chn].VideoInfo.vdoType;
				SlaveCreateVdec->chn = VochnInfo[chn].CurChnIndex;
				SlaveCreateVdec->VoChn = VochnInfo[chn].VoChn;
				SlaveCreateVdec->VdecChn = VochnInfo[chn].VoChn;
				SlaveCreateVdec->VdecCap = VochnInfo[chn].VdecCap;	
				SlaveCreateVdec->height = VochnInfo[chn].VideoInfo.height;
				SlaveCreateVdec->width = VochnInfo[chn].VideoInfo.width;		
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d Choose Slave chip! chn: %d ,VdecCap: %d, CurSlaveCap: %d, CurSlaveChnCount: %d\n", __LINE__, chn, VochnInfo[chn].VdecCap, CurSlaveCap, CurSlaveChnCount);
			}
		}
		else if((CurMasterCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP && DEV_CHIP_NUM > 1 && CurSlaveCap <= TOTALCAPPCHIP && (CurSlaveCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP)
		{
			if(CurMasterCap < CurSlaveCap)
			{
				VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
				VochnInfo[chn].SlaveId = PRV_MASTER;
				VochnInfo[chn].VoChn = chn;	
				VochnInfo[chn].IsConnect = 1;
				SlaveCreateVdec->SlaveId = PRV_MASTER;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d Choose master chip!\n", __LINE__);	
			}
			else
			{
				VochnInfo[chn].SlaveId = PRV_SLAVE_1;
				VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
				VochnInfo[chn].VoChn = chn;
				VochnInfo[chn].IsConnect = 1;
				CurSlaveCap += VochnInfo[chn].VdecCap;
				CurSlaveChnCount++;
				//从片创建解码通道信息
				SlaveCreateVdec->SlaveId = PRV_SLAVE_1;	
				SlaveCreateVdec->s32StreamChnIDs = MasterToSlaveChnId;
				SlaveCreateVdec->EncType = VochnInfo[chn].VideoInfo.vdoType;
				SlaveCreateVdec->chn = VochnInfo[chn].CurChnIndex;
				SlaveCreateVdec->VoChn = VochnInfo[chn].VoChn;
				SlaveCreateVdec->VdecChn = VochnInfo[chn].VoChn;
				SlaveCreateVdec->VdecCap = VochnInfo[chn].VdecCap;	
				SlaveCreateVdec->height = VochnInfo[chn].VideoInfo.height;
				SlaveCreateVdec->width = VochnInfo[chn].VideoInfo.width;		
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d Choose Slave chip! chn: %d ,VdecCap: %d, CurSlaveCap: %d, CurSlaveChnCount: %d\n", __LINE__, chn, VochnInfo[chn].VdecCap, CurSlaveCap, CurSlaveChnCount);
			}
		}
		else if(((CurMasterCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP) || (DEV_CHIP_NUM > 1 && CurSlaveCap <= TOTALCAPPCHIP && (CurSlaveCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP))
		{
			if((CurMasterCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP)
			{
				VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
				VochnInfo[chn].SlaveId = PRV_MASTER;
				VochnInfo[chn].VoChn = chn;	
				VochnInfo[chn].IsConnect = 1;
				SlaveCreateVdec->SlaveId = PRV_MASTER;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d Choose master chip!\n", __LINE__);	
			}
			else
			{
				VochnInfo[chn].SlaveId = PRV_SLAVE_1;
				VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
				VochnInfo[chn].VoChn = chn;
				VochnInfo[chn].IsConnect = 1;
				CurSlaveCap += VochnInfo[chn].VdecCap;
				CurSlaveChnCount++;
				//从片创建解码通道信息
				SlaveCreateVdec->SlaveId = PRV_SLAVE_1;	
				SlaveCreateVdec->s32StreamChnIDs = MasterToSlaveChnId;
				SlaveCreateVdec->EncType = VochnInfo[chn].VideoInfo.vdoType;
				SlaveCreateVdec->chn = VochnInfo[chn].CurChnIndex;
				SlaveCreateVdec->VoChn = VochnInfo[chn].VoChn;
				SlaveCreateVdec->VdecChn = VochnInfo[chn].VoChn;
				SlaveCreateVdec->VdecCap = VochnInfo[chn].VdecCap;	
				SlaveCreateVdec->height = VochnInfo[chn].VideoInfo.height;
				SlaveCreateVdec->width = VochnInfo[chn].VideoInfo.width;		
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d Choose Slave chip! chn: %d ,VdecCap: %d, CurSlaveCap: %d, CurSlaveChnCount: %d\n", __LINE__, chn, VochnInfo[chn].VdecCap, CurSlaveCap, CurSlaveChnCount);
			}
		}
		else
		{
			CurCap -= VochnInfo[chn].VdecCap;
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "line:%d The current cap: %d, master cap: %d,  slave cap: %d, the new chn---%d, VdecCap: %d\n", __LINE__, CurCap, CurMasterCap, CurSlaveCap, chn, VochnInfo[chn].VdecCap);
			return HI_FAILURE;
		}
	}
	
#else
	//优先放主片解码
	if(((CurMasterCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP) && (DEV_CHIP_NUM > 1 && CurSlaveCap <= TOTALCAPPCHIP && (CurSlaveCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP))
	{
		if(CurMasterCap <= CurSlaveCap)
		{
			VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
			VochnInfo[chn].SlaveId = PRV_MASTER;
			VochnInfo[chn].VoChn = chn;	
			//在此给这两个变量赋值是因为:如果当前在回放状态，有新的连接，暂时无法
			//创建解码通道，需要先保存信息，等退出回放后，会根据这些信息创建解码通道
			VochnInfo[chn].IsConnect = 1;
			//VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;	
			SlaveCreateVdec->SlaveId = PRV_MASTER;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Choose master chip!\n");
		}
		else
		{
			if(VochnInfo[chn].bIsPBStat)
			{
				CurCap -= VochnInfo[chn].VdecCap;
				TRACE(SCI_TRACE_HIGH, MOD_PRV, "line:%d The current cap: %d, master cap: %d,  slave cap: %d, the new chn---%d, VdecCap: %d\n", __LINE__, CurCap, CurMasterCap, CurSlaveCap, chn, VochnInfo[chn].VdecCap);
				return HI_FAILURE;
			}
			VochnInfo[chn].SlaveId = PRV_SLAVE_1;
			VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
			VochnInfo[chn].VoChn = chn;
			//VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;
			VochnInfo[chn].IsConnect = 1;
			CurSlaveCap += VochnInfo[chn].VdecCap;
			CurSlaveChnCount++;
			//从片创建解码通道信息
			//SlaveCreateVdec->SlaveId = VochnInfo[chn].SlaveId;	
			SlaveCreateVdec->SlaveId = PRV_SLAVE_1;	
			SlaveCreateVdec->s32StreamChnIDs = MasterToSlaveChnId;
			SlaveCreateVdec->EncType = VochnInfo[chn].VideoInfo.vdoType;
			SlaveCreateVdec->chn = VochnInfo[chn].CurChnIndex;
			SlaveCreateVdec->VoChn = VochnInfo[chn].VoChn;
			SlaveCreateVdec->VdecChn = VochnInfo[chn].VoChn;
			SlaveCreateVdec->VdecCap = VochnInfo[chn].VdecCap;	
			SlaveCreateVdec->height = VochnInfo[chn].VideoInfo.height;
			SlaveCreateVdec->width = VochnInfo[chn].VideoInfo.width;		
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Choose Slave chip! chn: %d ,VdecCap: %d, CurSlaveCap: %d, CurSlaveChnCount: %d\n", chn, VochnInfo[chn].VdecCap, CurSlaveCap, CurSlaveChnCount);
		}
	}
	//优先放主片解码
	else if((CurMasterCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP)
	{
		VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
		VochnInfo[chn].SlaveId = PRV_MASTER;
		VochnInfo[chn].VoChn = chn;	
		//在此给这两个变量赋值是因为:如果当前在回放状态，有新的连接，暂时无法
		//创建解码通道，需要先保存信息，等退出回放后，会根据这些信息创建解码通道
		VochnInfo[chn].IsConnect = 1;
		//VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;	
		SlaveCreateVdec->SlaveId = PRV_MASTER;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Choose master chip!\n");			
	}	
	else if(DEV_CHIP_NUM > 1 && CurSlaveCap <= TOTALCAPPCHIP && (CurSlaveCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP
		/*&& (CurSlaveChnCount < DEV_CHANNEL_NUM/DEV_CHIP_NUM
		|| (CurSlaveChnCount >= DEV_CHANNEL_NUM/DEV_CHIP_NUM && CurMasterCap > TOTALCAPPCHIP))*/)
	{				
		if(VochnInfo[chn].bIsPBStat)
		{
			CurCap -= VochnInfo[chn].VdecCap;
			TRACE(SCI_TRACE_HIGH, MOD_PRV, "line:%d The current cap: %d, master cap: %d,  slave cap: %d, the new chn---%d, VdecCap: %d\n", __LINE__, CurCap, CurMasterCap, CurSlaveCap, chn, VochnInfo[chn].VdecCap);
			return HI_FAILURE;
		}
		VochnInfo[chn].SlaveId = PRV_SLAVE_1;
		VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
		VochnInfo[chn].VoChn = chn;
		//VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;
		VochnInfo[chn].IsConnect = 1;
		CurSlaveCap += VochnInfo[chn].VdecCap;
		CurSlaveChnCount++;
		//从片创建解码通道信息
		//SlaveCreateVdec->SlaveId = VochnInfo[chn].SlaveId;	
		SlaveCreateVdec->SlaveId = PRV_SLAVE_1;	
		SlaveCreateVdec->s32StreamChnIDs = MasterToSlaveChnId;
		SlaveCreateVdec->EncType = VochnInfo[chn].VideoInfo.vdoType;
		SlaveCreateVdec->chn = VochnInfo[chn].CurChnIndex;
		SlaveCreateVdec->VoChn = VochnInfo[chn].VoChn;
		SlaveCreateVdec->VdecChn = VochnInfo[chn].VoChn;
		SlaveCreateVdec->VdecCap = VochnInfo[chn].VdecCap;	
		SlaveCreateVdec->height = VochnInfo[chn].VideoInfo.height;
		SlaveCreateVdec->width = VochnInfo[chn].VideoInfo.width;		
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Choose Slave chip! chn: %d ,VdecCap: %d, CurSlaveCap: %d, CurSlaveChnCount: %d\n", chn, VochnInfo[chn].VdecCap, CurSlaveCap, CurSlaveChnCount);
	}
	else
	{
		CurCap -= VochnInfo[chn].VdecCap;
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "line:%d The current cap: %d, master cap: %d,  slave cap: %d, the new chn---%d, VdecCap: %d\n", __LINE__, CurCap, CurMasterCap, CurSlaveCap, chn, VochnInfo[chn].VdecCap);
		//PRV_InitVochnInfo(chn);
		return HI_FAILURE;
		
	}
#endif
	
#elif defined(Hi3531)||defined(Hi3535)//3531解码器不包含从片
	CurSlaveCap = TOTALCAPPCHIP;


	//优先放从片解码
	//从片性能允许的情况下:
	//从片预览通道数未达到8时(一半)，放从片解码，防止太多预览通道在从片
	//从片预览通道数达到8是，放主片解码，直到主片性能不够，而从片还有剩余性能时，放从片解码
	if(DEV_CHIP_NUM > 1 && CurSlaveCap <= TOTALCAPPCHIP && (CurSlaveCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP
		&& (CurSlaveChnCount < DEV_CHANNEL_NUM/DEV_CHIP_NUM
		|| (CurSlaveChnCount >= DEV_CHANNEL_NUM/DEV_CHIP_NUM && CurMasterCap > TOTALCAPPCHIP)))
	{				
		VochnInfo[chn].SlaveId = PRV_SLAVE_1;
		VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
		VochnInfo[chn].VoChn = chn;
		//VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;
		VochnInfo[chn].IsConnect = 1;
		CurSlaveCap += VochnInfo[chn].VdecCap;
		CurSlaveChnCount++;
		//从片创建解码通道信息
		//SlaveCreateVdec->SlaveId = VochnInfo[chn].SlaveId;	
		SlaveCreateVdec->SlaveId = PRV_SLAVE_1;	
		SlaveCreateVdec->s32StreamChnIDs = MasterToSlaveChnId;
		SlaveCreateVdec->EncType = VochnInfo[chn].VideoInfo.vdoType;
		SlaveCreateVdec->chn = VochnInfo[chn].CurChnIndex;
		SlaveCreateVdec->VoChn = VochnInfo[chn].VoChn;
		SlaveCreateVdec->VdecChn = VochnInfo[chn].VoChn;
		SlaveCreateVdec->VdecCap = VochnInfo[chn].VdecCap;	
		SlaveCreateVdec->height = VochnInfo[chn].VideoInfo.height;
		SlaveCreateVdec->width = VochnInfo[chn].VideoInfo.width;		
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Choose Slave chip! chn: %d ,VdecCap: %d, CurSlaveCap: %d, CurSlaveChnCount: %d\n", chn, VochnInfo[chn].VdecCap, CurSlaveCap, CurSlaveChnCount);
	}
	else if((CurMasterCap + VochnInfo[chn].VdecCap) <= TOTALCAPPCHIP)
	{
		VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
		VochnInfo[chn].SlaveId = PRV_MASTER;
		VochnInfo[chn].VoChn = chn;	
		//在此给这两个变量赋值是因为:如果当前在回放状态，有新的连接，暂时无法
		//创建解码通道，需要先保存信息，等退出回放后，会根据这些信息创建解码通道
		VochnInfo[chn].IsConnect = 1;
		//VochnInfo[chn].VdecChn = VochnInfo[chn].VoChn;	
		SlaveCreateVdec->SlaveId = PRV_MASTER;
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Choose master chip!\n");			
	}		
	else
	{
		CurCap -= VochnInfo[chn].VdecCap;
		TRACE(SCI_TRACE_HIGH, MOD_PRV, "The current cap: %d, master cap: %d,  slave cap: %d, the new chn---%d, VdecCap: %d\n", CurCap, CurMasterCap, CurSlaveCap, chn, VochnInfo[chn].VdecCap);
		//PRV_InitVochnInfo(chn);
		return HI_FAILURE;
		
	}
#endif	
#endif
	return HI_SUCCESS;
}


//获取主从片数据传输的PCI通道	
void PRV_InitHostToSlaveStream()
{
	MasterToSlaveChnId = HostInitHostToSlaveStream(MOD_PRV, 1, PCIV_VDEC_STREAM_BUF_LEN);
	if (MasterToSlaveChnId <= 0)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------HostInitHostToSlaveStream error---0x%x\n", MasterToSlaveChnId);																																																																																																																																																																																																																																																								  
		MasterToSlaveChnId = 0;
	}
	else
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV,"-----HostInitHostToSlaveStream success---%d\n",  MasterToSlaveChnId);
	}
}


//检查获取的数据信息是否合法
HI_S32 PRV_CheckDataPara(AVPacket *DataFromRTSP)
{
	HI_S32 chn = 0;
	chn = DataFromRTSP->channel;
	if(chn < LOCALVEDIONUM || chn >= DEV_CHANNEL_NUM)
	{
		if(chn >= (DEV_CHANNEL_NUM * 4))
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------Invalid channel: %d, line: %d\n", chn, __LINE__);
		
		return HI_FAILURE;
	}
	
	if(DataFromRTSP->codec_id != CODEC_ID_H264 && DataFromRTSP->codec_id != CODEC_ID_PCMA && DataFromRTSP->codec_id != CODEC_ID_PCMU)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "-------Invalid data type: %d, chn: %d\n", DataFromRTSP->codec_id, chn);
		return HI_FAILURE;
	}
	
	return HI_SUCCESS;

}

//检测对应通道数据分辨率是否改变，改变时需要重新创建解码器
//返回值:HI_FAILURE:主片创建失败
//			1:选择从片创建
//			HI_SUCCESS:成功
int PRV_DetectResIsChg(int chn, AVPacket *DataStream)
{
	HI_S32 s32Ret = 0, s32Ret1 = 0, s32Ret2 = 0, index = 0,  new_width = 0, new_height = 0, NewVdecCap = 0; 
	HI_S32 aaaa = 0;
	PRV_MccCreateVdecReq	SlaveCreateVdecReq;
	PRV_MccReCreateVdecReq SlaveReCreateVdecReq;
	PlayBack_MccQueryStateReq QueryReq;
	RTSP_C_SDPInfo RTSP_SDP;
	if(DataStream == NULL)
	{
		return HI_FAILURE;
	}
	index = chn + LOCALVEDIONUM;
	//printf("===============PRV_DetectResIsChg===============11111111111111\n");
	s32Ret1 = PRV_SearchSPS(chn, DataStream, &new_width, &new_height);
	//aaaa = abs((DataStream->pts - PtsInfo[chn].PreVideoPts) * 1000/90000);
	//printf("==================aaaa: %d, DataStream->pts: %lld, PtsInfo[chn].PreVideoPts: %lld\n", aaaa, DataStream->pts, PtsInfo[chn].PreVideoPts);
	s32Ret2 = (aaaa > 8 * 1000) ? HI_FAILURE : HI_SUCCESS;
	if(s32Ret1 != HI_SUCCESS || s32Ret2 == HI_FAILURE)
		s32Ret = HI_FAILURE;
		
	TRACE(SCI_TRACE_NORMAL, MOD_DEC, "chn=%d, s32Ret: %d, s32Ret1: %d, s32Ret2: %d, IsHaveVdec: %d, bIsResetFile: %d, bIsPBStat: %d\n", chn, s32Ret, s32Ret1, s32Ret2, VochnInfo[index].IsHaveVdec, PlayStateInfo[index].bIsResetFile, VochnInfo[index].bIsPBStat);
	//回放下，切换文件时，原数据已经解完，需要重新获取新文件SDP信息
	if(s32Ret == HI_FAILURE && VochnInfo[index].bIsPBStat && PlayStateInfo[index].bIsResetFile)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===============PRV_DetectResIsChg===============4444444444444\n");
		if(FTPC_C_getParam(chn, &RTSP_SDP) != 0)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_DetectResIsChg==========chn: %d, RTSP_C_getParam(chn, &RTSP_SDP) fail!!!=========\n", chn);		
			VochnInfo[index].bIsWaitIFrame = 1; 
			return HI_FAILURE;	
		}
		else
		{
			PRV_SetVochnInfo(&(VochnInfo[index]), &RTSP_SDP);
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "------width: %d, high: %d\n", RTSP_SDP.width, RTSP_SDP.high);
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "------FTPC_C_getParam() Success---vdoType: %d, adoType: %d, framerate: %d\n",
											VochnInfo[index].VideoInfo.vdoType, VochnInfo[index].AudioInfo.adoType, VochnInfo[index].VideoInfo.framerate);			
			//可能收到数据的之前，Client链路已经断掉(切换比较快时)
			if(VochnInfo[index].VideoInfo.vdoType == 0)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_DetectResIsChg==========chn: %d, Allready Disconnect!\n",chn);			
				//VochnInfo[index].bIsWaitIFrame = 1;
				//return HI_FAILURE;	
				VochnInfo[index].VideoInfo.vdoType = PT_H264;
			}
		}
	}
	
	if(VochnInfo[index].IsHaveVdec && s32Ret == HI_FAILURE)//分辨率改变
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==================66666666666666666666666========\n");
		//	回放状态下，分辨率变化需要重建解码器，需要等待原数据解完
		if(VochnInfo[index].bIsPBStat && !PlayStateInfo[index].bIsResetFile)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===============PRV_DetectResIsChg===============22222222222\n");
			if(VochnInfo[index].SlaveId > PRV_MASTER)
			{
				PlayStateInfo[chn].QuerySlaveId = 1;
				QueryReq.VoChn = chn;	
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "============Send MSG_ID_PRV_MCC_PBQUERYSTATE_REQ\n");
				SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_PBQUERYSTATE_REQ, &QueryReq, sizeof(QueryReq));
			}
			PlayStateInfo[index].bIsResetFile = 1;//切换文件标识
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d============Send: MSG_ID_FTPC_PB_CHNSTOPWRITE_IND---StopWrite: %d\n", __LINE__, ChnStopWriteInd.StopWrite);
			Ftpc_ChnStopWrite(chn, 1);					
			VoChnState.bIsPBStat_StopWriteData[chn] = 1;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d============bIsPBStat_StopWriteData---StopWrite==1\n", __LINE__);
			return 2;
		}
		
		//按设备回放，原数据已解完，发送新文件数据时，需要重新设置
		if(PlayInfo.PlayBackState > PLAY_INSTANT)
		{
			//回放时第一帧数据时间戳标0
			VochnInfo[index].VideoInfo.height = new_height;
			VochnInfo[index].VideoInfo.width = new_width;
			PtsInfo[chn].PreVideoPts = DataStream->pts;
			PtsInfo[chn].CurVideoPts = DataStream->pts;
			PtsInfo[chn].PreSendPts = 0;
			PtsInfo[chn].CurSendPts = 0;
			PtsInfo[chn].FirstAudioPts = 0;
			PtsInfo[chn].FirstVideoPts = 0;
			PtsInfo[chn].CurVoChnPts = 0;
			PtsInfo[chn].PreVoChnPts = 0;
			DataStream->prvpts = PtsInfo[chn].CurSendPts;
			//TODO:重新计算总性能
			PlayBack_AdaptRealType();
			//TODO:重建解码器
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===============PRV_DetectResIsChg===============3333333333VochnInfo[index].SlaveId: %d\n", VochnInfo[index].SlaveId );
			if(VochnInfo[index].SlaveId > PRV_MASTER)
			{
				PlayBack_MccPBReCreateVdecReq PBReCreateReq;
				PBReCreateReq.SlaveId = VochnInfo[index].SlaveId;
				PBReCreateReq.EncType = VochnInfo[index].VideoInfo.vdoType;
				PBReCreateReq.s32VdecHeight = VochnInfo[index].VideoInfo.height;
				PBReCreateReq.s32VdecWidth  = VochnInfo[index].VideoInfo.width;
				PBReCreateReq.VoChn = VochnInfo[index].VoChn;
				PBReCreateReq.VdecChn = VochnInfo[index].VdecChn;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d===============Send MSG_ID_PRV_MCC_PBRECREATEVDEC_REQ\n", __LINE__);
				SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_PBRECREATEVDEC_REQ, &PBReCreateReq, sizeof(PBReCreateReq));					
				VochnInfo[index].MccReCreateingVdec = 1;
				VochnInfo[index].IsHaveVdec = 0;
			}
			else
			{
				PlayBack_DestroyVdec(HD, VochnInfo[index].VdecChn);
				PlayBack_CreatVdec(HD, VochnInfo[index].VdecChn);
			}
			
		}
		else
		{
			VochnInfo[index].VideoInfo.height = new_height;
			VochnInfo[index].VideoInfo.width = new_width;
			
			//即时回放下，送数据时发现数据变化，再获取数据信息			
			if(VochnInfo[index].bIsPBStat)
			{
				if(FTPC_C_getParam(chn, &RTSP_SDP) != 0)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_DetectResIsChg==========chn: %d, RTSP_C_getParam(chn, &RTSP_SDP) fail!!!=========\n", chn);		
					VochnInfo[index].bIsWaitIFrame = 1; 
					return HI_FAILURE;	
				}				
				else
				{
					PRV_SetVochnInfo(&(VochnInfo[index]), &RTSP_SDP);
					PtsInfo[chn].PreVideoPts = DataStream->pts;
					PtsInfo[chn].CurVideoPts = DataStream->pts;
					PtsInfo[chn].PreSendPts = 0;
					PtsInfo[chn].CurSendPts = 0;
					PtsInfo[chn].FirstAudioPts = 0;
					PtsInfo[chn].FirstVideoPts = 0;
					PtsInfo[chn].CurVoChnPts = 0;
					PtsInfo[chn].PreVoChnPts = 0;
					VoChnState.VideoDataTimeLag[chn] = 0;
					VoChnState.AudioDataTimeLag[chn] = 0;
					VoChnState.FirstHaveVideoData[chn] = 0;
					VoChnState.FirstHaveAudioData[chn] = 0;
					DataStream->prvpts = PtsInfo[chn].CurSendPts;
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d------FTPC_C_getParam() Success---vdoType: %d, adoType: %d\n", __LINE__, 
													VochnInfo[index].VideoInfo.vdoType, VochnInfo[index].AudioInfo.adoType);			
					//可能收到数据的之前，Client链路已经断掉(切换比较快时)
					if(VochnInfo[index].VideoInfo.vdoType == 0)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_DetectResIsChg==========chn: %d, Allready Disconnect!\n",chn);			
						//VochnInfo[index].bIsWaitIFrame = 1;
						//return HI_FAILURE;	
						VochnInfo[index].VideoInfo.vdoType = PT_H264;
					}
				}
			
			}
			//NewVdecCap = PRV_ComPareToD1(new_width, new_height);
			NewVdecCap = new_width * new_height;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "SlaveId: %d, new_width=%d new_height=%d ReCreate Vdec: %d, OldVdecCap: %d, NewVdecCap: %d\n", VochnInfo[index].SlaveId, new_width, new_height, VochnInfo[index].VdecChn, VochnInfo[index].VdecCap, NewVdecCap);
			if(VochnInfo[index].SlaveId == PRV_MASTER)
			{
				//主片重建解码通道
				s32Ret = PRV_ReCreateVdecChn(chn, VochnInfo[index].VideoInfo.vdoType, new_height, new_width, VochnInfo[index].u32RefFrameNum, NewVdecCap);
				if(s32Ret == HI_FAILURE)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_ReCreateVdecChn == HI_FAILURE\n");
					return HI_FAILURE;								
				}
				//该通道重新选择放从片解码
				else if(s32Ret == 1)
				{
					return 1;
				}
			}
			else if(VochnInfo[index].SlaveId > PRV_MASTER)
			{
				//通知从片重建解码通道
				
				CurCap = CurCap - VochnInfo[index].VdecCap + NewVdecCap; 
				if(CurCap > (TOTALCAPPCHIP * DEV_CHIP_NUM - LOCALVEDIONUM * 1))
				{
					if(VochnInfo[index].bIsPBStat)
					{
						Prv_Chn_ChnPBOverFlow_Ind ChnPbOver;
						SN_MEMSET(&ChnPbOver, 0, sizeof(Prv_Chn_ChnPBOverFlow_Ind));
						ChnPbOver.Chn = chn;
						ChnPbOver.NewWidth = new_width;
						ChnPbOver.NewHeight = new_height;							
						SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_FWK, 0, 0, MSG_ID_PRV_CHNPBOVERFLOW_IND, &ChnPbOver, sizeof(Prv_Chn_ChnPBOverFlow_Ind));					
						VochnInfo[index].bIsWaitGetIFrame = 1;
					
						//return HI_FAILURE;
					}
				}
				SlaveReCreateVdecReq.SlaveId = VochnInfo[index].SlaveId;
				SlaveReCreateVdecReq.VoChn = VochnInfo[index].VoChn;
				SlaveReCreateVdecReq.VdecChn = VochnInfo[index].VdecChn;
				SlaveReCreateVdecReq.height = new_height;
				SlaveReCreateVdecReq.width = new_width;
				SlaveReCreateVdecReq.VdecCap = NewVdecCap;
				SlaveReCreateVdecReq.EncType = VochnInfo[index].VideoInfo.vdoType;
				VochnInfo[index].MccReCreateingVdec = 1;
				VochnInfo[index].IsHaveVdec = 0;
				CurSlaveCap = CurSlaveCap - VochnInfo[index].VdecCap + NewVdecCap;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Send: MSG_ID_PRV_MCC_RECREATEVDEC_REQ---%d\n", SlaveReCreateVdecReq.VdecChn);
				SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_RECREATEVDEC_REQ, &SlaveReCreateVdecReq, sizeof(PRV_MccReCreateVdecReq));					
				//保存此帧已获取的数据，从片重建完解码器后，先发送此数据
				return 1;
		
			}
		}
	}
	//未创建时，选择主从片创建
	else if(!VochnInfo[index].IsHaveVdec)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s Line %d,===============PRV_DetectResIsChg===============55555555555\n",__func__,__LINE__);
		if(VochnInfo[index].bIsPBStat)
		{
			if(FTPC_C_getParam(chn, &RTSP_SDP) != 0)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_DetectResIsChg==========chn: %d, RTSP_C_getParam(chn, &RTSP_SDP) fail!!!=========\n", chn);		
				VochnInfo[index].bIsWaitIFrame = 1; 
				return HI_FAILURE;	
			}				
			else
			{
				PRV_SetVochnInfo(&(VochnInfo[index]), &RTSP_SDP);
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "------FTPC_C_getParam() Success---vdoType: %d, adoType: %d\n",
												VochnInfo[index].VideoInfo.vdoType, VochnInfo[index].AudioInfo.adoType);			
				//可能收到数据的之前，Client链路已经断掉(切换比较快时)
				if(VochnInfo[index].VideoInfo.vdoType == 0)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_DetectResIsChg==========chn: %d, Allready Disconnect!\n",chn);			
					//VochnInfo[index].bIsWaitIFrame = 1;
					//return HI_FAILURE;	
					VochnInfo[index].VideoInfo.vdoType = PT_H264;
				}
			}

		}
		
		VochnInfo[index].VideoInfo.height = new_height;
		VochnInfo[index].VideoInfo.width = new_width;
		
		if(VochnInfo[index].IsChooseSlaveId == 0)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s ===============line: %d\n", __func__,__LINE__);
			//按设备回放下，选择主从片和设置VO属性方式不一样
			if(PlayInfo.PlayBackState > PLAY_INSTANT)
			{
				PlayBack_ChooseSlaveId(chn, new_width, new_height, VochnInfo[index].VideoInfo.framerate);
				//TODO:计算回放性能
				PlayBack_AdaptRealType();
				
			}
			else
			{
				s32Ret = PRV_ChooseSlaveId(chn, &SlaveCreateVdecReq);
				if(s32Ret != HI_SUCCESS)
				{
					if((CurCap + VochnInfo[chn].VdecCap) > (TOTALCAPPCHIP * DEV_CHIP_NUM - LOCALVEDIONUM * 1))
					{
						if(VochnInfo[index].bIsPBStat)
						{
							Prv_Chn_ChnPBOverFlow_Ind ChnPbOver;
							SN_MEMSET(&ChnPbOver, 0, sizeof(Prv_Chn_ChnPBOverFlow_Ind));
							ChnPbOver.Chn = chn;
							ChnPbOver.NewWidth = new_width;
							ChnPbOver.NewHeight = new_height;							
							SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_FWK, 0, 0, MSG_ID_PRV_CHNPBOVERFLOW_IND, &ChnPbOver, sizeof(Prv_Chn_ChnPBOverFlow_Ind));					
						}	
						else
						{
							PRV_PtsInfoInit(chn);
							PRV_InitVochnInfo(index);	
						}
						
						PRV_VoChnStateInit(chn);
						VochnInfo[chn].bIsWaitGetIFrame = 1;
#if 1
						FWK_SetOverflowState(chn, EXP_LINK_OVERFLOW);
						ALM_STATUS_CHG_ST status;
						status.AlmCnt = 1;
						SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_MMI, 0, 0, MSG_ID_ALM_STATUS_CHG, &status, sizeof(status));


						deluser_used use;
						use.channel = chn;
						SN_SendMessageEx(SUPER_USER_ID, MOD_NTRANS, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ,(void*)&use,sizeof(deluser_used));
#endif
						return HI_FAILURE;
					}
					else if((CurMasterCap + VochnInfo[chn].VdecCap) >= TOTALCAPPCHIP
						&& (CurSlaveCap + VochnInfo[chn].VdecCap) >= TOTALCAPPCHIP)
					{
						HI_U32 tempVdecCap = 0, i = 0;
						int ret = 0;
						deluser_used DelUserReq;
						DelUserReq.channel = chn;
						int TmpIndex[8] = {-1, -1, -1, -1, -1, -1, -1, -1,};
						//该通道放主片需要额外的性能
						tempVdecCap = VochnInfo[chn].VdecCap - (TOTALCAPPCHIP - CurMasterCap);

						//tempVdecCap = PRV_ComPare(tempVdecCap);
						//在主片上找到满足额外性能的通道(1个或2个或3个或4个或更多)改放从片
						if(tempVdecCap >= TOTALCAPPCHIP)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "chn: %d, No Vaild Cap: %d\n", chn, VochnInfo[chn].VdecCap);
							SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ, &DelUserReq, sizeof(deluser_used));
							return HI_FAILURE;
						}

						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s line:%d CurMasterCap=%d, CurSlaveCap=%d, tempVdecCap=%d, total=%d, Need Find Master Chn!\n", __FUNCTION__, __LINE__, CurMasterCap, CurSlaveCap, tempVdecCap, TOTALCAPPCHIP);
						ret = PRV_FindMaster_Min(tempVdecCap, chn, TmpIndex);
						if(ret < 0)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Master: =======tempVdecCap: %d, No Find Master Chn ReChoose Slave---\n", tempVdecCap);
							SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ, &DelUserReq, sizeof(deluser_used));
							return HI_FAILURE;
						}
			
						for(i = 0; i < 8; i++)
						{
							//将在主片找到的通道改放从片
							if(TmpIndex[i] >= 0)
								PRV_MasterChnReChooseSlave(TmpIndex[i]);
							else
								break;
								
						}
				
						VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
						VochnInfo[chn].SlaveId = PRV_MASTER;
						VochnInfo[chn].VoChn = chn; 
						VochnInfo[chn].IsConnect = 1;
						SlaveCreateVdecReq.SlaveId = PRV_MASTER;
						
#if 0						
						PRV_FindMasterChnReChooseSlave(tempVdecCap, chn, TmpIndex);
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Master: tempVdecCap: %d <= 4,Need Find Master Chn!\n", tempVdecCap);			
						for(i = 0; i < 8; i++)
						{
							//将在主片找到的通道改放从片
							if(TmpIndex[i] >= 0)
								PRV_MasterChnReChooseSlave(TmpIndex[i]);
							else
								break;
								
						}
						if(i == 0)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Master: =======tempVdecCap: %d, No Find Master Chn ReChoose Slave---\n", tempVdecCap);
							SN_SendMessageEx(SUPER_USER_ID, MOD_PRV, MOD_NTRANS, 0, 0, MSG_ID_NTRANS_DELUSER_REQ, &DelUserReq, sizeof(deluser_used));
							return HI_FAILURE;
						}					
						VochnInfo[chn].CurChnIndex = chn - LOCALVEDIONUM;
						VochnInfo[chn].SlaveId = PRV_MASTER;
						VochnInfo[chn].VoChn = chn; 
						VochnInfo[chn].IsConnect = 1;
						SlaveCreateVdecReq.SlaveId = PRV_MASTER;
#endif					
					}
				}
			}
		}
		
		if(VochnInfo[index].bIsPBStat)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "====================line: %d, StartPts: %d\n", __LINE__, PtsInfo[chn].StartPts);
			if(PtsInfo[chn].StartPts == 0)
			{
				PRM_ID_TIME AllStartTime, AllEndTime;
				time_t AllStartPts, AllEndPts, QueryFinalPts;
				Ftpc_PlayFileAllTime(chn, &AllStartTime, &AllEndTime);
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========AllStartTime: %d-%d-%d,%d.%d.%d\n", AllStartTime.Year, AllStartTime.Month, AllStartTime.Day, AllStartTime.Hour, AllStartTime.Minute, AllStartTime.Second);
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========AllEndTime: %d-%d-%d,%d.%d.%d\n", AllEndTime.Year, AllEndTime.Month, AllEndTime.Day, AllEndTime.Hour, AllEndTime.Minute, AllEndTime.Second);
				AllStartPts = PlayBack_PrmTime_To_Sec(&AllStartTime);
				AllEndPts = PlayBack_PrmTime_To_Sec(&AllEndTime);
				QueryFinalPts = PlayBack_PrmTime_To_Sec(&PtsInfo[chn].QueryFinalTime);
				Probar_time[chn] = PlayBack_PrmTime_To_Sec(&PtsInfo[chn].QueryStartTime);

				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "====line: %d, AllStartPts=%d, AllEndPts=%d, QueryFinalPts=%d, Probar_time[chn]=%d\n", __LINE__, AllStartPts, AllEndPts, QueryFinalPts, Probar_time[chn]);

				if(Probar_time[chn] < AllStartPts)
				{
					Probar_time[chn] = AllStartPts;
					PtsInfo[chn].QueryStartTime = AllStartTime;
				}
				if(QueryFinalPts > AllEndPts)
				{
					PtsInfo[chn].QueryFinalTime = AllEndTime;
				}
				
			}
			Ftpc_PlayFileCurTime(chn, &PtsInfo[chn].StartPrm, &PtsInfo[chn].EndPrm);
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========PtsInfo[chn].StarPrm: %d-%d-%d,%d.%d.%d\n", PtsInfo[chn].StartPrm.Year, PtsInfo[chn].StartPrm.Month, PtsInfo[chn].StartPrm.Day, PtsInfo[chn].StartPrm.Hour, PtsInfo[chn].StartPrm.Minute, PtsInfo[chn].StartPrm.Second);
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========PtsInfo[chn].EndPrm: %d-%d-%d,%d.%d.%d\n", PtsInfo[chn].EndPrm.Year, PtsInfo[chn].EndPrm.Month, PtsInfo[chn].EndPrm.Day, PtsInfo[chn].EndPrm.Hour, PtsInfo[chn].EndPrm.Minute, PtsInfo[chn].EndPrm.Second);
			PtsInfo[chn].StartPts = PlayBack_PrmTime_To_Sec(&PtsInfo[chn].StartPrm);
			if(Probar_time[chn] < PtsInfo[chn].StartPts)
			{
				Probar_time[chn] = PtsInfo[chn].StartPts;					
			}
			
			PtsInfo[chn].CurShowPts = (HI_U64)Probar_time[chn]*1000000;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========QueryStarTime: %d-%d-%d,%d.%d.%d\n", PtsInfo[chn].QueryStartTime.Year, PtsInfo[chn].QueryStartTime.Month, PtsInfo[chn].QueryStartTime.Day, PtsInfo[chn].QueryStartTime.Hour, PtsInfo[chn].QueryStartTime.Minute, PtsInfo[chn].QueryStartTime.Second);
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========QueryFinalTime: %d-%d-%d,%d.%d.%d\n", PtsInfo[chn].QueryFinalTime.Year, PtsInfo[chn].QueryFinalTime.Month, PtsInfo[chn].QueryFinalTime.Day, PtsInfo[chn].QueryFinalTime.Hour, PtsInfo[chn].QueryFinalTime.Minute, PtsInfo[chn].QueryFinalTime.Second);
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "================PtsInfo[chn].CurShowPts: %lld\n", PtsInfo[chn].CurShowPts);
			
			Ftpc_ChnStopWrite(chn, 0);					
			VoChnState.bIsPBStat_StopWriteData[chn] = 0;
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d============bIsPBStat_StopWriteData---StopWrite==0\n", __LINE__);
		}
		VochnInfo[index].IsChooseSlaveId = 1;
		if(VochnInfo[index].SlaveId > PRV_MASTER)
		{
			if(PlayInfo.PlayBackState > PLAY_INSTANT)
			{
				PlayBack_MccPBCreateVdecReq PBCreateReq;
				PBCreateReq.SlaveId = VochnInfo[index].SlaveId;
				PBCreateReq.EncType = VochnInfo[index].VideoInfo.vdoType;
				PBCreateReq.s32VdecHeight = VochnInfo[index].VideoInfo.height;
				PBCreateReq.s32VdecWidth  = VochnInfo[index].VideoInfo.width;
				PBCreateReq.VoChn = VochnInfo[index].VoChn;
				PBCreateReq.VdecChn = VochnInfo[index].VoChn;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "============Send: MSG_ID_PRV_MCC_PBCREATEVDEC_REQ---%d\n", PBCreateReq.VdecChn);
				SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_PBCREATEVDEC_REQ, &PBCreateReq, sizeof(PBCreateReq));					
			}
			else
			{
				SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_CREATEVDEC_REQ, &SlaveCreateVdecReq, sizeof(PRV_MccCreateVdecReq));					
			}
			VochnInfo[index].MccCreateingVdec = 1;
			return 1;
		}
		else
		{
			//PRV_CreateVdecChn(VochnInfo[index].VideoInfo.vdoType, VochnInfo[index].VideoInfo.height, VochnInfo[index].VideoInfo.width, VochnInfo[index].VdecChn);
			if(PlayInfo.PlayBackState > PLAY_INSTANT)
			{
				s32Ret = PlayBack_CreatVdec(HD, VochnInfo[index].VoChn);
			}
			else
			{
				s32Ret = PRV_CreateVdecChn_EX(index);
			}
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===============line: %d, s32Ret: %d\n", __LINE__, s32Ret);
			if(s32Ret == HI_FAILURE)
			{
				return HI_FAILURE;
			}
		}
			
	}
	//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "End PRV_DetectResIsChg\n");
	
	return HI_SUCCESS;
}

//在本地缓存队列中获取第一个视频I帧
//返回值:HI_FAILURE:获取I帧失败
//			HI_SUCCESS:获取I帧成功，且在主片预览此通道
//			1:获取I帧成功，且在从片预览此通道
HI_S32 PRV_GetFirstIFrame(int chn, char **pv)
{
	//char *pv = NULL;
	AVPacket *PRVGetVideoData = NULL;
	HI_S32 s32Ret = 0, index = 0, TotalSize = 0, PRVStatus = 0; 

	index = chn + LOCALVEDIONUM;
	while(1)
	{
		s32Ret = BufferGet(chn + PRV_VIDEOBUFFER, pv, &TotalSize, &PRVStatus);
		if(s32Ret == 0 && pv != NULL)
		{
			PRVGetVideoData = (AVPacket *)*pv;
			if(PRVGetVideoData->frame_type != 1)
			{
				NTRANS_FreeMediaData(PRVGetVideoData); 
				PRVGetVideoData = NULL;
				continue;
			}
			//如果是I帧，判断此I帧分辨率是否与解码器分辨率一致
			else
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "========Found I Frame\n");
				s32Ret = PRV_DetectResIsChg(chn, PRVGetVideoData);
				if(s32Ret == HI_FAILURE)
				{
					NTRANS_FreeMediaData(PRVGetVideoData); 
					PRVGetVideoData = NULL; 	
				}
				else if(s32Ret == 1)//从片创建	
				{
					tmp_PRVGetVideoData[chn] = PRVGetVideoData; 
				}
				else if(s32Ret == 2)
				{
					Pb_FirstGetVideoData[chn] = PRVGetVideoData;
				}
				return s32Ret;
			}
		}	
		else
		{
			return HI_FAILURE;
		}
	}
	
	return HI_FAILURE;
}

int PRV_GetBufferSize(int chn)
{	
	int speed = 0, buf_size = 10;

	if(chn < 0 || chn >= DEV_CHANNEL_NUM)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "chn=%d, error\n", chn);
		return buf_size;
	}

	speed = PlayStateInfo[chn].CurSpeedState;

	switch(speed)
	{
		case DEC_SPEED_NORMALSTEP:
		case DEC_SPEED_NORMALSLOW8:
		case DEC_SPEED_NORMALSLOW4:
		case DEC_SPEED_NORMALSLOW2:
		{
			buf_size = 1;
		}
		break;
		case DEC_SPEED_NORMAL:
		{
			buf_size = 10;
		}
		break;
		case DEC_SPEED_NORMALFAST2:
		{
			buf_size = 5;
		}
		break;
		case DEC_SPEED_NORMALFAST4:
		case DEC_SPEED_NORMALFAST8:
		{
			buf_size = 2;
		}
		break;
		default:
		{
			buf_size = 10;
		}
		break;	
		
	}

	return buf_size;
}

//音视频同步处理
HI_S32 PRV_Video_Audio_SynPro(int chn)
{
	int count = 0, PRVStatus = 0,TotalSize = 0, cidx = 0, AudioFast = 0, VideoFast = 0, BufferJumpCount = 0;
	HI_S32 s32Ret = 0; 
	char *pv = NULL;
	AVPacket *PRVGetAudioData = NULL;
#if defined(Hi3531)
	AUDIO_DEV AoDev = 4;
#else
	AUDIO_DEV AoDev = 0;
#endif
	if(PRV_CurDecodeMode == PassiveDecode)
	{
		if(Achn < 0)
		{
			Achn = 0;
		}
	}
	
	//被动解码下，多画面预览时不需要音视频同步，因为无法确定音频与哪个视频通道匹配
	if(PRV_CurDecodeMode == PassiveDecode && g_stPreviewMode != SingleScene)
	{
		s32Ret = BufferGet(chn + PRV_AUDIOBUFFER, &pv, &TotalSize, &PRVStatus);
		if(s32Ret == 0 && pv != NULL)
		{		
			PRVGetAudioData = (AVPacket *)pv;
			PRV_IsAudioParaChange(Achn, PRVGetAudioData->DataSize - PRVGetAudioData->BufOffset);//音频参数有改变时，需要重设音频输出通道
			//printf("----------Send Video Data----------\n");
			s32Ret = PRV_SendData(chn, PRVGetAudioData, PRVGetAudioData->codec_id, MasterToSlaveChnId, 0);
			VoChnState.SendAudioDataCount[chn]++;
			NTRANS_FreeMediaData(PRVGetAudioData);
			PRVGetAudioData = NULL;
		}
		IsStopGetAudio = 0;

		return HI_SUCCESS;
	}

	if(VoChnState.AudioDataTimeLag[chn] - VoChnState.VideoDataTimeLag[chn] > 0)
	{
		//TRACE(SCI_TRACE_NORMAL, MOD_SLC, "--------chn: %d, IsStopGetAudio: %d, Lag::::Audio: %lld, Video: %lld, abs(VoChnState.AudioDataTimeLag - VoChnState.VideoDataTimeLag): %d\n", chn, IsStopGetAudio, VoChnState.AudioDataTimeLag[chn], VoChnState.VideoDataTimeLag[chn], abs(VoChnState.AudioDataTimeLag[chn] - VoChnState.VideoDataTimeLag[chn]));
		return HI_SUCCESS;
	}
	
	if(!IsStopGetAudio || PlayInfo.PlayBackState >= PLAY_INSTANT
		/*|| (PlayInfo.PlayBackState == PLAY_INSTANT && Achn == PlayInfo.InstantPbChn)*/)
	{
		s32Ret = BufferGet(chn + PRV_AUDIOBUFFER, &pv, &TotalSize, &PRVStatus);
		if(s32Ret == 0 && pv != NULL)
		{		
			PRVGetAudioData = (AVPacket *)pv;
			VoChnState.AudioDataTimeLag[chn] = (HI_S64)(PRVGetAudioData->pts - PtsInfo[chn].FirstAudioPts) * 1000 / 8000;//当前音频时间戳与基准时间戳的差值(换算成ms)
			if(PRVGetAudioData->codec_id != VochnInfo[chn].AudioInfo.adoType && (PlayInfo.PlayBackState == PLAY_INSTANT))
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d-ch=%d-Achn=%d--codec_id=%d--adoType=%d--\n", __LINE__, chn, Achn, PRVGetAudioData->codec_id, VochnInfo[chn].AudioInfo.adoType);
				VochnInfo[chn].AudioInfo.adoType = PRVGetAudioData->codec_id;							
				if(Achn == chn)
				{
					PRV_StopAdec();
					PRV_StartAdecAo(VochnInfo[chn]);
					IsCreateAdec = 1;
				}
			}

			if(PRVGetAudioData->codec_id != VochnInfo[chn].AudioInfo.adoType && (PlayInfo.PlayBackState > PLAY_INSTANT))
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d-ch=%d-Achn=%d--codec_id=%d--adoType=%d--\n", __LINE__, chn, Achn, PRVGetAudioData->codec_id, VochnInfo[chn].AudioInfo.adoType);
				VochnInfo[chn].AudioInfo.adoType = PRVGetAudioData->codec_id;
				Achn = ADECHN;
				PlayBack_StopAdec(AoDev, AOCHN, ADECHN);
				PlayBack_StartAdec(AoDev, AOCHN, ADECHN, VochnInfo[chn].AudioInfo.adoType);

			}
		}
		else
		{
			return HI_FAILURE;
		}
		if(PlayInfo.PlayBackState > PLAY_INSTANT
			/*|| (PlayInfo.PlayBackState == PLAY_INSTANT && Achn == PlayInfo.InstantPbChn)*/)
		{			
			goto SendAudioData;
		}
	}
#if 1
	//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "--------chn: %d, IsStopGetAudio: %d, Lag::::Audio: %lld, Video: %lld, abs(VoChnState.AudioDataTimeLag - VoChnState.VideoDataTimeLag): %d\n", chn, IsStopGetAudio, VoChnState.AudioDataTimeLag[chn], VoChnState.VideoDataTimeLag[chn], abs(VoChnState.AudioDataTimeLag[chn] - VoChnState.VideoDataTimeLag[chn]));
	if(abs(VoChnState.AudioDataTimeLag[chn] - VoChnState.VideoDataTimeLag[chn]) > TimeInterval)
	{
		if(VoChnState.AudioDataTimeLag[chn] > VoChnState.VideoDataTimeLag[chn])
		{	
			AudioFast = 1;
			VideoFast = 0;
			IsStopGetAudio = 1;
		}
		else
		{
			AudioFast = 0;
			VideoFast = 1;
			IsStopGetAudio = 0;
		}
	}
	else
	{
		AudioFast = 0;
		VideoFast = 0;
		IsStopGetAudio = 0;

	}
#endif
	//音频较快时，不需要再取音频数据
	if(AudioFast)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "AudioFast--------chn: %d, Lag::::Audio: %lld, Video: %lld, abs(VoChnState.AudioDataTimeLag - VoChnState.VideoDataTimeLag): %d\n", chn, VoChnState.AudioDataTimeLag[chn], VoChnState.VideoDataTimeLag[chn], abs(VoChnState.AudioDataTimeLag[chn] - VoChnState.VideoDataTimeLag[chn]));
		
		//BufferJumpCount = MAX_ARRAY_NODE/10;
	}
	else if(VideoFast)//视频较快时，只要音频buffer中不是最新数据，则查询音频数据
	{		
		s32Ret = BufferState(chn + PRV_AUDIOBUFFER, &count, &TotalSize, &cidx);
		if(cidx > 0)
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d, chn: %d, cidx: %d\n", __LINE__, chn, cidx);
		//如果音频buffer中未取数据大于20帧，则以20帧为基数跳帧取音频数据
		//再判断是否仍然不同步；如果一直不同步，则取最新数据判断
		while(s32Ret == 0 && cidx > 0)
		{
			if(cidx >= MAX_ARRAY_NODE/20)
				BufferJumpCount = MAX_ARRAY_NODE/20 - 1;
			else
				BufferJumpCount = cidx - 1;
				
			BufferJump(chn + PRV_AUDIOBUFFER, &BufferJumpCount);
			if(NULL != PRVGetAudioData)
			{
				NTRANS_FreeMediaData(PRVGetAudioData);	
				PRVGetAudioData = NULL;
			}
			s32Ret = BufferGet(chn + PRV_AUDIOBUFFER, &pv, &TotalSize, &PRVStatus);
			if(s32Ret == 0 && pv != NULL)
			{	
				PRVGetAudioData = (AVPacket *)pv;
				VoChnState.AudioDataTimeLag[chn] = (HI_S64)(PRVGetAudioData->pts - PtsInfo[chn].FirstAudioPts) * 1000/8000;//当前音频时间戳与基准时间戳的差值(换算成ms)
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "------------VoChnState.AudioDataTimeLag: %lld\n", VoChnState.AudioDataTimeLag[chn]);
			}
			s32Ret = BufferState(chn + PRV_AUDIOBUFFER, &count, &TotalSize, &cidx);
			//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d, chn: %d, cidx: %d\n", __LINE__, chn, cidx);
			
			if(VoChnState.VideoDataTimeLag[chn] - VoChnState.AudioDataTimeLag[chn] <= TimeInterval)
			{
				VideoFast = 0;
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Normal::::::::::::---chn: %d, video: %lld, audio: %lld@@@%lld, cidx: %d\n",
					chn, VoChnState.VideoDataTimeLag[chn], VoChnState.AudioDataTimeLag[chn], VoChnState.VideoDataTimeLag[chn] - VoChnState.AudioDataTimeLag[chn], cidx);				
				break;
			}
								
		}
	}
	
#if 0
	//音视频同步
	if(abs(VoChnState.VideoDataTimeLag[chn] - VoChnState.AudioDataTimeLag[chn]) > TimeInterval)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "syn::::::::Discard Audio Data------i: %d, video: %lld, audio: %lld,----%lld\n", chn, VoChnState.VideoDataTimeLag[chn], VoChnState.AudioDataTimeLag[chn], VoChnState.VideoDataTimeLag[chn] - VoChnState.AudioDataTimeLag[chn]);
		//不同步则丢弃音频帧
		if(NULL != PRVGetAudioData)
		{
			NTRANS_FreeMediaData(PRVGetAudioData);
			PRVGetAudioData = NULL;
			return HI_SUCCESS;
		}
	}					
	else
	{				
		IsStopGetAudio = 0;
	}
#endif
	
SendAudioData:
	if(NULL != PRVGetAudioData)
	{
		if(PtNumPerFrm > 0)//已经创建好音频解码通道
		{
			PRV_IsAudioParaChange(Achn, PRVGetAudioData->DataSize - PRVGetAudioData->BufOffset);//音频参数有改变时，需要重设音频输出通道
		}
		#if 0
		if(IsCreatingAdec)
		{
			NTRANS_FreeMediaData(PRVGetAudioData);
			PRVGetAudioData = NULL;
			return HI_SUCCESS;
		}
		#endif
		if(PlayInfo.PlayBackState > PLAY_INSTANT)
		{
			if(!PlayInfo.IsPlaySound || PlayStateInfo[chn].CurSpeedState != DEC_SPEED_NORMAL)
			{
				NTRANS_FreeMediaData(PRVGetAudioData);
				PRVGetAudioData = NULL;
				return HI_SUCCESS;
			}
		}

		if(!VoChnState.IsGetFirstData[chn])
		{
			NTRANS_FreeMediaData(PRVGetAudioData);
			PRVGetAudioData = NULL;
			return HI_SUCCESS;
		}

		
#if defined(Hi3531)||defined(Hi3535)		
		if(IsAdecBindAo)
#endif		
		{
			s32Ret = PRV_SendData(chn, PRVGetAudioData, PRVGetAudioData->codec_id, MasterToSlaveChnId, 0);
			if(s32Ret == HI_ERR_ADEC_BUF_FULL)
			{
				IsStopGetAudio = 1;
			}
			
			VoChnState.SendAudioDataCount[chn]++;
		}
		
		NTRANS_FreeMediaData(PRVGetAudioData);
		PRVGetAudioData = NULL;
	}

	if(VideoFast)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_PRV, "video is fast");
		return 1;
	}
	
	return HI_SUCCESS;
}

#define PATH_test "/tmp/test"
#define BUFSIZE 8*1024*1024
#define VIDEO_PACKET_SIZE 	1700		//视频包大小
int testsign = 1;

//读取文件，但是不打包
int find_nal_unit_test(uint8_t* buf, int size, int* nal_start, int* nal_end)
{
    int i = 0;
    // find start
    *nal_start = 0;
    *nal_end = 0;
    
    i = 0;
    while (   //( next_bits( 24 ) != 0x000001 && next_bits( 32 ) != 0x00000001 )
        (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0x01) && 
        (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0 || buf[i+3] != 0x01) 
        )
    {
        i++; // skip leading zero
        if (i+4 >= size) { return 0; } // did not find nal start
    }

    if  (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0x01) // ( next_bits( 24 ) != 0x000001 )
    {
        i++;
    }

    if  (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0x01) { /* error, should never happen */ return 0; }

	if(buf[i+3] == 0x41)
	{
		printf("11start sign:%x\n", buf[i+3]);
		i+= 3;
	    *nal_start = i;
	    
	    while (   //( next_bits( 24 ) != 0x000000 && next_bits( 24 ) != 0x000001 )
	        (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0) && 
	        (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0x01)
	        )
	    {
	        i++;
	        // FIXME the next line fails when reading a nal that ends exactly at the end of the data
	        if (i+3 >= size) { *nal_end = size; return -1; } // did not find nal end, stream ended first	
	    }

		i+= 3;

		while (   //( next_bits( 24 ) != 0x000000 && next_bits( 24 ) != 0x000001 )
	        (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0) && 
	        (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0x01)
	        )
	    {
	        i++;
	        // FIXME the next line fails when reading a nal that ends exactly at the end of the data
	        if (i+3 >= size) { *nal_end = size; return -1; } // did not find nal end, stream ended first	
	    }

		 printf("end sign:%x\n", buf[i+4]);
	}
	else if((buf[i+3]&0x1f) == 0x7)
	{
		printf("22start sign:%x\n", buf[i+3]);
		i+= 3;
	    *nal_start = i;
	    
	    while (   //( next_bits( 24 ) != 0x000000 && next_bits( 24 ) != 0x000001 )
        	(buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0 || buf[i+3] != 0x01 || buf[i+4] != 0x41) 
	        )
	    {
	        i++;
	        // FIXME the next line fails when reading a nal that ends exactly at the end of the data
	        if (i+4 >= size) { *nal_end = size; return -1; } // did not find nal end, stream ended first	
	    }
		 printf("end sign:%x\n", buf[i+4]);
	}
	else
	{
		printf("not find:%x\n", buf[i+3]);
		return -1;
	}
    
    *nal_end = i;
    return (*nal_end - *nal_start);
}


//读取文件后直接打包
int find_nal_unit_test1(uint8_t* buf, int size, int* nal_start, int* nal_end)
{
    int i = 0;
    // find start
    *nal_start = 0;
    *nal_end = 0;
    
    i = 0;
    while (   //( next_bits( 24 ) != 0x000001 && next_bits( 32 ) != 0x00000001 )
        (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0x01) && 
        (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0 || buf[i+3] != 0x01) 
        )
    {
        i++; // skip leading zero
        if (i+4 >= size) { return 0; } // did not find nal start
    }

    if  (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0x01) // ( next_bits( 24 ) != 0x000001 )
    {
        i++;
    }

    if  (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0x01) { /* error, should never happen */ return 0; }

	if(buf[i+3] == 0x41)
	{
		printf("11start sign:%x-----------------------------------------------\n", buf[i+3]);
		i+= 3;
	    *nal_start = i;
	    
	    while (   //( next_bits( 24 ) != 0x000000 && next_bits( 24 ) != 0x000001 )
	        (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0) && 
	        (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0x01)
	        )
	    {
	        i++;
	        // FIXME the next line fails when reading a nal that ends exactly at the end of the data
	        if (i+3 >= size) { *nal_end = size; return -1; } // did not find nal end, stream ended first	
	    }

		 printf("end sign:%x\n", buf[i+4]);
	}
	else if((buf[i+3]&0x1f) == 0x7)
	{
		printf("22start sign:%x------------------------------------------------\n", buf[i+3]);
		i+= 3;
	    *nal_start = i;
	    
	    while (   //( next_bits( 24 ) != 0x000000 && next_bits( 24 ) != 0x000001 )
        	(buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0 || buf[i+3] != 0x01 || buf[i+4] != 0x65) 
	        )
	    {
	        i++;
	        // FIXME the next line fails when reading a nal that ends exactly at the end of the data
	        if (i+4 >= size) { *nal_end = size; return -1; } // did not find nal end, stream ended first	
	    }
		i+= 3;
		while (   //( next_bits( 24 ) != 0x000000 && next_bits( 24 ) != 0x000001 )
        	(buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0 || buf[i+3] != 0x01 || buf[i+4] != 0x65) 
	        )
	    {
	        i++;
	        // FIXME the next line fails when reading a nal that ends exactly at the end of the data
	        if (i+4 >= size) { *nal_end = size; return -1; } // did not find nal end, stream ended first	
	    }
		 printf("end sign:%x\n", buf[i+4]);
	}
	else if(buf[i+3] == 0x65)
	{
		printf("33start sign:%x-----------------------------------------------\n", buf[i+3]);
		i+= 3;
	    *nal_start = i;
	    
	    while (   //( next_bits( 24 ) != 0x000000 && next_bits( 24 ) != 0x000001 )
        	(buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0 || buf[i+3] != 0x01 || buf[i+4] != 0x41) 
	        )
	    {
	        i++;
	        // FIXME the next line fails when reading a nal that ends exactly at the end of the data
	        if (i+4 >= size) { *nal_end = size; return -1; } // did not find nal end, stream ended first	
	    }
		 printf("end sign:%x\n", buf[i+4]);
	}
	else
	{
		printf("not find:%x\n", buf[i+3]);
		return -1;
	}
    
    *nal_end = i;
    return (*nal_end - *nal_start);
}

void *PRV_FileThread()
{
	int i = 0;
	char name[32];

	Log_pid(__FUNCTION__);
	pthread_detach(pthread_self());
	
	while(1)
	{	
		for(i=0; i<DEV_CHANNEL_NUM; i++)
		{	
			if(g_beginsign && access(PATH_STOP, F_OK) != 0)
			{
				SN_MEMSET(name, 0, sizeof(name));
				SN_SPRINTF(name, sizeof(name), "/tmp/get_ch%d.h264", i);
				if(access(name, F_OK) == 0)
				{
					g_filesign[i][0] = i;
				}
				else
				{
					g_filesign[i][0] = -1;
				}

				SN_MEMSET(name, 0, sizeof(name));
				SN_SPRINTF(name, sizeof(name), "/tmp/send_ch%d.h264", i);
				if(access(name, F_OK) == 0)
				{
					g_filesign[i][1] = i;
				}
				else
				{
					g_filesign[i][1] = -1;
				}

				SN_MEMSET(name, 0, sizeof(name));
				SN_SPRINTF(name, sizeof(name), "/tmp/recv_ch%d.h264", i);
				if(access(name, F_OK) == 0)
				{
					g_filesign[i][2] = i;
				}
				else
				{
					g_filesign[i][2] = -1;
				}
				
			}
			else
			{
				g_filesign[i][0] = -1;
				g_filesign[i][1] = -1;
				g_filesign[i][2] = -1;
			}		

			usleep(100 * 1000);			
		}

		if(testsign && access(PATH_test, F_OK) == 0)
		{
			testsign = 0;
			printf("enter test\n");
			uint8_t* buf = (uint8_t*)malloc(BUFSIZE);
			int fd = open("/tmp/test.h264", O_RDONLY);
		    if (fd == -1) { perror("could not open file"); exit(0); }

		    int rsz = 0;
		    int sz = 0;
		    int64_t off = 0;
			uint8_t* data= NULL;
		    uint8_t* p = buf;
			AVPacket *av_packet = NULL;
			AVPacket *tem_packet = NULL;
			AVPacket *bPacket = NULL;
			int ret = 0, cursize = 0, curOffset = 0, buflen = 0, curcount = 0;
			int bytesRead = 0, pack_len = 0;
		    int nal_start, nal_end;

		    while ((rsz = read(fd, buf + sz, BUFSIZE - sz)))
		    {
		        sz += rsz;
				printf("----1-----\n");
		        while (find_nal_unit_test1(p, sz, &nal_start, &nal_end) > 0)
		        {
					buflen = 0;
					cursize = curOffset = 0;
		            p += nal_start;
					data = p;
					bytesRead = nal_end - nal_start;
					printf("----2-bytesRead=%d----\n", bytesRead);
#if 1		
					if(bytesRead > 4)
					{
						av_packet = (AVPacket * )SN_MPMalloc(gVideoPool, VIDEO_PACKET_SIZE);
						if(av_packet != NULL)
						{
							av_packet->codec_id = CODEC_ID_H264;//初始化变量
							av_packet->pts = 0;
							av_packet->channel = 0;
							av_packet->seqno = curcount++;
							av_packet->pub_count = 1;
							av_packet->data_len = bytesRead + sizeof(int);
							av_packet->rtpOffset = 0;
							av_packet->DataSize = sizeof(AVPacket);
							av_packet->BufOffset = sizeof(AVPacket);
							av_packet->BufRead = 0;
							av_packet->isFirstPacket = FALSE;		  
							av_packet->next = NULL;
							av_packet->Extendnext= NULL;
							av_packet->shareprv =0;
							av_packet->sharesev=0;			
							av_packet->sharevam=0;
							
						}
						else
						{   
							TRACE(SCI_TRACE_HIGH, MOD_FTPC, "%s_line:%d av_packet error", __FUNCTION__, __LINE__);
							break;
						}

						pack_len = VIDEO_PACKET_SIZE - sizeof(AVPacket);

						if(bytesRead > pack_len)
						{
							SN_MEMCPY((char *)av_packet + av_packet->BufOffset, pack_len, (char *)data+cursize+curOffset, pack_len, pack_len);
							curOffset += pack_len;		 
							av_packet->DataSize +=  pack_len;
							bytesRead -= pack_len;
						}
						else
						{   
							SN_MEMCPY((char *)av_packet + av_packet->BufOffset, pack_len, (char *)data+cursize+curOffset, bytesRead, bytesRead);
							curOffset += bytesRead;
							av_packet->DataSize +=  bytesRead;
							bytesRead = 0;
						}

						bPacket = av_packet;
						buflen += VIDEO_PACKET_SIZE;
						while( bytesRead > 0)
						{
							tem_packet = (AVPacket * )SN_MPMalloc(gVideoPool, VIDEO_PACKET_SIZE);
							if(tem_packet != NULL)
							{	
								tem_packet->codec_id = CODEC_ID_H264;//初始化变量
								tem_packet->pts = 0;
								tem_packet->channel = 1;
								tem_packet->seqno = curcount;
								tem_packet->pub_count = 1;
								tem_packet->data_len = bytesRead + sizeof(int);
								tem_packet->rtpOffset = 0;
								tem_packet->DataSize = sizeof(AVPacket);
								tem_packet->BufOffset = sizeof(AVPacket);
								tem_packet->BufRead = 0;
								tem_packet->isFirstPacket = FALSE;		  
								tem_packet->next = NULL;
								tem_packet->Extendnext= NULL;
								tem_packet->shareprv =0;
								tem_packet->sharesev=0;			
								tem_packet->sharevam=0;
							}
							else
							{   
								TRACE(SCI_TRACE_HIGH, MOD_FTPC, "%s_line:%d  av_packet error",  __FUNCTION__, __LINE__);

								BufferFree((char *)av_packet);
								break;
							}
							
							if(bytesRead > pack_len)
							{
								SN_MEMCPY((char *)tem_packet + tem_packet->BufOffset, pack_len, (char *)data+cursize+curOffset, pack_len, pack_len);
								curOffset += pack_len;		 
								tem_packet->DataSize += pack_len;
								bytesRead -= pack_len;
							}
							else
							{   
								SN_MEMCPY((char *)tem_packet + tem_packet->BufOffset, pack_len, (char *)data+cursize+curOffset, bytesRead, bytesRead);
								curOffset += bytesRead;
								tem_packet->DataSize +=  bytesRead;
								bytesRead = 0;
							}
							
							bPacket->next = (void *)tem_packet;
							bPacket = tem_packet;
							buflen += VIDEO_PACKET_SIZE;
							
						}

						if(((((int*)(data+cursize))[0])&0x0000001f) == 0x00000007)
						{
							printf("is I frame \n");
							av_packet->frame_type = 1;
						}
						else if(((((int*)(data+cursize))[0])&0x0000001f) == 0x00000001)
						{
							printf("is p frame \n");
							av_packet->frame_type = 0;
						}
						else
						{
							printf("is p frame \n");
							av_packet->frame_type = 0;
						}

						//ret = PRV_WriteBuffer(1, av_packet->codec_id, av_packet);
						ret = BufferWrite(0, (char*)av_packet, buflen);
						if(ret != OK)
						{
							TRACE(SCI_TRACE_HIGH, MOD_FTPC, "%s_line:%d  BufferWrite error",  __FUNCTION__, __LINE__);
							BufferFree((char *)av_packet);
						}
					}
#endif
					
		            p += (nal_end - nal_start);
		            sz -= nal_end;
					printf("----pack end-curcount=%d----\n", curcount);
					long time_sin = (1000 * 1000 / 36);
					usleep(time_sin);
		        }

		        // if no NALs found in buffer, discard it
		        if (p == buf) 
		        {
		            p = buf + sz;
		            sz = 0;
		        }

		        memmove(buf, p, sz);
		        off += p - buf;
		        p = buf;
		    }

			printf("---all end---\n");
			free(buf);
		}
		else if(access(PATH_test, F_OK) != 0)
		{
			testsign = 1;
		}
		else
		{
			testsign = 0;
		}

		sleep(1);
		
	}

	return NULL;
	
}


unsigned char VideoData[MAXFRAMELEN] = {0};
/********************************************************
函数名:PRV_GetPrvDataThread
功     能:从Client缓冲区中获取数据，保存在本地预览缓冲区中，
			500ms后PRV_SendDataThread线程从本地缓冲区中取数据
参     数:无
返回值:  无

*********************************************************/
void* PRV_GetPrvDataThread()
{
	HI_S32 s32Ret = 0, doublechn = -1, chn = 0, index = 0, i = 0, ReGetIndexPts[MAX_IPC_CHNNUM] = {0}, count = 0, TotalSize = 0, cidx = 0, GetDataCount = 0;
	HI_S32 DiscardPFrameNum[MAX_IPC_CHNNUM] = {0}, WaitIFrameNum[MAX_IPC_CHNNUM] = {0}, lastTime[MAX_IPC_CHNNUM] = {0}, lastPts[MAX_IPC_CHNNUM] = {0};
	HI_S32 size = 0, RTSPStates = 0, new_width = 0, new_height = 0;
	unsigned int PreSegNo[MAX_IPC_CHNNUM] = {0}, CurSegNo[MAX_IPC_CHNNUM] = {0};//记录前一个视频的segno值，不管音频的segno
	AVPacket *DataFromRTSP = NULL;
	char *pdata = NULL;
	RTSP_C_SDPInfo RTSP_SDP;
	SN_MEMSET(&RTSP_SDP, 0, sizeof(RTSP_C_SDPInfo));
	VDEC_CHN_STAT_S pstStat;
	struct timeval oldTime[MAX_IPC_CHNNUM], curTime[MAX_IPC_CHNNUM];	
				
	Log_pid(__FUNCTION__);
	pthread_detach(pthread_self());
	while(1)
	{
		if(IsUpGrade)
		{
			return NULL;
		}
		if(PlayInfo.PlayBackState > PLAY_INSTANT)
		{
			sem_wait(&sem_PrvGetData);		
		}
		while(1)
		{
			if(PlayInfo.PlayBackState > PLAY_INSTANT)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_GetPrvDataThread====PlayInfo.PlayBackState: %d > PLAY_INSTANT\n", PlayInfo.PlayBackState);
				break;		
			}
			GetDataCount = 0;	
			
			pthread_mutex_lock(&send_data_mutex);
			//一次性多接几次队列0数据，
			//避免由于送数据线程for循环占用时间太长，来不及接队列0数据导致一些数据被顶掉
			while(GetDataCount < 1)
			{
				s32Ret = NTRANS_GetMediaData(&pdata, &size, &RTSPStates, PRVUSEONLY);
				if(s32Ret != 0 || pdata == NULL)
				{
					break;
				}
				GetDataCount++;			
				DataFromRTSP = (AVPacket *)pdata;
				chn = DataFromRTSP->channel;
				doublechn = -1;

				#if 0
				rtp_stream_testC(DataFromRTSP);
				#endif
				
				TRACE(SCI_TRACE_NORMAL, MOD_FMG, "PRV_GetPrvDataThread==chn: %d,bool=%d, index=%d, codec_id=%d\n", chn, PRV_GetDoubleIndex(), PRV_GetDoubleToSingleIndex(), DataFromRTSP->codec_id);
				
				if(PRV_GetDoubleIndex() && PlayInfo.PlayBackState < PLAY_INSTANT)//双击状态进入单画面
				{
					if(chn == MAX_IPC_CHNNUM)
					{
						chn = PRV_GetDoubleToSingleIndex();
						DataFromRTSP->channel = chn;

						index = chn + LOCALVEDIONUM;
						
						if(VochnInfo[chn].bIsDouble == 0 && DataFromRTSP->codec_id == CODEC_ID_H264 && DataFromRTSP->frame_type == 1)
						{
							VochnInfo[chn].bIsDouble = 1;
							BufferSet(VochnInfo[index].VoChn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);						
							BufferSet(VochnInfo[index].VoChn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
							HI_MPI_ADEC_ClearChnBuf(DecAdec);
						}
						else
						{
							if(VochnInfo[chn].bIsDouble == 0)
							{
								TRACE(SCI_TRACE_NORMAL, MOD_DEC, "PRV_GetPrvDataThread=1===chn: %d===\n", chn);
								NTRANS_FreeMediaData(DataFromRTSP); 
								DataFromRTSP = NULL;			
								continue;
							}							
						}
					}
					else
					{
						index = chn + LOCALVEDIONUM;
						if(VochnInfo[chn].bIsDouble  && !VochnInfo[index].bIsPBStat)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_DEC, "PRV_GetPrvDataThread=2===chn: %d===\n", chn);
							NTRANS_FreeMediaData(DataFromRTSP); 
							DataFromRTSP = NULL;			
							continue;
						}						
					}
				}
				
				index = chn + LOCALVEDIONUM;
				if(VochnInfo[index].bIsPBStat)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_DEC, "PRV_GetPrvDataThread=3===chn: %d===\n", chn);
					NTRANS_FreeMediaData(DataFromRTSP); 
					DataFromRTSP = NULL;			
					continue;
				}

				if(DataFromRTSP->codec_id == CODEC_ID_H264 && DataFromRTSP->frame_type == 1)
				{
					///TRACE(SCI_TRACE_NORMAL, MOD_SLC, "prv get I frame, pTmp->seqno=%u\n", DataFromRTSP->seqno);
				}
				
				if(RTSPStates == 1)
				{
					for(i = LOCALVEDIONUM; i < CHANNEL_NUM; i++)
					{
						VochnInfo[i].bIsWaitIFrame = 1;	
					}
					NTRANS_FreeMediaData(DataFromRTSP); 
					DataFromRTSP = NULL;			
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "============NTRANS_GetMediaData========chn: %d\n", chn);
					s32Ret = BufferState(0, &count, &TotalSize, &cidx);
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "=============s32Ret: %d, count: %d, TotalSize: %d, cidx: %d\n", s32Ret, count, TotalSize, cidx);
					continue;
				}
							
				s32Ret = PRV_CheckDataPara(DataFromRTSP);
				if(s32Ret != HI_SUCCESS)
				{
					NTRANS_FreeMediaData(DataFromRTSP); 
					DataFromRTSP = NULL;
					VochnInfo[index].bIsWaitIFrame = 1; 
					continue;
				}

				/*根据串口输入的命令,将ES流写到本地文件中*/
				if(DataFromRTSP->codec_id == CODEC_ID_H264 && g_filesign[chn][0] >= 0 && g_filesign[chn][0] == chn)
				{
					AVPacket *NextPacket = NULL;
					AVPacket *pTmp = NULL;
					int length = 0, TotalLength = 0;
					char StarCode[] = {0x00, 0x00, 0x00, 0x01};
					char name[32] = {0};
					
					pTmp = (AVPacket *)pdata;
					NextPacket = pTmp->next;
					
					while(pTmp != NULL)
					{
						length = pTmp->DataSize - pTmp->BufOffset;
						if(pTmp->frame_type == 0 || pTmp->frame_type == 1)
						{
							if(TotalLength + 4 <= MAXFRAMELEN)
							{
								SN_MEMCPY(VideoData + TotalLength, MAXFRAMELEN, StarCode, 4, 4);
								TotalLength += 4;
							}
							else
							{						
								TRACE(SCI_TRACE_NORMAL, MOD_PRV, "WARNING: TotalLength + 4 > MAXFRAMELEN\n"); 
								continue;
							}

						}
						if(TotalLength + length <= MAXFRAMELEN)
						{
							SN_MEMCPY(VideoData + TotalLength, MAXFRAMELEN, (char*)pTmp + pTmp->BufOffset, length, length);
							TotalLength += length;
						}
						else
						{						
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "WARNING: TotalLength + length > MAXFRAMELEN\n"); 
							continue;
						}
						
						if(pTmp->Extendnext == NULL)
						{
							pTmp = (AVPacket *)NextPacket;
							if(pTmp != NULL)
								NextPacket = pTmp->next;
						}
						else
						{
							pTmp = (AVPacket *)(pTmp->Extendnext);
						}						
					}

					SN_SPRINTF(name, sizeof(name), "/tmp/get_ch%d.h264", g_filesign[chn][0]);

					FILE *pf = fopen(name, "a");
					if(NULL!=pf)
					{
						if(TotalLength)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_SLC, "write=%d\n", TotalLength); 
							fwrite(VideoData,TotalLength,1,pf);
						}
						fclose(pf);
					}					
				}								
			
				if(DataFromRTSP->codec_id == CODEC_ID_H264)
				{
					CurSegNo[chn] = DataFromRTSP->seqno;
				}

				//等待I帧时，丢弃所有非I帧，音频数据不丢弃
				if(DataFromRTSP->codec_id == CODEC_ID_H264		//视频数据
					&& VochnInfo[index].bIsWaitIFrame)			//且需要丢帧
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s Line %d ===========> bIsWaitIFrame: %d, frame_type: %d, chn: %d\n", 
									__func__,__LINE__,VochnInfo[index].bIsWaitIFrame, DataFromRTSP->frame_type, chn);
				
					if(1 != DataFromRTSP->frame_type)//丢弃P帧，直到下一个I帧
					{	
						DiscardPFrameNum[chn]++;
						PreSegNo[chn] = CurSegNo[chn];
						NTRANS_FreeMediaData(DataFromRTSP); 
						DataFromRTSP = NULL;
						continue;
					}
					else		
					{
						VochnInfo[index].bIsWaitIFrame = 0;
						DiscardPFrameNum[chn] = 0;
					}
				}
				
				if(DataFromRTSP->codec_id == CODEC_ID_H264//保证是视频数据，才能根据segno值判断是否丢帧
					&& CurSegNo[chn] > PreSegNo[chn]
					&& (CurSegNo[chn] - PreSegNo[chn]) > 1//segno值不连续
					&& (1 != DataFromRTSP->frame_type))   //当前帧不是I帧。如果是I帧，之前即使丢了数据也没关系
				{
					VochnInfo[index].bIsWaitIFrame = 1;			
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Lost Frame---chn: %d, PreSegNo: %d, CurSegNo: %d, CurSegNo-PreSegNo: %d!!!\n", chn, PreSegNo[chn], CurSegNo[chn], CurSegNo[chn] - PreSegNo[chn]);
					PreSegNo[chn] = CurSegNo[chn];
					NTRANS_FreeMediaData(DataFromRTSP); 
					DataFromRTSP = NULL;
					continue;
				}
				
				if(DataFromRTSP->codec_id == CODEC_ID_H264 && (CurSegNo[chn] != 1 && CurSegNo[chn] <= PreSegNo[chn]))
				{
					ReGetIndexPts[chn] = 1;
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "========chn: %d, CurSegNo[chn]: %d, frame_type: %d\n", 
																chn, CurSegNo[chn], DataFromRTSP->frame_type);
					if(DataFromRTSP->frame_type != 1)
					{
						VochnInfo[index].bIsWaitIFrame = 1; 
						PreSegNo[chn] = CurSegNo[chn];	
						NTRANS_FreeMediaData(DataFromRTSP); 
						DataFromRTSP = NULL;
						continue;
					}
				}

				PreSegNo[chn] = CurSegNo[chn];	
				
				PRV_GetPrvMode_EX(&g_stPreviewMode);
				
				//被动解码下，只有一路数据有音频
				if((DataFromRTSP->codec_id == CODEC_ID_PCMA || DataFromRTSP->codec_id == CODEC_ID_PCMU) && 
					PRV_CurDecodeMode == PassiveDecode  && VoChnState.AudioDataCount[chn] <= 0)
				{
					//获取音频通道的SDP信息
					if(RTSP_C_getParam(chn, &RTSP_SDP) != 0)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "DataFromRTSP->codec_id == CODEC_ID_PCMA: chn: %d, RTSP_C_getParam(chn, &RTSP_SDP) fail!!!\n", chn); 		
						NTRANS_FreeMediaData(DataFromRTSP); 
						DataFromRTSP = NULL;
						continue;	
					}
					else
					{
						VochnInfo[index].AudioInfo.adoType = RTSP_SDP.adoType;
						VochnInfo[index].AudioInfo.samrate = RTSP_SDP.samrate;
						VochnInfo[index].AudioInfo.soundchannel = RTSP_SDP.soundchannel;
						//PRV_SetVochnInfo(&(VochnInfo[index]), &RTSP_SDP);
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "DataFromRTSP->codec_id == CODEC_ID_PCMA: RTSP_C_getParam() Success---vdoType: %d, adoType: %d, framerate=%d\n",
														VochnInfo[index].VideoInfo.vdoType, VochnInfo[index].AudioInfo.adoType, RTSP_SDP.framerate);			
					}

					TRACE(SCI_TRACE_NORMAL, MOD_DEC, "----------------index: %d\n", index);
					if(!IsCreateAdec && PRV_StartAdecAo(VochnInfo[index]) == HI_SUCCESS)
					{
						IsCreateAdec = 1;
					}
				}

				//数据源发生变化时，重新获取该通道的数据信息
				if(DataFromRTSP->codec_id == CODEC_ID_H264 && DataFromRTSP->frame_type == 1)
				{
					WaitIFrameNum[chn]++;
					s32Ret = PRV_SearchSPS(chn, DataFromRTSP, &new_width, &new_height);
					
					//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "index: %d===DataFromRTSP->pts: %lld, PtsInfo[chn].PreGetVideoPts: %lld\n", index, DataFromRTSP->pts, PtsInfo[chn].PreGetVideoPts);			
					if((s32Ret != HI_SUCCESS && ((VochnInfo[index].VideoInfo.width == 0 && VochnInfo[index].VideoInfo.height == 0)||
						(VochnInfo[index].VideoInfo.width != new_width && VochnInfo[index].VideoInfo.height != new_height)))
									|| ReGetIndexPts[chn] == 1)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line:%d, ReGetIndexPts: %d, s32Ret: %d------P-T-S---CHANGE------index: %d\n",
												__LINE__, ReGetIndexPts[chn], s32Ret, index);			
						PRV_VoChnStateInit(chn);
						PRV_PtsInfoInit(chn);

#if defined(SN9234H1)		
						/*Hi3520*/
						if(s32Ret == -2)  
						{
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========chn: %d, the video type is not support!!!=========\n", chn); 		
							NTRANS_FreeMediaData(DataFromRTSP); 
							DataFromRTSP = NULL;
							VochnInfo[index].bIsWaitIFrame = 1; 
							continue;
						}
#endif					
						/*双击情况下的通道号转换*/
						if(VochnInfo[chn].bIsDouble && PRV_GetDoubleIndex() && chn == PRV_GetDoubleToSingleIndex())
						{
							doublechn = MAX_IPC_CHNNUM;
						}
						else
						{
							doublechn = chn;
						}
						
						s32Ret = RTSP_C_getParam(doublechn, &RTSP_SDP);
						if(s32Ret != 0)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========chn: %d, RTSP_C_getParam(chn, &RTSP_SDP) fail!!!=========\n", doublechn); 		
							NTRANS_FreeMediaData(DataFromRTSP); 
							DataFromRTSP = NULL;
							VochnInfo[index].bIsWaitIFrame = 1; 
							continue;	
						}
						else
						{
							PRV_SetVochnInfo(&(VochnInfo[index]), &RTSP_SDP);

							VochnInfo[index].VideoInfo.height = 0;
							VochnInfo[index].VideoInfo.width = 0;

							/*单画面预览,doublechn的值为最大通道号(MAX_IPC_CHNNUM)*/
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Line %d---RTSP_C_getParam() Success--doublechn:%d-vdoType: %d, adoType: %d\n",
												__LINE__,doublechn, VochnInfo[index].VideoInfo.vdoType, VochnInfo[index].AudioInfo.adoType);			

							//可能在收到数据之前，Client链路已经断掉(切换比较快时)
							if(VochnInfo[index].VideoInfo.vdoType == 0)
							{
								TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s Line %d ==========> chn: %d, Allready Disconnect!\n",
																		__func__,__LINE__,chn);			
								VochnInfo[index].bIsWaitIFrame = 1;
								NTRANS_FreeMediaData(DataFromRTSP); 
								DataFromRTSP = NULL;
								continue;
							}
							
							//被动下，从第一个视频数据结构体中获取基准时间戳，并重新创建音频解码通道
							if(PRV_CurDecodeMode == PassiveDecode)
							{
								/*被动解码情况下,一个sock可能包含多个通道的码流*/
								PtsInfo[chn].FirstVideoPts = DataFromRTSP->pts;						
								PtsInfo[chn].BaseVideoPts = PtsInfo[chn].FirstVideoPts;
								
								TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s Line %d,PassiveDecode----FistVideopts: %lld\n",
													__func__,__LINE__,PtsInfo[chn].FirstVideoPts);					
							}
						}	

						ReGetIndexPts[chn] = 1;
						
					}
				}

				if(DataFromRTSP->codec_id == CODEC_ID_H264)
				{
					PtsInfo[chn].PreGetVideoPts = DataFromRTSP->pts;
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "DataFromRTSP->codec_id == CODEC_ID_H264---DataFromRTSP->pts: %llu\n", DataFromRTSP->pts);					
				}
				else
				{
					PtsInfo[chn].PreGetAudioPts = DataFromRTSP->pts;
					//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "DataFromRTSP->codec_id == CODEC_ID_PCMA---DataFromRTSP->pts: %lld\n", DataFromRTSP->pts);					
				}
				
				//更新最新的音视频时间戳
				if(PRV_CurDecodeMode == SwitchDecode)
				{
					//数据信息改变，重新获取音视频基准时间戳
					if(ReGetIndexPts[chn] == 1 && DataFromRTSP->codec_id == CODEC_ID_H264)
					{
						if(VochnInfo[chn].bIsDouble && PRV_GetDoubleIndex() && chn == PRV_GetDoubleToSingleIndex())
						{
							doublechn = MAX_IPC_CHNNUM;
						}
						else
						{
							doublechn = chn;
						}
						if(GB_Get_GBMode())
						{
							PtsInfo[chn].FirstVideoPts = DataFromRTSP->pts;	
						}
						else
						{
							NTRANS_getFirstMediaPts(doublechn, &(PtsInfo[chn].FirstVideoPts), &(PtsInfo[chn].FirstAudioPts));
						}
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "---%s line:%d-----chn=%d--Get Audio and Video data---doublechn: %d, FistVideopts: %lld, FirstAudiopts: %lld\n",
											__FUNCTION__, __LINE__, chn, doublechn, PtsInfo[chn].FirstVideoPts, PtsInfo[chn].FirstAudioPts);
						PtsInfo[chn].BaseVideoPts = PtsInfo[chn].FirstVideoPts;
					}

					//之前音视频基准时间戳获取失败，重新获取
					if(VochnInfo[index].AudioInfo.adoType >= 0 &&
						(PtsInfo[chn].FirstVideoPts == (~0x0) || PtsInfo[chn].FirstAudioPts == (~0x0)))
					{
						if(VoChnState.VideoDataCount[chn] > 0 && VoChnState.AudioDataCount[chn] > 0)
						{
							if(VochnInfo[chn].bIsDouble && PRV_GetDoubleIndex() && chn == PRV_GetDoubleToSingleIndex())
							{
								doublechn = MAX_IPC_CHNNUM;
							}
							else
							{
								doublechn = chn;
							}
							NTRANS_getFirstMediaPts(doublechn, &(PtsInfo[chn].FirstVideoPts), &(PtsInfo[chn].FirstAudioPts));
							if(PtsInfo[chn].FirstVideoPts != (~0x0) && PtsInfo[chn].FirstAudioPts != (~0x0))
							{
								TRACE(SCI_TRACE_NORMAL, MOD_PRV, "---%s line:%d-------Get Audio and Video data again success---doublechn: %d, FistVideopts: %lld, FirstAudiopts: %lld\n", __FUNCTION__, __LINE__, doublechn, PtsInfo[chn].FirstVideoPts, PtsInfo[chn].FirstAudioPts);
								TRACE(SCI_TRACE_NORMAL, MOD_PRV, "----------VoChnState.VideoDataCount[chn]: %d, VoChnState.AudioDataCount[chn]: %d\n", VoChnState.VideoDataCount[chn], VoChnState.AudioDataCount[chn]);
								PtsInfo[chn].BaseVideoPts = PtsInfo[chn].FirstVideoPts;
							}
						}
					}
				}
				//被动下只有单画面才需要做音视频同步，需要获取第一个音频数据时间戳
				else if(g_stPreviewMode == SingleScene && (DataFromRTSP->codec_id == CODEC_ID_PCMA || DataFromRTSP->codec_id == CODEC_ID_PCMU)
					&& (VochnInfo[index].AudioInfo.adoType >= 0 && PtsInfo[chn].FirstAudioPts == (~0x0)))
				{
					
					PtsInfo[chn].FirstAudioPts = DataFromRTSP->pts;
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "g_stPreviewMode == SingleScene,PassiveDecode----FirstAudioPts: %lld\n", PtsInfo[chn].FirstAudioPts);
				}
				
				if(ReGetIndexPts[chn] == 1 && DataFromRTSP->codec_id == CODEC_ID_H264 && !VochnInfo[index].bIsPBStat)
				{
					if(tmp_PRVGetVideoData[chn] != NULL)
					{
						NTRANS_FreeMediaData(tmp_PRVGetVideoData[chn]); 
						tmp_PRVGetVideoData[chn] = NULL;
					}
					BufferSet(VochnInfo[index].VoChn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);						
					BufferSet(VochnInfo[index].VoChn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
					
					ReGetIndexPts[chn] = 0;
					VochnInfo[index].IsDiscard = 0;
					VochnInfo[index].bIsWaitIFrame = 0;

					VochnInfo[index].PrvType = g_PrvType;
				}
				
				if(DataFromRTSP->codec_id == CODEC_ID_H264
					&& DataFromRTSP->frame_type == 1
					&& VochnInfo[index].PrvType != 0 && !VochnInfo[index].bIsPBStat)
				{
					if(VochnInfo[index].PrvType == Real_Type)//改变解码策略:流畅－实时，清空数据，送最新数据
					{
						BufferSet(VochnInfo[index].VoChn + PRV_VIDEOBUFFER, MAX_ARRAY_NODE);						
						BufferSet(VochnInfo[index].VoChn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
						if(VochnInfo[index].VdecChn >= 0 && VochnInfo[index].IsHaveVdec)
						{
							if(VochnInfo[index].SlaveId == PRV_MASTER)
							{
								CHECK(HI_MPI_VDEC_Query(VochnInfo[index].VdecChn, &pstStat));
								TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Master===VdecChn: %d pstStat.u32LeftStreamFrames: %u\n", VochnInfo[index].VdecChn, pstStat.u32LeftStreamFrames);
								PRV_ReStarVdec(VochnInfo[index].VdecChn);
							}
							else
							{
								SN_SendMccMessageEx(PRV_SLAVE_1, SUPER_USER_ID, MOD_PRV, MOD_PRV, 0, 0, MSG_ID_PRV_MCC_UPDATE_PRVTYPE_REQ, &(VochnInfo[index].VdecChn), sizeof(HI_S32)); 
							}
							
							if(Achn == index)
							{
								PRV_StopAdec();
								VochnInfo[index].AudioInfo.PtNumPerFrm = PtNumPerFrm;
								PRV_StartAdecAo(VochnInfo[index]);	
								IsCreateAdec = 1;
							}
						}
						VoChnState.BeginSendData[chn] = 1;
					}
					else//实时－流畅，等待500ms再开始送数据
					{
						VoChnState.BeginSendData[chn] = 0;
						PtsInfo[chn].BaseVideoPts = DataFromRTSP->pts;
						gettimeofday(&oldTime[chn], NULL);
					}
					VochnInfo[index].PrvType = 0;
				}

				if(DataFromRTSP->codec_id == CODEC_ID_H264 && !VoChnState.FirstHaveVideoData[chn])
					gettimeofday(&oldTime[chn], NULL);
				
				//将最新数据写入本地对应通道的预览缓冲区
				s32Ret = PRV_WriteBuffer(chn, DataFromRTSP->codec_id, DataFromRTSP);
				if(s32Ret != HI_SUCCESS)
				{
					DataFromRTSP = NULL;
					continue;
				}

				//PRV_SendDataThread线程延迟500ms取数据，及预览缓冲区保存500ms数据，防止网络抖动造成卡顿
				if(!VochnInfo[index].bIsPBStat && PtsInfo[chn].FirstVideoPts != (~0x0) && 
					DataFromRTSP->codec_id == CODEC_ID_H264 && !VoChnState.BeginSendData[chn])
				{
					gettimeofday(&curTime[chn], NULL);
					lastTime[chn] = (curTime[chn].tv_sec * 1000000 + curTime[chn].tv_usec) - (oldTime[chn].tv_sec * 1000000 + oldTime[chn].tv_usec);
					lastPts[chn] = (DataFromRTSP->pts - PtsInfo[chn].BaseVideoPts) * 1000/90000;
					//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "chn=%d, WaitIFrameNum=%d, lastPts=%d, lastTime=%d", chn, WaitIFrameNum[chn], lastPts[chn], lastTime[chn]);
					if(lastPts[chn] >= 500 || lastTime[chn] >= 1000 * 1000 || WaitIFrameNum[chn] >= 5)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s Line %d,chn: %d===BeginSendData---DataFromRTSP->pts: %llu, lastPts[chn]: %d, lastTime[chn]: %d",
															__func__,__LINE__,chn, DataFromRTSP->pts, lastPts[chn], lastTime[chn]);
						VoChnState.BeginSendData[chn] = 1;
					}
				}

				if(VoChnState.BeginSendData[chn])
				{
					WaitIFrameNum[chn] = 0;
				}
				
			}
			
			pthread_mutex_unlock(&send_data_mutex);
			if (pdata == NULL)
				usleep(1 * 1000);
		}
	}
}


void* PRV_GetPBDataThread()
{
	HI_S32 s32Ret = 0, chn = 0, index = 0, i = 0, ReGetIndexPts[MAX_IPC_CHNNUM] = {0}, count = 0, TotalSize = 0, cidx = 0;
	HI_S32 DiscardPFrameNum[MAX_IPC_CHNNUM] = {0};
	HI_S32 size = 0, RTSPStates = 0, new_width = 0, new_height = 0, MaxLoopTime = 0;
	unsigned int PreSegNo[MAX_IPC_CHNNUM] = {0}, CurSegNo[MAX_IPC_CHNNUM] = {0};//记录前一个视频的segno值，不管音频的segno
	HI_S32 GetDataCount[MAX_IPC_CHNNUM] = {0};
	AVPacket *DataFromRTSP = NULL;
	char *pdata = NULL;
	RTSP_C_SDPInfo RTSP_SDP;
	SN_MEMSET(&RTSP_SDP, 0, sizeof(RTSP_C_SDPInfo));
	Log_pid(__FUNCTION__);

	
	pthread_detach(pthread_self());
	while(1)
	{
		if(IsUpGrade)
		{
			return NULL;
		}
		if(PlayInfo.PlayBackState < PLAY_INSTANT)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_GetPBDataThread ===sem_wait(&sem_PBGetData)\n");
			
			sem_wait(&sem_PBGetData);	
		}
		while(1)
		{
			if(PlayInfo.PlayBackState < PLAY_INSTANT)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_GetPBDataThread===PlayInfo.PlayBackState != PLAY_INSTANT && PlayInfo.PlayBackState != PLAY_PROCESS\n");
				break;
			}
			
			if(PlayInfo.IsPause && PlayInfo.PlayBackState > PLAY_INSTANT)
			{
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_GetPBDataThread === After stPlayInfo.IsPause===");
				//pthread_mutex_unlock(&send_data_mutex);
				usleep(100 * 1000);
				continue;
			}
			
			if(PlayInfo.PlayBackState == PLAY_INSTANT)
				MaxLoopTime = 1;
			else
				MaxLoopTime = PlayInfo.ImagCount;
			//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "==============PRV_GetPBDataThread===MaxLoopTime: %d\n", MaxLoopTime);
			for(i = 0; i < MaxLoopTime; i++)
			{
				pthread_mutex_lock(&send_data_mutex);
				if(PlayInfo.PlayBackState == PLAY_INSTANT)
				{
					i = PlayInfo.InstantPbChn;
				}
				else
				{
					if(!VochnInfo[i].bIsPBStat)
					{
						//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_GetPBDataThread===chn: %d===No PBState\n", chn);
						pthread_mutex_unlock(&send_data_mutex);
						continue;				
					}
				}
				
				s32Ret = BufferState(i + PRV_VIDEOBUFFER, &count, &TotalSize, &cidx);
				if(PlayStateInfo[i].CurSpeedState != DEC_SPEED_NORMAL && s32Ret == 0 && cidx > PRV_GetBufferSize(i) && VochnInfo[i].IsHaveVdec)
				{
					Ftpc_ChnStopWrite(i, 1);					
					VoChnState.bIsPBStat_StopWriteData[i] = 1;
					TRACE(SCI_TRACE_NORMAL, MOD_DEC, "line: %d======PRV_GetBufferSize(%d)=%d,cidx:%d======bIsPBStat_StopWriteData---StopWrite==1\n", __LINE__, i, PRV_GetBufferSize(i),cidx);
					pthread_mutex_unlock(&send_data_mutex);
					continue;				
				}
				else if(PlayStateInfo[i].CurSpeedState != DEC_SPEED_NORMAL && s32Ret == 0 && cidx < PRV_GetBufferSize(i) && VochnInfo[i].IsHaveVdec)
				{
					Ftpc_ChnStopWrite(i, 0);					
					VoChnState.bIsPBStat_StopWriteData[i] = 0;
				}
				
				//即时回放暂停时不取数据
				if(PlayInfo.PlayBackState == PLAY_INSTANT && PlayStateInfo[PlayInfo.InstantPbChn].CurPlayState == DEC_STATE_NORMALPAUSE)
				{
					if(cidx >= PRV_GetBufferSize(i))
					{
						TRACE(SCI_TRACE_NORMAL, MOD_DEC, "PRV_GetPBDataThread line:%d ===Instant====pause, cidx=%d\n", __LINE__, cidx);
						pthread_mutex_unlock(&send_data_mutex);
						usleep(100 * 1000);
						continue;
					}
				}
				
GetPBDataAgain:				
				s32Ret = BufferGet_Ex(i+1, &pdata, &size, &RTSPStates, PRVUSEONLY);
				if(s32Ret != 0 || pdata == NULL)
				{
					//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===BufferGet_Ex fail===i: %d, s32Ret: %d\n", i, s32Ret);
					//s32Ret = BufferState(1, &count, &TotalSize, &cidx);
					//if(PlayInfo.PlayBackState == PLAY_INSTANT)
					//	usleep(10*1000);
					pthread_mutex_unlock(&send_data_mutex);
					continue;
				}
				
				DataFromRTSP = (AVPacket *)pdata;
				chn = DataFromRTSP->channel;
				index = chn + LOCALVEDIONUM;
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===BufferGet_Ex Success===chn: %d\n", chn);
				//BufferState(1, &count, &TotalSize, &cidx);
				//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===BufferState Success===cidx: %d\n", cidx);
				if(!VochnInfo[index].bIsPBStat)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_DEC, "PRV_GetPBDataThread====chn: %d===Not PBState\n", chn);
					NTRANS_FreeMediaData(DataFromRTSP); 
					DataFromRTSP = NULL;			
					pthread_mutex_unlock(&send_data_mutex);
					continue;
				
				}
				if(RTSPStates == 1)
				{
					VochnInfo[chn].bIsWaitIFrame = 1; 
					
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===PRV_GetPBDataThread===RTSPStates==1,chn: %d\n", chn);
					NTRANS_FreeMediaData(DataFromRTSP); 
					DataFromRTSP = NULL;			
					pthread_mutex_unlock(&send_data_mutex);
					continue;
				}
				s32Ret = PRV_CheckDataPara(DataFromRTSP);
				if(s32Ret != HI_SUCCESS)
				{
					NTRANS_FreeMediaData(DataFromRTSP); 
					DataFromRTSP = NULL;
					VochnInfo[index].bIsWaitIFrame = 1; 
					pthread_mutex_unlock(&send_data_mutex);
					continue;
				}
#if 1
				if(DataFromRTSP->codec_id == CODEC_ID_H264 && g_filesign[chn][0] >= 0 && g_filesign[chn][0] == chn)
				{
					AVPacket *NextPacket = NULL;
					AVPacket *pTmp = NULL;
					int length = 0, TotalLength = 0;
					char StarCode[] = {0x00, 0x00, 0x00, 0x01};
					char name[32] = {0};
					
					pTmp = (AVPacket *)pdata;
					NextPacket = pTmp->next;
					
					while(pTmp != NULL)
					{
						length = pTmp->DataSize - pTmp->BufOffset;
						if(pTmp->frame_type == 0 || pTmp->frame_type == 1)
						{
							if(TotalLength + 4 <= MAXFRAMELEN)
							{
								SN_MEMCPY(VideoData + TotalLength, MAXFRAMELEN, StarCode, 4, 4);
								TotalLength += 4;
							}
							else
							{						
								TRACE(SCI_TRACE_NORMAL, MOD_PRV, "WARNING: TotalLength + 4 > MAXFRAMELEN\n"); 
								continue;
							}

						}
						if(TotalLength + length <= MAXFRAMELEN)
						{
							SN_MEMCPY(VideoData + TotalLength, MAXFRAMELEN, (char*)pTmp + pTmp->BufOffset, length, length);
							TotalLength += length;
						}
						else
						{						
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "WARNING: TotalLength + length > MAXFRAMELEN\n"); 
							continue;
						}
						if(pTmp->Extendnext == NULL)
						{
							pTmp = (AVPacket *)NextPacket;
							if(pTmp != NULL)
								NextPacket = pTmp->next;
						}
						else
						{
							pTmp = (AVPacket *)(pTmp->Extendnext);
						}
						
					}

					SN_SPRINTF(name, sizeof(name), "/tmp/get_ch%d.h264", g_filesign[chn][0]);

					FILE *pf = fopen(name, "a");
					if(NULL!=pf)
					{
						if(TotalLength)
						{
							TRACE(SCI_TRACE_NORMAL, MOD_SLC, "write=%d\n", TotalLength); 
							fwrite(VideoData,TotalLength,1,pf);
						}
						fclose(pf);
					}
					
				}
#endif
				
				if(DataFromRTSP->codec_id == CODEC_ID_H264)
				{
					CurSegNo[chn] = DataFromRTSP->seqno;
				}
				#if 0
				if(PlayInfo.PlayBackState > PLAY_INSTANT
					&& PlayStateInfo[chn].SynState == SYN_PLAYING
					&& PlayStateInfo[chn].RealType == DEC_TYPE_NOREAL
					&& DataFromRTSP->frame_type != 1)
				{
					NTRANS_FreeMediaData(DataFromRTSP); 
					DataFromRTSP = NULL;
					VochnInfo[index].bIsWaitIFrame = 1; 
					pthread_mutex_unlock(&send_data_mutex);
					continue;

				}
				#endif
				//////////////////////////////////////////////////////////
				//等待I帧时，丢弃所有非I帧，音频数据不丢弃
				if(DataFromRTSP->codec_id == CODEC_ID_H264//视频数据
					&& VochnInfo[index].bIsWaitIFrame)//且需要丢帧
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "222222222============bIsWaitIFrame===chn: %d, frame_type: %d\n", chn, DataFromRTSP->frame_type);
				
					if(1 != DataFromRTSP->frame_type)//丢弃P帧，直到下一个I帧
					{	
						DiscardPFrameNum[chn]++;
						PreSegNo[chn] = CurSegNo[chn];
						NTRANS_FreeMediaData(DataFromRTSP); 
						DataFromRTSP = NULL;
						pthread_mutex_unlock(&send_data_mutex);
						continue;
					}
					else
					{
						VochnInfo[index].bIsWaitIFrame = 0;
						DiscardPFrameNum[chn] = 0;
					}
				}
				
				if(DataFromRTSP->codec_id == CODEC_ID_H264//保证是视频数据，才能根据segno值判断是否丢帧
					&& CurSegNo[chn] > PreSegNo[chn]
					&& (CurSegNo[chn] - PreSegNo[chn]) > 1//segno值不连续
					&& (1 != DataFromRTSP->frame_type))//当前帧不是I帧。如果是I帧，之前即使丢了数据也没关系
				{
					VochnInfo[index].bIsWaitIFrame = 1; 		
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Lost Frame---chn: %d, PreSegNo: %d, CurSegNo: %d, CurSegNo-PreSegNo: %d!!!\n", chn, PreSegNo[chn], CurSegNo[chn], CurSegNo[chn] - PreSegNo[chn]);
					PreSegNo[chn] = CurSegNo[chn];
					NTRANS_FreeMediaData(DataFromRTSP); 
					DataFromRTSP = NULL;
					pthread_mutex_unlock(&send_data_mutex);
					continue;
				}
				
				if(DataFromRTSP->codec_id == CODEC_ID_H264 && (CurSegNo[chn] == 1 || CurSegNo[chn] <= PreSegNo[chn]))
				{
					ReGetIndexPts[chn] = 1;
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "========chn: %d, CurSegNo[chn]: %d, frame_type: %d\n", chn, CurSegNo[chn], DataFromRTSP->frame_type);
					if(DataFromRTSP->frame_type != 1)
					{
						VochnInfo[index].bIsWaitIFrame = 1; 
						PreSegNo[chn] = CurSegNo[chn];	
						NTRANS_FreeMediaData(DataFromRTSP); 
						DataFromRTSP = NULL;
						pthread_mutex_unlock(&send_data_mutex);
						continue;
					}
				}
			
				PreSegNo[chn] = CurSegNo[chn];
				
				if(DataFromRTSP->codec_id == CODEC_ID_H264 && DataFromRTSP->frame_type == 1)
				{
					s32Ret = PRV_SearchSPS(chn, DataFromRTSP, &new_width, &new_height);
					//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "index: %d===DataFromRTSP->pts: %lld, PtsInfo[chn].PreGetVideoPts: %lld\n", index, DataFromRTSP->pts, PtsInfo[chn].PreGetVideoPts);			
					if((s32Ret != HI_SUCCESS && VochnInfo[index].VideoInfo.width == 0 && VochnInfo[index].VideoInfo.height == 0
						&& abs((DataFromRTSP->pts - PtsInfo[chn].PreGetVideoPts) * 1000/90000) > 8 * 1000)
						/*|| (s32Ret == HI_SUCCESS && abs((DataFromRTSP->pts - PtsInfo[chn].PreGetVideoPts) * 1000/90000) > 8 * 1000)*/
						|| ReGetIndexPts[chn] == 1)
					{
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "ReGetIndexPts: %d, s32Ret: %d------P-T-S---CHANGE------index: %d\n", ReGetIndexPts[chn], s32Ret, index);			
						PRV_VoChnStateInit(chn);
						BufferSet(chn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
						PtsInfo[chn].PreGetVideoPts = DataFromRTSP->pts;
						PlayStateInfo[chn].SynState = SYN_PLAYING;
						PlayStateInfo[chn].CurPlayState = DEC_STATE_NORMAL;
						//将最新数据写入本地对应通道的预览缓冲区
						s32Ret = PRV_WriteBuffer(chn, DataFromRTSP->codec_id, DataFromRTSP);
						if(s32Ret != HI_SUCCESS)
						{
							DataFromRTSP = NULL;
						}
						ReGetIndexPts[chn] = 0;
						if(VochnInfo[chn].IsHaveVdec)
						{
							Ftpc_ChnStopWrite(chn, 1);					
							VoChnState.bIsPBStat_StopWriteData[chn] = 1;
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d============bIsPBStat_StopWriteData---StopWrite==1\n", __LINE__);
						}
						
						pthread_mutex_unlock(&send_data_mutex);
						continue;					
					}
				
				}
			
				/////////////////////////////////////////////////////////////////////////
				if(DataFromRTSP->codec_id == CODEC_ID_H264)
				{
					PtsInfo[chn].PreGetVideoPts = DataFromRTSP->pts;
					//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "DataFromRTSP->codec_id == CODEC_ID_H264---DataFromRTSP->pts: %llu\n", DataFromRTSP->pts);					
				}
				else
				{
					PtsInfo[chn].PreGetAudioPts = DataFromRTSP->pts;
					//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "DataFromRTSP->codec_id == CODEC_ID_PCMA---DataFromRTSP->pts: %lld\n", DataFromRTSP->pts);					
				}
				s32Ret = PRV_WriteBuffer(chn, DataFromRTSP->codec_id, DataFromRTSP);
				if(s32Ret != HI_SUCCESS)
				{
					DataFromRTSP = NULL;
					pthread_mutex_unlock(&send_data_mutex);
					continue;
				}
				#if 1
				if(DataFromRTSP->codec_id != CODEC_ID_H264)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_DEC, "===DataFromRTSP->codec_id == %d===chn: %d\n", DataFromRTSP->codec_id, chn);
					goto GetPBDataAgain;
				}
				else
				{				
					GetDataCount[chn]++;
					if(GetDataCount[chn] <= 3)
					{
						goto GetPBDataAgain;
					}
					else
					{
						GetDataCount[chn] = 0;
					}
				}
				#endif
				pthread_mutex_unlock(&send_data_mutex);
			}		
			//pthread_mutex_unlock(&send_data_mutex);
			//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "PRV_GetPBDataThread===pthread_mutex_unlock(&send_data_mutex)\n");
			usleep(1 * 1000);
		}
	}
}


/********************************************************
函数名:PRV_SendDataThread
功     能:从本地缓冲区中取数据，每取一个I帧判断分辨率是否变化；
			每取一个I帧检测解码器状态
参     数:无
返回值:  无

*********************************************************/
void *PRV_SendDataThread()
{
	HI_S32 s32Ret = 0, chn = 0, index = 0, IsNoData = 0, GetDataCount = 0, GetDataLoopTime = 0, ret = 0, go_fast = 0;
	HI_S32 count = 0, cidx = 0, TotalSize = 0, PRVStatus = 0, MaxLoopTime = 0, LeftSteamFrames = 0, MaxLeftFrames = 0;
	AVPacket *PRVGetVideoData[MAX_IPC_CHNNUM] = {NULL};
	VDEC_CHN_STAT_S pstStat;	
	VO_QUERY_STATUS_S pstVoStatus;
	char *pv = NULL;
	RTSP_C_SDPInfo RTSP_SDP;
	SN_MEMSET(&RTSP_SDP, 0, sizeof(RTSP_C_SDPInfo));
	HI_S64 Data_OldPts[MAX_IPC_CHNNUM] = {0};
	HI_S64 Data_NewPts[MAX_IPC_CHNNUM] = {0};
	HI_S64 Device_Pts[MAX_IPC_CHNNUM] = {0};
	HI_S64 Device_NowPts[MAX_IPC_CHNNUM] = {0};
	
	SN_MEMSET(h_stream, 0, sizeof(h_stream));
	
	Log_pid(__FUNCTION__);
	pthread_detach(pthread_self());
	while(1)
	{
		if(IsUpGrade)
		{
			return NULL;
		}
		//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "PRV_SendDataThread===pthread_mutex_lock(&send_data_mutex)\n");
		pthread_mutex_lock(&send_data_mutex);
		if(PlayInfo.IsPause && PlayInfo.PlayBackState > PLAY_INSTANT)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_PRV, "PRV_SendPBDataThread === stPlayInfo.IsPause===");
			pthread_mutex_unlock(&send_data_mutex);
			usleep(100 * 1000);
			continue;
		}
		if(PlayInfo.PlayBackState <= PLAY_INSTANT)
			MaxLoopTime = MAX_IPC_CHNNUM;
		else
			MaxLoopTime = PlayInfo.ImagCount;
		
		//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "PRV_SendPBDataThread === MaxLoopTime: %d===", MaxLoopTime);
		for(chn = 0; chn < MaxLoopTime; chn++)
		{
			index = chn + LOCALVEDIONUM;
			
			//非回放下，流畅性预览需要等500ms
			if(!VochnInfo[index].bIsPBStat && (VoChnState.BeginSendData[chn] == 0 || VoChnState.VideoDataCount[chn] <= 0))
			{
				continue;
			}

			if(PlayInfo.PlayBackState >= PLAY_INSTANT)
			{
				SN_MEMSET(&pstStat, 0, sizeof(VDEC_CHN_STAT_S));
				if(HI_MPI_VDEC_Query(VochnInfo[index].VdecChn, &pstStat) != HI_SUCCESS)
					pstStat.u32LeftStreamFrames = 0;
				LeftSteamFrames = (HI_S32)pstStat.u32LeftStreamFrames;
			}
			
			//即时回放暂停时不送数据
			if(PlayInfo.PlayBackState == PLAY_INSTANT && VochnInfo[index].bIsPBStat && PlayStateInfo[chn].CurPlayState == DEC_STATE_NORMALPAUSE)
			{
				
				TRACE(SCI_TRACE_NORMAL, MOD_DEC, "1PRV_SendDataThread line:%d===Instant====pause, LeftSteamFrames=%d\n", __LINE__, LeftSteamFrames);
				if(LeftSteamFrames > 5)
				{
					TRACE(SCI_TRACE_NORMAL, MOD_DEC, "LeftSteamFrames=%d\n", LeftSteamFrames);
					continue;
				}
			}

			if(PlayInfo.PlayBackState > PLAY_INSTANT && VochnInfo[index].bIsPBStat)
			{
				if(LeftSteamFrames > PRV_GetBufferSize(index))	
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "%s line:%d, index=%d, LeftSteamFrames=%d, pic=%d\n", __FUNCTION__, __LINE__, index, LeftSteamFrames, (HI_S32)pstStat.u32LeftPics);
					continue;
				}
			}
			
			
			Data_NewPts[index] = get_cur_time();
			//被动解码状态下无视频数据需要去掉最后一个画面,时间为2秒,并销毁通道，避免从片闪屏
			if(PRV_CurDecodeMode == PassiveDecode && VochnInfo[index].IsHaveVdec && (Data_NewPts[index] - Data_OldPts[index] > 15.0 * 1000000LL))
			{
				//HI_MPI_VO_ClearChnBuffer(DHD0, VochnInfo[index].VoChn, HI_TRUE);
				//PRV_PassiveDisconnect(chn);
			}
			
			//从片正在(重新)创建该通道解码器时，需要等从片处理完再取数据
			if(VochnInfo[index].SlaveId > PRV_MASTER
				&& (VochnInfo[index].MccReCreateingVdec || VochnInfo[index].MccCreateingVdec))
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Slave Is Creating Vdec: %d, %d---%d\n", chn, VochnInfo[index].MccReCreateingVdec, VochnInfo[index].MccCreateingVdec);
				continue;
			}
			//需要将通知从片创建解码通道之前保存的I帧数据发送，否则缺少第一个I帧会花屏
			if(VochnInfo[index].SlaveId > PRV_MASTER && tmp_PRVGetVideoData[chn] != NULL)
			{
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "chn: %d, Send tmp_PRVGetVideoData\n", chn);
				//从片(重新)创建成功
				if(VochnInfo[index].IsHaveVdec)
				{
					PRV_SetChnPlayPts(chn, tmp_PRVGetVideoData[chn]);					
					PRV_SendData(chn, tmp_PRVGetVideoData[chn], CODEC_ID_H264, MasterToSlaveChnId, 0);	
				}
				else
				{
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "chn: %d,	(Re)Create Slave Vdec fail, Not Send tmp_PRVGetVideoData\n", chn);
					VochnInfo[index].bIsWaitGetIFrame = 1;
				}
				NTRANS_FreeMediaData(tmp_PRVGetVideoData[chn]); 
				tmp_PRVGetVideoData[chn] = NULL;
			}
			//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "before PlayStateInfo[index].bIsResetFile\n");
			//回放下切换文件，需要等数据解完
			if(VochnInfo[index].bIsPBStat && PlayStateInfo[index].bIsResetFile)
			{
				IsNoData = 0;
				if(VochnInfo[index].SlaveId == PRV_MASTER)
				{
					HI_MPI_VO_QueryChnStat(0, chn, &pstVoStatus);/* 切换文件时需等待文件数据全部回放完之后再打开新文件，并重设帧率 */
					if (pstVoStatus.u32ChnBufUsed <= 1)
					{
						HI_MPI_VDEC_Query(chn, &pstStat);
						if ((pstStat.u32LeftPics == 0) && (pstStat.u32LeftStreamFrames == 0))
						{
							IsNoData = 1;
						}
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "pstVoStatus.u32ChnBufUsed: %d, pstStat.u32LeftPics: %d, pstStat.u32LeftStreamFrames: %d\n", pstVoStatus.u32ChnBufUsed, pstStat.u32LeftPics, pstStat.u32LeftStreamFrames);
					}
				}
				else
				{
					if(PlayStateInfo[chn].QuerySlaveId == 0)
						IsNoData = 1;
				}
				TRACE(SCI_TRACE_NORMAL, MOD_PRV, "=========PlayStateInfo[index].bIsResetFile====chn: %d, VochnInfo[index].SlaveId: %d, IsNoData: %d\n", chn, VochnInfo[index].SlaveId, IsNoData);
				if(IsNoData)//原数据解完，重新解析原取出的新文件第一个I帧
				{
					if(Pb_FirstGetVideoData[chn] != NULL)
					{
						s32Ret = PRV_DetectResIsChg(chn, Pb_FirstGetVideoData[chn]);
						VochnInfo[index].bIsWaitGetIFrame = 0;
						if(s32Ret == HI_FAILURE)
						{
							NTRANS_FreeMediaData(Pb_FirstGetVideoData[chn]); 
							Pb_FirstGetVideoData[chn] = NULL; 
							VochnInfo[index].bIsWaitGetIFrame = 1;
							continue;
						}
						else if(s32Ret == 1)//从片
						{								
							tmp_PRVGetVideoData[chn] = Pb_FirstGetVideoData[chn];							
						}	
					}
					else
					{							
						VochnInfo[index].bIsWaitGetIFrame = 1;
					}
					PlayStateInfo[index].bIsResetFile = 0;
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "====================line: %d, StartPts: %d\n", __LINE__, PtsInfo[chn].StartPts);
			
					if(PtsInfo[chn].StartPts == 0)
					{
						PRM_ID_TIME AllStartTime, AllEndTime;
						time_t AllStartPts, AllEndPts, QueryFinalPts;
						Ftpc_PlayFileAllTime(chn, &AllStartTime, &AllEndTime);
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========AllEndTime: %d-%d-%d,%d.%d.%d\n", AllEndTime.Year, AllEndTime.Month, AllEndTime.Day, AllEndTime.Hour, AllEndTime.Minute, AllEndTime.Second);
						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========QueryFinalTime: %d-%d-%d,%d.%d.%d\n", PtsInfo[chn].QueryFinalTime.Year, PtsInfo[chn].QueryFinalTime.Month, PtsInfo[chn].QueryFinalTime.Day, PtsInfo[chn].QueryFinalTime.Hour, PtsInfo[chn].QueryFinalTime.Minute, PtsInfo[chn].QueryFinalTime.Second);

						AllStartPts = PlayBack_PrmTime_To_Sec(&AllStartTime);
						AllEndPts = PlayBack_PrmTime_To_Sec(&AllEndTime);
						QueryFinalPts = PlayBack_PrmTime_To_Sec(&PtsInfo[chn].QueryFinalTime);
						Probar_time[chn] = PlayBack_PrmTime_To_Sec(&PtsInfo[chn].QueryStartTime);

						TRACE(SCI_TRACE_NORMAL, MOD_PRV, "====line: %d, AllStartPts=%d, AllEndPts=%d, QueryFinalPts=%d, Probar_time[chn]=%d\n", __LINE__, AllStartPts, AllEndPts, QueryFinalPts, Probar_time[chn]);

						if(Probar_time[chn] < AllStartPts)
						{
							Probar_time[chn] = AllStartPts;
							PtsInfo[chn].QueryStartTime = AllStartTime;
						}
						if(QueryFinalPts > AllEndPts)
						{
							PtsInfo[chn].QueryFinalTime = AllEndTime;
						}
						
					}
					Ftpc_PlayFileCurTime(chn, &PtsInfo[chn].StartPrm, &PtsInfo[chn].EndPrm);
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========PtsInfo[chn].StarPrm: %d-%d-%d,%d.%d.%d\n", PtsInfo[chn].StartPrm.Year, PtsInfo[chn].StartPrm.Month, PtsInfo[chn].StartPrm.Day, PtsInfo[chn].StartPrm.Hour, PtsInfo[chn].StartPrm.Minute, PtsInfo[chn].StartPrm.Second);
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========PtsInfo[chn].EndPrm: %d-%d-%d,%d.%d.%d\n", PtsInfo[chn].EndPrm.Year, PtsInfo[chn].EndPrm.Month, PtsInfo[chn].EndPrm.Day, PtsInfo[chn].EndPrm.Hour, PtsInfo[chn].EndPrm.Minute, PtsInfo[chn].EndPrm.Second);
					PtsInfo[chn].StartPts = PlayBack_PrmTime_To_Sec(&PtsInfo[chn].StartPrm);
					if(Probar_time[chn] < (HI_U64)PtsInfo[chn].StartPts)
					{
						Probar_time[chn] = (HI_U64)PtsInfo[chn].StartPts;
			
					}
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "================PtsInfo[chn].StartPts: %d\n", (int)PtsInfo[chn].StartPts);
			
					//PtsInfo[chn].QueryStartTime = PtsInfo[chn].StartPrm;
					//PtsInfo[chn].QueryFinalTime = PtsInfo[chn].EndPrm;
			
					PtsInfo[chn].CurShowPts = (HI_U64)Probar_time[chn]*1000000;
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========QueryStarTime: %d-%d-%d,%d.%d.%d\n", PtsInfo[chn].QueryStartTime.Year, PtsInfo[chn].QueryStartTime.Month, PtsInfo[chn].QueryStartTime.Day, PtsInfo[chn].QueryStartTime.Hour, PtsInfo[chn].QueryStartTime.Minute, PtsInfo[chn].QueryStartTime.Second);
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "==========QueryFinalTime: %d-%d-%d,%d.%d.%d\n", PtsInfo[chn].QueryFinalTime.Year, PtsInfo[chn].QueryFinalTime.Month, PtsInfo[chn].QueryFinalTime.Day, PtsInfo[chn].QueryFinalTime.Hour, PtsInfo[chn].QueryFinalTime.Minute, PtsInfo[chn].QueryFinalTime.Second);
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "================PtsInfo[chn].CurShowPts: %lld\n", PtsInfo[chn].CurShowPts);

					Ftpc_ChnStopWrite(chn, 0);					
					VoChnState.bIsPBStat_StopWriteData[chn] = 0;
					TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d============bIsPBStat_StopWriteData---StopWrite==0\n", __LINE__);
					if(s32Ret == 1)
					{						
						continue;
					}
				}
				else
				{
					continue;
				}
			}
			
			if(PlayInfo.PlayBackState > PLAY_INSTANT)
				GetDataLoopTime = 1;
			else 
				GetDataLoopTime = 1;
			
			GetDataCount = 0;
			while(GetDataCount < GetDataLoopTime)
			{
				GetDataCount++;

				if((IsAudioOpen && index == 0 && PRV_CurDecodeMode == PassiveDecode) || (IsAudioOpen && index == Achn) || (PlayInfo.PlayBackState > PLAY_INSTANT && chn == 0))
				{
					if(VochnInfo[index].AudioInfo.adoType != -1 && PRV_GetVoiceTalkState() != HI_TRUE)
					{
						ret = PRV_Video_Audio_SynPro(chn);
						if(ret == 1)
						{
							continue;
						}
					}
				}
				
				if(!VoChnState.IsStopGetVideoData[chn]/* || (VochnInfo[index].bIsPBStat && PlayStateInfo[chn].CurPlayState < DEC_STATE_NORMALPAUSE)*/)
				{
					#if 0
					if(VochnInfo[index].bIsPBStat)
					{
						s32Ret = BufferState(chn + PRV_VIDEOBUFFER, &count, &TotalSize, &cidx);
						if(s32Ret == 0 && cidx > 10)
						{
							Ftpc_ChnStopWrite(chn, 1);					
							VoChnState.bIsPBStat_StopWriteData[chn] = 1;
							TRACE(SCI_TRACE_NORMAL, MOD_DEC, "line: %d============bIsPBStat_StopWriteData---StopWrite==1\n", __LINE__);
						}
							
						TRACE(SCI_TRACE_NORMAL, MOD_DEC, "line: %d, s32Ret: %d, chn: %d, BufferState=====cidx: %d\n", __LINE__, s32Ret, chn, cidx);
					}
					#endif

					s32Ret = BufferState(chn + PRV_VIDEOBUFFER, &count, &TotalSize, &cidx);
					//TRACE(SCI_TRACE_NORMAL, MOD_SLC, "PRVStatus==1----------chn: %d, BufferState===count: %d, TotalSize: %d, cidx: %d\n", chn, count, TotalSize, cidx);
					if(VochnInfo[index].PrvType == Real_Type)
					{
						if(cidx > 15)
						{
							go_fast = 1;
						}
						else
						{
							go_fast = 0;
						}

					}
					else
					{
						if(cidx > 30)
						{
							go_fast = 1;
						}
						else
						{
							go_fast = 0;
						}
						
					}
					s32Ret = BufferGet(chn + PRV_VIDEOBUFFER, &pv, &TotalSize, &PRVStatus);
					if(s32Ret == 0 && pv != NULL) 
					{
						Data_OldPts[index] = get_cur_time();
						Device_Pts[index] = get_cur_time();
						
						//如果该数据位置有数据被顶掉，则获取下一个I帧
						if(PRVStatus == 1)
						{
							PRVGetVideoData[chn] = (AVPacket *)pv;
							NTRANS_FreeMediaData(PRVGetVideoData[chn]); 
							PRVGetVideoData[chn] = NULL; 
							s32Ret = PRV_GetFirstIFrame(chn, &pv);
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "s32Ret = %d\n", s32Ret);
							
							if(s32Ret != HI_SUCCESS)
							{
								if(s32Ret == HI_FAILURE)
								{
									VochnInfo[index].bIsWaitGetIFrame = 1;
								}
								break;
							}
							PRVGetVideoData[chn] = (AVPacket *)pv;
							
						}
						else
						{
							PRVGetVideoData[chn] = (AVPacket *)pv;
							//每获取一个I帧，需要检测分辨率是否变化
							if(PRVGetVideoData[chn]->frame_type == 1)
							{							
								s32Ret = PRV_DetectResIsChg(chn, PRVGetVideoData[chn]);
								//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Chn: %d========PRVGetVideoData[chn]->frame_type=========s32Ret: %d\n", chn, s32Ret);
								VochnInfo[index].bIsWaitGetIFrame = 0;
								if(s32Ret == HI_FAILURE)
								{
									NTRANS_FreeMediaData(PRVGetVideoData[chn]); 
									PRVGetVideoData[chn] = NULL; 
									VochnInfo[index].bIsWaitGetIFrame = 1;
									break;
								}
								else if(s32Ret == 1)
								{								
									tmp_PRVGetVideoData[chn] = PRVGetVideoData[chn]; 
									break;
								}
								else if(s32Ret == 2)
								{
									Pb_FirstGetVideoData[chn] = PRVGetVideoData[chn];
									break;
								}
				
							}
						}
						
					//	GetDataCount++;
						if(VochnInfo[index].bIsWaitGetIFrame && PRVGetVideoData[chn]->frame_type != 1)
						{
							NTRANS_FreeMediaData(PRVGetVideoData[chn]); 
							PRVGetVideoData[chn] = NULL; 
							continue;
						}
						
						if(!VoChnState.IsGetFirstData[chn])
						{
							TRACE(SCI_TRACE_NORMAL, MOD_PRV, "Chn: %d========Get First Frame=====%d====\n", chn, VochnInfo[index].bIsPBStat);
							if(VochnInfo[index].bIsPBStat)
							{
								Ftpc_ChnStopWrite(chn, 0);					
								VoChnState.bIsPBStat_StopWriteData[chn] = 0;
								TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d============bIsPBStat_StopWriteData---StopWrite==0\n", __LINE__);
							}
							VoChnState.IsGetFirstData[chn] = 1;
							SendChnInfo.is_first[chn] = 1;
							PtsInfo[chn].DevicePts = get_cur_time();
						}
						
						//非回放下每一个I帧，查询解码器状态(主片)，检测是否需要清空缓冲区，防止延迟太大
						//性能不足时，解码器来不及解，频繁清缓冲区会造成卡顿
						if(!VochnInfo[index].bIsPBStat && PRVGetVideoData[chn]->frame_type == 1 && VochnInfo[index].IsHaveVdec && VochnInfo[index].SlaveId == PRV_MASTER)
						{	
							SN_MEMSET(&pstStat, 0, sizeof(VDEC_CHN_STAT_S));
							if(HI_MPI_VDEC_Query(VochnInfo[index].VdecChn, &pstStat) != HI_SUCCESS)
								pstStat.u32LeftStreamFrames = 0;
							LeftSteamFrames = (HI_S32)pstStat.u32LeftStreamFrames;
							if(LeftSteamFrames < 0)
								printf("====index=%d=====LeftSteamFrames: %d\n", index, LeftSteamFrames);
							
							//性能足够下可能是网络原因导致数据一下来的多导致来不及解
							//尽量不清数据

							if(CurMasterCap <= (TOTALCAPPCHIP/2))
							{
								if(g_PrvType == Real_Type)
								{
									MaxLeftFrames = 25;//50;
								}
								else
								{
									MaxLeftFrames = 45;//50;
								}
								
							}
							else
							{
								if(g_PrvType == Real_Type)
								{
									MaxLeftFrames = 15;//50;
								}
								else
								{
									MaxLeftFrames = 30;//50;
								}
								
							}
							
							//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "===========CurMasterCap: %d, VdecChn: %d, pstStat.u32LeftStreamFrames: %u\n",
							//		CurMasterCap, VochnInfo[index].VdecChn, pstStat.u32LeftStreamFrames);
							if(LeftSteamFrames >= MaxLeftFrames)
							{
								TRACE(SCI_TRACE_NORMAL, MOD_PRV, "======CurMasterCap: %d, g_PrvType=%s, LeftSteamFrames=%d, MaxLeftFrames=%d, VdecChn: %d, pstStat.u32LeftStreamFrames: %u\n",
										CurMasterCap, g_PrvType== Real_Type ? "Real_Type" : "Flow_Type", LeftSteamFrames, MaxLeftFrames, VochnInfo[index].VdecChn, pstStat.u32LeftStreamFrames);
						
								PRV_ReStarVdec(VochnInfo[index].VdecChn);
						
								if(Achn == index && VochnInfo[index].AudioInfo.adoType >= 0)
								{
									PRV_StopAdec();
									VochnInfo[index].AudioInfo.PtNumPerFrm = PtNumPerFrm;
									PRV_StartAdecAo(VochnInfo[index]);	
									IsCreateAdec = 1;
									HI_MPI_ADEC_ClearChnBuf(DecAdec);
								}
							}
						}
					}
					else
					{
						//TRACE(SCI_TRACE_NORMAL, MOD_PRV, "line: %d, BufferGet=====fail===s32Ret: %d\n", __LINE__, s32Ret);
						break;
					}	
				}
				
				if(PRVGetVideoData[chn] != NULL)
				{
					Device_NowPts[index] = get_cur_time()*9/100;
					PtsInfo[index].CurVideoPts = PRVGetVideoData[chn]->pts;
					
					TRACE(SCI_TRACE_NORMAL, MOD_WEB, "11 chn: %d, DeviceIntervalPts=%lld, PreVideoPts=%lld, CurVideoPts=%lld, (%lld) NowPts=%lld, DevicePts=%lld, (%lld)\n", 
							chn, PtsInfo[index].DeviceIntervalPts, PtsInfo[chn].PreVideoPts, PtsInfo[chn].CurVideoPts, (PtsInfo[chn].CurVideoPts - PtsInfo[chn].PreVideoPts), Device_NowPts[index], PtsInfo[chn].DevicePts, (Device_NowPts[index] - PtsInfo[chn].DevicePts));
					
					if(PlayInfo.PlayBackState < PLAY_INSTANT && VoChnState.IsGetFirstData[chn])
					{
						if(go_fast==0 && (PtsInfo[chn].PreVideoPts > 0 && ((Device_NowPts[index] < PtsInfo[chn].DevicePts) || ((Device_NowPts[index] - PtsInfo[chn].DevicePts) < (PtsInfo[chn].CurVideoPts - PtsInfo[chn].PreVideoPts))) && ((PtsInfo[chn].CurVideoPts - PtsInfo[chn].PreVideoPts) <= PtsInfo[index].DeviceIntervalPts)))
						{
							VoChnState.IsStopGetVideoData[chn] = 1;
							TRACE(SCI_TRACE_NORMAL, MOD_WEB, "22 chn: %d, DeviceIntervalPts=%lld, PreVideoPts=%lld, CurVideoPts=%lld, (%lld) NowPts=%lld, DevicePts=%lld, (%lld)\n", 
							chn, PtsInfo[index].DeviceIntervalPts, PtsInfo[chn].PreVideoPts, PtsInfo[chn].CurVideoPts, (PtsInfo[chn].CurVideoPts - PtsInfo[chn].PreVideoPts), Device_NowPts[index], PtsInfo[chn].DevicePts, (Device_NowPts[index] - PtsInfo[chn].DevicePts));
					
							continue;
						}
						else
						{
							if(PtsInfo[chn].PreVideoPts > 0 && PtsInfo[chn].CurVideoPts > PtsInfo[chn].PreVideoPts)
							{
								//PtsInfo[chn].DevicePts += (PtsInfo[chn].CurVideoPts - PtsInfo[chn].PreVideoPts);
								PtsInfo[chn].DevicePts = Device_NowPts[index];
								
								if(PtsInfo[index].DeviceIntervalPts == 0)
								{
									PtsInfo[index].DeviceIntervalPts = PtsInfo[chn].CurVideoPts - PtsInfo[chn].PreVideoPts;
								}
								
								if((PtsInfo[chn].CurVideoPts - PtsInfo[chn].PreVideoPts) > (PtsInfo[index].DeviceIntervalPts + 100))
								{
									PtsInfo[chn].DevicePts = get_cur_time()*9/100;
								}
								else
								{
									PtsInfo[index].DeviceIntervalPts = (PtsInfo[index].DeviceIntervalPts + (PtsInfo[chn].CurVideoPts - PtsInfo[chn].PreVideoPts))/2;
								}
							}
							else
							{
								PtsInfo[chn].DevicePts = get_cur_time()*9/100;
								PtsInfo[index].DeviceIntervalPts = 0;
							}
							
							
							VoChnState.IsStopGetVideoData[chn] = 0;
							TRACE(SCI_TRACE_NORMAL, MOD_WEB, "33 chn: %d, DeviceIntervalPts=%lld, PreVideoPts=%lld, CurVideoPts=%lld, (%lld) NowPts=%lld, DevicePts=%lld, (%lld)\n", 
							chn, PtsInfo[index].DeviceIntervalPts, PtsInfo[chn].PreVideoPts, PtsInfo[chn].CurVideoPts, (PtsInfo[chn].CurVideoPts - PtsInfo[chn].PreVideoPts), Device_NowPts[index], PtsInfo[chn].DevicePts, (Device_NowPts[index] - PtsInfo[chn].DevicePts));
						}

						
					}
					
					PRV_SetChnPlayPts(chn, PRVGetVideoData[chn]);					
					//按设备回放下，非实时回放时，非I帧不送
					if(PlayInfo.PlayBackState > PLAY_INSTANT
						&& PlayStateInfo[chn].SynState == SYN_PLAYING
						&& PlayStateInfo[chn].RealType == DEC_TYPE_NOREAL
						&& PRVGetVideoData[chn]->frame_type != 1)
					{
						//printf("==============line: %d\n", __LINE__);
						NTRANS_FreeMediaData(PRVGetVideoData[chn]); 
						PRVGetVideoData[chn] = NULL;
						break;
					}
					
					s32Ret = PRV_SendData(chn, PRVGetVideoData[chn], CODEC_ID_H264, MasterToSlaveChnId, 0);					
					if(VochnInfo[index].SlaveId == PRV_MASTER)
					{
						if(!VochnInfo[index].bIsPBStat && s32Ret == HI_ERR_VDEC_BUF_FULL)
						{
							PRV_ReStarVdec(VochnInfo[index].VdecChn);
							
							if(Achn == index)
							{
								PRV_StopAdec();
								VochnInfo[index].AudioInfo.PtNumPerFrm = PtNumPerFrm;
								PRV_StartAdecAo(VochnInfo[index]);
								IsCreateAdec = 1;
								//HI_MPI_ADEC_ClearChnBuf(DecAdec);
							}
							
							VochnInfo[index].bIsWaitGetIFrame = 1;						
						}		
						
						if(s32Ret == HI_SUCCESS)
						{
							VoChnState.VideoDataTimeLag[chn] = (HI_S64)(PRVGetVideoData[chn]->pts - PtsInfo[chn].FirstVideoPts) * 1000/90000;//当前视频时间戳与基准时间戳的差值(换算成ms)
						}
						else
						{
							VochnInfo[index].bIsWaitGetIFrame = 1;
						}
						
					}
					
					NTRANS_FreeMediaData(PRVGetVideoData[chn]); 
					PRVGetVideoData[chn] = NULL;
					
				}
				
			}			
		}
		pthread_mutex_unlock(&send_data_mutex);
		//TRACE(SCI_TRACE_NORMAL, MOD_DEC, "PRV_SendDataThread===pthread_mutex_unlock(&send_data_mutex)\n");
		usleep(1000);		
	}
}


int PRV_ResetAudioBuffer(int chn)
{
	if(chn < 0 || chn >= DEV_CHANNEL_NUM)
		return -1;
	if(PlayInfo.PlayBackState >= PLAY_INSTANT)
	{
		pthread_mutex_lock(&send_data_mutex);
		BufferSet(chn + PRV_AUDIOBUFFER, MAX_ARRAY_NODE);
		pthread_mutex_unlock(&send_data_mutex);
	}
	return 0;
}
