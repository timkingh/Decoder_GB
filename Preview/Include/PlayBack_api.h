/***********************************************************************************
  Copyright (C),   2014.1.4-, star-net.
  File name:       PlayBack_api.c
  Author:         罗峰
  Description:    解码器远程回放头文件，包含全局变量定义、宏定义、结构体定义、本文件实现的函数定义
  Others:         
  Function List:   
  History:       
  1. Date:   2014.1.4
  2. Author: 罗峰
  3. Modification: 
*************************************************************************************/

#ifndef __PLAYBACK_API_H__
#define __PLAYBACK_API_H__


void  PlayBack_Pro_PlayReq(SN_MSG *msg);

time_t PlayBack_PrmTime_To_Sec(PRM_ID_TIME *pTime);

void   PlayBack_Sec_To_PrmTime(time_t pts, PRM_ID_TIME *pTime);

void   PlayBack_GetCurPlayTime(UINT8 VoChn, PRM_ID_TIME *pTime);

void   PlayBack_AdaptRealType();

void   PlayBack_ChooseSlaveId(VO_CHN VoChn, HI_S32 width, HI_S32 heigth, HI_S32 framerate);

HI_S32 PlayBack_CheckIntervalPtsEx(HI_S32 VoChn);

void   PlayBack_DoubleClickReq();

void   PlayBack_Stop(UINT8 ImageId);

void   PlayBack_Pause();

void   PlayBack_MccCleanBufferRsp();

void   PlayBack_GetSlaveQueryRsp();

HI_S32 PlayBack_StartAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn, HI_S32 AdecEntype);

HI_S32 PlayBack_StopAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn);

HI_S32 PlayBack_StartVo();

HI_S32 PlayBack_Resume();

HI_S32 PlayBack_Pro_PauseReq();

HI_S32 PlayBack_Pro_ForFastReq();

HI_S32 PlayBack_Pro_ForSlowReq();

HI_S32 PlayBack_GetSpeedCprToNormal();

HI_S32 PlayBack_SetVoChnScreen();

HI_S32 PlayBack_SetVoZoomInWindow();

HI_S32 PlayBack_Pro_SingChnShowReq();

HI_S32 PlayBack_Check_ClearBuffer();

HI_S32 PlayBack_CheckIntervalPtsEx();

HI_S32 PlayBack_CheckIntervalPts();

HI_S32 Check_ChnDecodeRealType();

float  ConvertVdecSizeRatio();

void   PlayBack_Pro_MccFullScreenRsp();

void   PlayBack_ChangeSpeedControl();

void   PlayBack_ShowVoChn();

void   PlayBack_PauseReq();

void   PlayBack_SpeedControl();

void   PlayBack_GetNewVoShowSize();

void   PlayBack_ForWardFastReq();

void   PlayBack_ForWardSlowReq();

void   PlayBack_MsgZoomReq();

void   PlayBack_Pro_MccZoomRsp();
void*  PlayBack_VdecThread();
HI_S32 PlayBack_CreatVdec(VO_DEV VoDev, VO_CHN VoChn);
HI_S32 PlayBack_DestroyVdec(VO_DEV VoDev, VDEC_CHN VdecChn);

HI_S32 PlayBack_MccCreatVdec(SN_MSG *msg);
HI_S32 PlayBack_MccDestroyVdec(SN_MSG *msg);
HI_S32 PlayBack_MccReCreatVdec(SN_MSG *msg);


extern void Ftpc_PlayFileStartPrmTime(VO_CHN VoChn, PRM_ID_TIME *PrmTime);
extern HI_S32 PRV_VdecBindAllVpss(VO_DEV VoDev);
extern RECT_S PRV_ReSetVoRect();
extern int PRV_VPSS_ResetWH(int VpssGrp, int VdChn, int u32MaxW, int u32MaxH);

#endif
