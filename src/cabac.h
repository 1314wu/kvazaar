/**
 *  HEVC Encoder
 *  - Marko Viitanen ( fador at iki.fi ), Tampere University of Technology, Department of Computer Systems.
 */

/*! \file cabac.h
    \brief CABAC
    \author Marko Viitanen
    \date 2012-06
    
    Content-adaptive binary arithmetic coder
*/
#ifndef __CABAC_H
#define __CABAC_H

extern const uint8_t g_aucNextStateMPS[ 128 ];
extern const uint8_t g_aucNextStateLPS[ 128 ];
extern const uint32_t g_entropyBits[128];
extern uint8_t g_nextState[128][2];

extern const uint8_t g_aucLPSTable[64][4];
extern const uint8_t g_aucRenormTable[32];

typedef struct
{
  uint8_t  ucState;
  uint32_t binsCoded;
} cabac_ctx;

#define CTX_STATE(ctx) (ctx->ucState>>1)
#define CTX_MPS(ctx) (ctx->ucState&1)

void ctx_init(cabac_ctx* ctx,uint32_t qp, uint32_t initValue );
void ctx_buildNextStateTable();
void ctx_update(cabac_ctx* ctx, int val );
//void ctx_update_LPS(cabac_ctx* ctx);
//void ctx_update_MPS(cabac_ctx* ctx);
#define ctx_update_LPS(ctx) { (ctx)->ucState = g_aucNextStateLPS[ (ctx)->ucState ]; }
#define ctx_update_MPS(ctx) { (ctx)->ucState = g_aucNextStateMPS[ (ctx)->ucState ]; }


typedef struct
{
  cabac_ctx  *ctx;
  uint32_t   uiLow;
  uint32_t   uiRange;
  uint32_t   bufferedByte;
  int32_t    numBufferedBytes;
  int32_t    bitsLeft;
  uint32_t   uiBinsCoded;
  int32_t    binCountIncrement;
  uint64_t   fracBits;
  bitstream* stream;
} cabac_data;

extern cabac_data cabac;

void cabac_start(cabac_data* data);
void cabac_init(cabac_data* data);
void cabac_encodeBin(cabac_data* data, uint32_t binValue );
void cabac_encodeFlush(cabac_data* data, uint8_t end );
void cabac_encodeBinEP(cabac_data* data, uint32_t binValue );
void cabac_encodeBinsEP(cabac_data* data, uint32_t binValues, int numBins );
void cabac_encodeBinTrm(cabac_data* data, uint8_t binValue );
void cabac_write(cabac_data* data);
void cabac_finish(cabac_data* data);
void cabac_flush(cabac_data* data);



#ifdef _DEBUG
#define CABAC_BIN(data, value, name) {  uint32_t prev_state = (data)->ctx->ucState;\
                                        cabac_encodeBin(data, value); \
                                        printf("%s = %d prev_state=%d state=%d\n",name,value,prev_state, (data)->ctx->ucState);}

#define CABAC_BINS_EP(data, value,bins, name) {  uint32_t prev_state = (data)->ctx->ucState;\
  cabac_encodeBinsEP(data, value,bins); \
  printf("%s = %d prev_state=%d state=%d\n",name,value,prev_state, (data)->ctx->ucState);}

#define CABAC_BIN_EP(data, value, name) {  uint32_t prev_state = (data)->ctx->ucState;\
  cabac_encodeBinEP(data, value); \
  printf("%s = %d prev_state=%d state=%d\n",name,value,prev_state, (data)->ctx->ucState);}
#else
#define CABAC_BIN(data, value, name) cabac_encodeBin(data, value);
#define CABAC_BINS_EP(data, value,bins, name) cabac_encodeBinsEP(data, value,bins);
#define CABAC_BIN_EP(data, value,bins, name) cabac_encodeBinEP(data, value);
#endif

#endif
