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

#include <string.h>
#include <stdlib.h>

#include "cu.h"
#include "threads.h"


/**
 * \brief Number of PUs in a CU.
 *
 * Indexed by part_mode_t values.
 */
const uint8_t kvz_part_mode_num_parts[] = {
  1, // 2Nx2N
  2, // 2NxN
  2, // Nx2N
  4, // NxN
  2, // 2NxnU
  2, // 2NxnD
  2, // nLx2N
  2, // nRx2N
};

/**
 * \brief PU offsets.
 *
 * Indexed by [part mode][PU number][axis].
 *
 * Units are 1/4 of the width of the CU.
 */
const uint8_t kvz_part_mode_offsets[][4][2] = {
  { {0, 0}                         }, // 2Nx2N
  { {0, 0}, {0, 2}                 }, // 2NxN
  { {0, 0}, {2, 0}                 }, // Nx2N
  { {0, 0}, {2, 0}, {0, 2}, {2, 2} }, // NxN
  { {0, 0}, {0, 1}                 }, // 2NxnU
  { {0, 0}, {0, 3}                 }, // 2NxnD
  { {0, 0}, {1, 0}                 }, // nLx2N
  { {0, 0}, {3, 0}                 }, // nRx2N
};

/**
 * \brief PU sizes.
 *
 * Indexed by [part mode][PU number][axis].
 *
 * Units are 1/4 of the width of the CU.
 */
const uint8_t kvz_part_mode_sizes[][4][2] = {
  { {4, 4}                         }, // 2Nx2N
  { {4, 2}, {4, 2}                 }, // 2NxN
  { {2, 4}, {2, 4}                 }, // Nx2N
  { {2, 2}, {2, 2}, {2, 2}, {2, 2} }, // NxN
  { {4, 1}, {4, 3}                 }, // 2NxnU
  { {4, 3}, {4, 1}                 }, // 2NxnD
  { {1, 4}, {3, 4}                 }, // nLx2N
  { {3, 4}, {1, 4}                 }, // nRx2N
};


cu_info_t* kvz_cu_array_at(cu_array_t *cua, unsigned x_px, unsigned y_px)
{
  return (cu_info_t*) kvz_cu_array_at_const(cua, x_px, y_px);
}


const cu_info_t* kvz_cu_array_at_const(const cu_array_t *cua, unsigned x_px, unsigned y_px)
{
  assert(x_px < cua->width);
  assert(y_px < cua->height);
  return &(cua)->data[(x_px >> 2) + (y_px >> 2) * ((cua)->width >> 2)];
}


/**
 * \brief Allocate a CU array.
 *
 * \param width   width of the array in luma pixels
 * \param height  height of the array in luma pixels
 */
cu_array_t * kvz_cu_array_alloc(const int width, const int height) {
  cu_array_t *cua = MALLOC(cu_array_t, 1);

  // Round up to a multiple of cell width and divide by cell width.
  const int width_scu  = (width  + 15) >> 2;
  const int height_scu = (height + 15) >> 2;
  assert(width_scu  * 16 >= width);
  assert(height_scu * 16 >= height);
  const unsigned cu_array_size = width_scu * height_scu;
  cua->data = calloc(cu_array_size, sizeof(cu_info_t));
  cua->width  = width_scu  << 2;
  cua->height = height_scu << 2;
  cua->refcount = 1;

  return cua;
}


int kvz_cu_array_free(cu_array_t * const cua)
{
  int32_t new_refcount;
  if (!cua) return 1;

  new_refcount = KVZ_ATOMIC_DEC(&(cua->refcount));
  //Still we have some references, do nothing
  if (new_refcount > 0) return 1;

  FREE_POINTER(cua->data);
  free(cua);

  return 1;
}


/**
 * \brief Copy part of a cu array to another cu array.
 *
 * All values are in luma pixels.
 *
 * \param dst     destination array
 * \param dst_x   x-coordinate of the left edge of the copied area in dst
 * \param dst_y   y-coordinate of the top edge of the copied area in dst
 * \param src     source array
 * \param src_x   x-coordinate of the left edge of the copied area in src
 * \param src_y   y-coordinate of the top edge of the copied area in src
 * \param width   width of the area to copy
 * \param height  height of the area to copy
 */
void kvz_cu_array_copy(cu_array_t* dst,       int dst_x, int dst_y,
                       const cu_array_t* src, int src_x, int src_y,
                       int width, int height)
{
  // Convert values from pixel coordinates to array indices.
  int src_stride = src->width >> 2;
  int dst_stride = dst->width >> 2;
  const cu_info_t* src_ptr = &src->data[(src_x >> 2) + (src_y >> 2) * src_stride];
  cu_info_t* dst_ptr       = &dst->data[(dst_x >> 2) + (dst_y >> 2) * dst_stride];

  // Number of bytes to copy per row.
  const size_t row_size = sizeof(cu_info_t) * (width >> 2);

  width = MIN(width,   MIN(src->width  - src_x, dst->width  - dst_x));
  height = MIN(height, MIN(src->height - src_y, dst->height - dst_y));

  assert(src_x + width  <= src->width);
  assert(src_y + height <= src->height);
  assert(dst_x + width  <= dst->width);
  assert(dst_y + height <= dst->height);

  for (int i = 0; i < (height >> 2); ++i) {
    memcpy(dst_ptr, src_ptr, row_size);
    src_ptr += src_stride;
    dst_ptr += dst_stride;
  }
}

/**
 * \brief Copy an lcu to a cu array.
 *
 * All values are in luma pixels.
 *
 * \param dst     destination array
 * \param dst_x   x-coordinate of the left edge of the copied area in dst
 * \param dst_y   y-coordinate of the top edge of the copied area in dst
 * \param src     source lcu
 */
void kvz_cu_array_copy_from_lcu(cu_array_t* dst, int dst_x, int dst_y, const lcu_t *src)
{
  const int dst_stride = dst->width >> 2;
  for (int y = 0; y < LCU_WIDTH; y += SCU_WIDTH) {
    for (int x = 0; x < LCU_WIDTH; x += SCU_WIDTH) {
      const cu_info_t *from_cu = LCU_GET_CU_AT_PX(src, x, y);
      const int x_scu = (dst_x + x) >> 2;
      const int y_scu = (dst_y + y) >> 2;
      cu_info_t *to_cu = &dst->data[x_scu + y_scu * dst_stride];
      memcpy(to_cu,                  from_cu, sizeof(*to_cu));
    }
  }
}

// ***********************************************
// Modified for SHVC.

// Adapted from shm
cu_array_t *kvz_cu_array_upsampling(cu_array_t *base_cua, int32_t nw_in_lcu, int32_t nh_in_lcu, int32_t * mv_scale, int32_t * cu_pos_scale)
{
  //Define a few basic things
  //TODO: Just use MAX_DEPTH or MAX_PU_DEPTH?    v----log2_min_luma_transform_block_size
  uint8_t max_depth = MAX_DEPTH + MAX(0,MIN_SIZE-2); //If chroma format is 422 and log2_min_luma_transform_block_size > 2 need to add 1 
  uint16_t num_partitions = 1 << (max_depth << 1); //Number of cu in a CTU/LCU
  uint8_t w_min_pu = LCU_WIDTH / (1 << max_depth); //SCU_WIDTH
  uint8_t h_min_pu = LCU_WIDTH / (1 << max_depth);
  uint8_t block_w = 16;
  uint8_t block_h = 16;
  int8_t part_w = MAX( 1,  (int8_t)(block_w/w_min_pu)); //width of LCU in parts
  //int8_t part_h = MAX( 1,  (int8_t)(block_h/h_min_pu)); //height --||--
  int8_t part_num = MAX( 1, (int8_t)( (block_w/w_min_pu) * (block_h/h_min_pu))); //Number of scu in a 16x16 block, e.g. how many scu to skip to get to the next 16x16 block
  uint16_t num_blocks = num_partitions / part_num; //Number of 16x16 blocks in lcu

  //Allocate the new cua. Use cu_pos_scale to calculate the new size
  uint32_t n_width = nw_in_lcu * LCU_WIDTH;
  uint32_t n_height = nh_in_lcu * LCU_WIDTH;
  cu_array_t *cua = kvz_cu_array_alloc( n_width, n_height);

//#define LCUIND2X(ind,stride) (((ind) * LCU_WIDTH) % (stride))
//#define LCUIND2Y(ind,lcu_stride) (((ind) * LCU_WIDTH) / (lcu_stride))

#define IND2X(ind,step,stride) (((ind) * (step)) % (stride))
#define IND2Y(ind,step,stride) (((ind) / (stride)) * (step))

  //Loop over LCUs/CTUs
  //uint32_t frame_lcu_stride = nw_in_lcu;
  uint32_t num_lcu_in_frame = nw_in_lcu * nh_in_lcu;
  for ( uint32_t lcu_ind = 0; lcu_ind < num_lcu_in_frame; lcu_ind++ ) {
    uint32_t lcu_x = IND2X(lcu_ind,LCU_WIDTH,n_width);//(lcu_ind * LCU_WIDTH) % cua->width;
    uint32_t lcu_y = IND2Y(lcu_ind,LCU_WIDTH,nw_in_lcu);//(lcu_ind * LCU_WIDTH) / frame_lcu_stride; 

    //Loop over 16x16 blocks of the LCU. TODO: Best way to loop over cu memory access wise?
    for ( uint32_t part_ind = 0; part_ind < num_blocks; part_ind++) {
      uint32_t block_x = lcu_x + IND2X(part_ind,block_w,LCU_WIDTH);
      uint32_t block_y = lcu_y + IND2Y(part_ind,block_h,part_w);
      cu_info_t *cu = kvz_cu_array_at(cua, block_x, block_y);

      //Get co-located cu. Use center of 16x16 block to find co-located cu. TODO: Account for offsets?     
      int32_t col_px_x = (int32_t)block_x; //Go from scu pos to pixel pos
      int32_t col_px_y = (int32_t)block_y;

      if ( cu_pos_scale[0] != POS_SCALE_FAC_1X || cu_pos_scale[1] != POS_SCALE_FAC_1X) {
        //Need to round here for some reason acording to shm.
        col_px_x = ((SCALE_POS_COORD(col_px_x + (int32_t)(block_w >> 1), cu_pos_scale[0]) + 4 ) >> 4 ) << 4;
        col_px_y = ((SCALE_POS_COORD(col_px_y + (int32_t)(block_h >> 1), cu_pos_scale[1]) + 4 ) >> 4 ) << 4;
      }
      
      const cu_info_t *col = NULL;
      //Check that col is inside the frame. TODO: Use actual pic size?
      if (col_px_x >= 0 && col_px_y >= 0 && col_px_x < base_cua->width && col_px_y < base_cua->height ) {
        col = kvz_cu_array_at_const(base_cua, col_px_x, col_px_y);
      }
      
      if (col != NULL) {
        //Copy stuff from col to cu
        memcpy(cu, col, sizeof(cu_info_t));

        //Scale mv
        if (!col->skipped && col->type == CU_INTER) {
          cu->inter.mv[0][0] = (uint16_t)SCALE_MV_COORD((uint32_t)col->inter.mv[0][0], mv_scale[0]);
          cu->inter.mv[0][1] = (uint16_t)SCALE_MV_COORD((uint32_t)col->inter.mv[0][1], mv_scale[1]);
          cu->inter.mv[1][0] = (uint16_t)SCALE_MV_COORD((uint32_t)col->inter.mv[1][0], mv_scale[0]);
          cu->inter.mv[1][1] = (uint16_t)SCALE_MV_COORD((uint32_t)col->inter.mv[1][1], mv_scale[1]);
        } else {
          cu->type = CU_INTRA;
        }
      } else {
        //Col is outside of the picture so set block to CU_INTRA
        cu->type = CU_INTRA;
      }

      if( cu->type == CU_INTRA) {
          //TODO: Need to do something for intra? Should not access inter data structures.

      }
      
      //Set Partision size to 2Nx2N for all cu in the 16x16 block
      cu->part_size = SIZE_2Nx2N;

      //Copy results to other cu in the 16x16 block.
      for ( uint32_t i = 1; i < part_num; i++) {
        uint32_t sub_x = block_x + IND2X(i,w_min_pu,block_w);
        uint32_t sub_y = block_y + IND2Y(i,h_min_pu,part_w);
        cu_info_t *sub_cu = kvz_cu_array_at(cua, sub_x, sub_y);
        memcpy(sub_cu,cu,sizeof(cu_info_t));
      }
    }
  }

#undef IND2X
#undef IND2Y

  return cua;
}
// ***********************************************
