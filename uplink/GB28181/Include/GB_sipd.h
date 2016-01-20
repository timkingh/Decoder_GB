#ifndef _GB_SIPD_H_
#define _GB_SIPD_H_

#include "global_def.h"
#include "global_api.h"
#include "global_msg.h"

#if (DVR_SUPPORT_GB == 1)	

#include "gb_ProtocolLayer.h"

enum {
	GB_TRANSFER_UDP = 0,
	GB_TRANSFER_TCP,
};



enum {
	GB_STATE_IDEL,
	GB_STATE_WAIT_CLEAR,
	GB_STATE_CONNECTING,
	GB_STATE_REGISTER,
	GB_STATE_SERVER,
	GB_STATE_CONNECTED,
	GB_STATE_CFG_SYNC,
	GB_STATE_RUNNING,
};

#define GB_END_HEADERS_STR 					"\r\n\r\n"
#define GB_CLEN_HEADER_STR 					"\r\ncontent-length:"
#define GB_CLEN_HEADER_COMPACT_STR 			"\r\nl:"
#define GB_CLEN_HEADER_STR2 					"\r\ncontent-length "
#define GB_CLEN_HEADER_COMPACT_STR2 			"\r\nl "
#define GB_SIP_METHOD_MESSAGE					"MESSAGE"
#define GB_SIP_METHOD_SUBSCRIBE				"SUBSCRIBE"
#define GB_SIP_METHOD_BYE						"BYE"
#define GB_SIP_METHOD_CONTROL				"Control"
#define GB_SIP_METHOD_INFO						"INFO"


#define gb_const_strlen(x) 		(sizeof((x)) - 1)
#define IsTimeOfArrival(last_time, interval)	((last_time > 0) && (interval > 0) && ((get_cur_time()/1000 - (last_time)) > (interval)*1000))


#define GB_URI_MAX_LEN 100
#define MAX_GB_MSG_NUM 				(2)
#define MAX_GB_CONNECTION_NUM		(1)

#define MAX_GB_SIPD_PAYLOAD_SIZE			(4*1024)
#define MAX_GB_SIPD_SDP_SIZE			(256)

#define GB_TOTAL_CHN 	(DEV_DEC_TOTALVOCHN)
#define GB_SENDLIST_NUM_LIMIT 	(5)
#define GB_TOTAL_MONITOR_NUM 	(MAXVGANUM)
#define LISTEN_PORT 61000

typedef struct 
{
	struct pollfd *poll_entry;
	int server_fd;
	int client_fd;
}GBMsgSock;

GBMsgSock gb_msg_sock;


typedef struct
{
	int  cmd;  // gb_CommandType_enum
	osip_call_id_t *call_id;
	int num;
	int len; // data 的长度
	void *data;
	void *info;
}GB_Record_Node;

typedef struct
{
	int  type;  // gb_CommandType_enum, 订阅类型
	long long subscribe_time;  // 订阅时间
	int expires; // 0-取消订阅，0<expires<3600 - 订阅有效期
	char subscribe_id[GB_DEVICEID_LEN+1]; // 订阅者ID
	int SN;
	char subscribe_ip[GB_STR_LEN]; // 订阅者IP
	int subscribe_port; // 订阅者port
	char notify_id[GB_DEVICEID_LEN+1]; // 通知者ID
	long long last_sendtime;  // 上次发送时间
}GB_Subscribe_Node;


enum MediaSessionOpt
{
	MEDIA_SESSION_OPT_NONE,
	MEDIA_SESSION_OPT_INVITE,
	MEDIA_SESSION_OPT_ACK,
	MEDIA_SESSION_OPT_INFO,
	MEDIA_SESSION_OPT_BYE,	
	MEDIA_SESSION_OPT_QUERY,  // 回放和下载时，查询本地文件的响应
	MEDIA_SESSION_OPT_END,
	MEDIA_SESSION_OPT_DOWNLOAD,
};


typedef struct
{
	int chn; // 从0 开始
	enum MediaSessionOpt media_session_opt; // 本地消息的类型
	char TagID[MAX_GB_SIPD_SDP_SIZE]; // 会话唯一性标记
	char sdp[MAX_GB_SIPD_SDP_SIZE*2];   // 待解析的SDP字符串
	ST_FMG_QUERY_FILE_RSP *st_rsp;  // 查询文件的响应，拷贝完数据后要释放内存
}GB_media_session;


typedef struct _gb_connect_state_ 
{
	int cur_state;
	struct pollfd *poll_act;
	int connfd;
	struct sockaddr_in remoteAddr;
	
	long long last_keepalivetime;
	int keepalive_timeout_cnt;
	long long last_sendtime;
	long long last_registertime;
	unsigned int beginconect_time;

	UINT8  transfer_protocol;/*传输协议0:UDP  1:TCP*/
	unsigned int local_cseq;
	unsigned char bUnRegister;  // 1-注销
	osip_www_authenticate_t *wwwa;
	osip_list_t record_node_list;

	char fix_buffer[MAX_GB_SIPD_PAYLOAD_SIZE];
	int datasize;
	char *buffer_ptr, *buffer_end;
	int buffer_size;
	char *buffer;	/*the http/rtsp context*/
}GB_CONNECT_STATE;


void GB_GetLocalIPaddrFromSock(int sockfd,char *localIP, int localIP_len);
int GB_sipd_register(GB_CONNECT_STATE *gb_cons, int flag);
int GB_handle_messages (GB_CONNECT_STATE *gb_cons);
void GB_ResetConState(GB_CONNECT_STATE *gb_cons);
int is_recv_whole_messages(GB_CONNECT_STATE *gb_cons);
int GB_sipd_Keepalive(GB_CONNECT_STATE *gb_cons, gb_Keepalive_Struct *cmd);
int GB_Get_Chn_SumNum(void);
int GB_Refresh_Chn_SumNum(void);
int GB_Set_LocalIP(char *ip);
char *GB_Get_LocalIP();
int GB_Get_GBCfg(PRM_GB_SIPD_CFG *gb_cfg);

#endif

#endif

