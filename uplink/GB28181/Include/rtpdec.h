/*
 * RTP demuxer definitions
 * Copyright (c) 2002 Fabrice Bellard
 * Copyright (c) 2006 Ryan Martell <rdm4@martellventures.com>
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

#ifndef AVFORMAT_RTPDEC_H
#define AVFORMAT_RTPDEC_H

//#include "libavcodec/avcodec.h"
//#include "avformat.h"
//#include "rtp.h"
//#include "url.h"
//#include "srtp.h"


/*global*/
#include "global_api.h"
#include "global_def.h"
#include "global_msg.h"


typedef struct PayloadContext PayloadContext;
typedef struct RTPDynamicProtocolHandler RTPDynamicProtocolHandler;

//typedef char	  int8_t;
typedef short	  int16_t;
typedef int 	  int32_t;
//typedef long long int64_t;
typedef unsigned char	   uint8_t;
//typedef unsigned short	   uint16_t;
typedef unsigned int	   uint32_t;
typedef unsigned long long uint64_t;


#define RTP_MIN_PACKET_LENGTH 12
#define RTP_MAX_PACKET_LENGTH 8192

#define RTP_REORDER_QUEUE_DEFAULT_SIZE 10

#define RTP_NOTS_VALUE ((uint32_t)-1)
#define RTP_FLAG_KEY    0x1 ///< RTP packet contains a keyframe
#define RTP_FLAG_MARKER 0x2 ///< RTP marker bit was set for this packet

//typedef struct RTPDemuxContext RTPDemuxContext;


typedef struct RTPPacket {
    unsigned short seq;
    uint8_t *buf;
    int len;
    int64_t recvtime;
    struct RTPPacket *next;
} RTPPacket;


typedef struct SIP_Context SIP_Context;

/*用于RTP数据包解码*/
typedef struct
{
    //AVFormatContext *ic;
    //AVStream *st;
	SIP_Context * pSipContext; /*指向会话的指针*/
	short isFirst; /*用于记录RTP负载类型*/
	
    int payload_type;
    uint32_t ssrc;
    unsigned short seq;
    uint32_t timestamp;
    uint32_t base_timestamp;
    uint32_t cur_timestamp;
    int64_t  unwrapped_timestamp;
    int64_t  range_start_offset;
    int max_payload_size;
    /* used to send back RTCP RR */
    char hostname[256];

    /** Fields for packet reordering @{ */
    int prev_ret;     ///< The return value of the actual parsing of the previous packet
    RTPPacket* queue; ///< A sorted queue of buffered packets not yet returned  /*指向队列头部的指针*/
    int queue_len;    ///< The number of packets in queue
    int queue_size;   ///< The size of queue, or 0 if reordering is disabled

    /* rtcp sender statistics receive */
    uint64_t last_rtcp_ntp_time;
    int64_t last_rtcp_reception_time;
    uint64_t first_rtcp_ntp_time;
    uint32_t last_rtcp_timestamp;
    int64_t rtcp_ts_offset;

    /* rtcp sender statistics */
    unsigned int packet_count;
    unsigned int octet_count;
    unsigned int last_octet_count;
    int64_t last_feedback_time;

    /* dynamic payload stuff */
    const RTPDynamicProtocolHandler *handler;
    PayloadContext *dynamic_protocol_context;
}RTPDemuxContext;


#define AV_RB16(x)                           \
    ((((const uint8_t*)(x))[0] << 8) |          \
      ((const uint8_t*)(x))[1])


#define AV_RB32(x)                                \
    (((uint32_t)((const uint8_t*)(x))[0] << 24) |    \
               (((const uint8_t*)(x))[1] << 16) |    \
               (((const uint8_t*)(x))[2] <<  8) |    \
                ((const uint8_t*)(x))[3])


int ff_rtp_parse_packet(RTPDemuxContext *s, /*AVPacket *pkt,*/
                        uint8_t **buf, int len);


#endif /* AVFORMAT_RTPDEC_H */
