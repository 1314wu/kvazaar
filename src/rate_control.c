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

#include "encoder.h"
#include "kvazaar.h"


static const int SMOOTHING_WINDOW = 40;
static const double MIN_LAMBDA    = 0.1;
static const double MAX_LAMBDA    = 10000;

/**
 * \brief Clip lambda value to a valid range.
 */
static double clip_lambda(double lambda) {
  if (isnan(lambda)) return MAX_LAMBDA;
  return CLIP(MIN_LAMBDA, MAX_LAMBDA, lambda);
}

/**
 * \brief Update alpha and beta parameters.
 * \param state the main encoder state
 *
 * Sets global->rc_alpha and global->rc_beta of the encoder state.
 */
static void update_rc_parameters(encoder_state_t * state)
{
  const encoder_control_t * const encoder = state->encoder_control;

  const double pixels_per_picture = encoder->in.width * encoder->in.height;
  const double bpp = state->stats_bitstream_length * 8 / pixels_per_picture;
  const double log_bpp = log(bpp);

  const double alpha_old = state->frame->rc_alpha;
  const double beta_old = state->frame->rc_beta;
  // lambda computed from real bpp
  const double lambda_comp = clip_lambda(alpha_old * pow(bpp, beta_old));
  // lambda used in encoding
  const double lambda_real = state->frame->lambda;
  const double lambda_log_ratio = log(lambda_real) - log(lambda_comp);

  const double alpha = alpha_old + 0.1 * lambda_log_ratio * alpha_old;
  state->frame->rc_alpha = CLIP(0.05, 20, alpha);

  const double beta = beta_old + 0.05 * lambda_log_ratio * CLIP(-5, 1, log_bpp);
  state->frame->rc_beta = CLIP(-3, -0.1, beta);
}

/**
 * \brief Allocate bits for the current GOP.
 * \param state   the main encoder state
 * \return        target number of bits
 */
static double gop_allocate_bits(encoder_state_t * const state)
{
  const encoder_control_t * const encoder = state->encoder_control;

  // At this point, total_bits_coded of the current state contains the
  // number of bits written encoder->owf frames before the current frame.
  uint64_t bits_coded = state->frame->total_bits_coded;
  int pictures_coded = MAX(0, state->frame->num - encoder->owf);

  int gop_offset = (state->frame->gop_offset - encoder->owf) % MAX(1, encoder->cfg->gop_len);
  // Only take fully coded GOPs into account.
  if (encoder->cfg->gop_len > 0 && gop_offset != encoder->cfg->gop_len - 1) {
    // Subtract number of bits in the partially coded GOP.
    bits_coded -= state->frame->cur_gop_bits_coded;
    // Subtract number of pictures in the partially coded GOP.
    pictures_coded -= gop_offset + 1;
  }

  // Equation 12 from https://doi.org/10.1109/TIP.2014.2336550
  double gop_target_bits =
    (encoder->target_avg_bppic * (pictures_coded + SMOOTHING_WINDOW) - bits_coded)
    * MAX(1, encoder->cfg->gop_len) / SMOOTHING_WINDOW;
  // Allocate at least 200 bits for each GOP like HM does.
  return MAX(200, gop_target_bits);
}

/**
 * Allocate bits for the current picture.
 * \param state   the main encoder state
 * \return        target number of bits
 */
static double pic_allocate_bits(encoder_state_t * const state)
{
  const encoder_control_t * const encoder = state->encoder_control;

  if (encoder->cfg->gop_len == 0 ||
      state->frame->gop_offset == 0 ||
      state->frame->num == 0)
  {
    // A new GOP starts at this frame.
    state->frame->cur_gop_target_bits = gop_allocate_bits(state);
    state->frame->cur_gop_bits_coded  = 0;
  } else {
    state->frame->cur_gop_target_bits =
      state->previous_encoder_state->frame->cur_gop_target_bits;
  }

  if (encoder->cfg->gop_len <= 0) {
    return state->frame->cur_gop_target_bits;
  }

  const double pic_weight = encoder->gop_layer_weights[
    encoder->cfg->gop[state->frame->gop_offset].layer - 1];
  double pic_target_bits = state->frame->cur_gop_target_bits * pic_weight;
  // Allocate at least 100 bits for each picture like HM does.
  return MAX(100, pic_target_bits);
}

int8_t lambda_to_qp(const double lambda)
{
  const int8_t qp = 4.2005 * log(lambda) + 13.7223 + 0.5;
  return CLIP(0, 51, qp);
}

/**
 * \brief Allocate bits and set lambda and QP for the current picture.
 * \param state the main encoder state
 */
void kvz_set_picture_lambda_and_qp(encoder_state_t * const state)
{
  const encoder_control_t * const ctrl = state->encoder_control;

  if (ctrl->cfg->target_bitrate > 0) {
    // Rate control enabled

    if (state->frame->num > ctrl->owf) {
      // At least one frame has been written.
      update_rc_parameters(state);
    }

    // TODO: take the picture headers into account
    const double pic_target_bits = pic_allocate_bits(state);
    const double target_bpp = pic_target_bits / ctrl->in.pixels_per_pic;
    double lambda = state->frame->rc_alpha * pow(target_bpp, state->frame->rc_beta);
    lambda = clip_lambda(lambda);

    state->frame->lambda              = lambda;
    state->frame->QP                  = lambda_to_qp(lambda);
    state->frame->cur_pic_target_bits = pic_target_bits;

  } else {
    // Rate control disabled
    kvz_gop_config const * const gop = &ctrl->cfg->gop[state->frame->gop_offset];
    const int gop_len = ctrl->cfg->gop_len;
    const int period  = gop_len > 0 ? gop_len : ctrl->cfg->intra_period;

    state->frame->QP = ctrl->cfg->qp;

    if (gop_len > 0 && state->frame->slicetype != KVZ_SLICE_I) {
      state->frame->QP += gop->qp_offset;
    }

    double lambda = pow(2.0, (state->frame->QP - 12) / 3.0);

    if (state->frame->slicetype == KVZ_SLICE_I) {
      lambda *= 0.57;

      // Reduce lambda for I-frames according to the number of references.
      if (period == 0) {
        lambda *= 0.5;
      } else {
        lambda *= 1.0 - CLIP(0.0, 0.5, 0.05 * (period - 1));
      }
    } else if (gop_len > 0) {
      lambda *= gop->qp_factor;

    } else {
      lambda *= 0.4624;
    }

    // Increase lambda if not key-frame.
    if (period > 0 && state->frame->poc % period != 0) {
      lambda *= CLIP(2.0, 4.0, (state->frame->QP - 12) / 6.0);
    }

    state->frame->lambda = lambda;
  }
}

/**
 * \brief Allocate bits for a LCU.
 * \param state   the main encoder state
 * \param pos     location of the LCU as number of LCUs from top left
 * \return number of bits allocated for the LCU
 */
static double lcu_allocate_bits(encoder_state_t * const state,
                                vector2d_t pos)
{
  double lcu_weight;
  if (state->frame->num > state->encoder_control->owf) {
    lcu_weight = kvz_get_lcu_stats(state, pos.x, pos.y)->weight;
  } else {
    const uint32_t num_lcus = state->encoder_control->in.width_in_lcu *
                              state->encoder_control->in.height_in_lcu;
    lcu_weight = 1.0 / num_lcus;
  }

  // Target number of bits for the current LCU.
  const double lcu_target_bits = state->frame->cur_pic_target_bits * lcu_weight;

  // Allocate at least one bit for each LCU.
  return MAX(1, lcu_target_bits);
}

void kvz_set_lcu_lambda_and_qp(encoder_state_t * const state,
                               vector2d_t pos)
{
  const encoder_control_t * const ctrl = state->encoder_control;

  if (ctrl->cfg->target_bitrate > 0) {
    const int32_t pixels     = MIN(LCU_WIDTH, state->tile->frame->width  - LCU_WIDTH * pos.x) *
                               MIN(LCU_WIDTH, state->tile->frame->height - LCU_WIDTH * pos.y);
    const double target_bits = lcu_allocate_bits(state, pos);
    const double target_bpp  = target_bits / pixels;
    const double alpha = state->frame->rc_alpha;
    const double beta  = state->frame->rc_beta;

    double lambda = alpha * pow(target_bpp, beta);
    lambda = clip_lambda(lambda);

    state->qp          = lambda_to_qp(lambda);
    state->lambda      = lambda;
    state->lambda_sqrt = sqrt(lambda);

    lcu_stats_t *lcu_stats = kvz_get_lcu_stats(state, pos.x, pos.y);
    lcu_stats->lambda      = lambda;
    lcu_stats->rc_alpha    = alpha;
    lcu_stats->rc_beta     = beta;

  } else {
    state->qp          = state->frame->QP;
    state->lambda      = state->frame->lambda;
    state->lambda_sqrt = sqrt(state->frame->lambda);
  }
}
