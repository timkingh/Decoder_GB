/*
 * MPEG1/2 demuxer
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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


/*global*/
#include "global_def.h"
#include "global_api.h"
#include "global_msg.h"

#include "GB_Decode_PS_Data.h"
#include "rtpdec.h"

#define MAX_NET_PAYLOAD_LENGTH 1400


/*读取一个字节的数据*/
int avio_r8(SIP_Context *s)
{
    /*if (s->buf_ptr >= s->buf_end)
        fill_buffer(s);*/
	if(s->recv_buffer_ptr >= s->recv_buffer_end)
	{
		printf("The buffer has no data now!\n");
		return -1;
	}
	
    if (s->recv_buffer_ptr < s->recv_buffer_end)
        return *s->recv_buffer_ptr++;
	
    return 0;
}


unsigned int avio_rb16(SIP_Context *s)
{
    unsigned int val;
	int ret = -1;
	
	ret = avio_r8(s);
	if(ret < 0)
	{
		return -1;
	}
    val = ret << 8;

	ret = avio_r8(s);
	if(ret < 0)
	{
		return -1;
	}
    val |= ret;
	
    return val;
}


unsigned int avio_rb32(SIP_Context *s)
{
    unsigned int val;
    val = avio_rb16(s) << 16;
    val |= avio_rb16(s);
    return val;
}

#if 0
static int check_pes(const uint8_t *p, const uint8_t *end)
{
    int pes1;
    int pes2 = (p[3] & 0xC0) == 0x80 &&
               (p[4] & 0xC0) != 0x40 &&
               ((p[4] & 0xC0) == 0x00 ||
                (p[4] & 0xC0) >> 2 == (p[6] & 0xF0));

    for (p += 3; p < end && *p == 0xFF; p++) ;
    if ((*p & 0xC0) == 0x40)
        p += 2;

    if ((*p & 0xF0) == 0x20)
        pes1 = p[0] & p[2] & p[4] & 1;
    else if ((*p & 0xF0) == 0x30)
        pes1 = p[0] & p[2] & p[4] & p[5] & p[7] & p[9] & 1;
    else
        pes1 = *p == 0x0F;

    return pes1 || pes2;
}

static int check_pack_header(const uint8_t *buf)
{
    return (buf[1] & 0xC0) == 0x40 || (buf[1] & 0xF0) == 0x20;
}

static int mpegps_probe(AVProbeData *p)
{
    uint32_t code = -1;
    int i;
    int sys = 0, pspack = 0, priv1 = 0, vid = 0;
    int audio = 0, invalid = 0, score = 0;
    int endpes = 0;

    for (i = 0; i < p->buf_size; i++) {
        code = (code << 8) + p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
            int len  = p->buf[i + 1] << 8 | p->buf[i + 2];
            int pes  = endpes <= i && check_pes(p->buf + i, p->buf + p->buf_size);
            int pack = check_pack_header(p->buf + i);

            if (code == SYSTEM_HEADER_START_CODE)
                sys++;
            else if (code == PACK_START_CODE && pack)
                pspack++;
            else if ((code & 0xf0) == VIDEO_ID && pes) {
                endpes = i + len;
                vid++;
            }
            // skip pes payload to avoid start code emulation for private
            // and audio streams
            else if ((code & 0xe0) == AUDIO_ID &&  pes) {audio++; i+=len;}
            else if (code == PRIVATE_STREAM_1  &&  pes) {priv1++; i+=len;}
            else if (code == 0x1fd             &&  pes) vid++; //VC1

            else if ((code & 0xf0) == VIDEO_ID && !pes) invalid++;
            else if ((code & 0xe0) == AUDIO_ID && !pes) invalid++;
            else if (code == PRIVATE_STREAM_1  && !pes) invalid++;
        }
    }

    if (vid + audio > invalid + 1) /* invalid VDR files nd short PES streams */
        score = AVPROBE_SCORE_EXTENSION / 2;

//     av_log(NULL, AV_LOG_ERROR, "vid:%d aud:%d sys:%d pspack:%d invalid:%d size:%d \n",
//            vid, audio, sys, pspack, invalid, p->buf_size);

    if (sys > invalid && sys * 9 <= pspack * 10)
        return (audio > 12 || vid > 3 || pspack > 2) ? AVPROBE_SCORE_EXTENSION + 2
                                                     : AVPROBE_SCORE_EXTENSION / 2 + 1; // 1 more than mp3
    if (pspack > invalid && (priv1 + vid + audio) * 10 >= pspack * 9)
        return pspack > 2 ? AVPROBE_SCORE_EXTENSION + 2
                          : AVPROBE_SCORE_EXTENSION / 2; // 1 more than .mpg
    if ((!!vid ^ !!audio) && (audio > 4 || vid > 1) && !sys &&
        !pspack && p->buf_size > 2048 && vid + audio > invalid) /* PES stream */
        return (audio > 12 || vid > 3 + 2 * invalid) ? AVPROBE_SCORE_EXTENSION + 2
                                                     : AVPROBE_SCORE_EXTENSION / 2;

    // 02-Penguin.flac has sys:0 priv1:0 pspack:0 vid:0 audio:1
    // mp3_misidentified_2.mp3 has sys:0 priv1:0 pspack:0 vid:0 audio:6
    // Have\ Yourself\ a\ Merry\ Little\ Christmas.mp3 0 0 0 5 0 1 len:21618
    return score;
}



static int mpegps_read_header(AVFormatContext *s)
{
    MpegDemuxContext *m = s->priv_data;
    char buffer[7];
    int64_t last_pos = avio_tell(s->pb);

    m->header_state = 0xff;
    s->ctx_flags   |= AVFMTCTX_NOHEADER;

    avio_get_str(s->pb, 6, buffer, sizeof(buffer));
    if (!memcmp("IMKH", buffer, 4)) {
        m->imkh_cctv = 1;
    } else if (!memcmp("Sofdec", buffer, 6)) {
        m->sofdec = 1;
    } else
       avio_seek(s->pb, last_pos, SEEK_SET);

    /* no need to do more */
    return 0;
}
#endif

/**
 * Parse MPEG-PES five-byte timestamp
 * 规范参见ISO13818-1 表2-21
 */
static inline int64_t ff_parse_pes_pts(const uint8_t *buf) 
{
    return (int64_t)(*buf & 0x0e) << 29 |
            (AV_RB16(buf+1) >> 1) << 15 |
             AV_RB16(buf+3) >> 1;
}


static int64_t get_pts(SIP_Context *pb, int c)
{
    uint8_t buf[5];

    buf[0] = c < 0 ? avio_r8(pb) : c;
    //avio_read(pb, buf + 1, 4);
    /*拷贝四个字节*/
	SN_MEMCPY(buf + 1,4,pb->recv_buffer_ptr,pb->ps_data_length,4);
	pb->recv_buffer_ptr += 4;

    return ff_parse_pes_pts(buf);
}


static int find_next_start_code(SIP_Context *pb, int *size_ptr,int32_t *header_state)
{
	unsigned int state, v;
	int val, n;

	state = *header_state;
	n     = *size_ptr;

	while (n > 0) 
	{
		/*判断缓冲区的指针是否到了数据结尾*/
		if (pb->recv_buffer_ptr >= pb->recv_buffer_end)
		    break;
		    
		v = avio_r8(pb);
		n--;

		if (state == 0x000001) 
		{
		    state = ((state << 8) | v) & 0xffffff;
		    val   = state;
		    goto found;
		}
		state = ((state << 8) | v) & 0xffffff;
	}
	val = -1;

found:
	*header_state = state;
	*size_ptr     = n;
	
	return val;
}


/**
 * Extract stream types from a program stream map
 * According to ISO/IEC 13818-1 ('MPEG-2 Systems') table 2-35
 *
 * @return number of bytes occupied by PSM in the bitstream
 */
static long mpegps_psm_parse(MpegDemuxContext *m, SIP_Context *pb)
{
    int psm_length, ps_info_length, es_map_length;

    psm_length = avio_rb16(pb);  /*节目流映射的长度*/
    avio_r8(pb);
    avio_r8(pb);
    ps_info_length = avio_rb16(pb); /*节目流信息的长度*/

    /* skip program_stream_info */
    //avio_skip(pb, ps_info_length); /*跳过节目流信息*/
	pb->recv_buffer_ptr += ps_info_length;
	
    /*es_map_length = */avio_rb16(pb);  /*基本流映射的长度*/
    /* Ignore es_map_length, trust psm_length */
    es_map_length = psm_length - ps_info_length - 10; /*减去节目流映射的前十个字节*/

    /* at least one es available? */
    while (es_map_length >= 4) 
	{
		/*四个字节:按照“1字节流类型字段”“1字节基本流标识字段”
		       “2字节基本流信息长度字段”“基本流信息”循环。*/
        unsigned char type      = avio_r8(pb); /*H.264 - 0x1B*/
        unsigned char es_id     = avio_r8(pb); /*视频 - 0xE0*/
        uint16_t es_info_length = avio_rb16(pb);

        /* remember mapping from stream id to stream type */
        m->psm_es_type[es_id] = type;
		
        /* skip program_stream_info */
        //avio_skip(pb, es_info_length);
		pb->recv_buffer_ptr += es_info_length;
	
        es_map_length -= 4 + es_info_length;
    }
    avio_rb32(pb); /* crc32 */ /* CRC_32，循环冗余校验*/
	
    return 2 + psm_length;
}


/* read the next PES header. Return its position in ppos
 * (if not NULL), and its start code, pts and dts.
 */
/*
** ppos: PES包在缓冲区的偏移
** pstart_code: PES包的起始码,0x00 00 01 E0等(stream_id,0xE0表示视频,0xC0表示音频)
** 返回: PES包的长度
*/
static int mpegps_read_pes_header(SIP_Context *s,
                                  int64_t *ppos, int *pstart_code,
                                  int64_t *ppts, int64_t *pdts)
{
    MpegDemuxContext *m = s->mpegDemuxContext;
    int len, size, startcode, c, flags, header_len;
    int pes_ext, ext2_len, id_ext, skip;
    int64_t pts, dts;
   
redo:
    /* next start code (should be immediately after) */
    m->header_state = 0xff;
    size      = MAX_NET_PAYLOAD_LENGTH;
    startcode = find_next_start_code(s, &size, &m->header_state);
    if (startcode < 0) 
	{
		printf("%s Line %d: No startcode in this packet!\n",__func__,__LINE__);		
        return 0;
    }

    if (startcode == PACK_START_CODE) /*PS包头起始码*/
    {
    	TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s Line %d:there exists PS header(0x000001ba)!\n",__func__,__LINE__);
        goto redo;
    }
	
    if (startcode == SYSTEM_HEADER_START_CODE) /*PS系统头起始码*/
    {   	
    	TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s Line %d:there exists PS system header(0x000001bb)!\n",__func__,__LINE__);
        goto redo;
    }
	
    if (startcode == PADDING_STREAM) /*填充*/
	{
        //avio_skip(s->pb, avio_rb16(s->pb));      
    	TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s Line %d:there exists padding bits!\n",__func__,__LINE__);
		s->recv_buffer_ptr += avio_rb16(s); /*跳过填充字节*/
        goto redo;
    }
	
    if (startcode == PRIVATE_STREAM_2) /*私有*/
    {
    	/*这里要如何处理???*/
    	TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s Line %d:there exists private stream!\n",__func__,__LINE__);
    }
	
    if (startcode == PROGRAM_STREAM_MAP) 
	{	
		TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s Line %d:there exists PS system map(0x000001bc)!\n",__func__,__LINE__);
        mpegps_psm_parse(m, s);
        goto redo;
    }

    /* find matching stream */ /*开始读取PES包*/
    if (!((startcode >= 0x1c0 && startcode <= 0x1df) ||   /*音频流*/
          (startcode >= 0x1e0 && startcode <= 0x1ef) ||   /*视频流*/
          (startcode == 0x1bd) ||                       /*私有流*/
          (startcode == PRIVATE_STREAM_2) ||      /*私有流*/
          (startcode == 0x1fd)))           /*扩展流*/
    {
        goto redo;
    }
	
    if (ppos) 
	{
		/*暂时不解析*/
        //*ppos = avio_tell(s->pb) - 4; /*PES包在缓冲区的偏移*/
    }
	
    len = avio_rb16(s); /*PES包长度*/
    pts = 0;
    dts = 0;
	
    if (startcode != PRIVATE_STREAM_2)
	{
	    /* stuffing */
	    for (;;) 
		{
	        if (len < 1)
	        {
				printf("Line %d:The PES's length is wrong!\n",__LINE__);
				return -1;
	        }
			
	        c = avio_r8(s);
	        len--;
			
	        /* XXX: for mpeg1, should test only bit 7 */
	        if (c != 0xff)
	            break;
	    }
		
	    if ((c & 0xc0) == 0x40) 
		{
	        /* buffer scale & size */
	        avio_r8(s);
	        c    = avio_r8(s);
	        len -= 2;
	    }
		
	    if ((c & 0xe0) == 0x20) 
		{
	        dts  =
	        pts  = get_pts(s, c);
	        len -= 4;
	        if (c & 0x10) 
			{
	            dts  = get_pts(s, -1);
	            len -= 5;
	        }
	    } 
		else if ((c & 0xc0) == 0x80)  /*主要是这个分支,PES包的第7个字节*/
		{
	        /* mpeg 2 PES */
	        flags      = avio_r8(s); /*PES包的第8个字节*/
	        header_len = avio_r8(s); /*PES包的第9个字节*/
	        len       -= 2;
			
	        if (header_len > len)
	        {
				printf("Line %d:The PES's extension length is wrong!\n",__LINE__);
				return -1;
	        }
			
	        len -= header_len; /*得到PES包的数据长度*/
			
	        if (flags & 0x80) 
			{
	            dts         = pts = get_pts(s, -1);
	            header_len -= 5;
	            if (flags & 0x40) 
				{
	                dts         = get_pts(s, -1);
	                header_len -= 5;
	            }
	        }
			
	        if (flags & 0x3f && header_len == 0) 
			{
	            flags &= 0xC0;
	            printf("Further flags set but no bytes left\n");
	        }
			
	        if (flags & 0x01)  /*PES_extension_flag*/
			{ 
				/*应该比较少进入这个分支*/
				/* PES extension */
	            pes_ext = avio_r8(s);
	            header_len--;
				
	            /* Skip PES private data, program packet sequence counter
	             * and P-STD buffer */
	            skip  = (pes_ext >> 4) & 0xb;
	            skip += skip & 0x9;
	            if (pes_ext & 0x40 || skip > header_len) 
				{
	                printf("pes_ext %X is invalid\n", pes_ext);
	                pes_ext = skip = 0;
	            }
				
	            //avio_skip(s->pb, skip);
	            s->recv_buffer_ptr += skip;	           			
	            header_len -= skip;

	            if (pes_ext & 0x01) 
				{ 
					/* PES extension 2 */
	                ext2_len = avio_r8(s);
	                header_len--;
					
	                if ((ext2_len & 0x7f) > 0) 
					{
	                    id_ext = avio_r8(s);
	                    if ((id_ext & 0x80) == 0)
	                        startcode = ((startcode & 0xff) << 8) | id_ext;
	                    header_len--;
	                }
	            }
	        }
	        if (header_len < 0)
	        {
	            //goto error_redo;
				printf("Line %d:The PES's extension length is wrong!\n",__LINE__);
				return -1;
	        }
			
	        //avio_skip(s->pb, header_len);
			s->recv_buffer_ptr += header_len;	
	    }
		else if (c != 0xf)
	        goto redo;
	}

    if (startcode == PRIVATE_STREAM_1) 
	{
        startcode = avio_r8(s);
        len--;
    }
	
    if (len < 0)
    {
		printf("Line %d:The PES's length is wrong!\n",__LINE__);
		return -1;
    }

    *pstart_code = startcode;
    *ppts        = pts;
    *pdts        = dts;

	s->pes_packet_length = len;
	//printf("%s Line %d:The PES's length is %d Bytes!\n",__func__,__LINE__,s->pes_packet_length);
    return len;
}


static void rtp_stream_testB(AVPacket * av_packet)
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

		src_file = open("/var/tmp/buffer_write_stream_test.h264",O_RDWR | O_CREAT | O_APPEND);
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


static void rtp_stream_testA(AVPacket * av_packet)
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

		src_file = open("/var/tmp/preview_stream_test.h264",O_RDWR | O_CREAT | O_APPEND);
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


static void rtp_stream_test(AVPacket * av_packet)
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

		src_file = open("/var/tmp/rtp_stream_test.h264",O_RDWR | O_CREAT | O_APPEND);
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


static int GB_AVPacket_Free(AVPacket *head)
{
	AVPacket *p = head;
	AVPacket *p_next = head;
	AVPacket *next = NULL;
	int codeid;
	
	if(p == NULL) 
	{
		TRACE(SCI_TRACE_NORMAL, MOD_GB, "%s--%d a null packet?\n",__func__,__LINE__);
		return -1;
	}
	
	if(head->codec_id == CODEC_ID_H264 /*|| head->codec_id == CODEC_ID_H265*/)
	{
		codeid = gVideoPool;
	}
	else
	{
		codeid = gAudioPool;
	}
	
	while(p) 
	{
		p_next = p;
		p = p->next;
		while(p_next)
		{
			next = p_next->Extendnext;
			SN_MPFree(codeid, (void **)&p_next);
			p_next = next;
		}
	}
	
	return 0;
}


/*
** 功能: 解析NALU类型，跳过NALU起始码
** 定义: naluType ---->  1 - P帧; 2 - 帧的分片; 5 - I帧;
**                       6 - SEI帧;  7 - SPS帧; 8 - PPS帧
**                       9 - 分界符
** 返回: 0 - 成功; -1 - 失败
*/
static int GB_Parse_NALU_Type(AVPacket* packet)
{
	int naluType,i = 0;
	int datasize = 0;
	unsigned char* beginData;
	
	if (packet == NULL) 
	{
		printf("%s Line %d: The Packet is NULL!!!\n",__func__,__LINE__);
		return -1;
	}
	
	beginData = (unsigned char *)packet + packet->BufOffset;	
	datasize = packet->DataSize - packet->BufOffset;
	
	if(datasize > 20)
	{
		datasize = 16;
	}
	else if(datasize < 5)
	{
		datasize = 0;
	}
	else
	{
		datasize = datasize - 4;
	}
	
	for(i=0;i < datasize;i++)
	{
		if(beginData[i] == 0 && beginData[i+1] == 0
			&&beginData[i+2] == 0 && beginData[i+3] == 1)
		{
			break;
		}
	}

	if(i == datasize)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_GB,"%s Line %d: It is a fragmented frame!!!  datasize:%d\n",__func__,__LINE__,datasize);
		//printf("0x%x,0x%x,0x%x,0x%x\n",beginData[0],beginData[1],beginData[2],beginData[3]);
		naluType = 2; /*自定义，某个视频帧的分片*/
	}
	else
	{
		packet->BufOffset += i + 4; /*跳过NALU起始码*/
		naluType = beginData[i+4] & 0x1f;

		if(naluType == 0x01 || naluType == 0x09)
		{
			packet->frame_type = 0;
		}
		else if(naluType == 0x05 || naluType == 0x06 || naluType == 0x07
			  	|| naluType == 0x08)
		{
			packet->frame_type = 1;
		}
		else
		{			
			printf("%s Line %d: GB_Parse_NALU_Type ERROR!!! naluType:%d\n",__func__,__LINE__,naluType);
			return -1;
		}
	}

	packet->naluType = naluType;
	return 0;
}


static int GB_Buffer_Write(AVPacket* packet)
{
	AVPacket * tlist = packet;
	AVPacket * dp= NULL;
	AVPacket * ptem= NULL;
	unsigned int size = 0;
	int ret = -1;
	
	if(packet == NULL)
	{		
		printf("%s Line %d: packet is NULL!!!\n",__func__,__LINE__);
		return -1;
	}

	if(packet->codec_id == CODEC_ID_H264)
	{		
		while(tlist != NULL)
		{
			dp = (AVPacket *)tlist->next;
			if(tlist->frame_type == 0 || tlist->frame_type == 1)
				size += 4;
			
			while(tlist != NULL)
			{
				ptem = (AVPacket *)tlist->Extendnext;
				size += tlist->DataSize - tlist->BufOffset;
				tlist = ptem;
			}
			tlist = dp;
		}					
		
		packet->data_len = size;
	}

	rtp_stream_testA(packet);
	
	ret = BufferWrite(0, (char*)packet, packet->data_len + sizeof(AVPacket));
	if(ret < 0)
	{
		printf("%s Line %d: BufferWrite ERROR!!!\n",__func__,__LINE__);
		return -1;		
	}	
	//rtp_stream_testB(packet);
	
	return 0;
}


/*
** 功能: 将AVPacket写入数据链表中
** 参数:
*/
static int GB_Add_Packet_Into_List(SIP_Context *s)
{
	//printf("%s Line %d ---------> here!!!\n",__func__,__LINE__);
	AVPacket * bPacket = s->fPacketSentToDecoder;
	AVPacket * tempPacket = NULL;
	int isExistIframeOrPframe = 0;

	if(s->fPacketReadInProgress == NULL)
	{
		printf("%s Line %d: fPacketReadInProgress is NULL!!!\n",__func__,__LINE__);
		return -1;
	}

	if(bPacket == NULL)
	{
		TRACE(SCI_TRACE_NORMAL, MOD_GB,"%s Line %d ---------> start to store packet!!!\n",__func__,__LINE__);
		s->fPacketSentToDecoder = s->fPacketReadInProgress;
	}
	else
	{
		tempPacket = bPacket;
		while(tempPacket)
		{
			if(tempPacket->naluType == 1 || tempPacket->naluType == 5)
			{
				isExistIframeOrPframe = 1;
			}
			bPacket = tempPacket;
			tempPacket = bPacket->next;
		}

		if(isExistIframeOrPframe && s->fPacketReadInProgress->naluType != 2)
		{
			s->DataCount++;
			s->fPacketSentToDecoder->seqno = s->DataCount;
			
			/*AVPacket中已存在I帧或者P帧,而且最近读取到的一帧不属于之前收到帧的分片*/
			GB_Buffer_Write(s->fPacketSentToDecoder);
			s->fPacketSentToDecoder = s->fPacketReadInProgress;
		}
		else
		{
			/*将最近读取的数据帧挂到fPacketSentToDecoder链表后面*/
			bPacket->next = s->fPacketReadInProgress;
		}
	}

	return 0;
}



/*
** 功能: 将ES流写到AVPacket中
** 参数: DataReadLen - 要在缓冲区中读取的数据长度
*/
static int GB_Store_Packet(SIP_Context *s,int DataReadLen)
{
	AVPacket * bPacket = s->fPacketReadInProgress;
	AVPacket * tempPacket = NULL;
	unsigned int totalsize = 0;
	int dataSizeNeedCopy = DataReadLen; /*dataSizeNeedCopy: 需要拷贝的字节长度*/

	if (bPacket == NULL) /*分配内存给bPacket*/
	{		
		if(s->streamTypeBit == 0)
		{
			bPacket = (AVPacket * )SN_MPMalloc(gVideoPool, RTP_PACKET_SIZE);
			if(bPacket != NULL)
			{
				SN_MEMSET(bPacket,0,sizeof(AVPacket));
				bPacket->codec_id = s->code_id;
			}
		}
		else
		{
			bPacket = (AVPacket * )SN_MPMalloc(gAudioPool, RTP_PACKET_SIZE1);
			if(bPacket != NULL)
			{
				SN_MEMSET(bPacket,0,sizeof(AVPacket));
				bPacket->codec_id = CODEC_ID_PCMA;
			}
		}
		
		if(bPacket != NULL)
		{
			/*保存正在读取的Packet地址*/
			s->fPacketReadInProgress = bPacket;
		
			/*初始化变量*/
			bPacket->BufOffset = sizeof(AVPacket);
			bPacket->BufRead = bPacket->BufOffset;
			bPacket->DataSize = sizeof(AVPacket);
			bPacket->data_len = 0;
			bPacket->rtpOffset = sizeof(AVPacket);
			bPacket->frame_type = 3; /*默认是NALU的分片*/
			bPacket->isFirstPacket = FALSE;	
			bPacket->Extendnext =NULL;
		}
		else
		{
			printf("%s Line %d: SN_MPMalloc ERROR!!!\n",__func__,__LINE__);
			return -1;
		} 	
 	}	

	if(bPacket->codec_id == CODEC_ID_H264)
	{
		totalsize = RTP_PACKET_SIZE;
	}
	else
	{
		totalsize = RTP_PACKET_SIZE1;
	}

	/*将数据拷贝到AVPacket*/
	if(bPacket->Extendnext == NULL)
	{	
		if((dataSizeNeedCopy > totalsize - bPacket->DataSize) && (totalsize - bPacket->DataSize > 1))
		{
			/*一个AVPacket的空间无法存储下所有的数据*/
			SN_MEMCPY((char *)bPacket + bPacket->DataSize, totalsize - bPacket->DataSize,
							s->recv_buffer_ptr, s->recv_buffer_end - s->recv_buffer_ptr,
							totalsize - bPacket->DataSize);
						
			s->recv_buffer_ptr += (totalsize - bPacket->DataSize);			
			dataSizeNeedCopy -=  (totalsize - bPacket->DataSize);		
			bPacket->DataSize +=  (totalsize - bPacket->DataSize);
		}
		else if(dataSizeNeedCopy <= (totalsize - bPacket->DataSize))
		{
			/*一个AVPacket的空间可以存储下所有的数据*/
			SN_MEMCPY((char *)bPacket + bPacket->DataSize, totalsize - bPacket->DataSize,
								s->recv_buffer_ptr, s->recv_buffer_end - s->recv_buffer_ptr,
									dataSizeNeedCopy);

			bPacket->DataSize +=  dataSizeNeedCopy;			
			s->recv_buffer_ptr += dataSizeNeedCopy;		
			dataSizeNeedCopy =  0;
		}
		
		while(dataSizeNeedCopy > 0)  /*一个缓冲区没有存储下所有的数据，开辟扩展缓冲区*/
		{
			if(bPacket->codec_id == CODEC_ID_H264)
			{
				tempPacket = (AVPacket * )SN_MPMalloc(gVideoPool, RTP_PACKET_SIZE);
			}
			else
			{
				tempPacket = (AVPacket * )SN_MPMalloc(gAudioPool, RTP_PACKET_SIZE1);
			}
			
			if(tempPacket != NULL)
			{
				SN_MEMSET(tempPacket,0,sizeof(AVPacket));	
				tempPacket->BufOffset = sizeof(AVPacket);
				tempPacket->BufRead = 0;
				tempPacket->rtpOffset = 0;
				tempPacket->isFirstPacket = FALSE;			
				tempPacket->DataSize = sizeof(AVPacket);			
				tempPacket->frame_type = 3;
				tempPacket->next = NULL;
				tempPacket->Extendnext= NULL;
				tempPacket->data_len = 0;
			}
			else
			{	
				printf("%s Line %d: SN_MPMalloc ERROR!!!\n",__func__,__LINE__);
				return 0;
			}
			tempPacket->codec_id = bPacket->codec_id;
			
			if(dataSizeNeedCopy >=  (totalsize - sizeof(AVPacket)))
			{		
				SN_MEMCPY((char *)tempPacket + tempPacket->DataSize, totalsize - tempPacket->DataSize,
								s->recv_buffer_ptr, s->recv_buffer_end - s->recv_buffer_ptr,
								totalsize - sizeof(AVPacket));			
				
				s->recv_buffer_ptr += (totalsize - sizeof(AVPacket));	
				dataSizeNeedCopy -=  (totalsize - sizeof(AVPacket));
				tempPacket->DataSize += (totalsize - sizeof(AVPacket)); 					
			}
			else
			{	
				SN_MEMCPY((char *)tempPacket + tempPacket->DataSize, totalsize - tempPacket->DataSize,
									s->recv_buffer_ptr, s->recv_buffer_end - s->recv_buffer_ptr,
										dataSizeNeedCopy);
				
				tempPacket->DataSize += dataSizeNeedCopy;
				s->recv_buffer_ptr += dataSizeNeedCopy;				
				dataSizeNeedCopy = 0;
			}
			
			bPacket->Extendnext = (void *)tempPacket;
			bPacket = tempPacket;
		}
	}
	else if(bPacket->Extendnext != NULL)
	{
		while(bPacket->Extendnext != NULL)
		{				
			bPacket = (AVPacket *)bPacket->Extendnext;
		}
		
		if((dataSizeNeedCopy > totalsize - bPacket->DataSize) && (totalsize - bPacket->DataSize > 1))
		{
			SN_MEMCPY((char *)bPacket + bPacket->DataSize, totalsize - bPacket->DataSize,
								s->recv_buffer_ptr, s->recv_buffer_end - s->recv_buffer_ptr,
								totalsize - bPacket->DataSize);
			
			s->recv_buffer_ptr += (totalsize - bPacket->DataSize);
			dataSizeNeedCopy -=  (totalsize - bPacket->DataSize);
			bPacket->DataSize +=  (totalsize - bPacket->DataSize);
		}
		else if(dataSizeNeedCopy <= totalsize - bPacket->DataSize)
		{
			SN_MEMCPY((char *)bPacket + bPacket->DataSize, totalsize - bPacket->DataSize,
					s->recv_buffer_ptr, s->recv_buffer_end - s->recv_buffer_ptr,
						dataSizeNeedCopy);
			
			bPacket->DataSize +=  dataSizeNeedCopy;			
			s->recv_buffer_ptr += dataSizeNeedCopy;	
			dataSizeNeedCopy =  0;
		}
		
		while(dataSizeNeedCopy > 0)
		{
			if(bPacket->codec_id == CODEC_ID_H264)
				tempPacket = (AVPacket * )SN_MPMalloc(gVideoPool, RTP_PACKET_SIZE);
			else
				tempPacket = (AVPacket * )SN_MPMalloc(gAudioPool, RTP_PACKET_SIZE1);
			
			if(tempPacket != NULL)
			{
				SN_MEMSET(tempPacket,0,sizeof(AVPacket));
				tempPacket->BufOffset = sizeof(AVPacket);
				tempPacket->BufRead = 0;
				tempPacket->rtpOffset = 0;
				tempPacket->isFirstPacket = FALSE;			
				tempPacket->DataSize = sizeof(AVPacket);
				tempPacket->frame_type = 3;
				tempPacket->next = NULL;
				tempPacket->Extendnext= NULL;
				tempPacket->data_len = 0;
			}
			else
			{	
				printf("%s Line %d: SN_MPMalloc ERROR!!!\n",__func__,__LINE__);
				return 0;
			}
			tempPacket->codec_id = bPacket->codec_id;
			
			if(dataSizeNeedCopy >=  (totalsize - sizeof(AVPacket)))
			{		
				SN_MEMCPY((char *)tempPacket + tempPacket->DataSize, totalsize - tempPacket->DataSize,
								s->recv_buffer_ptr, s->recv_buffer_end - s->recv_buffer_ptr,
								totalsize - sizeof(AVPacket));			
				
				tempPacket->DataSize += (totalsize - sizeof(AVPacket)); 					
				s->recv_buffer_ptr += (totalsize - sizeof(AVPacket));	
				dataSizeNeedCopy -=  (totalsize - sizeof(AVPacket));
			}
			else
			{	
				SN_MEMCPY((char *)tempPacket + tempPacket->DataSize, totalsize - tempPacket->DataSize,
									s->recv_buffer_ptr, s->recv_buffer_end - s->recv_buffer_ptr,
										dataSizeNeedCopy);
				
				tempPacket->DataSize += dataSizeNeedCopy;
				s->recv_buffer_ptr += dataSizeNeedCopy;				
				dataSizeNeedCopy = 0;
			}
			bPacket->Extendnext = (void *)tempPacket;
			bPacket = tempPacket;
		}
	}

	s->pes_bytes_have_read += DataReadLen;	
	if(s->pes_bytes_have_read == s->pes_packet_length)
	{
		/*将AVPacket加入数据链表*/
		if(s->fPacketReadInProgress)
		{			
			/*解析Packet的帧类型，并将数据偏移指针指向ES流的第一个字节(跳过NALU起始码0x00 00 00 01)*/
			GB_Parse_NALU_Type(s->fPacketReadInProgress);

			/*时间戳管理*/
			s->fPacketReadInProgress->pts = (unsigned long long)s->rtpDemuxContext->timestamp;
			if(s->fPacketReadInProgress->naluType == 0x09)
			{
				GB_AVPacket_Free(s->fPacketReadInProgress);
			}
			else
			{
				//rtp_stream_test(s->fPacketReadInProgress);

				/*根据NALU类型决定是否将数据写入数据链表*/				
				GB_Add_Packet_Into_List(s);
			}
		}
		s->fPacketReadInProgress = NULL;
		
		TRACE(SCI_TRACE_NORMAL, MOD_GB,"%s Line %d: have read a complete PES packet(%d Bytes)!\n\n",
										__func__,__LINE__,s->pes_bytes_have_read);
		s->pes_bytes_have_read = 0;
	}
	else if(s->pes_bytes_have_read > s->pes_packet_length)
	{
		printf("%s Line %d: there is something wrong with reading  packet!\n",__func__,__LINE__);
		s->pes_bytes_have_read = 0; /*读取数据出错，重新开始读取数据*/
	}

	return 0;
}


/*解析PS包*/
int mpegps_read_packet(SIP_Context *s /*,AVPacket *pkt*/)
{
    MpegDemuxContext *m = s->mpegDemuxContext;
    int len = 0, startcode = 0, es_type;
    enum AVCodecID codec_id = AV_CODEC_ID_NONE;
    enum AVMediaType type;
    int64_t pts, dts, dummy_pos; // dummy_pos is needed for the index building to work
    int /*src_file = 0,rlen = 0,*/DataReadLen = 0,RemainedLen = 0;

redo:
	/*开始读取一个PES包*/
	if(s->pes_bytes_have_read == 0 || s->isMissedPacket == 1) /*如果发生丢包,重新读取下一个PES包*/
	{
	    len = mpegps_read_pes_header(s, &dummy_pos, &startcode, &pts, &dts);
		//printf("The length of PES packet:%d\n",len);
	    if (len > 0)
	    {   
	    	/*读取到新的PES包长度*/
	    	s->isMissedPacket = 0; 
			s->pes_bytes_have_read = 0;
	    }
		else if(len == 0 && s->isMissedPacket == 1)
		{
			/*发生丢包后，在后续到来的数据包中没有找到PES包起始码，缓冲区指针复位，认为该数据包
			  仍然属于上一个PES包*/
			s->recv_buffer_ptr = s->recv_buffer_ptr - s->ps_data_length;
		}

		/*PSM(节目流映射)中的编码类型*/
	    es_type = m->psm_es_type[startcode & 0xff];

		if (es_type == STREAM_TYPE_VIDEO_MPEG4) 
	    {
	        codec_id = AV_CODEC_ID_MPEG4;
	        type     = AVMEDIA_TYPE_VIDEO;
	    } 
		else if (es_type == STREAM_TYPE_VIDEO_H264)   /*0x1B: H.264码流*/
		{
	        codec_id = AV_CODEC_ID_H264;
	        type     = AVMEDIA_TYPE_VIDEO;

			s->streamTypeBit = 0;
			s->code_id = CODEC_ID_H264;
	    } 
		else if (m->imkh_cctv && es_type == 0x91) /*0x91: G.711.3音频格式 ; G.711的取值为0x90*/
		{
	        codec_id = AV_CODEC_ID_PCM_MULAW; /*pcm_mulaw数据包*/
	        type     = AVMEDIA_TYPE_AUDIO;
	    } 
	    else if(es_type == 0x90)  /*G.711音频格式*/
		{
			/*add by timkingh,15.12.30*/
	    	codec_id = AV_CODEC_ID_PCM_ALAW; /*pcm_alaw数据包*/
	    	type = AVMEDIA_TYPE_AUDIO;

			s->streamTypeBit = 1;
			s->code_id = CODEC_ID_PCMA;
	    }
		else if (startcode >= 0x1e0 && startcode <= 0x1ef) 
		{
			/*H.265在这里体现????*/
	    } 		 
	}

	RemainedLen = s->recv_buffer_end - s->recv_buffer_ptr;
	if(RemainedLen <= 0)
	{
		printf("There is no data in Buffer now!\n");
		return 0;
	}
	
	//printf("len:%d,RemainedLen:%d\n",len,RemainedLen);
	if(len == 0 || len > RemainedLen)
	{	
		/*len为零,则这个RTP负载属于上一个PES包*/
		DataReadLen = RemainedLen;
	}
	else if(len <= RemainedLen)
	{
		/*这个PES包完全包含于该RTP负载*/
		DataReadLen = len;
	}

	#if 0
    /*将数据拷贝到AVPacket*/
	src_file = open("/var/tmp/es_stream_file.h264",O_RDWR | O_CREAT | O_APPEND);
	if(src_file > 0)
	{
		rlen = write(src_file,s->recv_buffer_ptr,DataReadLen);
		//printf("\n%s Line %d -----> DataReadLen:%d,rlen:%d\n",__func__,__LINE__,DataReadLen,rlen);
	}
	close(src_file);
	#endif

	/*调用函数接口,将ES流写到AVPacket*/
	GB_Store_Packet(s,DataReadLen);

	len = 0; /*PES包长度归零*/

	if(s->recv_buffer_ptr < s->recv_buffer_end)
	{
		goto redo;
	}

    return 0;
}












