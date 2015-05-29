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

#include "rate_control.h"

#include <math.h>

static const int SMOOTHING_WINDOW = 40;

/**
 * \brief Allocate bits for the current GOP.
 * \param state the main encoder state
 *
 * If GOPs are not used, allocates bits for a single picture.
 *
 * Sets the cur_gop_target_bits of the encoder state.
 */
static void gop_allocate_bits(encoder_state_t * const state)
{
  const encoder_control_t * const encoder = state->encoder_control;

  const double avg_bits_per_picture =
    encoder->cfg->target_bitrate / encoder->cfg->framerate;

  // At this point, total_bits_coded of the current state contains the
  // number of bits written encoder->owf frames before the current frame.
  int bits_coded = state->global->total_bits_coded;
  int pictures_coded = MAX(0, state->global->frame - encoder->owf);

  int gop_offset = (state->global->gop_offset - encoder->owf) % MAX(1, encoder->cfg->gop_len);
  // Only take fully coded GOPs into account.
  if (encoder->cfg->gop_len > 0 && gop_offset != encoder->cfg->gop_len - 1) {
    // Subtract number of bits in the partially coded GOP.
    bits_coded -= state->global->cur_gop_bits_coded;
    // Subtract number of pictures in the partially coded GOP.
    pictures_coded -= gop_offset + 1;
  }

  double gop_target_bits =
    (avg_bits_per_picture * (pictures_coded + SMOOTHING_WINDOW) - bits_coded)
    * MAX(1, encoder->cfg->gop_len) / SMOOTHING_WINDOW;
  state->global->cur_gop_target_bits = MAX(200, gop_target_bits);
}

/**
 * Allocate bits for the current picture.
 * \param state the main encoder state
 * \return target number of bits
 */
double pic_allocate_bits(const encoder_state_t * const state) {
  const encoder_control_t * const encoder = state->encoder_control;

  if (encoder->cfg->gop_len <= 0) {
    return state->global->cur_gop_target_bits;
  }

  const double avg_bits_per_picture =
    encoder->cfg->target_bitrate / encoder->cfg->framerate;
  const int pixels_per_picture = encoder->in.width * encoder->in.height;
  const double avg_bits_per_pixel = avg_bits_per_picture / pixels_per_picture;

  int layer_weights[4];
#define SET_ARRAY(array, x0, x1, x2, x3) \
  do { array[0] = x0; array[1] = x1; array[2] = x2; array[3] = x3; } while(0)

  if (avg_bits_per_pixel <= 0.05) {
    SET_ARRAY(layer_weights, 30, 8, 4, 1);
  } else if (avg_bits_per_pixel <= 0.1) {
    SET_ARRAY(layer_weights, 25, 7, 4, 1);
  } else if (avg_bits_per_pixel <= 0.2) {
    SET_ARRAY(layer_weights, 20, 6, 4, 1);
  } else {
    SET_ARRAY(layer_weights, 15, 5, 4, 1);
  }
#undef SET_ARRAY

  const int pic_weight = layer_weights[encoder->cfg->gop[state->global->gop_offset].layer - 1];
  int sum_weights = 0;
  for (int i = 0; i < encoder->cfg->gop_len; ++i) {
    sum_weights += layer_weights[encoder->cfg->gop[i].layer - 1];
  }

  return state->global->cur_gop_target_bits * pic_weight / sum_weights;
}

/**
 * \brief Select a lambda value for encoding the next picture
 * \param state the main encoder state
 * \return lambda for the next picture
 */
double select_picture_lambda(encoder_state_t * const state)
{
  const encoder_control_t * const encoder = state->encoder_control;

  if (encoder->cfg->target_bitrate <= 0) {
    // Rate control disabled.
    return exp((encoder->cfg->qp - 13.7223 - 0.5) / 4.2005);
  }

  if (encoder->cfg->gop_len == 0 || state->global->gop_offset == 0) {
    // a new GOP begins at this frame
    gop_allocate_bits(state);
  } else {
    state->global->cur_gop_target_bits =
      state->previous_encoder_state->global->cur_gop_target_bits;
  }

  // TODO: take the picture headers into account
  const int pixels_per_picture = encoder->in.width * encoder->in.height;
  const double target_bits_current_picture = pic_allocate_bits(state);
  const double target_bits_per_pixel =
    target_bits_current_picture / pixels_per_picture;
  const double lambda = 3.2003 * pow(target_bits_per_pixel, -1.367);
  return CLIP(0.1, 10000, lambda);
}

int8_t lambda_to_QP(const double lambda)
{
  int8_t qp = 4.2005 * log(lambda) + 13.7223 + 0.5;
  return CLIP(0, 51, qp);
}
