#ifndef _PICTURE_X86_ASM_SAD_H_
#define _PICTURE_X86_ASM_SAD_H_
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

 /*! \file picture-x86-asm-sad.h
    \brief assembly functions header for sad
*/

unsigned kvz_sad_4x4_avx(const pixel*, const pixel*);
unsigned kvz_sad_8x8_avx(const pixel*, const pixel*);
unsigned kvz_sad_16x16_avx(const pixel*, const pixel*);

unsigned kvz_sad_4x4_stride_avx(const pixel *data1, const pixel *data2, unsigned stride);
unsigned kvz_sad_8x8_stride_avx(const pixel *data1, const pixel *data2, unsigned stride);
unsigned kvz_sad_16x16_stride_avx(const pixel *data1, const pixel *data2, unsigned stride);


#endif
