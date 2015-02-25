/*****************************************************************************
 * This file is part of Kvazaar HEVC encoder.
 *
 * Copyright (C) 2013-2015 Tampere University of Technology and others (see
 * COPYING file).
 *
 * Kvazaar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Kvazaar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Kvazaar.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

#include "test_strategies.h"

#include "src/strategyselector.h"


strategy_list strategies;


void init_test_strategies()
{
  strategies.allocated = 0;
  strategies.count = 0;
  strategies.strategies = NULL;

  // Init strategyselector because it sets hardware flags.
  strategyselector_init(1);

  // Collect all strategies to be tested.
  if (!strategy_register_picture(&strategies)) {
    fprintf(stderr, "strategy_register_picture failed!\n");
    return;
  }

  if (!strategy_register_dct(&strategies)) {
    fprintf(stderr, "strategy_register_partial_butterfly failed!\n");
    return;
  }
}
