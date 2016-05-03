#ifndef THREADS_H_
#define THREADS_H_
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

/**
 * \ingroup Threading
 * \file
 * Abstractions for operating system specific stuff.
 */

#include "global.h" // IWYU pragma: keep

#include <pthread.h>
#include <stdio.h>

#define E3 1000
#define E9 1000000000
#define FILETIME_TO_EPOCH 0x19DB1DED53E8000LL

#if defined(__GNUC__) && !defined(__MINGW32__) 
#include <unistd.h> // IWYU pragma: export
#include <time.h> // IWYU pragma: export

#define KVZ_CLOCK_T struct timespec

#ifdef __MACH__
// Workaround Mac OS not having clock_gettime.
#include <mach/clock.h> // IWYU pragma: export
#include <mach/mach.h> // IWYU pragma: export
#define KVZ_GET_TIME(clock_t) { \
  clock_serv_t cclock; \
  mach_timespec_t mts; \
  host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock); \
  clock_get_time(cclock, &mts); \
  mach_port_deallocate(mach_task_self(), cclock); \
  (clock_t)->tv_sec = mts.tv_sec; \
  (clock_t)->tv_nsec = mts.tv_nsec; \
}
#else
#define KVZ_GET_TIME(clock_t) { clock_gettime(CLOCK_MONOTONIC, (clock_t)); }
#endif

#define KVZ_CLOCK_T_AS_DOUBLE(ts) ((double)((ts).tv_sec) + (double)((ts).tv_nsec) / 1e9)
#define KVZ_CLOCK_T_DIFF(start, stop) ((double)((stop).tv_sec - (start).tv_sec) + (double)((stop).tv_nsec - (start).tv_nsec) / 1e9)

static INLINE struct timespec * ms_from_now_timespec(struct timespec * result, int wait_ms)
{
  KVZ_GET_TIME(result);
  int64_t secs = result->tv_sec + wait_ms / E3;
  int64_t nsecs = result->tv_nsec + (wait_ms % E3) * (E9 / E3);
  
  if (nsecs >= E9) {
    secs += 1;
    nsecs -= E9;
  }
  
  result->tv_sec = secs;
  result->tv_nsec = nsecs;

  return result;
}

#define KVZ_ATOMIC_INC(ptr)                     __sync_add_and_fetch((volatile int32_t*)ptr, 1)
#define KVZ_ATOMIC_DEC(ptr)                     __sync_add_and_fetch((volatile int32_t*)ptr, -1)

#else //__GNUC__
//TODO: we assume !GCC => Windows... this may be bad
#include <windows.h> // IWYU pragma: export

#define KVZ_CLOCK_T struct _FILETIME
#define KVZ_GET_TIME(clock_t) { GetSystemTimeAsFileTime(clock_t); }
// _FILETIME has 32bit low and high part of 64bit 100ns resolution timestamp (since 12:00 AM January 1, 1601)
#define KVZ_CLOCK_T_AS_DOUBLE(ts) ((double)(((uint64_t)(ts).dwHighDateTime)<<32 | (uint64_t)(ts).dwLowDateTime) / 1e7)
#define KVZ_CLOCK_T_DIFF(start, stop) ((double)((((uint64_t)(stop).dwHighDateTime)<<32 | (uint64_t)(stop).dwLowDateTime) - \
                                  (((uint64_t)(start).dwHighDateTime)<<32 | (uint64_t)(start).dwLowDateTime)) / 1e7)

static INLINE struct timespec * ms_from_now_timespec(struct timespec * result, int wait_ms)
{
  KVZ_CLOCK_T now;
  KVZ_GET_TIME(&now);

  int64_t moment_100ns = (int64_t)now.dwHighDateTime << 32 | (int64_t)now.dwLowDateTime;
  moment_100ns -= (int64_t)FILETIME_TO_EPOCH;
   
  int64_t secs = moment_100ns / (E9 / 100) + (wait_ms / E3);
  int64_t nsecs = (moment_100ns % (E9 / 100))*100 + ((wait_ms % E3) * (E9 / E3));
  
  if (nsecs >= E9) {
    secs += 1;
    nsecs -= E9;
  }

  result->tv_sec = secs;
  result->tv_nsec = nsecs;

  return result;
}

#define KVZ_ATOMIC_INC(ptr)                     InterlockedIncrement((volatile LONG*)ptr)
#define KVZ_ATOMIC_DEC(ptr)                     InterlockedDecrement((volatile LONG*)ptr)

#endif //__GNUC__

#undef E9
#undef E3

static INLINE int kvz_mutex_lock(pthread_mutex_t *mutex)
{
  int ret = pthread_mutex_lock(mutex);
  if (ret) {
    fprintf(stderr, "pthread_mutex_lock failed!\n");
    assert(0);
  }
  return ret;
}

static INLINE int kvz_mutex_unlock(pthread_mutex_t *mutex)
{
  int ret = pthread_mutex_unlock(mutex);
  if (ret) {
    fprintf(stderr, "pthread_mutex_unlock failed!\n");
    assert(0);
  }
  return ret;
}

#endif //THREADS_H_
