/* $Id: common.h,v 1.3 2003/07/23 14:51:39 stix Exp stix $ */

/*
 * Copyright (c) 2003 Paul Ripke. All rights reserved.
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

#ifndef COMMON_H
#define COMMON_H 1

#if HAVE_CONFIG_H
#include "config.h"
#endif

#if !HAVE_BZERO && HAVE_MEMSET
#define bzero(p, n) ((void) memset((p), 0, (n)))
#endif

/*
 * Custom assert which won't be disabled with "NDEBUG".
 */
#define MYASSERT(expr, err)				\
do {							\
	if (!(expr)) {					\
		flAborted = 1;				\
		perror(err);				\
		_exit(1);				\
	}						\
} while (/* CONSTCOND */ 0)

typedef enum { ALPHADATA, RANDDATA } dataType;

/*
 * Need fast random numbers, not necessarily good ones
 * Only used to generate "random" data. Good enough that tape
 * drives, compress, gzip and bzip2 achieve near 0% compression.
 */
#define	SRAND(x) { __seed = x; }
#define	RAND() (__seed = __seed * 1103515245L + 12345L)
unsigned long __seed;

/* Prototypes */
int64_t	getnum(char *);
void	initblock(char *, long, dataType, int64_t);
void	*getshm(long size);

#endif /* !COMMON_H */
