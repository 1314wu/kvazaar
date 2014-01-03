#ifndef INTRA_H_
#define INTRA_H_
/**
 * \file
 * \brief Handling Coding Units (CU's) for intra frames.
 * 
 * \author Marko Viitanen ( fador@iki.fi ), 
 *         Tampere University of Technology,
 *         Department of Pervasive Computing.
 * \author Ari Koivula ( ari@koivu.la ), 
 *         Tampere University of Technology,
 *         Department of Pervasive Computing.
 */

#include "global.h"

#include "picture.h"


void intra_set_block_mode(picture* pic,uint32_t x_ctb, uint32_t y_ctb, uint8_t depth, uint8_t mode, uint8_t part_mode);
int8_t intra_get_block_mode(picture* pic, uint32_t x_ctb, uint32_t y_ctb, uint8_t depth);

int8_t intra_get_dir_luma_predictor(picture* pic,uint32_t x_ctb, uint32_t y_ctb, uint8_t depth, int8_t* preds);
void intra_dc_pred_filtering(pixel* src, int32_t src_stride, pixel* dst, int32_t dst_stride, int32_t width, int32_t height );

void intra_build_reference_border(picture* pic, int32_t x_ctb, int32_t y_ctb, int16_t out_width, pixel* dst, int32_t dst_stride, int8_t chroma);
void intra_filter(pixel* ref, int32_t stride, int32_t width, int8_t mode);

/* Predictions */
int16_t intra_prediction(pixel* orig, int32_t orig_stride, pixel* rec, int32_t rec_stride,  uint32_t x_pos, uint32_t ypos, uint32_t width, pixel* dst, int32_t dst_stride, uint32_t *sad);

int16_t intra_get_dc_pred(pixel* pic, uint16_t pic_width, uint32_t x_pos, uint32_t y_pos, uint8_t width);
void intra_get_planar_pred(pixel* src,int32_t srcstride, uint32_t xpos, uint32_t ypos,uint32_t width, pixel* dst,int32_t dststride);
void intra_get_angular_pred(pixel* src, int32_t src_stride, pixel* p_dst, int32_t dst_stride, int32_t width, int32_t height, int32_t dir_mode, int8_t left_avail,int8_t top_avail, int8_t filter);

void intra_recon(pixel* rec, uint32_t rec_stride, uint32_t x_pos, uint32_t y_pos, uint32_t width, pixel* dst, int32_t dst_stride, int8_t mode, int8_t chroma);


#endif
