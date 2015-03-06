#ifndef CABAC_H_
#define CABAC_H_
/*****************************************************************************
 * This file is part of Kvazaar HEVC encoder.
 *
 * Copyright (C) 2013-2015 Tampere University of Technology and others (see
 * COPYING file).
 *
 * Kvazaar is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * Kvazaar is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Kvazaar.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

/*
 * \file
 * \brief The Content Adaptive Binary Arithmetic Coder (CABAC).
 */

#include "global.h"

#include "bitstream.h"


// Types
typedef struct
{
  uint8_t  uc_state;
} cabac_ctx;

typedef struct
{
  cabac_ctx *cur_ctx;
  uint32_t   low;
  uint32_t   range;
  uint32_t   buffered_byte;
  int32_t    num_buffered_bytes;
  int32_t    bits_left;
  int8_t     only_count;
  bitstream *stream;

  // CONTEXTS
  struct {
    cabac_ctx sao_merge_flag_model;
    cabac_ctx sao_type_idx_model;
    cabac_ctx split_flag_model[3]; //!< \brief split flag context models
    cabac_ctx intra_mode_model;    //!< \brief intra mode context models
    cabac_ctx chroma_pred_model[2];
    cabac_ctx inter_dir[5];
    cabac_ctx trans_subdiv_model[3]; //!< \brief intra mode context models
    cabac_ctx qt_cbf_model_luma[4];
    cabac_ctx qt_cbf_model_chroma[4];
    cabac_ctx part_size_model[4];
    cabac_ctx cu_sig_coeff_group_model[4];
    cabac_ctx cu_sig_model_luma[27];
    cabac_ctx cu_sig_model_chroma[15];
    cabac_ctx cu_ctx_last_y_luma[15];
    cabac_ctx cu_ctx_last_y_chroma[15];
    cabac_ctx cu_ctx_last_x_luma[15];
    cabac_ctx cu_ctx_last_x_chroma[15];
    cabac_ctx cu_one_model_luma[16];
    cabac_ctx cu_one_model_chroma[8];
    cabac_ctx cu_abs_model_luma[4];
    cabac_ctx cu_abs_model_chroma[2];
    cabac_ctx cu_pred_mode_model;
    cabac_ctx cu_skip_flag_model[3];
    cabac_ctx cu_merge_idx_ext_model;
    cabac_ctx cu_merge_flag_ext_model;
    cabac_ctx cu_mvd_model[2];
    cabac_ctx cu_ref_pic_model[2];
    cabac_ctx mvp_idx_model[2];
    cabac_ctx cu_qt_root_cbf_model;
    cabac_ctx transform_skip_model_luma;
    cabac_ctx transform_skip_model_chroma;
  } ctx;
} cabac_data;


// Globals
extern const uint8_t g_auc_next_state_mps[128];
extern const uint8_t g_auc_next_state_lps[128];
extern const uint8_t g_auc_lpst_table[64][4];
extern const uint8_t g_auc_renorm_table[32];


// Functions
void cabac_start(cabac_data *data);
void cabac_encode_bin(cabac_data *data, uint32_t bin_value);
void cabac_encode_bin_ep(cabac_data *data, uint32_t bin_value);
void cabac_encode_bins_ep(cabac_data *data, uint32_t bin_values, int num_bins);
void cabac_encode_bin_trm(cabac_data *data, uint8_t bin_value);
void cabac_write(cabac_data *data);
void cabac_finish(cabac_data *data);
void cabac_flush(cabac_data *data);
void cabac_write_coeff_remain(cabac_data *cabac, uint32_t symbol,
                              uint32_t r_param);
void cabac_write_ep_ex_golomb(cabac_data *data, uint32_t symbol,
                              uint32_t count);
void cabac_write_unary_max_symbol(cabac_data *data, cabac_ctx *ctx,
                                  uint32_t symbol, int32_t offset,
                                  uint32_t max_symbol);
void cabac_write_unary_max_symbol_ep(cabac_data *data, unsigned int symbol, unsigned int max_symbol);


// Macros
#define CTX_STATE(ctx) (ctx->uc_state >> 1)
#define CTX_MPS(ctx) (ctx->uc_state & 1)
#define CTX_UPDATE_LPS(ctx) { (ctx)->uc_state = g_auc_next_state_lps[ (ctx)->uc_state ]; }
#define CTX_UPDATE_MPS(ctx) { (ctx)->uc_state = g_auc_next_state_mps[ (ctx)->uc_state ]; }

#ifdef VERBOSE
  #define CABAC_BIN(data, value, name) { \
    uint32_t prev_state = (data)->ctx->uc_state; \
    cabac_encode_bin(data, value); \
    printf("%s = %u, state = %u -> %u\n", \
           name, (uint32_t)value, (uint32_t)prev_state, (data)->ctx->uc_state); }

  #define CABAC_BINS_EP(data, value, bins, name) { \
    uint32_t prev_state = (data)->ctx->uc_state; \
    cabac_encode_bins_ep(data, value, bins); \
    printf("%s = %u(%u bins), state = %u -> %u\n", \
           name, (uint32_t)value, (uint32_t)bins, prev_state, (data)->ctx->uc_state); }

  #define CABAC_BIN_EP(data, value, name) { \
    uint32_t prev_state = (data)->ctx->uc_state; \
    cabac_encode_bin_ep(data, value); \
    printf("%s = %u, state = %u -> %u\n", \
           name, (uint32_t)value, (uint32_t)prev_state, (data)->ctx->uc_state); }
#else
  #define CABAC_BIN(data, value, name) \
    cabac_encode_bin(data, value);
  #define CABAC_BINS_EP(data, value, bins, name) \
    cabac_encode_bins_ep(data, value, bins);
  #define CABAC_BIN_EP(data, value, name) \
    cabac_encode_bin_ep(data, value);
#endif

#endif
