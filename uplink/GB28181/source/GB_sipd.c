#include "GB_sipd.h"
#include "GB_adapter.h"

#if (DVR_SUPPORT_GB == 1)	

static int GBMsgQueue = -1;

int localmsg_writeSock = -1;
int localmsg_readSock = -1;
int localmsg_sockpair[2];
pthread_mutex_t localmsgSockPairLock;

int gbmsg_writeSock = -1;
int gbmsg_readSock = -1;
int gbmsg_sockpair[2];
pthread_mutex_t gbmsgSockPairLock;

static PRM_GB_SIPD_CFG gGBCfg;
pthread_mutex_t gGBCfgLock;

char gb_LocalIP[16];

static int gGBConnStatus;  //  注册状态，0-未注册，1-已注册，2-离线
pthread_mutex_t gGBConnStatusLock;

static int gb_ipchange = 0;
static long long gb_ipchange_time = 0;

static int gGBMode = 0; // 1-GB模式
PRM_Decode Decode_backup;
PRM_PasDecodeInfo PasDecodeInfo_backup;



extern GB_Record_Node * GB_Find_Record_Node_by_cmdType(GB_CONNECT_STATE *gb_cons, int cmdType, int idx, int *index);
extern int GB_Remove_Record_Node(GB_CONNECT_STATE *gb_cons, int index);
extern int GB_Send_One_Notify(GB_CONNECT_STATE *gb_cons, GB_Subscribe_Node *sub_node, void *info);
extern int GB_Send_Response(GB_CONNECT_STATE *gb_cons, void *cmd_struct, gb_CommandType_enum cmdType, osip_call_id_t **call_id);
extern int GB_Sned_One_Catalog_Notify(GB_CONNECT_STATE *gb_cons, int chn);
extern int GB_Send_DownLoadSDP(GB_CONNECT_STATE *gb_cons,GB_media_session *media_session);
extern int GB_UpdateParam(int Prm_id, void *prm_info, int len, int record);
extern void* GB_DecodePSData(void *Param);

static int socket_nonblock(int socket, int enable)
{
	if(enable)
      return fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) | O_NONBLOCK);
	else
      return fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) & ~O_NONBLOCK);
}

int GB_CreateSocket(int type, int port)
{
	int sockfd = -1;
	struct linger so_linger;
	int on;
	struct sockaddr_in seraddr;

	SN_MEMSET(&so_linger,0,sizeof(so_linger));

	sockfd = socket(AF_INET, type, 0);
	if(sockfd < 0)
	{
		perror("GB_CreateSocket");
		return -1;
	}

	socket_nonblock(sockfd, 1);
	so_linger.l_onoff = 1;
	so_linger.l_linger = 0;
	setsockopt(sockfd,SOL_SOCKET,SO_LINGER,&so_linger,sizeof(so_linger));

	if(port > 0)
	{
		#if defined(SO_REUSEADDR)	
		on = 1;
		if ((setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) == -1)
		{
			perror("setsockopt SO_REUSEADDR error");
		}
		#endif	

		bzero(&seraddr,sizeof(seraddr));
		seraddr.sin_family = AF_INET;
		seraddr.sin_addr.s_addr = htonl(INADDR_ANY);
		seraddr.sin_port= htons(port);
		
		if(bind(sockfd, (struct sockaddr*)&seraddr, sizeof(seraddr)) < 0)
		{
			perror("bind() error");
			close(sockfd);
			return -1;
		}
	}
	

	return sockfd;
}

void GB_GetLocalIPaddrFromSock(int sockfd,char *localIP, int localIP_len)
{
	struct sockaddr_in clientAddr;
	socklen_t clientAddrLen = sizeof(clientAddr);
	char ipAddress[16] = {0};
	
	if(localIP == NULL || sockfd <= 0)
	{
		return ;	
	}

	if(getsockname(sockfd, (struct sockaddr*)&clientAddr, &clientAddrLen) != 0)
	{
		perror("AM_GetLocalIPaddr getsockname");
		return ;
	}

	inet_ntop(AF_INET, &clientAddr.sin_addr, ipAddress, sizeof(ipAddress));

	SN_STRNCPY(localIP,localIP_len,ipAddress,sizeof(ipAddress));
	return ;
}

int GB_Set_LocalIP(char *ip)
{
	if(ip == NULL)
	{
		return -1;
	}

	SN_MEMSET(gb_LocalIP, 0, sizeof(gb_LocalIP));

	SN_STRCPY(gb_LocalIP, sizeof(gb_LocalIP),ip);

	return 0;
}

char *GB_Get_LocalIP()
{
	return gb_LocalIP;
}

int GB_Refresh_GBCfg()
{	
	SN_MEMSET(&gGBCfg,0,sizeof(gGBCfg));

	pthread_mutex_lock(&gGBCfgLock);
#if 0
	if(GetParameter (PRM_ID_GB_SIPD_CFG, NULL, &gGBCfg, sizeof(PRM_GB_SIPD_CFG), 1, 
			SUPER_USER_ID, NULL) != PARAM_OK)
	{
		printf("%s line=%d PRM_ID_GB_SIPD_CFG GetParameter err\n",__FUNCTION__, __LINE__);
		return -1;
	}
#else
	gGBCfg.enable = 1;
	SN_STRCPY((char *)gGBCfg.deviceID,sizeof(gGBCfg.deviceID),"34020000001140000003");
	SN_STRCPY((char *)gGBCfg.reg_pwd,sizeof(gGBCfg.reg_pwd),"12345678");
	SN_STRCPY((char *)gGBCfg.sipserver_ID,sizeof(gGBCfg.sipserver_ID),"34020000002000000001");
	gGBCfg.sipserver_ip[0] = 192;
	gGBCfg.sipserver_ip[1] = 168;
	gGBCfg.sipserver_ip[2] = 6;
	gGBCfg.sipserver_ip[3] = 133;
	gGBCfg.sipserver_port = 5060;
	gGBCfg.keepalive_interval = 60;
	gGBCfg.keepalive_timeout_cnt = 3;
	gGBCfg.register_period = 3600;
	gGBCfg.local_port = 5060;
#endif
	pthread_mutex_unlock(&gGBCfgLock);

	return 0;
}

int GB_Get_GBCfg(PRM_GB_SIPD_CFG *gb_cfg)
{
	if(gb_cfg == NULL)
	{
		return -1;
	}
	pthread_mutex_lock(&gGBCfgLock);
	SN_MEMCPY(gb_cfg,sizeof(PRM_GB_SIPD_CFG),&gGBCfg,sizeof(PRM_GB_SIPD_CFG),sizeof(PRM_GB_SIPD_CFG));
	pthread_mutex_unlock(&gGBCfgLock);

	return 0;
}


int GB_Set_gGBConnStatus(int status)
{
	if(status < 0 || status > 2)
	{
		return -1;
	}

	pthread_mutex_lock(&gGBConnStatusLock);
	gGBConnStatus = status;
	pthread_mutex_unlock(&gGBConnStatusLock);

	return 0;
}

int GB_Get_gGBConnStatus(void)
{
	int status = -1;
	
	pthread_mutex_lock(&gGBConnStatusLock);
	status = gGBConnStatus;
	pthread_mutex_unlock(&gGBConnStatusLock);

	return status;
}


static void GB_reset_recv_buffer(GB_CONNECT_STATE *gb_cons, int msglen)
{
	if(msglen != 0 && gb_cons->datasize > msglen)  // 接收到下一会话的部分数据
	{
//		memmove(gb_cons->buffer, gb_cons->buffer + msglen, gb_cons->datasize - msglen);
		SN_MEMCPY(gb_cons->buffer,msglen,gb_cons->buffer + msglen,gb_cons->datasize - msglen,gb_cons->datasize - msglen);
		gb_cons->datasize -= msglen;

		SN_MEMSET(gb_cons->buffer+(gb_cons->datasize - msglen), 0, gb_cons->buffer_size - (gb_cons->datasize - msglen));
	}
	else
	{
		SN_MEMSET(gb_cons->fix_buffer,0,sizeof(gb_cons->fix_buffer));
		if (gb_cons->buffer != NULL && gb_cons->buffer != gb_cons->fix_buffer)
		{
			SN_FREE(gb_cons->buffer);		
		}
		gb_cons->buffer = gb_cons->fix_buffer;
		gb_cons->buffer_size = sizeof(gb_cons->fix_buffer);
		gb_cons->datasize = 0;
	}

	gb_cons->buffer_ptr = gb_cons->buffer + gb_cons->datasize;
	gb_cons->buffer_end = gb_cons->buffer + gb_cons->buffer_size;
}

static void GB_reset_Record_Node_list(GB_CONNECT_STATE *gb_cons)
{
	GB_Record_Node *record = NULL;
	osip_event_t *osip_event = NULL;

	while((record = (GB_Record_Node *)osip_list_get(&(gb_cons->record_node_list), 0)) != NULL)
	{
		if(record->cmd == 0 && record->call_id == NULL && record->data == NULL)
		{
			osip_event = (osip_event_t *)record->info;

			if(osip_event != NULL)
			{
				gb_sip_free(osip_event);
				osip_event = NULL;
			}
		}
		else
		{
			if(record->call_id)
				osip_call_id_free(record->call_id);
			if(record->data)
				SN_FREE(record->data);
			if(record->info)
				SN_FREE(record->info);
		}
		
		osip_list_remove(&(gb_cons->record_node_list), 0);
		SN_FREE(record);
		record = NULL;
	}
}


void GB_ResetConState(GB_CONNECT_STATE *gb_cons)
{
	if (gb_cons == NULL)
		return;

	TRACE(SCI_TRACE_NORMAL,MOD_GB,"GB_ResetConState\n");
	
	gb_cons->cur_state = GB_STATE_IDEL;
	gb_cons->connfd = -1;
	gb_cons->local_cseq = 1;
	gb_cons->bUnRegister = 0;

	if (gb_cons->wwwa)
	{
		osip_www_authenticate_free(gb_cons->wwwa);
		gb_cons->wwwa = NULL;	
	}
			
	GB_reset_recv_buffer(gb_cons, 0);

	if(osip_list_size(&(gb_cons->record_node_list)) > 0)
	{
		GB_reset_Record_Node_list(gb_cons);
	}
	osip_list_init(&(gb_cons->record_node_list));


	gb_cons->keepalive_timeout_cnt = 0;
	gb_cons->last_keepalivetime = 0;
	gb_cons->last_sendtime = 0;
	gb_cons->last_registertime = 0;

	return ;
}



int GB_Send_KeepAlive(GB_CONNECT_STATE *gb_cons)
{
	gb_Keepalive_Struct Keepalive;
	PRM_GB_SIPD_CFG gb_cfg;
		
	if(gb_cons == NULL)
	{
		return -1;
	}

	SN_MEMSET(&Keepalive,0,sizeof(Keepalive));
	SN_MEMSET(&gb_cfg,0,sizeof(gb_cfg));

	GB_Get_GBCfg(&gb_cfg);

	Keepalive.SN = 43;
	SN_STRCPY(Keepalive.DeviceID.deviceID,sizeof(Keepalive.DeviceID.deviceID),(char *)gb_cfg.deviceID);
	Keepalive.resultType = resultType_OK;
	
	GB_sipd_Keepalive(gb_cons, &Keepalive);

	if(Keepalive.errorDeviceID != NULL)
		SN_FREE(Keepalive.errorDeviceID);

	return 0;
}

int GB_Change_Mode(int Flag)
{
	PRM_Decode decodemode;
	PRM_PasDecodeInfo PasDecodeInfo;
	
	SN_MEMSET(&decodemode,0,sizeof(decodemode));
	SN_MEMSET(&PasDecodeInfo,0,sizeof(PasDecodeInfo));
	
	if(Flag == 1 && gGBMode != 1)
	{
		printf("Enter GB28181 Mode\n");
		gGBMode = 1;
		
		SN_MEMSET(&Decode_backup,0,sizeof(Decode_backup));
		SN_MEMSET(&PasDecodeInfo_backup,0,sizeof(PasDecodeInfo_backup));
		if(GetParameter (PRM_ID_DECODEMODE_CFG, NULL, &Decode_backup, sizeof(PRM_Decode), 1, 
				SUPER_USER_ID, NULL) != PARAM_OK)
		{
			printf("%s line=%d PRM_ID_DECODEMODE_CFG GetParameter err\n",__FUNCTION__, __LINE__);
			return -1;
		}
		if(GetParameter (PRM_ID_PASSIVECHN_CFG, NULL, &PasDecodeInfo_backup, sizeof(PRM_PasDecodeInfo), 1, 
				SUPER_USER_ID, NULL) != PARAM_OK)
		{
			printf("%s line=%d PRM_ID_PASSIVECHN_CFG GetParameter err\n",__FUNCTION__, __LINE__);
			return -1;
		}

		decodemode.DecodeMode = PassiveDecode;

		GB_UpdateParam(PRM_ID_DECODEMODE_CFG,&decodemode,sizeof(decodemode),1);
		GB_UpdateParam(PRM_ID_PASSIVECHN_CFG,&PasDecodeInfo,sizeof(PasDecodeInfo),1);
	}
	
	if(Flag == 0 && gGBMode == 1)
	{
		printf("Exit GB28181 Mode\n");
		gGBMode=0;

		GB_UpdateParam(PRM_ID_DECODEMODE_CFG,&Decode_backup,sizeof(Decode_backup),1);
		GB_UpdateParam(PRM_ID_PASSIVECHN_CFG,&PasDecodeInfo_backup,sizeof(PasDecodeInfo_backup),1);
	}

	return 0;
}

static void *GB_Msg(void *data)
{
	int ret;
	SYS_MSG sys_msg;
	SN_MSG *msg = NULL;
	pthread_detach(pthread_self());
	
	if (GBMsgQueue < 0)
		return NULL;
	
	Log_pid(__FUNCTION__);
	while(1)
	{
		msg = SN_GetMessage(GBMsgQueue, MSG_GET_WAIT_ROREVER, &ret);
		if(ret == -1 || msg == NULL)
		{
			usleep(10);
			continue;
		}
		else
		{
			TRACE(SCI_TRACE_NORMAL,MOD_GB,"GB_Msg source:0x%x dest:0x%x  msg_id:0x%x\n",msg->source,msg->dest,msg->msgId);
			switch (msg->msgId)
			{
				default:
				{
					SN_MEMSET(&sys_msg, 0, sizeof(sys_msg));
					sys_msg.mtype = 1;
					sys_msg.pmsg = msg;
					sendto(localmsg_writeSock, &sys_msg, sizeof(sys_msg), 0, NULL, 0);
				}
				break;
			}
		}
	}
	return NULL;
}

static void GB_MsgHandle(SN_MSG *msg, GB_CONNECT_STATE *gb_cons)
{
	if (msg == NULL)
		return;

	switch (msg->msgId)
	{
		case MSG_ID_FWK_UPDATE_PARAM_IND:
		{
			stParamUpdateNotify *stNotify = (stParamUpdateNotify *)msg->para;
			switch(stNotify->prm_id)
			{
				case PRM_ID_GB_SIPD_CFG:
				{
					if(GB_Get_gGBConnStatus() == 0)
					{
						if(gb_cons->connfd > 0)
						{
							close(gb_cons->connfd);
							GB_ResetConState(gb_cons);
						}
						GB_Refresh_GBCfg();
					}
					else if((gb_cons->cur_state == GB_STATE_RUNNING && gb_cons->bUnRegister == 1)// 正在注销
						|| (GB_Get_gGBConnStatus() == 2) // 离线
						)  
					{
						if(gb_cons->connfd > 0)
						{
							close(gb_cons->connfd);
							GB_ResetConState(gb_cons);
						}
						GB_Set_gGBConnStatus(0);
						GB_Refresh_GBCfg();
					}
					else
					{
						gb_cons->bUnRegister = 1;
						GB_sipd_register(gb_cons, 1); // 不带认证的注销请求
						gb_cons->last_sendtime = get_cur_time()/1000;
						GB_Set_gGBConnStatus(0);

						//  退出国标模式
						GB_Change_Mode(0);
					}		
				}
				break;
				
				default:
				break;
			}
		}
		break;
		
		case MSG_ID_FWK_REBOOT_REQ:
		case MSG_ID_FWK_POWER_OFF_REQ:
		case MSG_IF_FWK_IPCHANGE_IND:
		{
			PRM_GB_SIPD_CFG gb_cfg;
			
			TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d    msg->msgId=%d\n",__FUNCTION__,__LINE__,msg->msgId);

			SN_MEMSET(&gb_cfg,0,sizeof(gb_cfg));

			GB_Get_GBCfg(&gb_cfg);

			if(gb_cfg.enable != 1)
			{
				break;
			}
			
			if(GB_Get_gGBConnStatus() == 1)
			{
				gb_cons->bUnRegister = 1;
				GB_sipd_register(gb_cons, 1); // 不带认证的注销请求
				gb_cons->last_sendtime = get_cur_time()/1000;
			}
			else
			{
				if(gb_cons->connfd > 0)
				{
					close(gb_cons->connfd);
					GB_ResetConState(gb_cons);
				}
			}

			gb_ipchange = 1;
			gb_ipchange_time = get_cur_time()/1000;
		}
		break;

		case MSG_ID_GB_GET_STATUS_REQ:
		{
			GB_GET_STATUS_RSP rsp;

			SN_MEMSET(&rsp,0,sizeof(rsp));

			rsp.result = 0;
			rsp.status = GB_Get_gGBConnStatus();

			SendMessageEx(msg->user, MOD_GB, msg->source, msg->xid, msg->thread, msg->msgId + 1, &rsp, sizeof(rsp));
		}
		break;
		
		default:
		{
			
		}
		break;
	}
	FreeMessage(&msg);
}


static void* GB_Server(void *pParam)
{
	pthread_detach(pthread_self());
	PRM_GB_SIPD_CFG gb_cfg;
	struct pollfd poll_table[MAX_GB_MSG_NUM+MAX_GB_CONNECTION_NUM+1];
	struct pollfd *poll_entry = NULL;
	int i, ret;
	int rlen = 0;
	GB_CONNECT_STATE *gb_cons = NULL;
	char localip[16] = {0};	
	char sipserver_ip[16] = {0};
	char localmsg_buf[GB_MAX_PLAYLOAD_BUF];

	gb_cons = SN_MALLOC(MAX_GB_CONNECTION_NUM*sizeof(GB_CONNECT_STATE));
	if(gb_cons == NULL)
	{
		printf("SN_MALLOC gb_cons Err!\n");
		return NULL;
	}
	SN_MEMSET(gb_cons, 0, MAX_GB_CONNECTION_NUM*sizeof(GB_CONNECT_STATE));	
	SN_MEMSET(&gb_cfg,0,sizeof(gb_cfg));

	for(i=0; i<MAX_GB_CONNECTION_NUM; i++)
	{
		GB_ResetConState(&gb_cons[i]);
	}

	Log_pid(__FUNCTION__);
	
	while(1)
	{
		SN_MEMSET(poll_table, 0, sizeof(poll_table));
		poll_entry = poll_table;

		if(localmsg_readSock > 0)
		{
			poll_entry->fd = localmsg_readSock;
			poll_entry->events = POLLIN;
			poll_entry++;
		}
		if(gbmsg_readSock > 0)
		{
			poll_entry->fd = gbmsg_readSock;
			poll_entry->events = POLLIN;
			poll_entry++;
		}

		if(gb_ipchange == 1)
		{
			if(IsTimeOfArrival(gb_ipchange_time,30))
			{
				gb_ipchange = 0;
			}
		}

		for(i=0; i<MAX_GB_CONNECTION_NUM; i++)
		{
			gb_cons[i].poll_act = NULL;

			if(gb_cons[i].cur_state > GB_STATE_CONNECTING)
			{
				poll_entry->fd = gb_cons[i].connfd;
				poll_entry->events = POLLIN;
				gb_cons[i].poll_act = poll_entry;
				poll_entry++;
			}
			else if(gb_cons[i].cur_state == GB_STATE_IDEL)
			{
				if(gb_ipchange == 1)
				{
					TRACE(SCI_TRACE_NORMAL,MOD_GB, "IP Changed! Please Wait a Moment!");
					continue;
				}

				GB_Get_GBCfg(&gb_cfg);

				if(gb_cfg.enable == 1)  // 启用
				{
					if(SN_STRLEN((char *)gb_cfg.deviceID) <= 0 || SN_STRLEN((char *)gb_cfg.sipserver_ID) <= 0)
					{
						TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d deviceID=%s   sipserver_ID=%s\n",__FUNCTION__,__LINE__,gb_cfg.deviceID,gb_cfg.sipserver_ID);

						if(gb_cons[i].connfd > 0)
							close(gb_cons[i].connfd);
						GB_ResetConState(&gb_cons[i]);
						GB_Refresh_GBCfg();
						continue;
					}

					//   进入国标模式
//					ret = GB_Change_Mode(1);
//					if(ret < 0)
//					{
//						continue;
//					}
					
					if(gb_cfg.transfer_protocol == GB_TRANSFER_UDP) // UDP
					{
						gb_cons[i].connfd = GB_CreateSocket(SOCK_DGRAM, gb_cfg.local_port);
					}
					else // TCP
					{
						gb_cons[i].connfd = GB_CreateSocket(SOCK_STREAM, gb_cfg.local_port);
					}

					if(gb_cons[i].connfd <= 0)
					{
						TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d GB_CreateSocket Err\n",__FUNCTION__,__LINE__);
						continue;
					}

					SN_MEMSET(sipserver_ip,0,sizeof(sipserver_ip));
					SN_SPRINTF(sipserver_ip,sizeof(sipserver_ip),"%d.%d.%d.%d",
						gb_cfg.sipserver_ip[0],gb_cfg.sipserver_ip[1],gb_cfg.sipserver_ip[2],gb_cfg.sipserver_ip[3]);

					gb_cons[i].transfer_protocol = gb_cfg.transfer_protocol;
					SN_MEMSET(&gb_cons[i].remoteAddr,0,sizeof(gb_cons[i].remoteAddr));
					gb_cons[i].remoteAddr.sin_family = AF_INET;
					gb_cons[i].remoteAddr.sin_addr.s_addr =inet_addr(sipserver_ip);
					gb_cons[i].remoteAddr.sin_port = htons(gb_cfg.sipserver_port);
					gb_cons[i].beginconect_time = system_uptime();
					gb_cons[i].cur_state = GB_STATE_CONNECTING;	
					ret = connect(gb_cons[i].connfd, (struct sockaddr *) &(gb_cons[i].remoteAddr), sizeof(gb_cons[i].remoteAddr));
					if ( ret == 0 || errno == EISCONN)
					{
						gb_cons[i].cur_state = GB_STATE_REGISTER;	
						poll_entry->fd = gb_cons[i].connfd;
						poll_entry->events = POLLIN;
						gb_cons[i].poll_act = poll_entry;
						poll_entry++;				

						GB_GetLocalIPaddrFromSock(gb_cons[i].connfd,localip,sizeof(localip));
						GB_Set_LocalIP(localip);
						
						GB_sipd_register(&gb_cons[i], 0); // 不带认证的注册请求
						gb_cons[i].last_sendtime = get_cur_time()/1000;
					}
				}
				else
				{
					GB_Set_gGBConnStatus(0);
					usleep(100);
				}
			}
			else if(gb_cons[i].cur_state == GB_STATE_CONNECTING)
			{
				ret = connect(gb_cons[i].connfd, (struct sockaddr *) &(gb_cons[i].remoteAddr), sizeof(gb_cons[i].remoteAddr));
				if ( ret == 0 || errno == EISCONN)
				{
					gb_cons[i].cur_state = GB_STATE_REGISTER;	
					poll_entry->fd = gb_cons[i].connfd;
					poll_entry->events = POLLIN;
					gb_cons[i].poll_act = poll_entry;
					poll_entry++;			

					GB_GetLocalIPaddrFromSock(gb_cons[i].connfd,localip,sizeof(localip));
					GB_Set_LocalIP(localip);
					
					GB_sipd_register(&gb_cons[i], 0); // 不带认证的注册请求
					gb_cons[i].last_sendtime = get_cur_time()/1000;
				}
				else
				{
					if (system_uptime() - gb_cons[i].beginconect_time > 10)
					{
						TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d connect timeout\n",__FUNCTION__,__LINE__);
						close(gb_cons[i].connfd);
						GB_ResetConState(&gb_cons[i]);						
					}
				}
			}

			if(gb_cons[i].cur_state == GB_STATE_REGISTER)  // 未成功注册上
			{
				if(IsTimeOfArrival(gb_cons[i].last_sendtime, 60)) // 间隔60s 后重新注册
				{
					TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d REGISTER Fail !  try again!\n",__FUNCTION__,__LINE__);
					close(gb_cons[i].connfd);
					GB_ResetConState(&gb_cons[i]);
					GB_Refresh_GBCfg();
				}
			}
			else if(gb_cons[i].cur_state == GB_STATE_RUNNING && gb_cons[i].bUnRegister == 1) // 注销检查
			{
				if(IsTimeOfArrival(gb_cons[i].last_sendtime, 5)) // 超过5s 则认为成功注销
				{
					TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d UNREGISTER Success!\n",__FUNCTION__,__LINE__);
					close(gb_cons[i].connfd);
					GB_ResetConState(&gb_cons[i]);
					GB_Set_gGBConnStatus(0);
					GB_Refresh_GBCfg();
				}
			}
			else if(gb_cons[i].cur_state == GB_STATE_RUNNING && gb_cons[i].bUnRegister != 1)
			{
				if(IsTimeOfArrival(gb_cons[i].last_registertime, gb_cfg.register_period-60)) // 刷新注册, 提前60s 
				{
					gb_cons[i].cur_state = GB_STATE_REGISTER;	
					GB_sipd_register(&gb_cons[i], 0); // 不带认证的注册请求
					gb_cons[i].last_sendtime = get_cur_time()/1000;
				}

				if(gb_cons[i].keepalive_timeout_cnt > gb_cfg.keepalive_timeout_cnt)  // 对方离线
				{
					close(gb_cons[i].connfd);
					GB_ResetConState(&gb_cons[i]);
					GB_Set_gGBConnStatus(2);
				}
				else if(IsTimeOfArrival(gb_cons[i].last_keepalivetime, gb_cfg.keepalive_interval-1)) // 心跳, 减1 是为了去除poll 的超时时间的影响
				{
					gb_cons[i].keepalive_timeout_cnt++;
					
					GB_Send_KeepAlive(&gb_cons[i]);
					
					gb_cons[i].last_keepalivetime = get_cur_time()/1000;
				}
			}
		}

		ret = poll(poll_table, poll_entry-poll_table, 1000);

		if(ret<0) 
		{
			perror("GB_Server:poll error");
			poll(NULL, 0, 1000);
			continue;
		}
		poll_entry = poll_table;

		if(localmsg_readSock > 0)
		{
			if(poll_entry->revents & POLLIN)
			{
				// 处理本地消息
				SN_MEMSET(localmsg_buf, 0, sizeof(localmsg_buf));
				rlen = recvfrom(localmsg_readSock, localmsg_buf,  sizeof(localmsg_buf), 0, NULL, 0);
				if (rlen > 0 && rlen <= sizeof(localmsg_buf))
				{
					SN_MSG * pMsg;
					SYS_MSG *sys_msg = (SYS_MSG *)localmsg_buf;
					pMsg = sys_msg->pmsg;
					GB_MsgHandle(pMsg, &(gb_cons[0]));
				}
			}

			poll_entry++;
		}

		if(gbmsg_readSock > 0)
		{
			if(poll_entry->revents & POLLIN)
			{
				// 处理媒体流模块发来的消息

				SN_MEMSET(localmsg_buf, 0, sizeof(localmsg_buf));
				rlen = recvfrom(gbmsg_readSock, localmsg_buf,  sizeof(localmsg_buf), 0, NULL, 0);
				if (rlen > 0 && rlen <= sizeof(localmsg_buf))
				{
					
				}
			}
			

			poll_entry++;
		}

		for(i=0; i<MAX_GB_CONNECTION_NUM; i++)
		{
			if (gb_cons[i].poll_act != NULL)
			{
				if(gb_cons[i].poll_act->revents & POLLIN)
				{
					rlen = recv(gb_cons[i].connfd, gb_cons[i].buffer_ptr,  gb_cons[i].buffer_end - gb_cons[i].buffer_ptr, 0);
					if(rlen > 0)
					{
						gb_cons[i].buffer_ptr += rlen;
						gb_cons[i].datasize += rlen;

						ret = is_recv_whole_messages(&gb_cons[i]);
						
						if(ret > 0) // 接收完全部数据
						{
							//处理接收到的数据			
							GB_handle_messages(&gb_cons[i]);

							GB_reset_recv_buffer(&gb_cons[i], ret);
						}
					}
				}
				else if (gb_cons[i].poll_act->revents & POLLERR || gb_cons[i].poll_act->revents & POLLHUP || gb_cons[i].poll_act->revents & POLLNVAL)
				{															
					if(gb_cons[i].cur_state == GB_STATE_RUNNING 
							&& gb_cons[i].keepalive_timeout_cnt > gb_cfg.keepalive_timeout_cnt)
					{
						close(gb_cons[i].connfd);
						GB_ResetConState(&gb_cons[i]);
						GB_Set_gGBConnStatus(2);
						GB_Refresh_GBCfg();
					}
				}
			}
		}
		
	}

	return NULL;
}

int GB28181_InitParam(void)
{
	int ret = -1;

	pthread_mutex_init(&localmsgSockPairLock, NULL);
	pthread_mutex_init(&gbmsgSockPairLock, NULL);
	pthread_mutex_init(&gGBCfgLock, NULL);
	pthread_mutex_init(&gGBConnStatusLock, NULL);

	if (0 != socketpair(AF_LOCAL,SOCK_STREAM, 0,localmsg_sockpair))
	{
		perror("socketpair");
	    	return -1;
	}
	fcntl(localmsg_sockpair[0], F_SETFL, fcntl(localmsg_sockpair[0], F_GETFL) | O_NONBLOCK);
	fcntl(localmsg_sockpair[1], F_SETFL, fcntl(localmsg_sockpair[1], F_GETFL) | O_NONBLOCK);
	localmsg_writeSock = localmsg_sockpair[0];
	localmsg_readSock = localmsg_sockpair[1];
	
	if (0 != socketpair(AF_LOCAL,SOCK_STREAM, 0,gbmsg_sockpair))
	{
		perror("socketpair");
	    	return -1;
	}
	fcntl(gbmsg_sockpair[0], F_SETFL, fcntl(gbmsg_sockpair[0], F_GETFL) | O_NONBLOCK);
	fcntl(gbmsg_sockpair[1], F_SETFL, fcntl(gbmsg_sockpair[1], F_GETFL) | O_NONBLOCK);
	gbmsg_writeSock = gbmsg_sockpair[0];
	gbmsg_readSock = gbmsg_sockpair[1];

	ret = parser_init();  // osip 全局变量初始化
	if (ret != 0)
	{
		printf("%s line=%d parser_init failed \n", __FUNCTION__, __LINE__);
		return -1;
	}

	GB_Refresh_GBCfg();
	gGBConnStatus = 0;
	gGBMode=0;
	
	return 0;
}

int GB28181_init(void)
{
	pthread_t msg_tid, protocol_tid,data_send_tid;
	int ret = -1;
	
	ret = GB28181_InitParam();
	if(ret < 0)
	{
		printf("GB28181_InitParam ERR! ret=%d\n",ret);
		return -1;
	}
	
	if((GBMsgQueue = CreatQueque(MOD_GB)) == -1)
	{
		printf("CreatQueque(MOD_GB) ERR\n");
		return -1;
	}

	if(pthread_create(&msg_tid,NULL,GB_Msg,NULL)!=0)
	{
		printf("%s line=%d create GB_Msg thread failed \n", __FUNCTION__, __LINE__);
		return -1;
	}

	if(pthread_create(&protocol_tid,NULL,GB_Server,NULL)!=0)
	{
		printf("%s line=%d create GB_Server thread failed \n", __FUNCTION__, __LINE__);
		return -1;
	}

	if(pthread_create(&data_send_tid,NULL,GB_DecodePSData,NULL) != 0)
	{
		printf("%s line=%d create GB_Server thread failed \n", __FUNCTION__, __LINE__);
		return -1;
	}
	
	return 0;
}

#endif
