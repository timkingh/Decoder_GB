/***********************************************************************************
  Copyright (C),   2011.12.22-, star-net.
  File name:       Dec_api.h
  Author:          罗峰;   Date: 2011.12.22
  Description:    包含全局变量定义、宏定义、结构体定义、本文件实现的函数定义
  Others:         
  Function List:   
  History:       
  1. Date:   2011.7.12
  2. Author: 罗峰
  3. Modification: 
*************************************************************************************/
#ifndef __DEC_API_H__
#define __DEC_API_H__


#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* End of #ifdef __cplusplus */


void PRV_GetPlayInfo(g_PlayInfo *pPlayInfo);
void PRV_SetPlayInfo(g_PlayInfo *pPlayInfo);

void PRV_GetVoChnPtsInfo(HI_S32 VoChn, g_PRVPtsinfo *pPtsInfo);
void PRV_SetVoChnPtsInfo(HI_S32 VoChn, g_PRVPtsinfo *pPtsInfo);

void PRV_GetVoChnPlayStateInfo(HI_S32 VoChn, g_ChnPlayStateInfo *pPlayStateInfo);
void PRV_SetVoChnPlayStateInfo(HI_S32 VoChn, g_ChnPlayStateInfo *pPlayStateInfo);

int PRV_GetBufferSize(int chn);
HI_S32 PRV_WriteBuffer(HI_S32 chn, int dataType, AVPacket *DataFromRTSP);
HI_S32 PRV_CreateVdecChn(HI_S32 EncType, HI_S32 height, HI_S32 width, HI_U32 u32RefFrameNum, VDEC_CHN VdecChn);
HI_S32 PRV_SendData(HI_S32 chn, AVPacket *PRV_DataFromRTSP, /*int dataLen, */HI_S32 datatype, HI_S32 s32StreamChnIDs, HI_S32 PRV_State);
HI_S32 PRV_SendVideoData(HI_S32 chn);
HI_S32 PRV_SendAudioData(HI_S32 chn);
HI_S32 PRV_WaitDestroyVdecChn(HI_S32 VdecChn);
HI_S32 PRV_ReCreateVdec(HI_S32 VdecChn, int SlaveId, int NewSizeType, int NewEncType);
HI_VOID PRV_ReleaseVdecData(int VdecChn);
HI_S32 PRV_StartAdec(PAYLOAD_TYPE_E AdecEntype, ADEC_CHN AdecChn);
HI_S32 PRV_StartAdecAo(g_PRVVoChnInfo playInfo);
HI_S32 PRV_StopAdec();
HI_VOID PRV_ReStarVdec(VDEC_CHN VdecChn);
HI_U64 PRV_GetChnPts(VO_CHN VoChn);
void PRV_InitVochnInfo(VO_CHN chn);
void PRV_PtsInfoInit(int chn);
void PRV_VoChnStateInit(int chn);
void PRV_DECInfoInit();
void* PRV_FileThread();
void* PRV_GetPrvDataThread();
void* PRV_SendPrvDataThread();
void* PRV_GetPBDataThread();
void* PRV_SendPBDataThread();
void *PRV_SendDataThread();
HI_S32 PRV_SetVochnInfo(g_PRVVoChnInfo *chnInfo, RTSP_C_SDPInfo *RTSP_SDP);
HI_VOID PRV_CheckIsDestroyVdec();
HI_S32 PRV_ChooseSlaveId(int chn, PRV_MccCreateVdecReq *SlaveCreateVdec);
void	PRV_InitHostToSlaveStream();
HI_S32 PRV_ReCreateVdecChn(HI_S32 chn, HI_S32 EncType, HI_S32 new_height, HI_S32 new_width, HI_U32 u32RefFrameNum, HI_S32 NewVdeCap);
void PRV_MCC_RecreateRsp(SN_MSG * msg_rsp);
void PRV_MCC_CreateVdecRsp(SN_MSG * msg_rsp);

extern AUDIO_DATABUF Prv_Audiobuf;
#if defined(Hi3531)||defined(Hi3535)
extern HI_S32 PRV_VDEC_UnBindVpss(VDEC_CHN VdChn, VPSS_GRP VpssGrp);
#endif
extern int PRV_GetVoChnIndex(int VoChn);
extern int PRV_GetPrvStat();
extern HI_S32 PRV_CreateVdecChn_EX(HI_S32 chn);
extern HI_BOOL PRV_GetVoiceTalkState();
extern HI_VOID PRV_FindMasterChnReChooseSlave(int ExtraCap, int index, int TmpIndex[]);
extern HI_VOID PRV_MasterChnReChooseSlave(int index);
extern HI_S32 PRV_AUDIO_AoBindAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn);
extern HI_S32 PRV_AUDIO_AoUnbindAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif

