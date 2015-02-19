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

#include <string.h>
#include <stdlib.h>

#include "cu.h"
#include "threads.h"


void coefficients_blit(const coefficient * const orig, coefficient * const dst,
                         const unsigned width, const unsigned height,
                         const unsigned orig_stride, const unsigned dst_stride)
{
  unsigned y;

  for (y = 0; y < height; ++y) {
    memcpy(&dst[y*dst_stride], &orig[y*orig_stride], width * sizeof(coefficient));
  }
}

unsigned coefficients_calc_abs(const coefficient *const buf, const int buf_stride,
                        const int width)
{
  int sum = 0;
  int y, x;

  for (y = 0; y < width; ++y) {
    for (x = 0; x < width; ++x) {
      sum += abs(buf[x + y * buf_stride]);
    }
  }

  return sum;
}

cu_array * cu_array_alloc(const int width_in_scu, const int height_in_scu) {
  unsigned cu_array_size = height_in_scu * width_in_scu;
  cu_array *cua;
  cua = MALLOC(cu_array, 1);
  cua->data = (cu_info*)malloc(sizeof(cu_info) * cu_array_size);
  cua->refcount = 1;
  FILL_ARRAY(cua->data, 0, cu_array_size);
  return cua;
}

int cu_array_free(cu_array * const cua)
{
  int32_t new_refcount;
  if (!cua) return 1;
  
  new_refcount = ATOMIC_DEC(&(cua->refcount));
  //Still we have some references, do nothing
  if (new_refcount > 0) return 1;
  
  FREE_POINTER(cua->data);
  free(cua);

  return 1;
}
