/*system*/
#include <errno.h>
#include <stdio.h>

#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/time.h>
#include <netinet/in.h> 
#include <sched.h>

/*global*/
#include "global_def.h"
#include "global_api.h"
#include "global_msg.h"


/*module*/
//#include "type_def.h"
//#include "rtsp_cmn.h"
//#include "rtsp.h"
//#include "codec.h"
#include <netinet/tcp.h>

#include "GB_Decode_PS_Data.h"
#include "rtpdec.h"

#define MAX_DECODE_CLIENT_NUM   1   /*3536可以两个显示器输出*/
#define LISTEN_PORT 61000
#define SIPD_VIDEO_PLAY					("Play")
#define SIPD_VIDEO_PLAYBACK				("Playback")
#define SIPD_VIDEO_DOWNLOAD					("Download")

#define SIPD_RTP_DEFAULT_AUDIO_SSRC				("000000000")
#define SIPD_RTP_DEFAULT_VIDEO_SSRC				("000000001")

#define GB_IOBUFFER_SIZE	(8*1024)
#define MAX_NET_PAYLOAD		(1400)
#define GB_SEG_SIZE	1.5*1024*1024
#define GB_MSG_LEN	20
#define UDP_BUF_LEN 65535
//#define RTP_REORDER_QUEUE_DEFAULT_SIZE 10  /*RTP包重排队列的长度*/
//#define RTP_MIN_PACKET_LENGTH 12
//#define RTP_MAX_PACKET_LENGTH 8192
#define RECVBUF_SIZE 10*RTP_MAX_PACKET_LENGTH




static SIP_Context *g_sipClient[MAX_DECODE_CLIENT_NUM];
//static char pes_recv_buf[RECVBUF_SIZE];



// 创建接收视音频流的socket，并绑定ip和端口
// 返回: 0 - 成功 ;  -1 - 失败
static int GB_Create_Data_Socket(SIP_Context *c,int LocalPort)
{
	//char* pLocal_IP = GB_Get_LocalIP();
	unsigned long local_ip = 0;
	int on = 1;
	extern int errno;
	int ret = -1;
	int size = 2*1024*1024;
	struct sockaddr_in video_addr;
	int RecvBufLen = 0;
	socklen_t len;
	len = sizeof(RecvBufLen);

	SN_MEMSET(&video_addr,0,sizeof(struct sockaddr_in));	

	local_ip = inet_addr(GB_Get_LocalIP());
	
	if(LocalPort != 0)    //
	{
		/*本地端口不为0,创建视频SOCKET*/
		if(-1 == c->sip_video_fd)
		{
			c->sip_video_fd = socket(AF_INET,SOCK_DGRAM,0);  // udp socket
		}
		else
		{
			printf("%s line=%d OpenDataFd Fail,some wrong dataFd.video_fd = %d\n", __FUNCTION__, __LINE__, c->sip_video_fd);
			return -1;
		}
		
		if(-1 == c->sip_video_fd)
		{
			printf("%s line=%d OpenDataFd Fail,video fd socket fail\n", __FUNCTION__, __LINE__);
			return -1;
		}
		
		if(0 != setsockopt(c->sip_video_fd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on)))
		{
			printf("%s line=%d OpenDataFd Fail,setsockopt  video fd(%d) fail:errno %d\n",__FUNCTION__, __LINE__, c->sip_video_fd,errno);
		}

		/*将socket设置为非阻塞模式*/
		fcntl(c->sip_video_fd, F_SETFL, fcntl(c->sip_video_fd, F_GETFL) | O_NONBLOCK);

		/*设置接收和发送缓冲区的大小*/
		if(setsockopt(c->sip_video_fd,SOL_SOCKET,SO_RCVBUF,(const char*)&size,sizeof(int)) < 0)
		{
			perror("setsockopt for recv buf");
		}
		if(setsockopt(c->sip_video_fd,SOL_SOCKET,SO_SNDBUF,(const char*)&size,sizeof(int)) < 0)
		{
			perror("setsockopt for send buf");
		}
		
		if(getsockopt(c->sip_video_fd,SOL_SOCKET,SO_RCVBUF,(void*)&RecvBufLen,&len) < 0)
		{
			perror("getsockopt for recv buf");
		}
		printf("--------------------------------------> UDP RecvBufLen:%d\n",RecvBufLen);
		
		/*绑定视频SOCKET*/
		video_addr.sin_family = AF_INET;  // IPv4
		video_addr.sin_port = htons(LocalPort);
		video_addr.sin_addr.s_addr = /*local_ip*/INADDR_ANY;

		ret = bind(c->sip_video_fd,(struct sockaddr *)(&video_addr),sizeof(struct sockaddr));
		if(-1 == ret)
		{
			printf("%s line=%d OpenDataFd Fail,bind  video fd(%d) fail:errno %d\n", __FUNCTION__, __LINE__,c->sip_video_fd,errno);
			return -1;
		}
	}	
	else
	{
		printf("%s line=%d Local Port Error!\n", __FUNCTION__, __LINE__);
		return -1;
	}
	
	return 0;
}


/*初始化会话变量*/
static int Init_SipContext(SIP_Context * c)
{
	if(c)
	{
		c->sip_video_fd = -1;
		c->send_ps_fd = -1;
		c->recv_ps_fd = -1;
	
		c->poll_entry = NULL;
		c->poll_entry_stream = NULL;

		c->recv_buf_size = RECVBUF_SIZE;
		c->recv_buffer = NULL;
		c->recv_buffer_ptr = c->recv_buffer;
		c->recv_buffer_end = c->recv_buffer;

		c->ps_data_length = 0;
		c->pes_packet_length = 0;
		c->pes_bytes_have_read = 0;
		c->isMissedPacket = 0;

		c->fPacketReadInProgress = NULL;
		c->fPacketSentToDecoder = NULL;
		c->streamTypeBit = -1;
		c->code_id = CODEC_ID_INVALID;

		c->rtpDemuxContext = SN_MALLOC(sizeof(RTPDemuxContext));
		if(!c->rtpDemuxContext)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s:%d: SN_MALLOC failed\n", __func__, __LINE__);
			return -1;			
		}

		c->rtpDemuxContext->queue = NULL; /*SN_MALLOC(sizeof(RTPPacket))*/ /*初始化时,重排队列里没有RTP包*/
		c->rtpDemuxContext->seq = 0;
		c->rtpDemuxContext->queue = NULL;
		c->rtpDemuxContext->queue_len = 0;
		c->rtpDemuxContext->queue_size = RTP_REORDER_QUEUE_DEFAULT_SIZE;
		c->rtpDemuxContext->pSipContext = c;	
		c->rtpDemuxContext->isFirst = 1;

		c->mpegDemuxContext = SN_MALLOC(sizeof(MpegDemuxContext));
		SN_MEMSET(c->mpegDemuxContext, 0, sizeof(MpegDemuxContext));
	}

	return 0;
}


/*接收PS流数据,并解析得到ES流*/
static int GB_Recv_PS_Data(SIP_Context * c)
{
	int len = -1, src_file = 0;
	int rlen = 0;
	char RecvBuf[UDP_BUF_LEN] = {0};

	len = recv(c->recv_ps_fd,RecvBuf,sizeof(RecvBuf),0);
	if(len < 0)
	{
		/*由于是非阻塞的模式,所以当errno为EAGAIN时,表示当前缓冲区已无数据可读*/
		if(errno == EAGAIN)
			return 0;
		else
		{
			perror("recv failed!");
			return -1;
		}
	}

	src_file = open("/var/tmp/ps_stream_file.vob",O_RDWR | O_CREAT | O_APPEND);
	if(src_file > 0)
	{
		rlen = write(src_file,(char*)RecvBuf,len);
		//printf("PS : recv %d Bytes!\n",rlen);
	}
	close(src_file);

	return 0;
}


int tmpLen = 0;
int tmpSeq = 0;
/*接收RTP数据,并解析得到PS流*/
static int GB_Recv_Rtp_Data(SIP_Context * c)
{
	int len = -1;
	int ret = -1;	
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(struct sockaddr_in); 
	
	if(NULL == c->recv_buffer)
	{
		c->recv_buffer = SN_MALLOC(c->recv_buf_size); /*后续补充: 会话结束时要释放内存*/
		if(c->recv_buffer == NULL)
		{
			printf("SN_MALLOC for recv_buffer ERROR!\n");
			return -1;
		}
		SN_MEMSET(c->recv_buffer, 0, c->recv_buf_size);
	}

	len = recvfrom(c->sip_video_fd,c->recv_buffer,c->recv_buf_size,0,(struct sockaddr *)&addr, &addr_len);
	if(len < 0)
	{
		/*由于是非阻塞的模式,所以当errno为EAGAIN时,表示当前缓冲区已无数据可读*/
		if(errno == EAGAIN)
			return 0;
		else
		{
			perror("recv failed!");
			return -1;
		}
	}
	c->recv_buffer_end = c->recv_buffer + len; 

	if(tmpLen < 5*1024*1024)
	{		
		if(AV_RB16(c->recv_buffer + 2) > tmpSeq + 1)
		{
			printf("missed some packet!!!\n");
			printf("%s Line %d ======> recv %d Bytes,cur_seq:%d,last_seq:%d\n\n",__func__,__LINE__,
										len,AV_RB16(c->recv_buffer + 2),tmpSeq);
		}
		
		tmpSeq = AV_RB16(c->recv_buffer + 2);
		
		ret = ff_rtp_parse_packet(c->rtpDemuxContext,&c->recv_buffer, len);
		if(ret < 0)
		{
		}
		tmpLen += len;
	}	

	return 0;
}


/*创建本机内部使用的sock:将PS流发给另一个处理接口
**返回: 成功 - 0 ; 失败 - -1
*/
static int GB_Create_Internal_Data_Socket(SIP_Context * c)
{
	int fdpare_video[2] = {-1, -1};

	if (0 != socketpair(AF_LOCAL,SOCK_DGRAM,0,fdpare_video))
	{
		perror("socketpair");
		TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s:%d: GB_Create_Internal_Data_Socket failed\n", __func__, __LINE__);
		return -1;
	}
	
	fcntl(fdpare_video[0], F_SETFL, fcntl(fdpare_video[0], F_GETFL) | O_NONBLOCK);
	fcntl(fdpare_video[1], F_SETFL, fcntl(fdpare_video[1], F_GETFL) | O_NONBLOCK);

	c->send_ps_fd = fdpare_video[0];
	c->recv_ps_fd = fdpare_video[1];

	return 0;
}


void* GB_DecodePSData(void *Param)
{
	Log_pid(__func__);

	int delay = 1000;
	int idx = 0, ret = 0;
	struct pollfd *poll_table, *poll_entry;

	poll_table = SN_MALLOC((MAX_DECODE_CLIENT_NUM + 1)*2*sizeof(struct pollfd));
	if(NULL == poll_table) 
	{
		TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s:%d: Malloc poll table failed\n", __func__, __LINE__);
		return NULL;
	}

	for(idx = 0;idx < MAX_DECODE_CLIENT_NUM;idx++)
	{
		g_sipClient[idx] = SN_MALLOC(sizeof(SIP_Context));
		if(NULL == g_sipClient[idx])
		{
			TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s:%d: Malloc failed\n", __func__, __LINE__);
			return NULL;
		}
		
		ret = Init_SipContext(g_sipClient[idx]);
		if(ret < 0)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s:%d: Init_SipContext failed\n", __func__, __LINE__);
			return NULL;
		}
		
		ret = GB_Create_Data_Socket(g_sipClient[idx],LISTEN_PORT + idx*10);
		if(ret < 0)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s:%d: GB_Create_Data_Socket failed\n", __func__, __LINE__);
			return NULL;
		}

		ret = GB_Create_Internal_Data_Socket(g_sipClient[idx]);
		if(ret < 0)
		{
			TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s:%d: GB_Create_Internal_Data_Socket failed\n", __func__, __LINE__);
			return NULL;
		}
	}

	while(1)
	{
		delay = 1000;	
		poll_entry = poll_table;

		for(idx = 0;idx < MAX_DECODE_CLIENT_NUM;idx++)
		{
			poll_entry->fd = g_sipClient[idx]->sip_video_fd;	
			g_sipClient[idx]->poll_entry = poll_entry;
			poll_entry->events = POLLIN;
			poll_entry++;

			poll_entry->fd = g_sipClient[idx]->recv_ps_fd;
			g_sipClient[idx]->poll_entry_stream = poll_entry;
			poll_entry->events = POLLIN;
			poll_entry++;
		}						

		ret = poll(poll_table, poll_entry-poll_table, delay);
		if(ret < 0) 
		{
			perror("GB28181:poll error");
			poll(NULL, 0, 1000);
			continue;
		}

		for(idx = 0;idx < MAX_DECODE_CLIENT_NUM;idx++) 
		{		
			if(g_sipClient[idx]->poll_entry && (g_sipClient[idx]->poll_entry->revents & POLLIN))
			{	
				/*接收RTP数据*/
				if(GB_Recv_Rtp_Data(g_sipClient[idx]) != 0) 
				{
					//gb_close_connection(p_rtp->rtsp_c); 
				}
			}

			if(g_sipClient[idx]->poll_entry_stream && (g_sipClient[idx]->poll_entry_stream->revents & POLLIN))
			{
				/*接收PS数据*/
				if(GB_Recv_PS_Data(g_sipClient[idx]) != 0)
				{
				}
			}
		}
	}

	return NULL;
}







