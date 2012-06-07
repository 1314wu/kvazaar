/**
 *  HEVC Encoder
 *  - Marko Viitanen ( fador at iki.fi ), Tampere University of Technology, Department of Computer Systems.
 */

/*! \file encoder.c
    \brief Encoding related functions
    \author Marko Viitanen
    \date 2012-06
    
    Encoder main level
*/
/* Suppress some windows warnings */
#ifdef WIN32
  #define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "config.h"
#include "encoder.h"
#include "cabac.h"
#include "picture.h"
#include "nal.h"

void init_encoder_control(encoder_control* control,bitstream* output)
{
  control->stream = output;
}

void init_encoder_input(encoder_input* input,FILE* inputfile, uint32_t width, uint32_t height)
{
  input->file = inputfile;
  input->width = width;
  input->height = height;

  input->height_in_LCU = height / LCU_WIDTH;
  input->width_in_LCU =  width / LCU_WIDTH;
  if(input->height_in_LCU * LCU_WIDTH < height)
    input->height_in_LCU++;
  if(input->width_in_LCU * LCU_WIDTH < width)
    input->width_in_LCU++;

  input->cur_pic.width = width;
  input->cur_pic.height = height;
  input->cur_pic.referenced = 0;
  /* Allocate buffers */
  input->cur_pic.yData = (uint8_t *)malloc(width*height);
  input->cur_pic.uData = (uint8_t *)malloc((width*height)>>2);
  input->cur_pic.vData = (uint8_t *)malloc((width*height)>>2);
}


void encode_one_frame(encoder_control* encoder)
{
  //output parameters before first frame
  if(encoder->frame == 0)
  {
    encode_seq_parameter_set(encoder);
    bitstream_align(encoder->stream);
    bitstream_flush(encoder->stream);
    nal_write(encoder->output, encoder->stream->buffer, encoder->stream->buffer_pos, 1, NAL_SEQ_PARAMETER_SET, 0);
    bitstream_clear_buffer(encoder->stream);

    encode_pic_parameter_set(encoder);
    bitstream_align(encoder->stream);
    bitstream_flush(encoder->stream);
    nal_write(encoder->output, encoder->stream->buffer, encoder->stream->buffer_pos, 1, NAL_PIC_PARAMETER_SET, 0);
    bitstream_clear_buffer(encoder->stream);


    cabac_start(&cabac);
    encode_slice_header(encoder);
    encode_slice_data(encoder);
    cabac_flush(&cabac);
    bitstream_align(encoder->stream);
    bitstream_flush(encoder->stream);
    nal_write(encoder->output, encoder->stream->buffer, encoder->stream->buffer_pos, 0, NAL_IDR_SLICE, 0);
    bitstream_clear_buffer(encoder->stream);
  }
  else
  {
    /*
    encode_slice_header(encoder);
    encode_slice_data(encoder);
    cabac_flush(&cabac);
    bitstream_align(encoder->stream);
    bitstream_flush(encoder->stream);
    nal_write(encoder->output, encoder->stream->buffer, encoder->stream->buffer_pos, 0, NAL_IDR_SLICE, 0);
    bitstream_clear_buffer(encoder->stream);
    */
  }
}

void encode_pic_parameter_set(encoder_control* encoder)
{
#ifdef _DEBUG
  printf("=========== Picture Parameter Set ID: 0 ===========\n");
#endif
  WRITE_UE(encoder->stream, 0, "pic_parameter_set_id");
  WRITE_UE(encoder->stream, 0, "seq_parameter_set_id");
  WRITE_U(encoder->stream, 0, 1, "sign_data_hiding_flag");
  WRITE_U(encoder->stream, 0, 1, "cabac_init_present_flag");
  WRITE_U(encoder->stream, 0, 3, "num_ref_idx_l0_default_active_minus1");
  WRITE_U(encoder->stream, 0, 3, "num_ref_idx_l1_default_active_minus1");
  /*
  //Should be this
  WRITE_UE(encoder->stream, 0, "num_ref_idx_l0_default_active_minus1");
  WRITE_UE(encoder->stream, 0, "num_ref_idx_l1_default_active_minus1");
  */
  WRITE_SE(encoder->stream, encoder->QP-26, "pic_init_qp_minus26");
  WRITE_U(encoder->stream, 0, 1, "constrained_intra_pred_flag");
  WRITE_U(encoder->stream, 0, 1, "enable_temporal_mvp_flag");
  WRITE_U(encoder->stream, 0, 2, "slice_granularity");
  WRITE_UE(encoder->stream, 0, "max_cu_qp_delta_depth");
  WRITE_SE(encoder->stream, 0, "cb_qp_offset");
  WRITE_SE(encoder->stream, 0, "cr_qp_offset");
  WRITE_U(encoder->stream, 0, 1, "weighted_pred_flag");
  WRITE_U(encoder->stream, 0, 2, "weighted_bipred_idc");  
  WRITE_U(encoder->stream, 1, 1, "output_flag_present_flag");  
  WRITE_U(encoder->stream, 0, 1, "deblocking_filter_control_present_flag");
  WRITE_UE(encoder->stream, 0, "log2_parallel_merge_level_minus2");
  WRITE_U(encoder->stream, 0, 1, "pps_extension_flag");  
}

void encode_seq_parameter_set(encoder_control* encoder)
{
#ifdef _DEBUG
  printf("=========== Sequence Parameter Set ID: 0 ===========\n");
#endif
  WRITE_U(encoder->stream, 0, 8, "profile_idc");
  WRITE_U(encoder->stream, 0, 8, "reserved_zero_8bits");
  WRITE_U(encoder->stream, 0, 8, "level_idc");
  WRITE_UE(encoder->stream, 0, "seq_parameter_set_id");
  WRITE_UE(encoder->stream, 0, "chroma_format_idc"); /* 0 = 4:0:0, 1 = 4:2:0, 2 = 4:2:2, 3 = 4:4:4 */
  WRITE_U(encoder->stream, 0, 3, "max_temporal_layers_minus1");
  WRITE_UE(encoder->stream, encoder->in.width, "pic_width_in_luma_samples");
  WRITE_UE(encoder->stream, encoder->in.height, "pic_height_in_luma_samples");
  WRITE_U(encoder->stream, 0, 1, "pic_cropping_flag");
  WRITE_UE(encoder->stream, 0, "bit_depth_luma_minus8");
  WRITE_UE(encoder->stream, 0, "bit_depth_chroma_minus8");
  WRITE_U(encoder->stream, 1, 1, "pcm_enabled_flag");
  WRITE_U(encoder->stream, 7, 4, "pcm_bit_depth_luma_minus1");
  WRITE_U(encoder->stream, 7, 4, "pcm_bit_depth_chroma_minus1");
  WRITE_U(encoder->stream, 0, 1, "qpprime_y_zero_transquant_bypass_flag");
  WRITE_UE(encoder->stream, 4, "log2_max_pic_order_cnt_lsb_minus4");
  WRITE_UE(encoder->stream, 0, "max_dec_pic_buffering");
  WRITE_UE(encoder->stream, 0, "num_reorder_pics");
  WRITE_UE(encoder->stream, 0, "max_latency_increase");
  WRITE_U(encoder->stream, 0, 1, "restricted_ref_pic_lists_flag");
  WRITE_UE(encoder->stream, 1, "log2_min_coding_block_size_minus3");
  WRITE_UE(encoder->stream, 2, "log2_diff_max_min_coding_block_size");
  WRITE_UE(encoder->stream, 0, "log2_min_transform_block_size_minus2");
  WRITE_UE(encoder->stream, 3, "log2_diff_max_min_transform_block_size");

  //If log2MinCUSize == 3
  //WRITE_U(encoder->stream, 0, 1, "DisInter4x4");

  //IF PCM
  {
    WRITE_UE(encoder->stream, 0, "log2_min_pcm_coding_block_size_minus3");
    WRITE_UE(encoder->stream, 2, "log2_diff_max_min_pcm_coding_block_size");
  }
  

  WRITE_UE(encoder->stream, 2, "max_transform_hierarchy_depth_inter");
  WRITE_UE(encoder->stream, 2, "max_transform_hierarchy_depth_intra");

  WRITE_U(encoder->stream, 0, 1, "scaling_list_enable_flag");
  WRITE_U(encoder->stream, 0, 1, "chroma_pred_from_luma_enabled_flag");
  WRITE_U(encoder->stream, 0, 1, "transform_skip_enabled_flag");
  WRITE_U(encoder->stream, 0, 1, "deblocking_filter_in_aps_enabled_flag");
  WRITE_U(encoder->stream, 0, 1, "seq_loop_filter_across_slices_enabled_flag");
  WRITE_U(encoder->stream, 0, 1, "asymmetric_motion_partitions_enabled_flag");
  WRITE_U(encoder->stream, 0, 1, "nsrqt_enabled_flag");
  WRITE_U(encoder->stream, 0, 1, "sample_adaptive_offset_enabled_flag");
	WRITE_U(encoder->stream, 0, 1, "adaptive_loop_filter_enabled_flag");
  //IF PCM
    WRITE_U(encoder->stream, 1, 1, "pcm_loop_filter_disable_flag");
  //endif
  WRITE_U(encoder->stream, 0, 1, "temporal_id_nesting_flag");	
  WRITE_UE(encoder->stream, 0, "num_short_term_ref_pic_sets");	
  WRITE_U(encoder->stream, 0, 1, "long_term_ref_pics_present_flag");	
  WRITE_U(encoder->stream, 0, 2, "tiles_or_entropy_coding_sync_idc");	
	WRITE_U(encoder->stream, 0, 1, "sps_extension_flag");  
}

void encode_slice_header(encoder_control* encoder)
{
#ifdef _DEBUG
  printf("=========== Slice ===========\n");
#endif

  WRITE_U(encoder->stream, 1, 1, "first_slice_in_pic_flag");
  WRITE_UE(encoder->stream, SLICE_I, "slice_type");

  WRITE_U(encoder->stream, 0, 1, "entropy_slice_flag");
  // if !entropy_slice_flag
    WRITE_UE(encoder->stream, 0, "pic_parameter_set_id");
    //if output_flag_present_flag
      WRITE_U(encoder->stream, 1, 1, "pic_output_flag");
    //end if
    //if( IdrPicFlag ) <- nal_unit_type == 5
      WRITE_UE(encoder->stream, encoder->frame&1, "idr_pic_id");
      WRITE_U(encoder->stream, 0, 1, "no_output_of_prior_pics_flag");
    //else
      /*


      */
    //end if
  //end if
  /*
   Skip unpresent flags */
  // if !entropy_slice_flag
    WRITE_UE(encoder->stream, 0, "slice_qp_delta");
    WRITE_UE(encoder->stream, 0, "5_minus_max_num_merge_cand");
}
  
cabac_ctx SplitFlagSCModel;
cabac_ctx PCMFlagSCModel;
cabac_ctx PartSizeSCModel;

void encode_slice_data(encoder_control* encoder)
{
  uint16_t xCtb,yCtb;
  cxt_init(&SplitFlagSCModel, encoder->QP, 107);
  cxt_init(&PCMFlagSCModel, encoder->QP, 0);
  cxt_init(&PartSizeSCModel, encoder->QP, 0);
  //SplitFlagSCModel.ucState = 15;
  PCMFlagSCModel.ucState = 0;
  PartSizeSCModel.ucState = 0;
  
  //cxt_init(&cabac.ctx, 26, 87);
  
  for(yCtb = 0; yCtb < encoder->in.height_in_LCU; yCtb++)
  {
    for(xCtb = 0; xCtb < encoder->in.width_in_LCU; xCtb++)
    {
      uint8_t depth = 0;
      encode_coding_tree(encoder, xCtb,yCtb, depth);
    }
  }
}

void encode_coding_tree(encoder_control* encoder,uint16_t xCtb,uint16_t yCtb, uint8_t depth)
{    
  int i;
  uint8_t split_flag = (depth!=1)?1:0;
  cabac.ctx = &SplitFlagSCModel;
  CABAC_BIN(&cabac, split_flag, "SplitFlag");
  if(split_flag)
  {
    encode_coding_tree(encoder,xCtb,yCtb,depth+1);
    encode_coding_tree(encoder,xCtb+1,yCtb,depth+1);
    encode_coding_tree(encoder,xCtb,yCtb+1,depth+1);
    encode_coding_tree(encoder,xCtb+1,yCtb+1,depth+1);
    return;
  }
  /* coding_unit( x0, y0, log2CbSize ) */
   /* prediction_unit 2Nx2N*/
    //if !intra PREDMODE
   //PartSize
    //cabac.ctx = &PartSizeSCModel;
    //CABAC_BIN(&cabac, 1, "PartSize");
   //If MODE_INTRA
    cabac.ctx = &PCMFlagSCModel;
    cabac_encodeBinTrm(&cabac, 1);
    printf("\tIPCMFlag = 1\n");
    cabac_finish(&cabac);
    WRITE_U(cabac.stream, 1, 1, "stop_bit");
    WRITE_U(cabac.stream, 0, 1, "stop_bit");
    //WRITE_U(cabac.stream, 0, 3, "num_subsequent_pcm");
    bitstream_align(cabac.stream);
     /* PCM sample */
      for(i = 0; i < 16*16; i++)
      {
        bitstream_put(cabac.stream, 125, 8);
      }
   
    /* end PCM sample
   //endif
   
   /* end prediction unit */  
   //cabac_encodeBin(&cabac, 0); //prev_intra_luma_pred_flag

   //cabac_encodeBin(&cabac, 1); //rem_intra_luma_pred_mode

  /* end coding_unit */
  
}

