#ifndef ENCODER_STATE_GEOMETRY_H_
#define ENCODER_STATE_GEOMETRY_H_
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

#include "global.h"


// Forward declare because including the header would lead  to a cyclic
// dependency.
struct encoder_control;
struct encoder_state;


int lcu_at_slice_start(const struct  encoder_control * const encoder, int lcu_addr_in_ts);
int lcu_at_slice_end(const struct  encoder_control * const encoder, int lcu_addr_in_ts);
int lcu_at_tile_start(const struct  encoder_control * const encoder, int lcu_addr_in_ts);
int lcu_at_tile_end(const struct  encoder_control * const encoder, int lcu_addr_in_ts);
int lcu_in_first_row(const struct encoder_state * const encoder_state, int lcu_addr_in_ts);
int lcu_in_last_row(const struct encoder_state * const encoder_state, int lcu_addr_in_ts);
int lcu_in_first_column(const struct encoder_state * const encoder_state, int lcu_addr_in_ts);
int lcu_in_last_column(const struct encoder_state * const encoder_state, int lcu_addr_in_ts);


#endif // ENCODER_STATE_GEOMETRY_H_
