/* 
 * h264bitstream - a library for reading and writing H.264 video
 * Copyright (C) 2005-2007 Auroras Entertainment, LLC
 * 
 * Written by Alex Izvorski <aizvorski@gmail.com>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdint.h>

#ifndef _H264_STREAM_H
#define _H264_STREAM_H        1

#include "bs.h"

#include "h264_sei.h"
#include "global_def.h" 
#ifdef __cplusplus
extern "C" {
#endif



h264_stream_t* h264_new();
void h264_free(h264_stream_t* h);

int find_nal_unit(uint8_t* buf, int size, int* nal_start, int* nal_end);

void rbsp_to_nal(uint8_t* rbsp_buf, int* rbsp_size, uint8_t* nal_buf, int* nal_size);
void nal_to_rbsp(uint8_t* nal_buf, int* nal_size, uint8_t* rbsp_buf, int* rbsp_size);

int read_nal_unit(h264_stream_t* h, uint8_t* buf, int size);
int read_IFrame_nal_unit(h264_stream_t* h, UINT8* buf, int size);


void read_seq_parameter_set_rbsp(h264_stream_t* h, bs_t* b);
void read_scaling_list(bs_t* b, int* scalingList, int sizeOfScalingList, int useDefaultScalingMatrixFlag );
void read_vui_parameters(h264_stream_t* h, bs_t* b);
void read_hrd_parameters(h264_stream_t* h, bs_t* b);

void read_pic_parameter_set_rbsp(h264_stream_t* h, bs_t* b);

void read_sei_rbsp(h264_stream_t* h, bs_t* b);
void read_sei_message(h264_stream_t* h, bs_t* b);
void read_access_unit_delimiter_rbsp(h264_stream_t* h, bs_t* b);
void read_end_of_seq_rbsp(h264_stream_t* h, bs_t* b);
void read_end_of_stream_rbsp(h264_stream_t* h, bs_t* b);
void read_filler_data_rbsp(h264_stream_t* h, bs_t* b);

void read_slice_layer_rbsp(h264_stream_t* h, bs_t* b);
void read_rbsp_slice_trailing_bits(h264_stream_t* h, bs_t* b);
void read_rbsp_trailing_bits(h264_stream_t* h, bs_t* b);
void read_slice_header(h264_stream_t* h, bs_t* b);
void read_ref_pic_list_reordering(h264_stream_t* h, bs_t* b);
void read_pred_weight_table(h264_stream_t* h, bs_t* b);
void read_dec_ref_pic_marking(h264_stream_t* h, bs_t* b);

int more_rbsp_trailing_data(h264_stream_t* h, bs_t* b);


int write_nal_unit(h264_stream_t* h, uint8_t* buf, int size);

void write_seq_parameter_set_rbsp(h264_stream_t* h, bs_t* b);
void write_scaling_list(bs_t* b, int* scalingList, int sizeOfScalingList, int useDefaultScalingMatrixFlag );
void write_vui_parameters(h264_stream_t* h, bs_t* b);
void write_hrd_parameters(h264_stream_t* h, bs_t* b);

void write_pic_parameter_set_rbsp(h264_stream_t* h, bs_t* b);

void write_sei_rbsp(h264_stream_t* h, bs_t* b);
void write_sei_message(h264_stream_t* h, bs_t* b);
void write_access_unit_delimiter_rbsp(h264_stream_t* h, bs_t* b);
void write_end_of_seq_rbsp(h264_stream_t* h, bs_t* b);
void write_end_of_stream_rbsp(h264_stream_t* h, bs_t* b);
void write_filler_data_rbsp(h264_stream_t* h, bs_t* b);

void write_slice_layer_rbsp(h264_stream_t* h, bs_t* b);
void write_rbsp_slice_trailing_bits(h264_stream_t* h, bs_t* b);
void write_rbsp_trailing_bits(h264_stream_t* h, bs_t* b);
void write_slice_header(h264_stream_t* h, bs_t* b);
void write_ref_pic_list_reordering(h264_stream_t* h, bs_t* b);
void write_pred_weight_table(h264_stream_t* h, bs_t* b);
void write_dec_ref_pic_marking(h264_stream_t* h, bs_t* b);

void debug_sps(sps_t* sps);
void debug_pps(pps_t* pps);
void debug_slice_header(slice_header_t* sh);
void debug_nal(h264_stream_t* h, nal_t* nal);

void debug_bytes(uint8_t* buf, int len);
void debug_bs(bs_t* b);

void read_sei_payload( h264_stream_t* h, bs_t* b, int payloadType, int payloadSize);
void write_sei_payload( h264_stream_t* h, bs_t* b, int payloadType, int payloadSize);


//Table 7-1 NAL unit type codes
#define NAL_UNIT_TYPE_UNSPECIFIED                    0    // Unspecified
#define NAL_UNIT_TYPE_CODED_SLICE_NON_IDR            1    // Coded slice of a non-IDR picture
#define NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_A   2    // Coded slice data partition A
#define NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_B   3    // Coded slice data partition B
#define NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_C   4    // Coded slice data partition C
#define NAL_UNIT_TYPE_CODED_SLICE_IDR                5    // Coded slice of an IDR picture
#define NAL_UNIT_TYPE_SEI                            6    // Supplemental enhancement information (SEI)
#define NAL_UNIT_TYPE_SPS                            7    // Sequence parameter set
#define NAL_UNIT_TYPE_PPS                            8    // Picture parameter set
#define NAL_UNIT_TYPE_AUD                            9    // Access unit delimiter
#define NAL_UNIT_TYPE_END_OF_SEQUENCE               10    // End of sequence
#define NAL_UNIT_TYPE_END_OF_STREAM                 11    // End of stream
#define NAL_UNIT_TYPE_FILLER                        12    // Filler data
#define NAL_UNIT_TYPE_SPS_EXT                       13    // Sequence parameter set extension
                                             // 14..18    // Reserved
#define NAL_UNIT_TYPE_CODED_SLICE_AUX               19    // Coded slice of an auxiliary coded picture without partitioning
                                             // 20..23    // Reserved
                                             // 24..31    // Unspecified

//7.4.3 Table 7-6. Name association to slice_type
#define SH_SLICE_TYPE_P        0        // P (P slice)
#define SH_SLICE_TYPE_B        1        // B (B slice)
#define SH_SLICE_TYPE_I        2        // I (I slice)
#define SH_SLICE_TYPE_SP       3        // SP (SP slice)
#define SH_SLICE_TYPE_SI       4        // SI (SI slice)
//as per footnote to Table 7-6, the *_ONLY slice types indicate that all other slices in that picture are of the same type
#define SH_SLICE_TYPE_P_ONLY    5        // P (P slice)
#define SH_SLICE_TYPE_B_ONLY    6        // B (B slice)
#define SH_SLICE_TYPE_I_ONLY    7        // I (I slice)
#define SH_SLICE_TYPE_SP_ONLY   8        // SP (SP slice)
#define SH_SLICE_TYPE_SI_ONLY   9        // SI (SI slice)

//Appendix E. Table E-1  Meaning of sample aspect ratio indicator
#define SAR_Unspecified  0           // Unspecified
#define SAR_1_1        1             //  1:1
#define SAR_12_11      2             // 12:11
#define SAR_10_11      3             // 10:11
#define SAR_16_11      4             // 16:11
#define SAR_40_33      5             // 40:33
#define SAR_24_11      6             // 24:11
#define SAR_20_11      7             // 20:11
#define SAR_32_11      8             // 32:11
#define SAR_80_33      9             // 80:33
#define SAR_18_11     10             // 18:11
#define SAR_15_11     11             // 15:11
#define SAR_64_33     12             // 64:33
#define SAR_160_99    13             // 160:99
                                     // 14..254           Reserved
#define SAR_Extended      255        // Extended_SAR

//7.4.3.1 Table 7-7 reordering_of_pic_nums_idc operations for reordering of reference picture lists
#define RPLR_IDC_ABS_DIFF_ADD       0
#define RPLR_IDC_ABS_DIFF_SUBTRACT  1
#define RPLR_IDC_LONG_TERM          2
#define RPLR_IDC_END                3

//7.4.3.3 Table 7-9 Memory management control operation (memory_management_control_operation) values
#define MMCO_END                     0
#define MMCO_SHORT_TERM_UNUSED       1
#define MMCO_LONG_TERM_UNUSED        2
#define MMCO_SHORT_TERM_TO_LONG_TERM 3
#define MMCO_LONG_TERM_MAX_INDEX     4
#define MMCO_ALL_UNUSED              5
#define MMCO_CURRENT_TO_LONG_TERM    6

//7.4.2.4 Table 7-5 Meaning of primary_pic_type
#define AUD_PRIMARY_PIC_TYPE_I       0                // I
#define AUD_PRIMARY_PIC_TYPE_IP      1                // I, P
#define AUD_PRIMARY_PIC_TYPE_IPB     2                // I, P, B
#define AUD_PRIMARY_PIC_TYPE_SI      3                // SI
#define AUD_PRIMARY_PIC_TYPE_SISP    4                // SI, SP
#define AUD_PRIMARY_PIC_TYPE_ISI     5                // I, SI
#define AUD_PRIMARY_PIC_TYPE_ISIPSP  6                // I, SI, P, SP
#define AUD_PRIMARY_PIC_TYPE_ISIPSPB 7                // I, SI, P, SP, B

#ifdef __cplusplus
}
#endif

#endif
