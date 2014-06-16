/*****************************************************************************
 * This file is part of Kvazaar HEVC encoder.
 *
 * Copyright (C) 2013-2014 Tampere University of Technology and others (see
 * COPYING file).
 *
 * Kvazaar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Kvazaar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Kvazaar.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

/*
 * \file
 */

#include "search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "config.h"
#include "bitstream.h"
#include "image.h"
#include "intra.h"
#include "inter.h"
#include "filter.h"
#include "rdo.h"
#include "transform.h"
#include "encoder.h"

// Temporarily for debugging.
#define SEARCH_MV_FULL_RADIUS 0

#define IN_FRAME(x, y, width, height, block_width, block_height) \
  ((x) >= 0 && (y) >= 0 \
  && (x) + (block_width) <= (width) \
  && (y) + (block_height) <= (height))

/**
 * This is used in the hexagon_search to select 3 points to search.
 *
 * The start of the hexagonal pattern has been repeated at the end so that
 * the indices between 1-6 can be used as the start of a 3-point list of new
 * points to search.
 *
 *   6 o-o 1 / 7
 *    /   \
 * 5 o  0  o 2 / 8
 *    \   /
 *   4 o-o 3
 */
const vector2d large_hexbs[10] = {
  { 0, 0 },
  { 1, -2 }, { 2, 0 }, { 1, 2 }, { -1, 2 }, { -2, 0 }, { -1, -2 },
  { 1, -2 }, { 2, 0 }
};

/**
 * This is used as the last step of the hexagon search.
 */
const vector2d small_hexbs[5] = {
  { 0, 0 },
  { -1, -1 }, { -1, 0 }, { 1, 0 }, { 1, 1 }
};


static uint32_t get_ep_ex_golomb_bitcost(uint32_t symbol, uint32_t count)
{
  int32_t num_bins = 0;
  while (symbol >= (uint32_t)(1 << count)) {
    ++num_bins;
    symbol -= 1 << count;
    ++count;
  }
  num_bins ++;

  return num_bins;
}

static uint32_t get_mvd_coding_cost(vector2d *mvd)
{
  uint32_t bitcost = 0;
  const int32_t mvd_hor = mvd->x;
  const int32_t mvd_ver = mvd->y;
  const int8_t hor_abs_gr0 = mvd_hor != 0;
  const int8_t ver_abs_gr0 = mvd_ver != 0;
  const uint32_t mvd_hor_abs = abs(mvd_hor);
  const uint32_t mvd_ver_abs = abs(mvd_ver);

  // Greater than 0 for x/y
  bitcost += 2;

  if (hor_abs_gr0) {
    if (mvd_hor_abs > 1) {
      bitcost += get_ep_ex_golomb_bitcost(mvd_hor_abs-2, 1) - 2; // TODO: tune the costs
    }
    // Greater than 1 + sign
    bitcost += 2;
  }

  if (ver_abs_gr0) {
    if (mvd_ver_abs > 1) {
      bitcost += get_ep_ex_golomb_bitcost(mvd_ver_abs-2, 1) - 2; // TODO: tune the costs
    }
    // Greater than 1 + sign
    bitcost += 2;
  }

  return bitcost;
}

static int calc_mvd_cost(const encoder_state * const encoder_state, int x, int y,
                         int16_t mv_cand[2][2], int16_t merge_cand[MRG_MAX_NUM_CANDS][3],
                         int16_t num_cand,int32_t ref_idx, uint32_t *bitcost)
{
  uint32_t temp_bitcost = 0;
  uint32_t merge_idx;
  int cand1_cost,cand2_cost;
  vector2d mvd_temp1, mvd_temp2;
  int8_t merged      = 0;
  int8_t cur_mv_cand = 0;

  x <<= 2;
  y <<= 2;

  // Check every candidate to find a match
  for(merge_idx = 0; merge_idx < (uint32_t)num_cand; merge_idx++) {
    if (merge_cand[merge_idx][0] == x &&
        merge_cand[merge_idx][1] == y &&
        merge_cand[merge_idx][2] == ref_idx) {
      temp_bitcost += merge_idx;
      merged = 1;
      break;
    }
  }

  // Check mvd cost only if mv is not merged
  if(!merged) {
    mvd_temp1.x = x - mv_cand[0][0];
    mvd_temp1.y = y - mv_cand[0][1];
    cand1_cost = get_mvd_coding_cost(&mvd_temp1);

    mvd_temp2.x = x - mv_cand[1][0];
    mvd_temp2.y = y - mv_cand[1][1];
    cand2_cost = get_mvd_coding_cost(&mvd_temp2);

    // Select candidate 1 if it has lower cost
    if (cand2_cost < cand1_cost) {
      cur_mv_cand = 1;
    }
    temp_bitcost += cur_mv_cand ? cand2_cost : cand1_cost;
  }
  *bitcost = temp_bitcost;
  return temp_bitcost*(int32_t)(encoder_state->global->cur_lambda_cost+0.5);
}


/**
 * \brief Do motion search using the HEXBS algorithm.
 *
 * \param depth      log2 depth of the search
 * \param pic        Picture motion vector is searched for.
 * \param ref        Picture motion vector is searched from.
 * \param orig       Top left corner of the searched for block.
 * \param mv_in_out  Predicted mv in and best out. Quarter pixel precision.
 *
 * \returns  Cost of the motion vector.
 *
 * Motion vector is searched by first searching iteratively with the large
 * hexagon pattern until the best match is at the center of the hexagon.
 * As a final step a smaller hexagon is used to check the adjacent pixels.
 *
 * If a non 0,0 predicted motion vector predictor is given as mv_in_out,
 * the 0,0 vector is also tried. This is hoped to help in the case where
 * the predicted motion vector is way off. In the future even more additional
 * points like 0,0 might be used, such as vectors from top or left.
 */
static unsigned hexagon_search(const encoder_state * const encoder_state, unsigned depth,
                               const image *pic, const image *ref,
                               const vector2d *orig, vector2d *mv_in_out,
                               int16_t mv_cand[2][2], int16_t merge_cand[MRG_MAX_NUM_CANDS][3],
                               int16_t num_cand, int32_t ref_idx, uint32_t *bitcost_out)
{
  vector2d mv = { mv_in_out->x >> 2, mv_in_out->y >> 2 };
  int block_width = CU_WIDTH_FROM_DEPTH(depth);
  unsigned best_cost = UINT32_MAX;
  uint32_t best_bitcost = 0, bitcost;
  unsigned i;
  unsigned best_index = 0; // Index of large_hexbs or finally small_hexbs.


  // Search the initial 7 points of the hexagon.
  for (i = 0; i < 7; ++i) {
    const vector2d *pattern = &large_hexbs[i];
    unsigned cost = image_calc_sad(pic, ref, orig->x, orig->y,
                             (encoder_state->tile->lcu_offset_x * LCU_WIDTH) + orig->x + mv.x + pattern->x, 
                             (encoder_state->tile->lcu_offset_y * LCU_WIDTH) + orig->y + mv.y + pattern->y,
                             block_width, block_width);
    cost += calc_mvd_cost(encoder_state, mv.x + pattern->x, mv.y + pattern->y, mv_cand,merge_cand,num_cand,ref_idx, &bitcost);

    if (cost < best_cost) {
      best_cost    = cost;
      best_index   = i;
      best_bitcost = bitcost;
    }
  }

  // Try the 0,0 vector.
  if (!(mv.x == 0 && mv.y == 0)) {
    unsigned cost = image_calc_sad(pic, ref, orig->x, orig->y,
                             (encoder_state->tile->lcu_offset_x * LCU_WIDTH) + orig->x, 
                             (encoder_state->tile->lcu_offset_y * LCU_WIDTH) + orig->y,
                             block_width, block_width);
    cost += calc_mvd_cost(encoder_state, 0, 0, mv_cand,merge_cand,num_cand,ref_idx, &bitcost);

    // If the 0,0 is better, redo the hexagon around that point.
    if (cost < best_cost) {
      best_cost    = cost;
      best_bitcost = bitcost;
      best_index   = 0;
      mv.x = 0;
      mv.y = 0;

      for (i = 1; i < 7; ++i) {
        const vector2d *pattern = &large_hexbs[i];
        unsigned cost = image_calc_sad(pic, ref, orig->x, orig->y,
                                 (encoder_state->tile->lcu_offset_x * LCU_WIDTH) + orig->x + pattern->x,
                                 (encoder_state->tile->lcu_offset_y * LCU_WIDTH) + orig->y + pattern->y,
                                 block_width, block_width);
        cost += calc_mvd_cost(encoder_state, pattern->x, pattern->y, mv_cand,merge_cand,num_cand,ref_idx, &bitcost);

        if (cost < best_cost) {
          best_cost    = cost;
          best_index   = i;
          best_bitcost = bitcost;
        }
      }
    }
  }

  // Iteratively search the 3 new points around the best match, until the best
  // match is in the center.
  while (best_index != 0) {
    unsigned start; // Starting point of the 3 offsets to be searched.
    if (best_index == 1) {
      start = 6;
    } else if (best_index == 8) {
      start = 1;
    } else {
      start = best_index - 1;
    }

    // Move the center to the best match.
    mv.x += large_hexbs[best_index].x;
    mv.y += large_hexbs[best_index].y;
    best_index = 0;

    // Iterate through the next 3 points.
    for (i = 0; i < 3; ++i) {
      const vector2d *offset = &large_hexbs[start + i];
      unsigned cost = image_calc_sad(pic, ref, orig->x, orig->y,
                               (encoder_state->tile->lcu_offset_x * LCU_WIDTH) + orig->x + mv.x + offset->x,
                               (encoder_state->tile->lcu_offset_y * LCU_WIDTH) + orig->y + mv.y + offset->y,
                               block_width, block_width);
      cost += calc_mvd_cost(encoder_state, mv.x + offset->x, mv.y + offset->y, mv_cand,merge_cand,num_cand,ref_idx, &bitcost);

      if (cost < best_cost) {
        best_cost    = cost;
        best_index   = start + i;
        best_bitcost = bitcost;
      }
      ++offset;
    }
  }

  // Move the center to the best match.
  mv.x += large_hexbs[best_index].x;
  mv.y += large_hexbs[best_index].y;
  best_index = 0;

  // Do the final step of the search with a small pattern.
  for (i = 1; i < 5; ++i) {
    const vector2d *offset = &small_hexbs[i];
    unsigned cost = image_calc_sad(pic, ref, orig->x, orig->y,
                             (encoder_state->tile->lcu_offset_x * LCU_WIDTH) + orig->x + mv.x + offset->x,
                             (encoder_state->tile->lcu_offset_y * LCU_WIDTH) + orig->y + mv.y + offset->y,
                             block_width, block_width);
    cost += calc_mvd_cost(encoder_state, mv.x + offset->x, mv.y + offset->y, mv_cand,merge_cand,num_cand,ref_idx, &bitcost);

    if (cost > 0 && cost < best_cost) {
      best_cost    = cost;
      best_index   = i;
      best_bitcost = bitcost;
    }
  }

  // Adjust the movement vector according to the final best match.
  mv.x += small_hexbs[best_index].x;
  mv.y += small_hexbs[best_index].y;

  // Return final movement vector in quarter-pixel precision.
  mv_in_out->x = mv.x << 2;
  mv_in_out->y = mv.y << 2;

  *bitcost_out = best_bitcost;

  return best_cost;
}


#if SEARCH_MV_FULL_RADIUS
static unsigned search_mv_full(unsigned depth,
                               const picture *pic, const picture *ref,
                               const vector2d *orig, vector2d *mv_in_out,
                               int16_t mv_cand[2][2], int16_t merge_cand[MRG_MAX_NUM_CANDS][3],
                               int16_t num_cand, int32_t ref_idx, uint32_t *bitcost_out)
{
  vector2d mv = { mv_in_out->x >> 2, mv_in_out->y >> 2 };
  int block_width = CU_WIDTH_FROM_DEPTH(depth);
  unsigned best_cost = UINT32_MAX;
  int x, y;
  uint32_t best_bitcost = 0, bitcost;
  vector2d min_mv, max_mv;

  /*if (abs(mv.x) > SEARCH_MV_FULL_RADIUS || abs(mv.y) > SEARCH_MV_FULL_RADIUS) {
    best_cost = calc_sad(pic, ref, orig->x, orig->y,
                         orig->x, orig->y,
                         block_width, block_width);
    mv.x = 0;
    mv.y = 0;
  }*/

  min_mv.x = mv.x - SEARCH_MV_FULL_RADIUS;
  min_mv.y = mv.y - SEARCH_MV_FULL_RADIUS;
  max_mv.x = mv.x + SEARCH_MV_FULL_RADIUS;
  max_mv.y = mv.y + SEARCH_MV_FULL_RADIUS;

  for (y = min_mv.y; y < max_mv.y; ++y) {
    for (x = min_mv.x; x < max_mv.x; ++x) {
      unsigned cost = calc_sad(pic, ref, orig->x, orig->y,
                               orig->x + x,
                               orig->y + y,
                               block_width, block_width);
      cost += calc_mvd_cost(x, y, mv_cand,merge_cand,num_cand,ref_idx, &bitcost);
      if (cost < best_cost) {
        best_cost    = cost;
        best_bitcost = bitcost;
        mv.x = x;
        mv.y = y;
      }
    }
  }

  mv_in_out->x = mv.x << 2;
  mv_in_out->y = mv.y << 2;

  *bitcost_out = best_bitcost;

  return best_cost;
}
#endif

/**
 * Update lcu to have best modes at this depth.
 * \return Cost of best mode.
 */
static int search_cu_inter(const encoder_state * const encoder_state, int x, int y, int depth, lcu_t *lcu)
{
  const videoframe * const frame = encoder_state->tile->frame;
  uint32_t ref_idx = 0;
  int x_local = (x&0x3f), y_local = (y&0x3f);
  int x_cu = x>>3;
  int y_cu = y>>3;
  int cu_pos = LCU_CU_OFFSET+(x_local>>3) + (y_local>>3)*LCU_T_CU_WIDTH;

  cu_info *cur_cu = &lcu->cu[cu_pos];

  int16_t mv_cand[2][2];
  // Search for merge mode candidate
  int16_t merge_cand[MRG_MAX_NUM_CANDS][3];
  // Get list of candidates
  int16_t num_cand = inter_get_merge_cand(x, y, depth, merge_cand, lcu);

  // Select better candidate
  cur_cu->inter.mv_cand = 0; // Default to candidate 0

  cur_cu->inter.cost = UINT_MAX;

  for (ref_idx = 0; ref_idx < encoder_state->global->ref->used_size; ref_idx++) {
    image *ref_image = encoder_state->global->ref->images[ref_idx];
    const cu_info *ref_cu = &encoder_state->global->ref->cu_arrays[ref_idx][x_cu + y_cu * (frame->width_in_lcu << MAX_DEPTH)];
    uint32_t temp_bitcost = 0;
    uint32_t temp_cost = 0;
    vector2d orig, mv, mvd;
    int32_t merged = 0;
    uint8_t cu_mv_cand = 0;
    int8_t merge_idx = 0;
    int8_t temp_ref_idx = cur_cu->inter.mv_ref;
    orig.x = x_cu * CU_MIN_SIZE_PIXELS;
    orig.y = y_cu * CU_MIN_SIZE_PIXELS;
    mv.x = 0;
    mv.y = 0;
    if (ref_cu->type == CU_INTER) {
      mv.x = ref_cu->inter.mv[0];
      mv.y = ref_cu->inter.mv[1];
    }
    // Get MV candidates
    cur_cu->inter.mv_ref = ref_idx;
    inter_get_mv_cand(encoder_state, x, y, depth, mv_cand, cur_cu, lcu);
    cur_cu->inter.mv_ref = temp_ref_idx;

#if SEARCH_MV_FULL_RADIUS
    temp_cost += search_mv_full(depth, frame, ref_pic, &orig, &mv, mv_cand, merge_cand, num_cand, ref_idx, &temp_bitcost);
#else
    temp_cost += hexagon_search(encoder_state, depth, frame->source, ref_image, &orig, &mv, mv_cand, merge_cand, num_cand, ref_idx, &temp_bitcost);
#endif

    merged = 0;
    // Check every candidate to find a match
    for(merge_idx = 0; merge_idx < num_cand; merge_idx++) {
      if (merge_cand[merge_idx][0] == mv.x &&
          merge_cand[merge_idx][1] == mv.y &&
          (uint32_t)merge_cand[merge_idx][2] == ref_idx) {
        merged = 1;
        break;
      }
    }

    // Only check when candidates are different
    if (!merged && (mv_cand[0][0] != mv_cand[1][0] || mv_cand[0][1] != mv_cand[1][1])) {
      vector2d mvd_temp1, mvd_temp2;
      int cand1_cost,cand2_cost;

      mvd_temp1.x = mv.x - mv_cand[0][0];
      mvd_temp1.y = mv.y - mv_cand[0][1];
      cand1_cost = get_mvd_coding_cost(&mvd_temp1);

      mvd_temp2.x = mv.x - mv_cand[1][0];
      mvd_temp2.y = mv.y - mv_cand[1][1];
      cand2_cost = get_mvd_coding_cost(&mvd_temp2);

      // Select candidate 1 if it has lower cost
      if (cand2_cost < cand1_cost) {
        cu_mv_cand = 1;
      }
    }
    mvd.x = mv.x - mv_cand[cu_mv_cand][0];
    mvd.y = mv.y - mv_cand[cu_mv_cand][1];

    if(temp_cost < cur_cu->inter.cost) {
      cur_cu->merged        = merged;
      cur_cu->merge_idx     = merge_idx;
      cur_cu->inter.mv_ref  = ref_idx;
      cur_cu->inter.mv_dir  = 1;
      cur_cu->inter.mv[0]   = (int16_t)mv.x;
      cur_cu->inter.mv[1]   = (int16_t)mv.y;
      cur_cu->inter.mvd[0]  = (int16_t)mvd.x;
      cur_cu->inter.mvd[1]  = (int16_t)mvd.y;
      cur_cu->inter.cost    = temp_cost;
      cur_cu->inter.bitcost = temp_bitcost + ref_idx;
      cur_cu->inter.mv_cand = cu_mv_cand;
    }
  }

  return cur_cu->inter.cost;
}


/**
 * Copy all non-reference CU data from depth+1 to depth.
 */
static void work_tree_copy_up(int x_px, int y_px, int depth, lcu_t work_tree[MAX_PU_DEPTH + 1])
{
  // Copy non-reference CUs.
  {
    const int x_cu = SUB_SCU(x_px) >> MAX_DEPTH;
    const int y_cu = SUB_SCU(y_px) >> MAX_DEPTH;
    const int width_cu = LCU_WIDTH >> MAX_DEPTH >> depth;
    int x, y;
    for (y = y_cu; y < y_cu + width_cu; ++y) {
      for (x = x_cu; x < x_cu + width_cu; ++x) {
        const cu_info *from_cu = &work_tree[depth + 1].cu[LCU_CU_OFFSET + x + y * LCU_T_CU_WIDTH];
        cu_info *to_cu = &work_tree[depth].cu[LCU_CU_OFFSET + x + y * LCU_T_CU_WIDTH];
        memcpy(to_cu, from_cu, sizeof(*to_cu));
      }
    }
  }

  // Copy reconstructed pixels.
  {
    const int x = SUB_SCU(x_px);
    const int y = SUB_SCU(y_px);
    const int width_px = LCU_WIDTH >> depth;
    const int luma_index = x + y * LCU_WIDTH;
    const int chroma_index = (x / 2) + (y / 2) * (LCU_WIDTH / 2);

    const lcu_yuv_t *from = &work_tree[depth + 1].rec;
    lcu_yuv_t *to = &work_tree[depth].rec;

    const lcu_coeff_t *from_coeff = &work_tree[depth + 1].coeff;
    lcu_coeff_t *to_coeff = &work_tree[depth].coeff;

    pixels_blit(&from->y[luma_index], &to->y[luma_index],
                        width_px, width_px, LCU_WIDTH, LCU_WIDTH);
    pixels_blit(&from->u[chroma_index], &to->u[chroma_index],
                        width_px / 2, width_px / 2, LCU_WIDTH / 2, LCU_WIDTH / 2);
    pixels_blit(&from->v[chroma_index], &to->v[chroma_index],
                        width_px / 2, width_px / 2, LCU_WIDTH / 2, LCU_WIDTH / 2);

    // Copy coefficients up. They do not have to be copied down because they
    // are not used for the search.
    coefficients_blit(&from_coeff->y[luma_index], &to_coeff->y[luma_index],
                        width_px, width_px, LCU_WIDTH, LCU_WIDTH);
    coefficients_blit(&from_coeff->u[chroma_index], &to_coeff->u[chroma_index],
                        width_px / 2, width_px / 2, LCU_WIDTH / 2, LCU_WIDTH / 2);
    coefficients_blit(&from_coeff->v[chroma_index], &to_coeff->v[chroma_index],
                        width_px / 2, width_px / 2, LCU_WIDTH / 2, LCU_WIDTH / 2);
  }
}


/**
 * Copy all non-reference CU data from depth to depth+1..MAX_PU_DEPTH.
 */
static void work_tree_copy_down(int x_px, int y_px, int depth, lcu_t work_tree[MAX_PU_DEPTH + 1])
{
  // TODO: clean up to remove the copy pasta
  const int width_px = LCU_WIDTH >> depth;

  int d;

  for (d = depth + 1; d < MAX_PU_DEPTH + 1; ++d) {
    const int x_cu = SUB_SCU(x_px) >> MAX_DEPTH;
    const int y_cu = SUB_SCU(y_px) >> MAX_DEPTH;
    const int width_cu = width_px >> MAX_DEPTH;

    int x, y;
    for (y = y_cu; y < y_cu + width_cu; ++y) {
      for (x = x_cu; x < x_cu + width_cu; ++x) {
        const cu_info *from_cu = &work_tree[depth].cu[LCU_CU_OFFSET + x + y * LCU_T_CU_WIDTH];
        cu_info *to_cu = &work_tree[d].cu[LCU_CU_OFFSET + x + y * LCU_T_CU_WIDTH];
        memcpy(to_cu, from_cu, sizeof(*to_cu));
      }
    }
  }

  // Copy reconstructed pixels.
  for (d = depth + 1; d < MAX_PU_DEPTH + 1; ++d) {
    const int x = SUB_SCU(x_px);
    const int y = SUB_SCU(y_px);

    const int luma_index = x + y * LCU_WIDTH;
    const int chroma_index = (x / 2) + (y / 2) * (LCU_WIDTH / 2);

    lcu_yuv_t *from = &work_tree[depth].rec;
    lcu_yuv_t *to = &work_tree[d].rec;

    pixels_blit(&from->y[luma_index], &to->y[luma_index],
                        width_px, width_px, LCU_WIDTH, LCU_WIDTH);
    pixels_blit(&from->u[chroma_index], &to->u[chroma_index],
                        width_px / 2, width_px / 2, LCU_WIDTH / 2, LCU_WIDTH / 2);
    pixels_blit(&from->v[chroma_index], &to->v[chroma_index],
                        width_px / 2, width_px / 2, LCU_WIDTH / 2, LCU_WIDTH / 2);
  }
}


static void lcu_set_intra_mode(lcu_t *lcu, int x_px, int y_px, int depth, int tr_depth, int pred_mode, int chroma_mode, int part_mode)
{
  const int width_cu = LCU_CU_WIDTH >> depth;
  const int x_cu = SUB_SCU(x_px) >> MAX_DEPTH;
  const int y_cu = SUB_SCU(y_px) >> MAX_DEPTH;
  cu_info *const lcu_cu = &lcu->cu[LCU_CU_OFFSET];
  int x, y;

  // NxN can only be applied to a single CU at a time.
  if (part_mode == SIZE_NxN) {
    cu_info *cu = &lcu_cu[x_cu + y_cu * LCU_T_CU_WIDTH];
    cu->depth = MAX_DEPTH;
    cu->type = CU_INTRA;
    // It is assumed that cu->intra[].mode's are already set.
    cu->part_size = part_mode;
    cu->tr_depth = tr_depth;
    return;
  }

  // Set mode in every CU covered by part_mode in this depth.
  for (y = y_cu; y < y_cu + width_cu; ++y) {
    for (x = x_cu; x < x_cu + width_cu; ++x) {
      cu_info *cu = &lcu_cu[x + y * LCU_T_CU_WIDTH];
      cu->depth = depth;
      cu->type = CU_INTRA;
      cu->intra[0].mode = pred_mode;
      cu->intra[1].mode = pred_mode;
      cu->intra[2].mode = pred_mode;
      cu->intra[3].mode = pred_mode;
      cu->intra[0].mode_chroma = chroma_mode;
      cu->part_size = part_mode;
      cu->tr_depth = tr_depth;
      cu->coded = 1;
    }
  }
}


static void lcu_set_inter(lcu_t *lcu, int x_px, int y_px, int depth, cu_info *cur_cu)
{
  const int width_cu = LCU_CU_WIDTH >> depth;
  const int x_cu = SUB_SCU(x_px) >> MAX_DEPTH;
  const int y_cu = SUB_SCU(y_px) >> MAX_DEPTH;
  cu_info *const lcu_cu = &lcu->cu[LCU_CU_OFFSET];
  int x, y;
  // Set mode in every CU covered by part_mode in this depth.
  for (y = y_cu; y < y_cu + width_cu; ++y) {
    for (x = x_cu; x < x_cu + width_cu; ++x) {
      cu_info *cu = &lcu_cu[x + y * LCU_T_CU_WIDTH];
      //Check if this could be moved inside the if
      cu->coded    = 1;
      if (cu != cur_cu) {
        cu->depth    = cur_cu->depth;
        cu->type     = CU_INTER;
        cu->tr_depth = cur_cu->tr_depth;
        cu->merged   = cur_cu->merged;
        cu->skipped  = cur_cu->skipped;
        memcpy(&cu->inter, &cur_cu->inter, sizeof(cu_info_inter));
      }
    }
  }
}


static void lcu_set_coeff(lcu_t *lcu, int x_px, int y_px, int depth, cu_info *cur_cu)
{
  const int width_cu = LCU_CU_WIDTH >> depth;
  const int x_cu = SUB_SCU(x_px) >> MAX_DEPTH;
  const int y_cu = SUB_SCU(y_px) >> MAX_DEPTH;
  cu_info *const lcu_cu = &lcu->cu[LCU_CU_OFFSET];
  int x, y;
  int tr_split = cur_cu->tr_depth-cur_cu->depth;

  // Set coeff flags in every CU covered by part_mode in this depth.
  for (y = y_cu; y < y_cu + width_cu; ++y) {
    for (x = x_cu; x < x_cu + width_cu; ++x) {
      cu_info *cu = &lcu_cu[x + y * LCU_T_CU_WIDTH];
      // Use TU top-left CU to propagate coeff flags
      uint32_t mask = ~((width_cu>>tr_split)-1);
      cu_info *cu_from = &lcu_cu[(x & mask) + (y & mask) * LCU_T_CU_WIDTH];
      if (cu != cu_from) {
        // Chroma coeff data is not used, luma is needed for deblocking
        cu->cbf.y = cu_from->cbf.y;
      }
    }
  }
}


/**
 * Calculate "final cost" for the block
 * \return Cost of block
 *
 * Take SSD between reconstruction and original and add cost from
 * coding (bitcost * lambda) and cost for coding coefficients (estimated
 * here as (coefficient_sum * 1.5) * lambda)
 */
static int lcu_get_final_cost_luma(const encoder_state * const encoder_state,
                                   const int x_px, const int y_px,
                                   const int depth, lcu_t *lcu)
{
  cu_info *cur_cu;
  int x_local = (x_px&0x3f), y_local = (y_px&0x3f);
  int cost = 0;
  int coeff_cost = 0;
  const int rdo = encoder_state->encoder_control->rdo;

  int width = LCU_WIDTH>>depth;
  int x,y;
  cur_cu = &lcu->cu[LCU_CU_OFFSET+(x_local>>3) + (y_local>>3)*LCU_T_CU_WIDTH];

  // SSD between reconstruction and original
  for (y = y_local; y < y_local+width; ++y) {
    for (x = x_local; x < x_local+width; ++x) {
      int diff = (int)lcu->rec.y[y * LCU_WIDTH + x] - (int)lcu->ref.y[y * LCU_WIDTH + x];
      cost += diff*diff;
    }
  }

  if(rdo == 1) {
    // sum of coeffs
    for (y = y_local; y < y_local+width; ++y) {
      for (x = x_local; x < x_local+width; ++x) {
        coeff_cost += abs((int)lcu->coeff.y[y * LCU_WIDTH + x]);
      }
    }
    // Coefficient costs
    cost += (coeff_cost + (coeff_cost>>1)) * (int32_t)(encoder_state->global->cur_lambda_cost+0.5);

  // Calculate actual bit costs for coding the coeffs
  // RDO
  } else if (rdo == 2) {
    coefficient coeff_temp[32*32];
    int i;
    int blocks = (width == 64)?4:1;
    int8_t luma_scan_mode = get_scan_order(cur_cu->type, cur_cu->intra[PU_INDEX(x_px / 4, y_px / 4)].mode, depth);

    for(i = 0; i < blocks; i++) {
      // For 64x64 blocks we need to do transform split to 32x32
      int blk_y = i&2 ? 32:0 + y_local;
      int blk_x = i&1 ? 32:0 + x_local;
      int blockwidth = (width == 64)?32:width;

      // Calculate luma coeff bit count
      coefficients_blit(&lcu->coeff.y[(blk_y*LCU_WIDTH)+blk_x],coeff_temp,blockwidth,blockwidth,LCU_WIDTH,blockwidth);
      coeff_cost += get_coeff_cost(encoder_state, coeff_temp, blockwidth, 0, luma_scan_mode);
    }
    // Multiply bit count with lambda to get RD-cost
    cost += coeff_cost * (int32_t)(encoder_state->global->cur_lambda_cost+0.5);
  }

  return cost;
}

static int lcu_get_final_cost_chroma(const encoder_state * const encoder_state,
                                     const int x_px, const int y_px,
                                     const int depth, lcu_t *lcu)
{
  cu_info *cur_cu;
  int x_local = (x_px&0x3f), y_local = (y_px&0x3f);
  int cost = 0;
  int coeff_cost = 0;
  const int rdo = encoder_state->encoder_control->rdo;

  int width = LCU_WIDTH>>depth;
  int x,y;
  cur_cu = &lcu->cu[LCU_CU_OFFSET+(x_local>>3) + (y_local>>3)*LCU_T_CU_WIDTH];

  // Chroma SSD
  for (y = y_local>>1; y < (y_local+width)>>1; ++y) {
    for (x = x_local>>1; x < (x_local+width)>>1; ++x) {
      int diff = (int)lcu->rec.u[y * (LCU_WIDTH>>1) + x] - (int)lcu->ref.u[y * (LCU_WIDTH>>1) + x];
      cost += diff*diff;
      diff = (int)lcu->rec.v[y * (LCU_WIDTH>>1) + x] - (int)lcu->ref.v[y * (LCU_WIDTH>>1) + x];
      cost += diff*diff;
    }
  }

  if(rdo == 1) {
    // Chroma sum of coeffs
    for (y = y_local>>1; y < (y_local+width)>>1; ++y) {
      for (x = x_local>>1; x < (x_local+width)>>1; ++x) {
        coeff_cost += abs((int)lcu->coeff.u[y * (LCU_WIDTH>>1) + x]);
        coeff_cost += abs((int)lcu->coeff.v[y * (LCU_WIDTH>>1) + x]);
      }
    }
    // Coefficient costs
    cost += (coeff_cost + (coeff_cost>>1)) * (int32_t)(encoder_state->global->cur_lambda_cost+0.5);

  // Calculate actual bit costs for coding the coeffs
  // RDO
  } else if (rdo == 2) {
    coefficient coeff_temp_u[16*16];
    coefficient coeff_temp_v[16*16];
    int i;
    int blocks = (width == 64)?4:1;
    int8_t chroma_scan_mode = get_scan_order(cur_cu->type, cur_cu->intra[0].mode_chroma, depth);

    for(i = 0; i < blocks; i++) {
      // For 64x64 blocks we need to do transform split to 32x32
      int blk_y = i&2 ? 32:0 + y_local;
      int blk_x = i&1 ? 32:0 + x_local;
      int blockwidth = (width == 64)?32:width;

      blk_y >>= 1;
      blk_x >>= 1;
      if (blockwidth > 4) {
        // Chroma is 1/4th of luma unless luma is 4x4.
        blockwidth >>= 1;
      } else if (x_px % 8 != 0 || y_px % 8 != 0) {
        // Only add chroma cost for 4x4 blocks for the one on the 8x8 grid.
        break;
      }

      coefficients_blit(&lcu->coeff.u[(blk_y*(LCU_WIDTH>>1))+blk_x],coeff_temp_u,blockwidth,blockwidth,LCU_WIDTH>>1,blockwidth);
      coefficients_blit(&lcu->coeff.v[(blk_y*(LCU_WIDTH>>1))+blk_x],coeff_temp_v,blockwidth,blockwidth,LCU_WIDTH>>1,blockwidth);

      coeff_cost += get_coeff_cost(encoder_state, coeff_temp_u, blockwidth, 2, chroma_scan_mode);
      coeff_cost += get_coeff_cost(encoder_state, coeff_temp_v, blockwidth, 2, chroma_scan_mode);
    }
    // Multiply bit count with lambda to get RD-cost
    cost += coeff_cost * (int32_t)(encoder_state->global->cur_lambda_cost+0.5);
  }

  return cost;
}



static void sort_modes(int8_t *modes, uint32_t *costs, int length)
{
  int i, j;
  for (i = 0; i < length; ++i) {
    j = i;
    while (j > 0 && costs[j] < costs[j - 1]) {
      SWAP(costs[j], costs[j - 1], uint32_t);
      SWAP(modes[j], modes[j - 1], int8_t);
      --j;
    }
  }
}


static void search_intra_rough(encoder_state * const encoder_state, 
                               pixel *orig, int32_t origstride,
                               pixel *rec, int16_t recstride,
                               int width, int8_t *intra_preds,
                               int8_t modes[35], uint32_t costs[35])
{
  int16_t mode;
  cost_pixel_nxn_func * cost_func = pixels_get_sad_func(width);

  // Temporary block arrays
  pixel pred[LCU_WIDTH * LCU_WIDTH + 1];
  pixel orig_block[LCU_WIDTH * LCU_WIDTH + 1];
  pixel rec_filtered_temp[(LCU_WIDTH * 2 + 8) * (LCU_WIDTH * 2 + 8) + 1];

  pixel *ref[2] = {rec, &rec_filtered_temp[recstride + 1]};

  assert(width == 4 || width == 8 || width == 16 || width == 32);

  // Store original block for SAD computation
  pixels_blit(orig, orig_block, width, width, origstride, width);

  // Generate filtered reference pixels.
  {
    int16_t x, y;
    for (y = -1; y < recstride; y++) {
      ref[1][y*recstride - 1] = rec[y*recstride - 1];
                                                }
    for (x = 0; x < recstride; x++) {
      ref[1][x - recstride] = rec[x - recstride];
    }
    intra_filter(ref[1], recstride, width, 0);
  }

  // Try all modes and select the best one.
  for (mode = 0; mode < 35; mode++) {
    uint32_t sad = 0;
    uint32_t mode_cost = intra_pred_ratecost(mode, intra_preds);
    intra_get_pred(encoder_state->encoder_control, ref, recstride, pred, width, mode, 0);

    sad = cost_func(pred, orig_block);
    sad += mode_cost * (int)(encoder_state->global->cur_lambda_cost + 0.5);
    costs[mode] = sad;
    modes[mode] = mode;
  }

  sort_modes(modes, costs, 35);
}


static void search_intra_rdo(encoder_state * const encoder_state, 
                             pixel *orig, int32_t origstride,
                             pixel *rec, int16_t recstride,
                             int width, int8_t *intra_preds,
                             int8_t modes[35], uint32_t costs[35],
                             int modes_to_check)
{
  pixel pred[LCU_WIDTH * LCU_WIDTH + 1];
  pixel orig_block[LCU_WIDTH * LCU_WIDTH + 1];
  int rdo_mode;
  int pred_mode;

  pixel rec_filtered_temp[(LCU_WIDTH * 2 + 8) * (LCU_WIDTH * 2 + 8) + 1];
  pixel *ref[2] = {rec, &rec_filtered_temp[recstride + 1]};

  // Generate filtered reference pixels.
  {
    int x, y;
    for (y = -1; y < recstride; y++) {
      ref[1][y*recstride - 1] = rec[y*recstride - 1];
    }
    for (x = 0; x < recstride; x++) {
      ref[1][x - recstride] = rec[x - recstride];
    }
    intra_filter(ref[1], recstride, width, 0);
  }

  pixels_blit(orig, orig_block, width, width, origstride, width);

  // Check that the predicted modes are in the RDO mode list
  for(pred_mode = 0; pred_mode < 3; pred_mode++) {
    int mode_found = 0;
    for(rdo_mode = 0; rdo_mode < modes_to_check; rdo_mode ++) {
      if(intra_preds[pred_mode] == modes[rdo_mode]) {
        mode_found = 1;
        break;
      }
    }
    // Add this prediction mode to RDO checking
    if(!mode_found) {
      modes[modes_to_check] = intra_preds[pred_mode];
      modes_to_check++;
    }
  }

  for(rdo_mode = 0; rdo_mode < modes_to_check; rdo_mode ++) {
    int rdo_bitcost;
    // The reconstruction is calculated again here, it could be saved from before..
    intra_get_pred(encoder_state->encoder_control, ref, recstride, pred, width, modes[rdo_mode], 0);

    costs[rdo_mode] = rdo_cost_intra(encoder_state,pred,orig_block,width,modes[rdo_mode], width == 4 ? 1 : 0);
    // Bitcost also calculated again for this mode
    rdo_bitcost = intra_pred_ratecost(modes[rdo_mode],intra_preds);
    // Add bitcost * lambda
    costs[rdo_mode] += rdo_bitcost * (int)(encoder_state->global->cur_lambda_cost + 0.5);
  }

  sort_modes(modes, costs, modes_to_check);
}


/**
 * Update lcu to have best modes at this depth.
 * \return Cost of best mode.
 */
static int search_cu_intra(encoder_state * const encoder_state,
                           const int x_px, const int y_px,
                           const int depth, lcu_t *lcu)
{
  const videoframe * const frame = encoder_state->tile->frame;
  const vector2d lcu_px = { x_px & 0x3f, y_px & 0x3f };
  const vector2d lcu_cu = { lcu_px.x >> 3, lcu_px.y >> 3 };
  const int8_t cu_width = (LCU_WIDTH >> (depth));
  const int cu_index = LCU_CU_OFFSET + lcu_cu.x + lcu_cu.y * LCU_T_CU_WIDTH;

  cu_info *cur_cu = &lcu->cu[cu_index];

  pixel rec_buffer[(LCU_WIDTH * 2 + 1) * (LCU_WIDTH * 2 + 1)];
  pixel *cu_in_rec_buffer = &rec_buffer[cu_width * 2 + 8 + 1];

  int8_t candidate_modes[3];

  cu_info *left_cu = 0;
  cu_info *above_cu = 0;

  if ((x_px >> 3) > 0) {
    left_cu = &lcu->cu[cu_index - 1];
  }
  // Don't take the above CU across the LCU boundary.
  if ((y_px >> 3) > 0 && lcu_cu.y != 0) {
    above_cu = &lcu->cu[cu_index - LCU_T_CU_WIDTH];
  }

  // Get intra predictors
  intra_get_dir_luma_predictor(x_px, y_px, candidate_modes, cur_cu, left_cu, above_cu);

  // Build reconstructed block to use in prediction with extrapolated borders
  intra_build_reference_border(encoder_state->encoder_control, x_px, y_px, cu_width * 2 + 8,
                               rec_buffer, cu_width * 2 + 8, 0,
                               frame->width,
                               frame->height,
                               lcu);

  // Find best intra mode for 2Nx2N.
  {
    pixel *ref_pixels = &lcu->ref.y[lcu_px.x + lcu_px.y * LCU_WIDTH];
    unsigned pu_index = PU_INDEX(x_px >> 2, y_px >> 2);
    int8_t modes[35];
    uint32_t costs[35];
    
    search_intra_rough(encoder_state, 
                       ref_pixels, LCU_WIDTH,
                       cu_in_rec_buffer, cu_width * 2 + 8,
                       cu_width, candidate_modes,
                       modes, costs);

    if (encoder_state->encoder_control->rdo == 2) {
      int num_modes_to_check = (cu_width <= 8) ? 8 : 3;
      search_intra_rdo(encoder_state, 
                       ref_pixels, LCU_WIDTH,
                       cu_in_rec_buffer, cu_width * 2 + 8,
                       cu_width, candidate_modes,
                       modes, costs, num_modes_to_check);
    }

    cur_cu->intra[pu_index].mode = modes[0];
    cur_cu->intra[pu_index].cost = costs[0];
    cur_cu->intra[pu_index].bitcost = intra_pred_ratecost(modes[0], candidate_modes);

    cur_cu->intra[0].mode_chroma = cur_cu->intra[0].mode;
  }

  return cur_cu->intra[PU_INDEX(x_px >> 2, y_px >> 2)].cost;
}



/**
 * Search every mode from 0 to MAX_PU_DEPTH and return cost of best mode.
 * - The recursion is started at depth 0 and goes in Z-order to MAX_PU_DEPTH.
 * - Data structure work_tree is maintained such that the neighbouring SCUs
 *   and pixels to the left and up of current CU are the final CUs decided
 *   via the search. This is done by copying the relevant data to all
 *   relevant levels whenever a decision is made whether to split or not.
 * - All the final data for the LCU gets eventually copied to depth 0, which
 *   will be the final output of the recursion.
 */
static int search_cu(encoder_state * const encoder_state, int x, int y, int depth, lcu_t work_tree[MAX_PU_DEPTH])
{
  const videoframe * const frame = encoder_state->tile->frame;
  int cu_width = LCU_WIDTH >> depth;
  int cost = MAX_INT;
  cu_info *cur_cu;
  int x_local = (x&0x3f), y_local = (y&0x3f);

  // Stop recursion if the CU is completely outside the frame.
  if (x >= frame->width || y >= frame->height) {
    // Return zero cost because this CU does not have to be coded.
    return 0;
  }

  cur_cu = &(&work_tree[depth])->cu[LCU_CU_OFFSET+(x_local>>3) + (y_local>>3)*LCU_T_CU_WIDTH];
  // Assign correct depth
  cur_cu->depth = depth > MAX_DEPTH ? MAX_DEPTH : depth;
  cur_cu->tr_depth = depth > 0 ? depth : 1;
  cur_cu->type = CU_NOTSET;
  cur_cu->part_size = depth > MAX_DEPTH ? SIZE_NxN : SIZE_2Nx2N;
  // If the CU is completely inside the frame at this depth, search for
  // prediction modes at this depth.
  if (x + cu_width <= frame->width &&
      y + cu_width <= frame->height)
  {

    if (encoder_state->global->slicetype != SLICE_I &&
        depth >= MIN_INTER_SEARCH_DEPTH &&
        depth <= MAX_INTER_SEARCH_DEPTH)
    {
      int mode_cost = search_cu_inter(encoder_state, x, y, depth, &work_tree[depth]);
      if (mode_cost < cost) {
        cost = mode_cost;
        cur_cu->type = CU_INTER;
      }
    }

    if (depth >= MIN_INTRA_SEARCH_DEPTH &&
        depth <= MAX_INTRA_SEARCH_DEPTH)
    {
      int mode_cost = search_cu_intra(encoder_state, x, y, depth, &work_tree[depth]);
      if (mode_cost < cost) {
        cost = mode_cost;
        cur_cu->type = CU_INTRA;
      }
    }

    // Reconstruct best mode because we need the reconstructed pixels for
    // mode search of adjacent CUs.
    if (cur_cu->type == CU_INTRA) {
      lcu_set_intra_mode(&work_tree[depth], x, y, depth, cur_cu->tr_depth,
                         cur_cu->intra[PU_INDEX(x >> 2, y >> 2)].mode,
                         cur_cu->intra[0].mode_chroma,
                         cur_cu->part_size);
      intra_recon_lcu_luma(encoder_state, x, y, depth, &work_tree[depth]);
      intra_recon_lcu_chroma(encoder_state, x, y, depth, &work_tree[depth]);
    } else if (cur_cu->type == CU_INTER) {
      int cbf;
      inter_recon_lcu(encoder_state, encoder_state->global->ref->images[cur_cu->inter.mv_ref], x, y, LCU_WIDTH>>depth, cur_cu->inter.mv, &work_tree[depth]);
      quantize_lcu_luma_residual(encoder_state, x, y, depth, &work_tree[depth]);
      quantize_lcu_chroma_residual(encoder_state, x, y, depth, &work_tree[depth]);

      cbf = cbf_is_set(cur_cu->cbf.y, depth) || cbf_is_set(cur_cu->cbf.u, depth) || cbf_is_set(cur_cu->cbf.v, depth);

      if(cur_cu->merged && !cbf) {
        cur_cu->merged = 0;
        cur_cu->skipped = 1;
        // Selecting skip reduces bits needed to code the CU
        cur_cu->inter.bitcost--;
      }
      lcu_set_inter(&work_tree[depth], x, y, depth, cur_cu);
      lcu_set_coeff(&work_tree[depth], x, y, depth, cur_cu);
    }
  }
  if (cur_cu->type == CU_INTRA || cur_cu->type == CU_INTER) {
    cost = lcu_get_final_cost_luma(encoder_state, x, y, depth, &work_tree[depth]);
    cost += lcu_get_final_cost_chroma(encoder_state, x, y, depth, &work_tree[depth]);
    
    // Bitcost
    cost += (cur_cu->type == CU_INTER ? cur_cu->inter.bitcost : cur_cu->intra[PU_INDEX(x >> 2, y >> 2)].bitcost) * (int32_t)(encoder_state->global->cur_lambda_cost+0.5);
  }

  // Recursively split all the way to max search depth.
  if (depth < MAX_INTRA_SEARCH_DEPTH || depth < MAX_INTER_SEARCH_DEPTH) {
    int half_cu = cu_width / 2;
    int split_cost = (int)(4.5 * encoder_state->global->cur_lambda_cost);
    int cbf = cbf_is_set(cur_cu->cbf.y, depth) || cbf_is_set(cur_cu->cbf.u, depth) || cbf_is_set(cur_cu->cbf.v, depth);

    // If skip mode was selected for the block, skip further search.
    // Skip mode means there's no coefficients in the block, so splitting
    // might not give any better results but takes more time to do.
    if(cur_cu->type == CU_NOTSET || cbf) {
      split_cost += search_cu(encoder_state, x,           y,           depth + 1, work_tree);
      split_cost += search_cu(encoder_state, x + half_cu, y,           depth + 1, work_tree);
      split_cost += search_cu(encoder_state, x,           y + half_cu, depth + 1, work_tree);
      split_cost += search_cu(encoder_state, x + half_cu, y + half_cu, depth + 1, work_tree);
    } else {
      split_cost = INT_MAX;
    }
    if (split_cost < cost) {
      // Copy split modes to this depth.
      cost = split_cost;
      work_tree_copy_up(x, y, depth, work_tree);
    } else {
      // Copy this CU's mode all the way down for use in adjacent CUs mode
      // search.
      work_tree_copy_down(x, y, depth, work_tree);
    }
  }

  return cost;
}


/**
 * Initialize lcu_t for search.
 * - Copy reference CUs.
 * - Copy reference pixels from neighbouring LCUs.
 * - Copy reference pixels from this LCU.
 */
static void init_lcu_t(const encoder_state * const encoder_state, const int x, const int y, lcu_t *lcu, const yuv_t *hor_buf, const yuv_t *ver_buf)
{
  const videoframe * const frame = encoder_state->tile->frame;
  
  // Copy reference cu_info structs from neighbouring LCUs.
  {
    const int x_cu = x >> MAX_DEPTH;
    const int y_cu = y >> MAX_DEPTH;

    // Use top-left sub-cu of LCU as pointer to lcu->cu array to make things
    // simpler.
    cu_info *lcu_cu = &lcu->cu[LCU_CU_OFFSET];

    // Copy top CU row.
    if (y_cu > 0) {
      int i;
      for (i = 0; i < LCU_CU_WIDTH; ++i) {
        const cu_info *from_cu = videoframe_get_cu_const(frame, x_cu + i, y_cu - 1);
        cu_info *to_cu = &lcu_cu[i - LCU_T_CU_WIDTH];
        memcpy(to_cu, from_cu, sizeof(*to_cu));
      }
    }
    // Copy left CU column.
    if (x_cu > 0) {
      int i;
      for (i = 0; i < LCU_CU_WIDTH; ++i) {
        const cu_info *from_cu = videoframe_get_cu_const(frame, x_cu - 1, y_cu + i);
        cu_info *to_cu = &lcu_cu[-1 + i * LCU_T_CU_WIDTH];
        memcpy(to_cu, from_cu, sizeof(*to_cu));
      }
    }
    // Copy top-left CU.
    if (x_cu > 0 && y_cu > 0) {
      const cu_info *from_cu = videoframe_get_cu_const(frame, x_cu - 1, y_cu - 1);
      cu_info *to_cu = &lcu_cu[-1 - LCU_T_CU_WIDTH];
      memcpy(to_cu, from_cu, sizeof(*to_cu));
    }

    // Copy top-right CU.
    if (y_cu > 0 && x + LCU_WIDTH < frame->width) {
      const cu_info *from_cu = videoframe_get_cu_const(frame, x_cu + LCU_CU_WIDTH, y_cu - 1);
      cu_info *to_cu = &lcu->cu[LCU_T_CU_WIDTH*LCU_T_CU_WIDTH];
      memcpy(to_cu, from_cu, sizeof(*to_cu));
    }
  }

  // Copy reference pixels.
  {
    const int pic_width = frame->width;
    // Copy top reference pixels.
    if (y > 0) {
      // hor_buf is of size pic_width so there might not be LCU_REF_PX_WIDTH
      // number of allocated pixels left.
      int x_max = MIN(LCU_REF_PX_WIDTH, pic_width - x);
      int x_min_in_lcu = (x>0) ? 0 : 1;
      memcpy(&lcu->top_ref.y[x_min_in_lcu], &hor_buf->y[OFFSET_HOR_BUF(x, y, frame, x_min_in_lcu-1)], x_max + (1-x_min_in_lcu));
      memcpy(&lcu->top_ref.u[x_min_in_lcu], &hor_buf->u[OFFSET_HOR_BUF_C(x, y, frame, x_min_in_lcu-1)], x_max / 2 + (1-x_min_in_lcu));
      memcpy(&lcu->top_ref.v[x_min_in_lcu], &hor_buf->v[OFFSET_HOR_BUF_C(x, y, frame, x_min_in_lcu-1)], x_max / 2 + (1-x_min_in_lcu));
    }
    // Copy left reference pixels.
    if (x > 0) {
      int y_min_in_lcu = (y>0) ? 0 : 1;
      memcpy(&lcu->left_ref.y[y_min_in_lcu], &ver_buf->y[OFFSET_VER_BUF(x, y, frame, y_min_in_lcu-1)], LCU_WIDTH + (1-y_min_in_lcu));
      memcpy(&lcu->left_ref.u[y_min_in_lcu], &ver_buf->u[OFFSET_VER_BUF_C(x, y, frame, y_min_in_lcu-1)], LCU_WIDTH / 2 + (1-y_min_in_lcu));
      memcpy(&lcu->left_ref.v[y_min_in_lcu], &ver_buf->v[OFFSET_VER_BUF_C(x, y, frame, y_min_in_lcu-1)], LCU_WIDTH / 2 + (1-y_min_in_lcu));
    }
  }

  // Copy LCU pixels.
  {
    const videoframe * const frame = encoder_state->tile->frame;
    int x_max = MIN(x + LCU_WIDTH, frame->width) - x;
    int y_max = MIN(y + LCU_WIDTH, frame->height) - y;

    int x_c = x / 2;
    int y_c = y / 2;
    int x_max_c = x_max / 2;
    int y_max_c = y_max / 2;

    pixels_blit(&frame->source->y[x + y * frame->source->stride], lcu->ref.y,
                        x_max, y_max, frame->source->stride, LCU_WIDTH);
    pixels_blit(&frame->source->u[x_c + y_c * frame->source->stride/2], lcu->ref.u,
                        x_max_c, y_max_c, frame->source->stride/2, LCU_WIDTH / 2);
    pixels_blit(&frame->source->v[x_c + y_c * frame->source->stride/2], lcu->ref.v,
                        x_max_c, y_max_c, frame->source->stride/2, LCU_WIDTH / 2);
  }
}


/**
 * Copy CU and pixel data to it's place in picture datastructure.
 */
static void copy_lcu_to_cu_data(const encoder_state * const encoder_state, int x_px, int y_px, const lcu_t *lcu)
{
  // Copy non-reference CUs to picture.
  {
    const int x_cu = x_px >> MAX_DEPTH;
    const int y_cu = y_px >> MAX_DEPTH;
    videoframe * const frame = encoder_state->tile->frame;

    // Use top-left sub-cu of LCU as pointer to lcu->cu array to make things
    // simpler.
    const cu_info *const lcu_cu = &lcu->cu[LCU_CU_OFFSET];

    int x, y;
    for (y = 0; y < LCU_CU_WIDTH; ++y) {
      for (x = 0; x < LCU_CU_WIDTH; ++x) {
        const cu_info *from_cu = &lcu_cu[x + y * LCU_T_CU_WIDTH];
        cu_info *to_cu = videoframe_get_cu(frame, x_cu + x, y_cu + y);
        memcpy(to_cu, from_cu, sizeof(*to_cu));
      }
    }
  }

  // Copy pixels to picture.
  {
    videoframe * const pic = encoder_state->tile->frame;
    const int pic_width = pic->width;
    const int x_max = MIN(x_px + LCU_WIDTH, pic_width) - x_px;
    const int y_max = MIN(y_px + LCU_WIDTH, pic->height) - y_px;
    const int luma_index = x_px + y_px * pic_width;
    const int chroma_index = (x_px / 2) + (y_px / 2) * (pic_width / 2);

    pixels_blit(lcu->rec.y, &pic->rec->y[x_px + y_px * pic->rec->stride],
                        x_max, y_max, LCU_WIDTH, pic->rec->stride);
    coefficients_blit(lcu->coeff.y, &pic->coeff_y[luma_index],
                        x_max, y_max, LCU_WIDTH, pic_width);

    pixels_blit(lcu->rec.u, &pic->rec->u[(x_px / 2) + (y_px / 2) * (pic->rec->stride / 2)],
                        x_max / 2, y_max / 2, LCU_WIDTH / 2, pic->rec->stride / 2);
    pixels_blit(lcu->rec.v, &pic->rec->v[(x_px / 2) + (y_px / 2) * (pic->rec->stride / 2)],
                        x_max / 2, y_max / 2, LCU_WIDTH / 2, pic->rec->stride / 2);
    coefficients_blit(lcu->coeff.u, &pic->coeff_u[chroma_index],
                        x_max / 2, y_max / 2, LCU_WIDTH / 2, pic_width / 2);
    coefficients_blit(lcu->coeff.v, &pic->coeff_v[chroma_index],
                        x_max / 2, y_max / 2, LCU_WIDTH / 2, pic_width / 2);
  }
}


/**
 * Search LCU for modes.
 * - Best mode gets copied to current picture.
 */
void search_lcu(encoder_state * const encoder_state, const int x, const int y, const yuv_t * const hor_buf, const yuv_t * const ver_buf)
{
  lcu_t work_tree[MAX_PU_DEPTH + 1];
  int depth;
  // Initialize work tree.
  for (depth = 0; depth <= MAX_PU_DEPTH; ++depth) {
    memset(&work_tree[depth], 0, sizeof(work_tree[depth]));
    init_lcu_t(encoder_state, x, y, &work_tree[depth], hor_buf, ver_buf);
  }

  // Start search from depth 0.
  search_cu(encoder_state, x, y, 0, work_tree);

  copy_lcu_to_cu_data(encoder_state, x, y, &work_tree[0]);
}
