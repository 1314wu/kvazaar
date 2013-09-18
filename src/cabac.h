/**
 *  HEVC Encoder
 *  - Marko Viitanen ( fador at iki.fi ), Tampere University of Technology, Department of Pervasive Computing.
 */

/*! \file cabac.h
    \brief CABAC
    \author Marko Viitanen
    \date 2012-06
    
    Content-adaptive binary arithmetic coder
*/
#ifndef __CABAC_H
#define __CABAC_H

#include "global.h"

#include "bitstream.h"


extern const uint8_t g_auc_next_state_mps[ 128 ];
extern const uint8_t g_auc_next_state_lps[ 128 ];
extern uint8_t g_next_state[128][2];

extern const uint8_t g_auc_lpst_table[64][4];
extern const uint8_t g_aucRenormTable[32];

typedef struct
{
  uint8_t  uc_state;
  uint32_t bins_coded;
} cabac_ctx;

#define CTX_STATE(ctx) (ctx->uc_state >> 1)
#define CTX_MPS(ctx) (ctx->uc_state & 1)

void ctx_init(cabac_ctx* ctx,uint32_t qp, uint32_t init_value );
void ctx_build_next_state_table();
void ctx_update(cabac_ctx* ctx, int val );
//void ctx_update_LPS(cabac_ctx* ctx);
//void ctx_update_MPS(cabac_ctx* ctx);
#define ctx_update_LPS(ctx) { (ctx)->uc_state = g_auc_next_state_lps[ (ctx)->uc_state ]; }
#define ctx_update_MPS(ctx) { (ctx)->uc_state = g_auc_next_state_mps[ (ctx)->uc_state ]; }


typedef struct
{
  cabac_ctx  *ctx;
  uint32_t   ui_low;
  uint32_t   ui_range;
  uint32_t   buffered_ryte;
  int32_t    num_buffered_bytes;
  int32_t    bits_left;
  uint32_t   ui_bins_coded;
  int32_t    bin_count_increment;
  uint64_t   frac_bits;
  bitstream* stream;
} cabac_data;

extern cabac_data cabac;

void cabac_start(cabac_data* data);
void cabac_init(cabac_data* data);
void cabac_encode_bin(cabac_data* data, uint32_t bin_value );
void cabac_encode_bin_ep(cabac_data* data, uint32_t bin_value );
void cabac_encode_bins_ep(cabac_data* data, uint32_t bin_values, int num_bins );
void cabac_encode_bin_trm(cabac_data* data, uint8_t bin_value );
void cabac_write(cabac_data* data);
void cabac_finish(cabac_data* data);
void cabac_flush(cabac_data* data);
void cabac_write_coeff_remain(cabac_data* cabac,uint32_t symbol, uint32_t r_param );
void cabac_write_ep_ex_golomb(cabac_data* data, uint32_t symbol, uint32_t count );
void cabac_write_unary_max_symbol(cabac_data* data,cabac_ctx* ctx, uint32_t symbol,int32_t offset, uint32_t max_symbol);

#ifdef VERBOSE
#define CABAC_BIN(data, value, name) {  uint32_t prev_state = (data)->ctx->uc_state;\
                                        cabac_encodeBin(data, value); \
                                        printf("%s = %d prev_state=%d state=%d\n",name,value,prev_state, (data)->ctx->uc_state);}

#define CABAC_BINS_EP(data, value,bins, name) {  uint32_t prev_state = (data)->ctx->uc_state;\
  cabac_encodeBinsEP(data, value,bins); \
  printf("%s = %d prev_state=%d state=%d\n",name,value,prev_state, (data)->ctx->uc_state);}

#define CABAC_BIN_EP(data, value, name) {  uint32_t prev_state = (data)->ctx->uc_state;\
  cabac_encodeBinEP(data, value); \
  printf("%s = %d prev_state=%d state=%d\n",name,value,prev_state, (data)->ctx->uc_state);}
#else
#define CABAC_BIN(data, value, name) cabac_encode_bin(data, value);
#define CABAC_BINS_EP(data, value,bins, name) cabac_encode_bins_ep(data, value,bins);
#define CABAC_BIN_EP(data, value, name) cabac_encode_bin_ep(data, value);
#endif

#endif
