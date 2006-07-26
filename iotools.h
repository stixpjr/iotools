/* $Id$ */

/*
 * Copyright (c) 2004 Paul Ripke. All rights reserved.
 *
 *  This software is distributed under the so-called ``revised Berkeley
 *  License'':
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * This software is provided ``as is'', and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the author be liable for any direct, indirect,
 * incidental, special, exemplary, or consequential damages (including,
 * but not limited to, procurement of substitute goods or services;
 * loss of use, data, or profits; or business interruption) however
 * caused and on any theory of liability, whether in contract, strict
 * liability, or tort (including negligence or otherwise) arising in
 * any way out of the use of this software, even if advised of the
 * possibility of such damage.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef USE_SYSVSHM
#include <sys/ipc.h>
#include <sys/shm.h>
#else
#include <sys/mman.h>
#endif /* USE_SYSVSHM */

#include <ctype.h>

#if HAVE_ERRNO_H
# include <errno.h>
#endif
#ifndef errno
extern int errno;
#endif

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>

#if STDC_HEADERS
# include <stdlib.h>
# include <string.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif

#ifdef HAVE_STRING_H
# if !STDC_HEADERS && HAVE_MEMORY_H
#  include <memory.h>
# endif
# include <string.h>
#endif

#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#endif

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef USE_PTHREADS
# include <pthread.h>
#endif

#ifdef HAVE_GETOPT_H
# include <getopt.h>
#endif

#ifndef HAVE_OPTARG_D
extern char	*optarg;
#endif

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#ifndef HAVE_INT64_T
# ifdef HAVE_LONG_LONG
typedef long long int64_t;
# else
typedef long int64_t;
# endif
#endif
