/*
 * RTP input format
 * Copyright (c) 2002 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


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

#include "rtpdec.h"
#include "GB_Decode_PS_Data.h"

#define RTP_VERSION 2

extern int mpegps_read_packet(SIP_Context *s /*,AVPacket *pkt*/);

/*
** 功能: 解析RTP包头,并将RTP负载 传递给 PS流解析接口
** 返回: 0 - 成功;  -1 - 失败
*/
static int rtp_parse_packet_internal(RTPDemuxContext *s, /*AVPacket *pkt,*/
                                     const uint8_t *buf, int len)
{
    unsigned int ssrc;
    int payload_type, seq, flags = 0;
    int ext, csrc;
    uint32_t timestamp;
    int rv = 0;

    csrc         = buf[0] & 0x0f;
    ext          = buf[0] & 0x10;
    payload_type = buf[1] & 0x7f;
	
    if (buf[1] & 0x80)
        flags |= RTP_FLAG_MARKER;
	
    seq       = AV_RB16(buf + 2);
    timestamp = AV_RB32(buf + 4);
    ssrc      = AV_RB32(buf + 8);
    /* store the ssrc in the RTPDemuxContext */
    s->ssrc = ssrc;

	if(s->isFirst)
	{
		s->payload_type = payload_type; /*这里的负载类型应该与信令交互时的类型一致*/
		s->isFirst = 0;		
		TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s:%d: The payload type of RTP Packet is %d,SSRC:%u!!!\n",
							__func__, __LINE__,s->payload_type,s->ssrc);
	}
	
    /* NOTE: we can handle only one payload type */
    if (s->payload_type != payload_type)
    {  	
		TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s:%d: The payload type of RTP Packet has been changed!!! pre_payload:%d,now_payload:%d\n",
								__func__, __LINE__,s->payload_type,payload_type);
        return -1;
    }

    if (buf[0] & 0x20) 
	{
        int padding = buf[len - 1];
        if (len >= 12 + padding)
            len -= padding;
    }

    s->seq = seq;
    len   -= 12;
    buf   += 12;

    len   -= 4 * csrc;
    buf   += 4 * csrc;
    if (len < 0)
        return -1 /*AVERROR_INVALIDDATA*/;

    /* RFC 3550 Section 5.3.1 RTP Header Extension handling */
    if (ext) 
	{
        if (len < 4)
            return -1;
		
        /* calculate the header extension length (stored as number
         * of 32-bit words) */
        ext = (AV_RB16(buf + 2) + 1) << 2;

        if (len < ext)
            return -1;
		
        // skip past RTP header extension
        len -= ext;
        buf += ext;
    }

	s->pSipContext->recv_buffer_ptr = (unsigned char*)buf;
	s->pSipContext->ps_data_length = len;
	s->pSipContext->recv_buffer_end = s->pSipContext->recv_buffer_ptr + s->pSipContext->ps_data_length;

	mpegps_read_packet(s->pSipContext);

    return rv;
}


/*RTP包进入重排队列*/
static void enqueue_packet(RTPDemuxContext *s, uint8_t *buf, int len)
{
    uint16_t seq   = AV_RB16(buf + 2);
    RTPPacket **cur = &s->queue, *packet;

    /* Find the correct place in the queue to insert the packet */
    while (*cur) 
	{
        int16_t diff = seq - (*cur)->seq;
        if (diff < 0)
            break;
        cur = &(*cur)->next;
    }

    packet = SN_MALLOC(sizeof(*packet));
    if (!packet)
    {   	
		TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s:%d: SN_MALLOC failed\n", __func__, __LINE__);
        return;
    }
	
    //packet->recvtime = av_gettime_relative();
    packet->seq      = seq;
    packet->len      = len;
    //packet->buf      = buf;  /*是否将数据重新拷贝一份??*/
    packet->buf = NULL;
	
	packet->buf = SN_MALLOC(packet->len);
	if(!packet->buf)
	{
		return;
	}
	SN_MEMCPY(packet->buf,packet->len,buf,len,len); /*拷贝乱序的RTP包*/
	
    packet->next     = *cur;
    *cur = packet;
    s->queue_len++;
}


/*重排队列里包含下一个RTP包*/
static int has_next_packet(RTPDemuxContext *s)
{
    return s->queue && s->queue->seq == (uint16_t) (s->seq + 1);
}


/*
** 功能: 取出重排队列的头节点
** 返回: 0 - 成功;  -1 - 失败
*/
static int MissedPacketCount = 0;
static int rtp_parse_queued_packet(RTPDemuxContext *s /*, AVPacket *pkt*/)
{
    int rv;
    RTPPacket *next;

    if (s->queue_len <= 0)
        return -1;

    if (!has_next_packet(s))
    {
    	s->pSipContext->isMissedPacket = 1;
        printf("RTP: missed %d packets,received seq:%d,sent seq:%d\n", s->queue->seq - s->seq - 1,
									s->queue->seq,s->seq);
		MissedPacketCount += s->queue->seq - s->seq - 1;
		printf("MissedPacketCount:%d\n",MissedPacketCount);
    }

    /* Parse the first packet in the queue, and dequeue it */
    rv   = rtp_parse_packet_internal(s, s->queue->buf, s->queue->len);
    next = s->queue->next;
    SN_FREE(s->queue->buf);
    SN_FREE(s->queue);
    s->queue = next;
    s->queue_len--;
	
    return rv;
}

/*
**bufptr: RTP数据指针的指针; len : RTP数据的长度(包括RTP头)
*/
static int rtp_parse_one_packet(RTPDemuxContext *s, /*AVPacket *pkt,*/
                                uint8_t **bufptr, int len)
{
    uint8_t *buf = bufptr ? *bufptr : NULL;
    //int flags = 0;
    //uint32_t timestamp;
    int rv = 0;
	
    if (!buf) 
	{
		printf("ERROR!!! The buf is NULL!\n");
		return -1;
    }
	
	/*开始解析RTP包头*/
    if (len < 12)
    {   	
		TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s:%d: The length of RTP Packet is less than 12!!!\n", __func__, __LINE__);
        return -1;
    }

    if ((buf[0] & 0xc0) != (RTP_VERSION << 6)) /*RTP版本号*/
    {   
		TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s:%d: The version number of RTP Packet is wrong!!!\n", __func__, __LINE__);
        return -1;
    }

    if ((s->seq == 0 && !s->queue) || s->queue_size <= 1) 
	{
        /* First packet, or no reordering */	
        return rtp_parse_packet_internal(s,buf, len);
    } 
	else
	{
        uint16_t seq = AV_RB16(buf + 2);
        int16_t diff = seq - s->seq;

		//printf("now_seq:%d,sent_seq:%d\n",seq,s->seq);
        if (diff < 0)
		{
            /* Packet older than the previously emitted one, drop */
			printf("RTP: dropping old packet received too late\n");
            return -1;
        } 
		else if (diff <= 1)
		{
            /* Correct packet */
            rv = rtp_parse_packet_internal(s, buf, len);
            return rv;
        } 
		else 
		{
            /* Still missing some packet, enqueue this one. */
            enqueue_packet(s, buf, len);
            *bufptr = NULL;
            /* Return the first enqueued packet if the queue is full,
             * even if we're missing something */
            if (s->queue_len >= s->queue_size)  /*重排RTP包的队列已满*/
            {        	
				printf("%s Line %d ------> The re-order queue is full!! queue_len:%d\n",
											__func__,__LINE__,s->queue_len);
                return rtp_parse_queued_packet(s);
            }
            return -1;
        }
    }
}


/**
 * Parse an RTP or RTCP packet directly sent as a buffer.
 * @param s RTP parse context.
 * @param pkt returned packet
 * @param bufptr pointer to the input buffer or NULL to read the next packets
 * @param len buffer len
 * @return 0 if a packet is returned, 1 if a packet is returned and more can follow
 * (use buf as NULL to read the next). -1 if no packet (error or no more packet).
 */
/*
** 功能: 解析RTP数据包,去除RTP包头,将RTP负载传递给PS流解析接口
*/
int ff_rtp_parse_packet(RTPDemuxContext *s, /* AVPacket *pkt,*/
                        uint8_t **bufptr, int len)
{
    int rv;
	
    rv = rtp_parse_one_packet(s, bufptr, len);
    s->prev_ret = rv;
	
    while (has_next_packet(s))
    {
    	printf("%s Line %d ------> there exists next packet!! sent_seq:%d\n",__func__,__LINE__,s->seq);
        rv = rtp_parse_queued_packet(s);
    }
	
    return rv ? rv : has_next_packet(s);
}





