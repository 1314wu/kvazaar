#ifndef THREADS_H_
#define THREADS_H_
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

#include <pthread.h>

#ifdef __GNUC__
#include <unistd.h>

#ifdef _DEBUG
#include <time.h>
#define CLOCK_T struct timespec
#define GET_TIME(clock_t) clock_gettime(CLOCK_MONOTONIC, (clock_t))
#define CLOCK_T_AS_DOUBLE(ts) ((double)((ts).tv_sec) + (double)((ts).tv_nsec) / (double)1000000000L)
#define CLOCK_T_DIFF(start, stop) ((double)((stop).tv_sec - (start).tv_sec) + (double)((stop).tv_nsec - (start).tv_nsec) / (double)1000000000L)
#endif

#define ATOMIC_INC(ptr)                     __sync_add_and_fetch((volatile int32_t*)ptr, 1)
#define ATOMIC_DEC(ptr)                     __sync_add_and_fetch((volatile int32_t*)ptr, -1)
#define SLEEP()                             usleep(0)

#else //__GNUC__
//TODO: we assume !GCC => Windows... this may be bad
#include <Windows.h>

#ifdef _DEBUG
#define CLOCK_T struct _FILETIME
#define GET_TIME(clock_t) GetSystemTimeAsFileTime(clock_t)
// _FILETIME has 32bit low and high part of 64bit 100ns resolution timestamp (since 12:00 AM January 1, 1601)
#define CLOCK_T_AS_DOUBLE(ts) ((double)(((uint64_t)(ts).dwHighDateTime)<<32 | (uint64_t)(ts).dwLowDateTime) / (double)10000000L)
#define CLOCK_T_DIFF(start, stop) ((double)((((uint64_t)(stop).dwHighDateTime)<<32 | (uint64_t)(stop).dwLowDateTime) - \
                                  (((uint64_t)(start).dwHighDateTime)<<32 | (uint64_t)(start).dwLowDateTime)) / (double)10000000L)
#endif

#define ATOMIC_INC(ptr)                     InterlockedIncrement((volatile LONG*)ptr)
#define ATOMIC_DEC(ptr)                     InterlockedDecrement((volatile LONG*)ptr)
#define SLEEP()                             Sleep(0)

#endif //__GNUC__

#endif //THREADS_H_
