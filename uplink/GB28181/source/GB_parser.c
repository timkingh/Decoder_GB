#include "GB_sipd.h"
#include "GB_adapter.h"


#if (DVR_SUPPORT_GB == 1)	

static char *Transport_Str[] = {
	"UDP",
	"TCP",
};

extern int DeviceID2LocalChn(char *deviceID, int *localchn);
extern int gb_msg_WriteData(int size, void *data);
extern int GB_Refresh_GBCfg();
extern int GB_Set_gGBConnStatus(int status);
int GB_Send_Reply(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, int status_code);


static int GB_SocketSendData(int socketFd, char *host, int port, void *pBuffer, int size, int flag)
{
	int sent=0;
	int tmpres=0;
	int ret = 0;
	int delay = 0;
	struct pollfd poll_table[1];	
	struct pollfd *poll_entry;

	if (socketFd < 0)
		return -1;

	while(sent < size)
	{
		SN_MEMSET(poll_table, 0, sizeof(poll_table));
		poll_entry = poll_table;

		poll_entry->fd = socketFd;
		poll_entry->events = POLLOUT;
		poll_entry++;

		delay = 1000;

		ret = poll(poll_table, poll_entry-poll_table, delay);
		if(ret<0) 
		{
			perror("GB_SocketSendData:poll error");
			return -1;
		}
		poll_entry = poll_table;
		/*new connection*/
		if(poll_entry->revents & POLLOUT) 
		{
			tmpres = send(socketFd, pBuffer+sent, size-sent, flag);
			if (tmpres < 0)
			{
				perror("GB socketSend");
				return -1;
			}
			sent += tmpres;
		}
		else if (poll_entry->revents & POLLERR || poll_entry->revents & POLLHUP || poll_entry->revents & POLLNVAL)
		{
			return -1;
		}
	}
	return sent;
}

/*
	is_recv_whole_messages 判断是否接收完全部的数据
	返回值: 0 - 未接收完
				> 0 - 已接收完, 其值为所要接收数据的长度
*/
int is_recv_whole_messages(GB_CONNECT_STATE *gb_cons)
{
	int consumed = 0;
	char *end_headers;
	char *buf = gb_cons->buffer;
	size_t buflen = gb_cons->datasize;

	while (buflen > 0 && (end_headers = gb_buffer_find (buf, buflen, GB_END_HEADERS_STR)) != NULL) 
	{
		int clen, msglen;
		char *clen_header;

		if (buf == end_headers) 
		{
			/* skip tcp standard keep-alive */
			OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "socket %s:%i: standard keep alive received (CRLFCRLF)\n", sockinfo->remote_ip, sockinfo->remote_port, buf));
			consumed += 4;
			buflen -= 4;
			buf += 4;
			continue;
		}

		/* stuff a nul in so we can use osip_strcasestr */
		*end_headers = '\0';

		/* ok we have complete headers, find content-length: or l: */
		clen_header = osip_strcasestr (buf, GB_CLEN_HEADER_STR);
		if (!clen_header)
			clen_header = osip_strcasestr (buf, GB_CLEN_HEADER_STR2);
		if (!clen_header)
			clen_header = osip_strcasestr (buf, GB_CLEN_HEADER_COMPACT_STR);
		if (!clen_header)
			clen_header = osip_strcasestr (buf, GB_CLEN_HEADER_COMPACT_STR2);
		if (clen_header != NULL)
		{
			clen_header = strchr (clen_header, ':');
			clen_header++;
		}
		if (!clen_header) 
		{
			/* Oops, no content-length header.      Presume 0 (below) so we
			consume the headers and make forward progress.  This permits
			server-side keepalive of "\r\n\r\n". */
			OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "socket %s:%i: message has no content-length: <%s>\n", sockinfo->remote_ip, sockinfo->remote_port, buf));
		}
		clen = clen_header ? atoi (clen_header) : 0;

		/* undo our overwrite and advance end_headers */
		*end_headers = GB_END_HEADERS_STR[0];
		end_headers += gb_const_strlen (GB_END_HEADERS_STR);

		/* do we have the whole message? */
		msglen = (int) (end_headers - buf + clen);
		if (msglen > buflen)
		{
			/* nope */
			if (msglen > gb_cons->buffer_size && msglen < 10*1024*1024)
			{
				char *tmpbuf = SN_MALLOC(msglen+1024);
				if (tmpbuf == NULL)
				{
					printf("%s  line=%d  malloc error\n",__FUNCTION__,__LINE__);
					return consumed;
				}
				SN_MEMSET(tmpbuf,0,msglen+1024);
				
				SN_MEMCPY(tmpbuf, msglen+1024, gb_cons->buffer, gb_cons->buffer_size, gb_cons->buffer_size);
				if (gb_cons->buffer != gb_cons->fix_buffer)
				{
					SN_FREE(gb_cons->buffer);	
				}
				gb_cons->buffer = tmpbuf;
				gb_cons->buffer_size = msglen+1024;
				gb_cons->buffer_ptr = gb_cons->buffer + gb_cons->datasize;
				gb_cons->buffer_end = gb_cons->buffer + gb_cons->buffer_size;
			}
			return consumed;
		}

		consumed += msglen;
		buflen -= msglen;
		buf += msglen;
	}

	return consumed;
}

int GB_Add_Record_Node(GB_CONNECT_STATE *gb_cons,  GB_Record_Node *new_record)
{
	GB_Record_Node *record = NULL;

	if(gb_cons == NULL || new_record == NULL)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d  Err\n",__FUNCTION__,__LINE__);
		return -1;
	}

	//  size > 200 先清空链表?
	
	record = (GB_Record_Node *)SN_MALLOC(sizeof(GB_Record_Node));
	if(record == NULL)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d  Err\n",__FUNCTION__,__LINE__);
		return -1;
	}
	SN_MEMSET(record,0,sizeof(GB_Record_Node));

	SN_MEMCPY(record,sizeof(GB_Record_Node),new_record,sizeof(GB_Record_Node),sizeof(GB_Record_Node));

	if(new_record->call_id != NULL)
	{
		osip_call_id_clone(new_record->call_id,&(record->call_id));
	}

	if(new_record->data != NULL && new_record->len > 0)
	{
		record->data = SN_MALLOC(new_record->len);
		if(record->data == NULL)
		{
			TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d  Err\n",__FUNCTION__,__LINE__);
			if(record->call_id)
				osip_call_id_free(record->call_id);
			SN_FREE(record);
			return -1;
		}
		SN_MEMSET(record->data,0,new_record->len);

		SN_MEMCPY(record->data,new_record->len,new_record->data,new_record->len,new_record->len);
	}

	osip_list_add(&(gb_cons->record_node_list),(void *)record, -1);  /*add to list tail*/

	return 0;
	
}

int GB_Remove_Record_Node(GB_CONNECT_STATE *gb_cons, int index)
{
	GB_Record_Node *record = NULL;

	if(gb_cons == NULL)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d  Err\n",__FUNCTION__,__LINE__);
		return -1;
	}
	
	if(index <= osip_list_size(&(gb_cons->record_node_list)) && index >= 0)
	{
		record = (GB_Record_Node *)osip_list_get(&(gb_cons->record_node_list), index);

		if(record)
		{
			if(record->call_id)
				osip_call_id_free(record->call_id);
			if(record->data)
				SN_FREE(record->data);
			osip_list_remove(&(gb_cons->record_node_list), index);
			SN_FREE(record);

			return 0;
		}
	}

	return -1;
	
}


GB_Record_Node * GB_Find_Record_Node_by_Call_ID(GB_CONNECT_STATE *gb_cons, osip_call_id_t *call_id, int *index)
{
	GB_Record_Node *record = NULL;
	int pos;
	int list_size = 0;

	if(gb_cons == NULL || call_id == NULL)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d  Err\n",__FUNCTION__,__LINE__);
		return NULL;
	}

	list_size = osip_list_size(&(gb_cons->record_node_list));
	if(list_size <= 0)
	{
		return NULL;	
	}
	
	for(pos=0; pos<list_size; pos++)
	{
		record = osip_list_get(&(gb_cons->record_node_list), pos);
		if(record)
		{
			if(osip_call_id_match(record->call_id, call_id) == 0)
			{
				*index = pos;
				return record;
			}
			record = NULL;
		}
	}

	return NULL;	
}

GB_Record_Node * GB_Find_Record_Node_by_cmdType(GB_CONNECT_STATE *gb_cons, int cmdType, int idx, int *index)
{
	GB_Record_Node *record = NULL;
	int pos;
	int list_size = 0;

	if(gb_cons == NULL || cmdType < 0 || cmdType >= gb_CommandType_NUM || idx < 0)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d  Err\n",__FUNCTION__,__LINE__);
		return NULL;
	}

	list_size = osip_list_size(&(gb_cons->record_node_list));
	if(list_size <= 0)
	{
		return NULL;	
	}
	
	for(pos=idx; pos<list_size; pos++)
	{
		record = osip_list_get(&(gb_cons->record_node_list), pos);
		if(record)
		{
			if(record->cmd == cmdType && record->call_id == NULL)
			{
				*index = pos;
				return record;
			}
			record = NULL;
		}
	}

	return NULL;	
}


int GB_sipd_register(GB_CONNECT_STATE *gb_cons, int flag)
{
	osip_message_t *reg = NULL;
	PRM_GB_SIPD_CFG gb_cfg;
	char from[GB_URI_MAX_LEN] = {0};
	char proxy[GB_URI_MAX_LEN] = {0};
	int ret;
	char *result = NULL;
	size_t length;
	char localip[20];
	char sipserver_ip[20] = {0};
	
	SN_MEMSET(&gb_cfg, 0, sizeof(gb_cfg));

	GB_Get_GBCfg(&gb_cfg);

	SN_MEMSET(localip,0,sizeof(localip));
	SN_STRCPY(localip,sizeof(localip),GB_Get_LocalIP());
	
	SN_SPRINTF(sipserver_ip,sizeof(sipserver_ip),"%d.%d.%d.%d",
		gb_cfg.sipserver_ip[0],gb_cfg.sipserver_ip[1],gb_cfg.sipserver_ip[2],gb_cfg.sipserver_ip[3]);

	SN_SPRINTF(from, GB_URI_MAX_LEN, "sip:%s@%s", gb_cfg.deviceID, localip);
	SN_SPRINTF(proxy, GB_URI_MAX_LEN, "sip:%s@%s:%d", gb_cfg.sipserver_ID,sipserver_ip, gb_cfg.sipserver_port);

	if(flag == 0)  // 注册
	{
		ret = gb_generating_register(&reg, Transport_Str[gb_cons->transfer_protocol], from, proxy, NULL, gb_cfg.register_period, localip,gb_cfg.local_port, gb_cons->local_cseq);
	}
	else  //  注销
	{
		ret = gb_generating_register(&reg, Transport_Str[gb_cons->transfer_protocol], from, proxy, NULL, 0, localip,gb_cfg.local_port, gb_cons->local_cseq);	
	}
	
	if (ret < 0)
	{
		osip_message_free (reg);
		return -1;
	}
	
	//printf("gb_cons->callID:%s\n", gb_cons->callID);
	ret = osip_message_to_str(reg, &result, &length);
	if (ret == -1) 
	{
		printf("ERROR: failed while printing message!\n");
		osip_message_free (reg);
		return -1;
	}	
	gb_cons->local_cseq++;
	GB_SocketSendData(gb_cons->connfd,inet_ntoa(gb_cons->remoteAddr.sin_addr), ntohs(gb_cons->remoteAddr.sin_port), result, length, 0);
	SN_FREE(result);
	osip_message_free(reg);
	
	return 0;
}


int GB_sipd_register_auth(GB_CONNECT_STATE *gb_cons, int flag)
{
	osip_message_t *reg = NULL;
	PRM_GB_SIPD_CFG gb_cfg;
	char from[GB_URI_MAX_LEN] = {0};
	char proxy[GB_URI_MAX_LEN] = {0};
	int ret;
	char *result = NULL;
	size_t length;
	char localip[20];
	char sipserver_ip[20] = {0};
	char *uri = NULL;
	osip_authorization_t *aut = NULL;
	
	SN_MEMSET(&gb_cfg, 0, sizeof(gb_cfg));
	
	GB_Get_GBCfg(&gb_cfg);

	SN_MEMSET(localip,0,sizeof(localip));
	SN_STRCPY(localip,sizeof(localip),GB_Get_LocalIP());
		
	SN_SPRINTF(sipserver_ip,sizeof(sipserver_ip),"%d.%d.%d.%d",
		gb_cfg.sipserver_ip[0],gb_cfg.sipserver_ip[1],gb_cfg.sipserver_ip[2],gb_cfg.sipserver_ip[3]);

	SN_SPRINTF(from, GB_URI_MAX_LEN, "sip:%s@%s", gb_cfg.deviceID, localip);
	SN_SPRINTF(proxy, GB_URI_MAX_LEN, "sip:%s@%s:%d", gb_cfg.sipserver_ID,sipserver_ip, gb_cfg.sipserver_port);

	if(flag == 0)  // 注册
	{
		ret = gb_generating_register(&reg, Transport_Str[gb_cons->transfer_protocol], from, proxy, NULL, gb_cfg.register_period, localip,gb_cfg.local_port, gb_cons->local_cseq);
	}
	else  //  注销
	{
		ret = gb_generating_register(&reg, Transport_Str[gb_cons->transfer_protocol], from, proxy, NULL, 0, localip,gb_cfg.local_port, gb_cons->local_cseq);
	}
		
	if (ret < 0)
	{
		osip_message_free (reg);
		return -1;
	}

	ret = osip_uri_to_str (reg->req_uri, &uri);
	if (ret != 0)
		return ret;
	
	ret = gb_create_authorization_header (gb_cons->wwwa, uri, ADMIN_NAME, (char *)gb_cfg.reg_pwd, NULL, &aut, "REGISTER", NULL, 0);

	osip_free (uri);
	if (ret != 0)
		return ret;

	if (aut != NULL) 
	{
		if (osip_strcasecmp (reg->sip_method, "REGISTER") == 0)
			osip_list_add (&reg->authorizations, aut, -1);
		else
			osip_list_add (&reg->proxy_authorizations, aut, -1);
		osip_message_force_update (reg);	
	}

	ret = osip_message_to_str(reg, &result, &length);
	if (ret == -1) 
	{
		printf("ERROR: failed while printing message!\n");
		osip_message_free (reg);
		return -1;
	}	
	gb_cons->local_cseq++;
	GB_SocketSendData(gb_cons->connfd,inet_ntoa(gb_cons->remoteAddr.sin_addr), ntohs(gb_cons->remoteAddr.sin_port), result, length, 0);
	SN_FREE(result);
	osip_message_free(reg);
	
	return 0;
}

int GB_sipd_Keepalive(GB_CONNECT_STATE *gb_cons, gb_Keepalive_Struct *cmd)
{
	osip_message_t *reg = NULL;
	PRM_GB_SIPD_CFG gb_cfg;
	char to[GB_URI_MAX_LEN] = {0};
	char from[GB_URI_MAX_LEN] = {0};
	char proxy[GB_URI_MAX_LEN] = {0};
	int ret;
	char *result = NULL;
	size_t length;
	char localip[20];
	char sipserver_ip[20] = {0};
	GB_Record_Node record;
	
	SN_MEMSET(&gb_cfg, 0, sizeof(gb_cfg));	
	SN_MEMSET(&record, 0, sizeof(record));
	
	GB_Get_GBCfg(&gb_cfg);

	SN_MEMSET(localip,0,sizeof(localip));
	SN_STRCPY(localip,sizeof(localip),GB_Get_LocalIP());
	
	SN_SPRINTF(sipserver_ip,sizeof(sipserver_ip),"%d.%d.%d.%d",
		gb_cfg.sipserver_ip[0],gb_cfg.sipserver_ip[1],gb_cfg.sipserver_ip[2],gb_cfg.sipserver_ip[3]);

	SN_SPRINTF(from, GB_URI_MAX_LEN, "sip:%s@%s", gb_cfg.deviceID, localip);
	SN_SPRINTF(to, GB_URI_MAX_LEN, "sip:%s@%s", gb_cfg.sipserver_ID, sipserver_ip);
	SN_SPRINTF(proxy, GB_URI_MAX_LEN, "sip:%s@%s:%d", gb_cfg.sipserver_ID,sipserver_ip, gb_cfg.sipserver_port);
	
	ret = gb_generating_MESSAGE(&reg, Transport_Str[gb_cons->transfer_protocol], from, to, proxy, 
					localip,gb_cfg.local_port, gb_cons->local_cseq, (void *)cmd, gb_CommandType_KeepAlive);
		
	if (ret < 0)
	{
		osip_message_free (reg);
		printf("%s  line=%d\n",__FUNCTION__,__LINE__);
		return -1;
	}

	record.cmd = gb_CommandType_KeepAlive;
	osip_call_id_clone(reg->call_id,&(record.call_id));
	
	GB_Add_Record_Node(gb_cons, &record);
	
	ret = osip_message_to_str(reg, &result, &length);

	if (ret == -1) 
	{
		printf("ERROR: failed while printing message!\n");
		osip_message_free (reg);
		return -1;
	}	
	gb_cons->local_cseq++;
	GB_SocketSendData(gb_cons->connfd,inet_ntoa(gb_cons->remoteAddr.sin_addr), ntohs(gb_cons->remoteAddr.sin_port), result, length, 0);
	SN_FREE(result);
	osip_message_free(reg);
	
	return 0;
}


void GB_Free_Query_Req(gb_Query_Req_Struct *Query)
{
	if(Query == NULL)
		return ;
	
	if(Query->DeviceStatus)
	{
		SN_FREE(Query->DeviceStatus);
	}

	if(Query->Catalog)
	{
		SN_FREE(Query->Catalog);
	}

	if(Query->DeviceInfo)
	{
		SN_FREE(Query->DeviceInfo);
	}

	if(Query->RecordInfo)
	{
		SN_FREE(Query->RecordInfo);
	}

	if(Query->Alarm)
	{
		SN_FREE(Query->Alarm);
	}
	
	SN_FREE(Query);		

	return ;
}

void GB_Free_Control_Req(gb_Control_Req_Struct *Control_Req)
{
	if(Control_Req == NULL)
		return ;
	
	if(Control_Req->DeviceControl)
	{
		SN_FREE(Control_Req->DeviceControl);
	}

	if(Control_Req->DeviceConfig)
	{
		SN_FREE(Control_Req->DeviceConfig);
	}
	
	SN_FREE(Control_Req);		

	return ;
}


int GB_handle_RCV_REQINVITE(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, int *flag)
{
	int ret = -1;
	osip_message_t *rsp = NULL;
	char *result = NULL;
	size_t length;
	int status_code = 200;
	char sdpStr_response[1024] = {0};
	char *pRsp = NULL;
//	struct sipd_media_session sdpInfo;
	GB_media_session media_session;
	int chn = -1;
	PRM_GB_SIPD_CFG gb_cfg;
	osip_uri_param_t *tag = NULL;
	
//	SN_MEMSET(&sdpInfo,0,sizeof(sdpInfo));
	SN_MEMSET(&media_session,0,sizeof(media_session));
	SN_MEMSET(&gb_cfg,0,sizeof(gb_cfg));

	GB_Get_GBCfg(&gb_cfg);

	if(gb_cons == NULL || osip_event == NULL || osip_event->sip == NULL || 
		osip_event->sip->req_uri == NULL || osip_event->sip->req_uri->username == NULL ||
		osip_event->sip->from == NULL || osip_event->sip->from->url == NULL || 
		osip_event->sip->from->url->username == NULL)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Something is NULL\n",__FUNCTION__,__LINE__);
		return -1;
	}
	
	DeviceID2LocalChn(osip_event->sip->req_uri->username,&chn);

	TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d INVITE from:%s  to:%s\n",__FUNCTION__,__LINE__,
			osip_event->sip->from->url->username,osip_event->sip->req_uri->username);
	
	if(chn <= 0)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  Unknow DeviceID(%s)\n",__FUNCTION__,osip_event->sip->req_uri->username);
		status_code = 404;  // 没找到
		pRsp = NULL;
	}
	else
	{
		// 流量控制判断等接口调用

		osip_from_get_tag(osip_event->sip->from,&tag);

		if(tag == NULL || tag->gvalue == NULL)
		{
			status_code = 400;  // 错误的请求
			pRsp = NULL;
		}
		else
		{
			media_session.chn = chn - 1;
			media_session.media_session_opt = MEDIA_SESSION_OPT_INVITE;
			SN_STRCPY(media_session.TagID,sizeof(media_session.TagID),tag->gvalue);
						
			write(gb_msg_sock.client_fd, (void *)&media_session, sizeof(media_session));

			snprintf(sdpStr_response,1024,
			"v=0\r\n"\
			"o=34020000001330000001 0 0 IN IP4 %s\r\n"\
			"s=Play\r\n"\
			"c=IN IP4 %s\r\n"\
			"t=0 0\r\n"\
			"m=video %d RTP/AVP 96\r\n"\
			"a=recvonly\r\n"\
			"a=rtpmap:96 PS/90000\r\n"\
			"y=0100000001\r\n"\
			"f=\r\n",
			GB_Get_LocalIP(), GB_Get_LocalIP(),LISTEN_PORT);

			ret = 0;
			if(ret == 0)
			{
				pRsp = sdpStr_response;
			}
			else
			{
				pRsp = NULL;
				status_code = 488;  // 这里不能接受 
			}
		}
	}
	
	ret = gb_build_response_message(&rsp, NULL, status_code,osip_event->sip, GB_MANSCDP_SDP,pRsp,SN_STRLEN(sdpStr_response));

	if(ret < 0)
	{
		osip_message_free (rsp);
		printf("%s  line=%d\n",__FUNCTION__,__LINE__);
		return -1;
	}

	gb_build_response_Contact(rsp, GB_Get_LocalIP(), gb_cfg.local_port, (char *)(gb_cfg.deviceID));

	ret = osip_message_to_str(rsp, &result, &length);

	if (ret == -1) 
	{
		printf("ERROR: failed while printing message!\n");
		osip_message_free (rsp);
		return -1;
	}	
	
	GB_SocketSendData(gb_cons->connfd,inet_ntoa(gb_cons->remoteAddr.sin_addr), ntohs(gb_cons->remoteAddr.sin_port), result, length, 0);
	SN_FREE(result);
	osip_message_free(rsp);

	return 0;
}

int GB_handle_RCV_REQACK(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event)
{
	GB_media_session media_session;	
	int chn = -1;
	osip_uri_param_t *tag = NULL;
	
	SN_MEMSET(&media_session,0,sizeof(media_session));

	if(gb_cons == NULL || osip_event == NULL || osip_event->sip == NULL || osip_event->sip->to == NULL || 
		osip_event->sip->to->url == NULL || osip_event->sip->to->url->username == NULL ||
		osip_event->sip->from == NULL || osip_event->sip->from->url == NULL ||
		osip_event->sip->from->url->username == NULL)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Something is NULL\n",__FUNCTION__,__LINE__);
		return -1;
	}
	
	DeviceID2LocalChn(osip_event->sip->to->url->username,&chn);

	TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d ACK from:%s  to:%s\n",__FUNCTION__,__LINE__,
				osip_event->sip->from->url->username,osip_event->sip->to->url->username);

	if(chn <= 0)
	{
		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  Unknow DeviceID(%s)\n",__FUNCTION__,osip_event->sip->to->url->username);
	}
	else
	{
		osip_from_get_tag(osip_event->sip->from,&tag);

		if(tag == NULL || tag->gvalue == NULL)
		{
			TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  Can't Find Tag!\n",__FUNCTION__);
		}
		else
		{
			media_session.chn = chn - 1;
			media_session.media_session_opt = MEDIA_SESSION_OPT_ACK;
			SN_STRCPY(media_session.TagID,sizeof(media_session.TagID),tag->gvalue);

			write(gb_msg_sock.client_fd, (void *)&media_session, sizeof(media_session));			
		}
	}
	return 0;
}

int GB_handle_RCV_REQUEST(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event)
{
	osip_body_t *mbody = NULL;
	int pos = 0;	
	int code = -1;
	void *Req = NULL;	

	if(gb_cons == NULL || osip_event == NULL || osip_event->sip == NULL || osip_event->sip->sip_method == NULL)
	{
		return -1;
	}
	
	if(strcmp(osip_event->sip->sip_method, GB_SIP_METHOD_MESSAGE) == 0)
	{
		osip_message_get_body(osip_event->sip, pos, &mbody);
		pos++;

		if(mbody == NULL || mbody->body == NULL)
		{
			GB_Send_Reply(gb_cons,osip_event,400);
			return -1;
		}
		
		gb_parser_Req_XML(mbody->body, &code, &Req);

		switch(code)
		{
			case Code_Query_Req:
			{
				GB_Deal_Query_Req(gb_cons, osip_event, (gb_Query_Req_Struct *)Req);
				GB_Free_Query_Req((gb_Query_Req_Struct *)Req);
			}
			break;

			case Code_Control_Req:
			{
				GB_Deal_Control_Req(gb_cons, osip_event, (gb_Control_Req_Struct *) Req);
				GB_Free_Control_Req((gb_Control_Req_Struct *) Req);
			}
			break;

			default:
			{
			}
			break;
		}

		return 0;
	}
	else if(strcmp(osip_event->sip->sip_method, GB_SIP_METHOD_BYE) == 0)
	{
		GB_media_session media_session;	
		int chn = -1;
		osip_uri_param_t *tag = NULL;
		
		SN_MEMSET(&media_session,0,sizeof(media_session));

		if(osip_event->sip->to == NULL || osip_event->sip->to->url == NULL || osip_event->sip->to->url->username == NULL
			|| osip_event->sip->from == NULL || osip_event->sip->from->url == NULL ||
				osip_event->sip->from->url->username == NULL)
		{
			TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d Something is NULL\n",__FUNCTION__,__LINE__);
			return -1;
		}
		
		DeviceID2LocalChn(osip_event->sip->to->url->username,&chn);

		TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d BYE from:%s  to:%s\n",__FUNCTION__,__LINE__,
				osip_event->sip->from->url->username,osip_event->sip->to->url->username);

		if(chn <= 0)
		{
			TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  Unknow DeviceID(%s)\n",__FUNCTION__,osip_event->sip->req_uri->username);
		}
		else
		{		
			osip_from_get_tag(osip_event->sip->from,&tag);

			if(tag == NULL || tag->gvalue == NULL)
			{
				TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  Can't Find Tag!\n",__FUNCTION__);
				GB_Send_Reply(gb_cons,osip_event,400);
		
				return 0;
			}
			else
			{
				media_session.chn = chn - 1;
				media_session.media_session_opt = MEDIA_SESSION_OPT_BYE;
				SN_STRCPY(media_session.TagID,sizeof(media_session.TagID),tag->gvalue);

				write(gb_msg_sock.client_fd, (void *)&media_session, sizeof(media_session));				
			}
		}

		GB_Send_Reply(gb_cons,osip_event,200);
		
		return 0;
	}

	
	GB_Send_Reply(gb_cons,osip_event,400);
	return 0;
}


int GB_handle_RCV_STATUS_1XX(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event)
{
	TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d\n",__FUNCTION__,__LINE__);
	return 0;
}

int GB_handle_RCV_STATUS_2XX(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event)
{
	GB_Record_Node *record = NULL;
	int index = -1;

	if(gb_cons == NULL || osip_event == NULL || osip_event->sip == NULL || osip_event->sip->call_id == NULL)
	{
		return -1;
	}
	
	record = GB_Find_Record_Node_by_Call_ID(gb_cons,osip_event->sip->call_id ,&index);
	if(record != NULL)
	{
		switch(record->cmd)
		{
			case gb_CommandType_KeepAlive:
			{
				gb_cons->keepalive_timeout_cnt = 0;
			}
			break;
			
			default:
			{
				TRACE(SCI_TRACE_NORMAL,MOD_GB,"Get 200 OK, But do nothing\n");
			}
			break;
		}

		GB_Remove_Record_Node(gb_cons, index);
	}
	else
	{
		if(gb_cons->cur_state == GB_STATE_REGISTER)  
		{
			gb_cons->cur_state = GB_STATE_RUNNING;	// 注册成功

			if (gb_cons->wwwa)
			{
				osip_www_authenticate_free(gb_cons->wwwa);
				gb_cons->wwwa = NULL;	
			}

			gb_cons->last_registertime = gb_cons->last_keepalivetime = get_cur_time()/1000;
			GB_Set_gGBConnStatus(1);

			TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d REGISTER Success!\n",__FUNCTION__,__LINE__);

//			GB_Change_Mode(0);
		}
		else if(gb_cons->cur_state == GB_STATE_RUNNING && gb_cons->bUnRegister == 1)  
		{
			if (gb_cons->wwwa)
			{
				osip_www_authenticate_free(gb_cons->wwwa);
				gb_cons->wwwa = NULL;	
			}

			close(gb_cons->connfd);
			GB_ResetConState(gb_cons);

			GB_Refresh_GBCfg();
			GB_Set_gGBConnStatus(0);

			TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d UNREGISTER Success!\n",__FUNCTION__,__LINE__);
		}
	}

	return 0;
}

int GB_handle_RCV_STATUS_3XX(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event)
{
	TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d\n",__FUNCTION__,__LINE__);
	return 0;
}

int GB_handle_RCV_STATUS_4XX(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event)
{
	if(gb_cons == NULL || osip_event == NULL || osip_event->sip == NULL)
	{
		return -1;
	}
	
	switch(osip_event->sip->status_code)
	{
		case 401: // 未授权
		{
			if(gb_cons->cur_state == GB_STATE_REGISTER)  
			{
				osip_www_authenticate_t * wwwa;

				if(gb_cons->wwwa != NULL)
				{
					TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d  Unauthorized, Please Check Carefully!\n",__FUNCTION__,__LINE__);
				}
				
				wwwa = osip_list_get (&osip_event->sip->www_authenticates, 0);
				if (wwwa != NULL)
				{
					osip_www_authenticate_free(gb_cons->wwwa);
					osip_www_authenticate_clone(wwwa, &gb_cons->wwwa);
					GB_sipd_register_auth(gb_cons, 0); //  带认证的注册请求
					gb_cons->last_sendtime = get_cur_time()/1000;
				}
			}
			else if(gb_cons->cur_state == GB_STATE_RUNNING && gb_cons->bUnRegister == 1)  
			{
				osip_www_authenticate_t * wwwa;

				wwwa = osip_list_get (&osip_event->sip->www_authenticates, 0);
				if (wwwa != NULL)
				{
					osip_www_authenticate_free(gb_cons->wwwa);
					osip_www_authenticate_clone(wwwa, &gb_cons->wwwa);
					GB_sipd_register_auth(gb_cons, 1); //  带认证的注销请求
					gb_cons->last_sendtime = get_cur_time()/1000;
				}
			}
		}
		break;
		case 403: // 禁止
		{
			// 重新认证
			if(gb_cons->cur_state == GB_STATE_RUNNING && gb_cons->bUnRegister == 0)
			{
				close(gb_cons->connfd);
				GB_ResetConState(gb_cons);

				GB_Refresh_GBCfg();
				GB_Set_gGBConnStatus(0);

				TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d  Get 403 err, Register again\n",__FUNCTION__,__LINE__);
			}
			
		}
		break;

		default:
			TRACE(SCI_TRACE_NORMAL,MOD_GB,"Can't handle sip->status_code=%d  message\n",osip_event->sip->status_code);
			break;
	}
	
	return 0;
}

int GB_handle_RCV_STATUS_5XX(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event)
{
	TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d\n",__FUNCTION__,__LINE__);
	return 0;
}

int GB_handle_RCV_STATUS_6XX(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event)
{
	TRACE(SCI_TRACE_NORMAL,MOD_GB,"%s  line=%d\n",__FUNCTION__,__LINE__);
	return 0;
}


//  对接收到的消息进行应答
int GB_Send_Reply(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, int status_code)
{
	int ret = -1;
	osip_message_t *rsp = NULL;
	char *result = NULL;
	size_t length;

	if(gb_cons == NULL || osip_event == NULL)
	{
		return -1;
	}
	
	ret = gb_build_response_message(&rsp, NULL, status_code,osip_event->sip, NULL,NULL,0);
	if(ret != 0)
	{
		osip_message_free (rsp);
		printf("%s  line=%d\n",__FUNCTION__,__LINE__);
		return -1;
	}

	ret = osip_message_to_str(rsp, &result, &length);

	if (ret == -1) 
	{
		printf("ERROR: failed while printing message!\n");
		osip_message_free (rsp);
		return -1;
	}	
	
	GB_SocketSendData(gb_cons->connfd,inet_ntoa(gb_cons->remoteAddr.sin_addr), ntohs(gb_cons->remoteAddr.sin_port), result, length, 0);
	SN_FREE(result);
	osip_message_free(rsp);

	return 0;
}

int GB_Send_Sub_Reply(GB_CONNECT_STATE *gb_cons, osip_event_t * osip_event, gb_BaseInfo_Query *BaseInfo,int status_code)
{
	int ret = -1;
	osip_message_t *rsp = NULL;
	char *result = NULL;
	size_t length;
	char body_str[GB_MAX_PLAYLOAD_BUF];

	if(gb_cons == NULL || osip_event == NULL || BaseInfo == NULL)
	{
		return -1;
	}	

	SN_MEMSET(&body_str,0,sizeof(body_str));

	if(BaseInfo->Cmdtype == gb_CommandType_Catalog)
	{
		if(status_code == 200)
			SN_SPRINTF(body_str,sizeof(body_str),
				"<?xml version=\"1.0\"?>\r\n"\
				"<Response>\r\n"\
				"<CmdType>Catalog</CmdType>\r\n"\
				"<SN>%d</SN>\r\n"\
				"<DeviceID>%s</DeviceID>\r\n"\
				"<Result>OK</Result>\r\n"\
				"</Response>\r\n",
				BaseInfo->SN,BaseInfo->DeviceID.deviceID);
		else
			SN_SPRINTF(body_str,sizeof(body_str),
				"<?xml version=\"1.0\"?>\r\n"\
				"<Response>\r\n"\
				"<CmdType>Catalog</CmdType>\r\n"\
				"<SN>%d</SN>\r\n"\
				"<DeviceID>%s</DeviceID>\r\n"\
				"<Result>ERROR</Result>\r\n"\
				"</Response>\r\n",
				BaseInfo->SN,BaseInfo->DeviceID.deviceID);
	}
	else if(BaseInfo->Cmdtype == gb_CommandType_Alarm)
	{
		if(status_code == 200)
			SN_SPRINTF(body_str,sizeof(body_str),
				"<?xml version=\"1.0\"?>\r\n"\
				"<Response>\r\n"\
				"<CmdType>Alarm</CmdType>\r\n"\
				"<SN>%d</SN>\r\n"\
				"<DeviceID>%s</DeviceID>\r\n"\
				"<Result>OK</Result>\r\n"\
				"</Response>\r\n",
				BaseInfo->SN,BaseInfo->DeviceID.deviceID);
		else
			SN_SPRINTF(body_str,sizeof(body_str),
				"<?xml version=\"1.0\"?>\r\n"\
				"<Response>\r\n"\
				"<CmdType>Alarm</CmdType>\r\n"\
				"<SN>%d</SN>\r\n"\
				"<DeviceID>%s</DeviceID>\r\n"\
				"<Result>ERROR</Result>\r\n"\
				"</Response>\r\n",
				BaseInfo->SN,BaseInfo->DeviceID.deviceID);
	}
	
	ret = gb_build_response_message(&rsp, NULL, status_code,osip_event->sip, NULL, body_str,strlen(body_str));
	if(ret != 0)
	{
		osip_message_free (rsp);
		printf("%s  line=%d\n",__FUNCTION__,__LINE__);
		return -1;
	}

	ret = osip_message_to_str(rsp, &result, &length);

	if (ret == -1) 
	{
		printf("ERROR: failed while printing message!\n");
		osip_message_free (rsp);
		return -1;
	}	
	
	GB_SocketSendData(gb_cons->connfd,inet_ntoa(gb_cons->remoteAddr.sin_addr), ntohs(gb_cons->remoteAddr.sin_port), result, length, 0);
	SN_FREE(result);
	osip_message_free(rsp);

	return 0;
}


//  发送响应消息
int GB_Send_Response(GB_CONNECT_STATE *gb_cons, void *cmd_struct, gb_CommandType_enum cmdType, osip_call_id_t **call_id)
{
	osip_message_t *reg = NULL;
	PRM_GB_SIPD_CFG gb_cfg;
	char to[GB_URI_MAX_LEN] = {0};
	char from[GB_URI_MAX_LEN] = {0};
	char proxy[GB_URI_MAX_LEN] = {0};
	int ret;
	char *result = NULL;
	size_t length;
	char localip[20];
	char sipserver_ip[20] = {0};
	
	SN_MEMSET(&gb_cfg, 0, sizeof(gb_cfg));
	GB_Get_GBCfg(&gb_cfg);

	SN_MEMSET(localip,0,sizeof(localip));
	SN_STRCPY(localip,sizeof(localip),GB_Get_LocalIP());
		
	SN_SPRINTF(sipserver_ip,sizeof(sipserver_ip),"%d.%d.%d.%d",
		gb_cfg.sipserver_ip[0],gb_cfg.sipserver_ip[1],gb_cfg.sipserver_ip[2],gb_cfg.sipserver_ip[3]);

	SN_SPRINTF(from, GB_URI_MAX_LEN, "sip:%s@%s", gb_cfg.deviceID, localip);
	SN_SPRINTF(to, GB_URI_MAX_LEN, "sip:%s@%s", gb_cfg.sipserver_ID, sipserver_ip);
	SN_SPRINTF(proxy, GB_URI_MAX_LEN, "sip:%s@%s:%d", gb_cfg.sipserver_ID,sipserver_ip, gb_cfg.sipserver_port);
	
	ret = gb_generating_MESSAGE(&reg, Transport_Str[gb_cons->transfer_protocol], from, to, proxy, 
					localip,gb_cfg.local_port, gb_cons->local_cseq, (void *)cmd_struct, cmdType);
		
	if (ret < 0)
	{
		osip_message_free (reg);
		printf("%s	line=%d\n",__FUNCTION__,__LINE__);
		return -1;
	}

	if(call_id != NULL)
	{
		osip_call_id_clone(reg->call_id,call_id);
	}
	
	ret = osip_message_to_str(reg, &result, &length);

	if (ret == -1) 
	{
		printf("ERROR: failed while printing message!\n");
		osip_message_free (reg);
		return -1;
	}	
	gb_cons->local_cseq++;
	GB_SocketSendData(gb_cons->connfd,inet_ntoa(gb_cons->remoteAddr.sin_addr), ntohs(gb_cons->remoteAddr.sin_port), result, length, 0);
	SN_FREE(result);
	osip_message_free(reg);
	
	return 0;
}


int GB_handle_messages (GB_CONNECT_STATE *gb_cons)
{
	osip_event_t * osip_event = NULL;
	int ret = -1;
	int isDownLoad = 0;
	
	ret = gb_sip_messages_parse(&osip_event,gb_cons->buffer,gb_cons->datasize);

	if(ret == 0)
	{
		switch(osip_event->type)
		{
			// 请求消息
			case RCV_REQINVITE:
			{
				GB_handle_RCV_REQINVITE(gb_cons, osip_event, &isDownLoad);
			}
			break;
			case RCV_REQACK:
			{
				GB_handle_RCV_REQACK(gb_cons, osip_event);
			}
			break;
			case RCV_REQUEST:
			{
				GB_handle_RCV_REQUEST(gb_cons, osip_event);
			}
			break;

			// 响应消息
			case RCV_STATUS_1XX:
			{
				GB_handle_RCV_STATUS_1XX(gb_cons, osip_event);
			}
			break;
			case RCV_STATUS_2XX:
			{
				GB_handle_RCV_STATUS_2XX(gb_cons, osip_event);
			}
			break;
			case RCV_STATUS_3456XX:
			{
				GB_Record_Node *record = NULL;
				int index = -1;
				
				record = GB_Find_Record_Node_by_Call_ID(gb_cons,osip_event->sip->call_id ,&index);
				if(record)
				{
					if(record->info)
						SN_FREE(record->info);
					GB_Remove_Record_Node(gb_cons, index);
				}
	
				if(MSG_IS_STATUS_3XX(osip_event->sip))
				{
					GB_handle_RCV_STATUS_3XX(gb_cons, osip_event);
				}
				else if(MSG_IS_STATUS_4XX(osip_event->sip))
				{
					GB_handle_RCV_STATUS_4XX(gb_cons, osip_event);
				}
				else if(MSG_IS_STATUS_5XX(osip_event->sip))
				{
					GB_handle_RCV_STATUS_5XX(gb_cons, osip_event);
				}
				else if(MSG_IS_STATUS_6XX(osip_event->sip))
				{
					GB_handle_RCV_STATUS_6XX(gb_cons, osip_event);
				}
				else
				{
					
				}
			}
			break;
			default:
			{
				TRACE(SCI_TRACE_NORMAL,MOD_GB,"\n%s\n",gb_cons->buffer);
			}
			break;
			
		}
	}


	if(osip_event != NULL && isDownLoad == 0)  // 若是下载，则先不释放osip_event
	{
		gb_sip_free(osip_event);
		osip_event = NULL;
	}
	
	return 0;
}

#endif

