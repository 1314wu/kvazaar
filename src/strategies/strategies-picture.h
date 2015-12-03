#ifndef STRATEGIES_PICTURE_H_
#define STRATEGIES_PICTURE_H_
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

#include "../image.h"

typedef kvz_pixel (*pred_buffer)[32 * 32];

typedef unsigned(reg_sad_func)(const kvz_pixel *const data1, const kvz_pixel *const data2,
  const int width, const int height,
  const unsigned stride1, const unsigned stride2);
typedef unsigned (cost_pixel_nxn_func)(const kvz_pixel *block1, const kvz_pixel *block2);
typedef void (cost_pixel_nxn_multi_func)(const pred_buffer preds, const kvz_pixel *orig, unsigned num_modes, unsigned *costs_out);

typedef void pixels_blit_func(const kvz_pixel* orig, kvz_pixel *dst,
                         unsigned width, unsigned height,
                         unsigned orig_stride, unsigned dst_stride);


// Declare function pointers.
extern reg_sad_func * kvz_reg_sad;

extern cost_pixel_nxn_func * kvz_sad_4x4;
extern cost_pixel_nxn_func * kvz_sad_8x8;
extern cost_pixel_nxn_func * kvz_sad_16x16;
extern cost_pixel_nxn_func * kvz_sad_32x32;
extern cost_pixel_nxn_func * kvz_sad_64x64;

extern cost_pixel_nxn_func * kvz_satd_4x4;
extern cost_pixel_nxn_func * kvz_satd_8x8;
extern cost_pixel_nxn_func * kvz_satd_16x16;
extern cost_pixel_nxn_func * kvz_satd_32x32;
extern cost_pixel_nxn_func * kvz_satd_64x64;

extern cost_pixel_nxn_multi_func * kvz_sad_4x4_dual;
extern cost_pixel_nxn_multi_func * kvz_sad_8x8_dual;
extern cost_pixel_nxn_multi_func * kvz_sad_16x16_dual;
extern cost_pixel_nxn_multi_func * kvz_sad_32x32_dual;
extern cost_pixel_nxn_multi_func * kvz_sad_64x64_dual;

extern cost_pixel_nxn_multi_func * kvz_satd_4x4_dual;
extern cost_pixel_nxn_multi_func * kvz_satd_8x8_dual;
extern cost_pixel_nxn_multi_func * kvz_satd_16x16_dual;
extern cost_pixel_nxn_multi_func * kvz_satd_32x32_dual;
extern cost_pixel_nxn_multi_func * kvz_satd_64x64_dual;

extern pixels_blit_func * kvz_pixels_blit;


int kvz_strategy_register_picture(void* opaque, uint8_t bitdepth);
cost_pixel_nxn_func * kvz_pixels_get_satd_func(unsigned n);
cost_pixel_nxn_func * kvz_pixels_get_sad_func(unsigned n);
cost_pixel_nxn_multi_func * kvz_pixels_get_satd_dual_func(unsigned n);
cost_pixel_nxn_multi_func * kvz_pixels_get_sad_dual_func(unsigned n);

#define STRATEGIES_PICTURE_EXPORTS \
  {"reg_sad", (void**) &kvz_reg_sad}, \
  {"sad_4x4", (void**) &kvz_sad_4x4}, \
  {"sad_8x8", (void**) &kvz_sad_8x8}, \
  {"sad_16x16", (void**) &kvz_sad_16x16}, \
  {"sad_32x32", (void**) &kvz_sad_32x32}, \
  {"sad_64x64", (void**) &kvz_sad_64x64}, \
  {"satd_4x4", (void**) &kvz_satd_4x4}, \
  {"satd_8x8", (void**) &kvz_satd_8x8}, \
  {"satd_16x16", (void**) &kvz_satd_16x16}, \
  {"satd_32x32", (void**) &kvz_satd_32x32}, \
  {"satd_64x64", (void**) &kvz_satd_64x64}, \
  {"sad_4x4_dual", (void**) &kvz_sad_4x4_dual}, \
  {"sad_8x8_dual", (void**) &kvz_sad_8x8_dual}, \
  {"sad_16x16_dual", (void**) &kvz_sad_16x16_dual}, \
  {"sad_32x32_dual", (void**) &kvz_sad_32x32_dual}, \
  {"sad_64x64_dual", (void**) &kvz_sad_64x64_dual}, \
  {"satd_4x4_dual", (void**) &kvz_satd_4x4_dual}, \
  {"satd_8x8_dual", (void**) &kvz_satd_8x8_dual}, \
  {"satd_16x16_dual", (void**) &kvz_satd_16x16_dual}, \
  {"satd_32x32_dual", (void**) &kvz_satd_32x32_dual}, \
  {"satd_64x64_dual", (void**) &kvz_satd_64x64_dual}, \
  {"pixels_blit", (void**) &kvz_pixels_blit}, \



#endif //STRATEGIES_PICTURE_H_
