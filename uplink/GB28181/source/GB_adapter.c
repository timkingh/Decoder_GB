#include "GB_sipd.h"
#include "GB_adapter.h"

#if (DVR_SUPPORT_GB == 1)	

extern int GB_Send_Response(GB_CONNECT_STATE *gb_cons, void *cmd_struct, gb_CommandType_enum cmdType, osip_call_id_t **call_id);
extern int GB_Add_Record_Node(GB_CONNECT_STATE *gb_cons,  GB_Record_Node *new_record);
extern GB_Record_Node * GB_Find_Record_Node_by_cmdType(GB_CONNECT_STATE *gb_cons, int cmdType, int idx, int *index);
extern int GB_Send_Reply(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, int status_code);


/********************************************************
函数名: DeviceID2LocalChn
参数: deviceID[in]  统一编码
		 localchn[out]  对应本地通道号，0:解码器设备编号，[1,GB_TOTAL_MONITOR_NUM):监视器编号
功能: 查找deviceID 所对应的通道号
********************************************************/
int DeviceID2LocalChn(char *deviceID, int *localchn)
{
	int chn;
	PRM_GB_SIPD_MONITOR_CFG monitor_cfg;
	PRM_GB_SIPD_CFG gb_cfg;

	*localchn = -1;
	
	if(deviceID == NULL || localchn == NULL)
	{
		return -1;
	}

	SN_MEMSET(&gb_cfg,0,sizeof(gb_cfg));

	GB_Get_GBCfg(&gb_cfg);

	if(strcmp((char *)gb_cfg.deviceID, deviceID) == 0)
	{
		*localchn = 0;
		return 0;
	}
	else
	{
		for(chn=1; chn<=GB_TOTAL_MONITOR_NUM; chn++)
		{
			SN_MEMSET(&monitor_cfg,0,sizeof(monitor_cfg));

			if(GetParameter (PRM_ID_GB_SIPD_MONITOR_CFG, NULL, &monitor_cfg, sizeof(PRM_GB_SIPD_MONITOR_CFG), chn, 
					SUPER_USER_ID, NULL) != PARAM_OK)
			{
				printf("%s line=%d PRM_ID_GB_SIPD_MONITOR_CFG GetParameter err\n",__FUNCTION__, __LINE__);
				continue;
			}
			
			if(SN_STRLEN((char *)monitor_cfg.MonitorID) > 0 && strcmp((char *)monitor_cfg.MonitorID, deviceID) == 0)
			{
				*localchn = chn;
				return 0;
			}
		}
	}
	
	TRACE(SCI_TRACE_NORMAL,MOD_GB,"Can't Matches %s to LocalChn\n",deviceID);		
	
	return 0;
}

int GB_UpdateParam(int Prm_id, void *prm_info, int len, int record)
{
	param_manage_info *sinfo = NULL;	
	int size = sizeof (param_manage_info) + len;
	sinfo = SN_MALLOC (size);
	if (sinfo == NULL)
	{
		return -1;
	}
	SN_MEMSET (sinfo, 0, size);
	sinfo->prm_id = Prm_id;
	SN_MEMCPY (sinfo->prm_info, len, prm_info, len, len);
	sinfo->len = len;
	sinfo->record = record;
	sinfo->userid = SUPER_USER_ID;
	sinfo->opt = PARAM_UPDATE;
	SendMessageEx (SUPER_USER_ID, MOD_GB, MOD_FWK, 0, 0, MSG_ID_PRM_REQ, sinfo, size);
	if (sinfo)
	{
		SN_FREE (sinfo);
		sinfo = NULL;
	}
	
	return 0;	
}


int GB_Deal_Query_DeviceStatus(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, gb_BaseInfo_Query *DeviceStatus)
{
	int chn = -1;
	gb_BaseInfo_Query *req = NULL;
	gb_Query_DeviceStatus_Rsp rsp;
	SN_DEVICEINFO stDeviceInfo;
	ALM_ALL_STATUS_ST pstStatus;
	int status_code = 200;
	
	time_t ltime;
	struct tm result;

	if(gb_cons == NULL || osip_event == NULL || DeviceStatus == NULL)
	{
		return -1;
	}
	
	req = DeviceStatus;
	
	SN_MEMSET(&rsp,0,sizeof(rsp));
	SN_MEMSET(&ltime,0,sizeof(ltime));	
	SN_MEMSET(&result,0,sizeof(result));
	SN_MEMSET(&stDeviceInfo,0,sizeof(stDeviceInfo));
	SN_MEMSET(&pstStatus,0,sizeof(pstStatus));

	DeviceID2LocalChn(req->DeviceID.deviceID, &chn);

	rsp.BaseInfo.Cmdtype = req->Cmdtype;
	rsp.BaseInfo.SN = req->SN;		
	SN_STRCPY(rsp.BaseInfo.DeviceID.deviceID, sizeof(rsp.BaseInfo.DeviceID.deviceID), req->DeviceID.deviceID);

	DVRGetVersionINfo(&stDeviceInfo);
	
	if(chn == 0)  // 解码器本身
	{		
		rsp.Result = resultType_OK;
		rsp.Online = ONLINE;
		rsp.Status = resultType_OK;

		rsp.Encode = NO_SHOW_ITEM;
		rsp.Decode= statusType_ON;
		rsp.Record = NO_SHOW_ITEM;
		
#if 0
		PRM_NET_CFG_ADV stGet;

		SN_MEMSET(&stGet, 0x0, sizeof(PRM_NET_CFG_ADV));
		
		if (GetParameter(PRM_ID_NET_CFG_ADV, NULL, &stGet, sizeof(PRM_NET_CFG_ADV), 
			1, SUPER_USER_ID, NULL) != OK)
		{
			TRACE(SCI_TRACE_HIGH, MOD_GB, "Get param fail %s line=%d\n", __FUNCTION__, __LINE__);					
			break;
		}
		
		time(&ltime );
		ltime = ltime - ((int)(stGet.TimezoneSelect) - GMT_ADD_0) * 3600;
#else
		time(&ltime );
#endif
		gmtime_r( &ltime, &result );
		SN_SPRINTF(rsp.DeviceTime,sizeof(rsp.DeviceTime),"%04d-%02d-%02dT%02d:%02d:%02d",
				result.tm_year + 1900,result.tm_mon+1,result.tm_mday,result.tm_hour,result.tm_min,result.tm_sec);	
	}
	else if(chn > 0 && chn <= GB_TOTAL_MONITOR_NUM) // 监视器
	{		
		rsp.Result = resultType_OK;
		rsp.Online = ONLINE;
		rsp.Status = resultType_OK;

		rsp.Encode = NO_SHOW_ITEM;
		rsp.Decode= statusType_OFF;
		rsp.Record = NO_SHOW_ITEM;
		
#if 0
		PRM_NET_CFG_ADV stGet;

		SN_MEMSET(&stGet, 0x0, sizeof(PRM_NET_CFG_ADV));
		
		if (GetParameter(PRM_ID_NET_CFG_ADV, NULL, &stGet, sizeof(PRM_NET_CFG_ADV), 
			1, SUPER_USER_ID, NULL) != OK)
		{
			TRACE(SCI_TRACE_HIGH, MOD_GB, "Get param fail %s line=%d\n", __FUNCTION__, __LINE__);					
			break;
		}
		
		time(&ltime );
		ltime = ltime - ((int)(stGet.TimezoneSelect) - GMT_ADD_0) * 3600;
#else
		time(&ltime );
#endif
		gmtime_r( &ltime, &result );
		SN_SPRINTF(rsp.DeviceTime,sizeof(rsp.DeviceTime),"%04d-%02d-%02dT%02d:%02d:%02d",
				result.tm_year + 1900,result.tm_mon+1,result.tm_mday,result.tm_hour,result.tm_min,result.tm_sec);	
	}
	else
	{
		rsp.Result = resultType_ERROR;
		rsp.Online = OFFLINE;
		rsp.Status = resultType_ERROR;
		SN_STRCPY(rsp.Reason,sizeof(rsp.Reason),"Device ID not exist");

		rsp.Encode = NO_SHOW_ITEM;
		rsp.Decode= NO_SHOW_ITEM;
		rsp.Record = NO_SHOW_ITEM;
	}

	GB_Send_Reply(gb_cons,osip_event,status_code);

	if(status_code == 200)
		GB_Send_Response(gb_cons,(void *)&rsp, rsp.BaseInfo.Cmdtype, NULL);
		
	return 0;
}

int GB_Deal_Query_Catalog(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, gb_Catalog_Query *Catalog)
{
	gb_Catalog_Query *req = NULL;
	gb_Query_Catalog_Rsp rsp;
	int i, chn = -1;
	PRM_GB_SIPD_CFG gb_cfg;
	int status_code = 200;

	if(gb_cons == NULL || Catalog == NULL)
	{
		return -1;
	}

	req = Catalog;
	
	SN_MEMSET(&rsp,0,sizeof(rsp));
	SN_MEMSET(&gb_cfg,0,sizeof(gb_cfg));

	DeviceID2LocalChn(req->Query.DeviceID.deviceID, &chn);

	rsp.BaseInfo.Cmdtype = req->Query.Cmdtype;
	rsp.BaseInfo.SN = req->Query.SN;		
	SN_STRCPY(rsp.BaseInfo.DeviceID.deviceID, sizeof(rsp.BaseInfo.DeviceID.deviceID), req->Query.DeviceID.deviceID);

	GB_Get_GBCfg(&gb_cfg);
	
	if(chn == 0)  // 解码器本身
	{
		gb_itemType *DeviceList = NULL;
		PRM_GB_SIPD_MONITOR_CFG monitor_cfg;

		rsp.DeviceList = DeviceList = (gb_itemType *)SN_MALLOC(sizeof(gb_itemType)*GB_TOTAL_MONITOR_NUM);
		if(DeviceList == NULL)
		{
			printf("%s	line=%d  gb_Query_Catalog_Rsp  SN_MALLOC  Err\n",__FUNCTION__,__LINE__);
			status_code = 500;  // 服务器内部错误
		}
		else
		{
			SN_MEMSET(DeviceList,0,sizeof(gb_itemType)*GB_TOTAL_MONITOR_NUM);
			
			for(i=0; i<GB_TOTAL_MONITOR_NUM; i++)
			{
				SN_MEMSET(&monitor_cfg,0,sizeof(monitor_cfg));
			
				if(GetParameter (PRM_ID_GB_SIPD_MONITOR_CFG, NULL, &monitor_cfg, sizeof(PRM_GB_SIPD_MONITOR_CFG), 
						i+1, SUPER_USER_ID, NULL) != PARAM_OK)
				{
					printf("%s line=%d  GetParameter PRM_ID_GB_SIPD_MONITOR_CFG err\n",__FUNCTION__, __LINE__);
					continue;
				}

				if(SN_STRLEN((char *)monitor_cfg.MonitorID) <= 0)
				{
					continue;
				}

				SN_STRCPY(DeviceList->DeviceID.deviceID,sizeof(DeviceList->DeviceID.deviceID),(char *)monitor_cfg.MonitorID);	
				SN_STRCPY(DeviceList->Name,sizeof(DeviceList->Name),(char *)monitor_cfg.MonitorID);

				SN_STRCPY(DeviceList->Manufacturer,sizeof(DeviceList->Manufacturer),"PRIVATE");

				SN_STRCPY(DeviceList->Model,sizeof(DeviceList->Model),"VIDEOOUT");
				SN_STRCPY(DeviceList->Owner,sizeof(DeviceList->Owner),"Owner");
				SN_STRCPY(DeviceList->CivilCode,sizeof(DeviceList->CivilCode),"UNKNOW");
				DeviceList->Block = NO_SHOW_ITEM;
				SN_STRCPY(DeviceList->Address,sizeof(DeviceList->Address),"Address");
				DeviceList->Parental = 0;
				SN_STRCPY(DeviceList->ParentID,sizeof(DeviceList->ParentID),(char *)gb_cfg.deviceID);
				DeviceList->RegisterWay = 1;
				DeviceList->Secrecy = 0;
				DeviceList->Status = statusType_ON;
				DeviceList->Longitude = NO_SHOW_ITEM;
				DeviceList->Latitude = NO_SHOW_ITEM;

				DeviceList++;
				rsp.SumNum++;
				rsp.Num = rsp.SumNum;
			}
		}
		if(osip_event != NULL)
			GB_Send_Reply(gb_cons,osip_event,status_code);

		if(status_code == 200)
		{
			GB_Send_Response(gb_cons,(void *)&rsp, rsp.BaseInfo.Cmdtype, NULL);
		}
			
		if(rsp.DeviceList)
			SN_FREE(rsp.DeviceList);
		
	}
	else if(chn > 0 && chn <= GB_TOTAL_MONITOR_NUM) // 监视器
	{
		rsp.SumNum = 0;

		if(osip_event != NULL)
			GB_Send_Reply(gb_cons,osip_event,status_code);
			
		GB_Send_Response(gb_cons,(void *)&rsp, rsp.BaseInfo.Cmdtype, NULL);
	}
	else
	{
		GB_Send_Reply(gb_cons,osip_event,400);
	}

	return 0;
}


int GB_Deal_Query_DeviceInfo(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, gb_BaseInfo_Query *DeviceInfo)
{
	int chn = -1;
	gb_BaseInfo_Query *req = NULL;
	gb_Query_DeviceInfo_Rsp rsp;
	SN_DEVICEINFO stDeviceInfo;
	int status_code = 200;

	if(gb_cons == NULL || osip_event == NULL || DeviceInfo == NULL)
	{
		return -1;
	}
	
	req = DeviceInfo;
	
	SN_MEMSET(&rsp,0,sizeof(rsp));
	SN_MEMSET(&stDeviceInfo,0,sizeof(stDeviceInfo));

	DeviceID2LocalChn(req->DeviceID.deviceID, &chn);

	rsp.BaseInfo.Cmdtype = req->Cmdtype;
	rsp.BaseInfo.SN = req->SN;		
	SN_STRCPY(rsp.BaseInfo.DeviceID.deviceID, sizeof(rsp.BaseInfo.DeviceID.deviceID), req->DeviceID.deviceID);

	DVRGetVersionINfo(&stDeviceInfo);
	
	if(chn == 0)  // 解码器本身
	{
		rsp.Result = resultType_OK;
		SN_STRCPY(rsp.DeviceName, sizeof(rsp.DeviceName), stDeviceInfo.DeviceName);
	#ifndef BLANKDVR	
		SN_STRCPY(rsp.Manufacturer, sizeof(rsp.Manufacturer), "STAR-NET");
	#else
		SN_STRCPY(rsp.Manufacturer, sizeof(rsp.Manufacturer), "OEM");
	#endif
		SN_STRCPY(rsp.Model, sizeof(rsp.Model), (char *)stDeviceInfo.ProductType);
		SN_SPRINTF(rsp.Firmware,sizeof(rsp.Firmware), "V%d.%d.%d.%d", 
														(stDeviceInfo.dwSoftwareVersion&0XFF000000)>>24, 
                                                          (stDeviceInfo.dwSoftwareVersion&0X00FF0000)>>16, 
                                                          (stDeviceInfo.dwSoftwareVersion&0X0000FF00)>>8,
                                                          (stDeviceInfo.dwSoftwareVersion&0X000000FF));

		SN_STRCPY(rsp.DeviceType, sizeof(rsp.DeviceType), "Decoder");
		rsp.Channel = NO_SHOW_ITEM;
		rsp.MaxCamera = NO_SHOW_ITEM;
		rsp.MaxAlarm = NO_SHOW_ITEM;
		rsp.MaxOut = GB_TOTAL_MONITOR_NUM;
	}
	else if(chn > 0 && chn <= GB_TOTAL_MONITOR_NUM) // 监视器
	{
		rsp.Result = resultType_OK;
	#if 0
		char tmp[64] = {0};
		SN_SPRINTF(tmp,sizeof(tmp),"Monitor_%d",chn);
		SN_STRCPY(rsp.DeviceName, sizeof(rsp.DeviceName), tmp);
	#endif
	#ifndef BLANKDVR	
		SN_STRCPY(rsp.Manufacturer, sizeof(rsp.Manufacturer), "STAR-NET");
	#else
		SN_STRCPY(rsp.Manufacturer, sizeof(rsp.Manufacturer), "OEM");
	#endif
		SN_STRCPY(rsp.Model, sizeof(rsp.Model), (char *)stDeviceInfo.ProductType);
		SN_STRCPY(rsp.Firmware, sizeof(rsp.Firmware), "TODO");
		SN_STRCPY(rsp.DeviceType, sizeof(rsp.DeviceType), "Output");
		rsp.Channel = NO_SHOW_ITEM;
		rsp.MaxCamera = NO_SHOW_ITEM;
		rsp.MaxAlarm = NO_SHOW_ITEM;
		rsp.MaxOut = GB_TOTAL_MONITOR_NUM;
	}
	else
	{
		rsp.Result = resultType_ERROR;
		rsp.Channel = NO_SHOW_ITEM;
		rsp.MaxCamera = NO_SHOW_ITEM;
		rsp.MaxAlarm = NO_SHOW_ITEM;
		rsp.MaxOut = NO_SHOW_ITEM;
	}

	GB_Send_Reply(gb_cons,osip_event,status_code);
	
	GB_Send_Response(gb_cons,(void *)&rsp, rsp.BaseInfo.Cmdtype, NULL);
	return 0;
}


int GB_Deal_Query_Req(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, gb_Query_Req_Struct *Req)
{
	if(gb_cons == NULL || osip_event == NULL || Req == NULL)
	{
		return -1;
	}
	
	if(Req->DeviceStatus)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d GB_Deal_Query_DeviceStatus\n",__FUNCTION__,__LINE__);
		GB_Deal_Query_DeviceStatus(gb_cons, osip_event, Req->DeviceStatus);
	}
	else if(Req->Catalog)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d GB_Deal_Query_Catalog\n",__FUNCTION__,__LINE__);
		GB_Deal_Query_Catalog(gb_cons, osip_event, Req->Catalog);
	}
	else if(Req->DeviceInfo)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d GB_Deal_Query_DeviceInfo\n",__FUNCTION__,__LINE__);
		GB_Deal_Query_DeviceInfo(gb_cons, osip_event, Req->DeviceInfo);
	}
	else if(Req->RecordInfo)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Don't Deal With RecordInfo\n",__FUNCTION__,__LINE__);
	}
	else if(Req->Alarm)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Don't Deal With Alarm\n",__FUNCTION__,__LINE__);
	}
	else if(Req->ConfigDownload)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Don't Deal With ConfigDownload\n",__FUNCTION__,__LINE__);
	}
	else if(Req->PersetQuery)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Don't Deal With PersetQuery\n",__FUNCTION__,__LINE__);
	}

	return 0;
}

int GB_Send_Control_RSP(GB_CONNECT_STATE *gb_cons, gb_BaseInfo_Query *BaseInfo, int Result)
{
	gb_DeviceControl_Rsp rsp;
	
	if(gb_cons == NULL || BaseInfo == NULL)
	{
		return -1;
	}

	SN_MEMSET(&rsp,0,sizeof(rsp));

	SN_MEMCPY(&(rsp.BaseInfo),sizeof(gb_BaseInfo_Query),BaseInfo,sizeof(gb_BaseInfo_Query),sizeof(gb_BaseInfo_Query));

	rsp.Result = Result;

	GB_Send_Response(gb_cons,(void *)&rsp,rsp.BaseInfo.Cmdtype,NULL);

	return 0;
}


int GB_Deal_Control_DeviceControl(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, gb_DeviceControl_Req *DeviceControl)
{
	int chn = -1;
	
	if(gb_cons == NULL || osip_event== NULL || DeviceControl == NULL)
	{
		return -1;
	}

	DeviceID2LocalChn(DeviceControl->BaseInfo.DeviceID.deviceID, &chn);
	
	if(SN_STRLEN(DeviceControl->PTZCmd) > 0)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Don't Deal With PTZCmd\n",__FUNCTION__,__LINE__);
		GB_Send_Reply(gb_cons,osip_event,400);
		return 0;
	}
	else if(DeviceControl->TeleBoot == 1)
	{
		if(chn == 0)
		{
			GB_Send_Reply(gb_cons,osip_event,200);

			if(MMI_IsInConfig() == TRUE)
			{
				GB_Send_Control_RSP(gb_cons, &(DeviceControl->BaseInfo), resultType_ERROR);
			}
			else
			{
				TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d  TeleBoot\n",__FUNCTION__,__LINE__);

				SendMessageEx (SUPER_USER_ID, MOD_GB, MOD_FWK, 0, 0, MSG_ID_FWK_REBOOT_REQ, NULL, 0);
			}
		}
		else
		{
			GB_Send_Reply(gb_cons,osip_event,200);
			GB_Send_Control_RSP(gb_cons, &(DeviceControl->BaseInfo), resultType_ERROR);
		}

		return 0;
	}
	else if(DeviceControl->RecordCmd != -1)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Don't Deal With RecordCmd\n",__FUNCTION__,__LINE__);
		GB_Send_Reply(gb_cons,osip_event,400);
		return 0;
	}
	else if(DeviceControl->GuardCmd != -1)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Don't Deal With GuardCmd\n",__FUNCTION__,__LINE__);
		GB_Send_Reply(gb_cons,osip_event,400);
		return 0;
	}
	else if(DeviceControl->AlarmCmd == 1)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Don't Deal With AlarmCmd\n",__FUNCTION__,__LINE__);
		GB_Send_Reply(gb_cons,osip_event,400);
		return 0;
	}
	else if(DeviceControl->DragZoomFlag != 0)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Don't Deal With DragZoom\n",__FUNCTION__,__LINE__);
		GB_Send_Reply(gb_cons,osip_event,400);
		return 0;
	}

	GB_Send_Reply(gb_cons,osip_event,400);
	
	return 0;
}


int GB_Deal_Control_Req(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, gb_Control_Req_Struct *Req)
{
	if(gb_cons == NULL || osip_event == NULL || Req == NULL)
	{
		return -1;
	}
	
	if(Req->DeviceControl)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d GB_Deal_Control_DeviceControl\n",__FUNCTION__,__LINE__);
		GB_Deal_Control_DeviceControl(gb_cons, osip_event, Req->DeviceControl);
	}
	else if(Req->DeviceConfig)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Don't Deal With DeviceConfig\n",__FUNCTION__,__LINE__);
		GB_Send_Reply(gb_cons,osip_event,400);
	}

	return 0;
}

#endif
