/* $Id: fblckgen.c,v 1.5 2003/07/24 12:50:23 stix Exp stix $ */

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

#include <sys/time.h>
#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef USE_PTHREADS
#include <pthread.h>
#endif

#ifdef NEEDS_GETOPT
#include <getopt.h>
#endif

#include "common.h"

static char const rcsid[] = "$Id: fblckgen.c,v 1.5 2003/07/24 12:50:23 stix Exp stix $";

/* Prototypes */
static void	*makeBlocks(void *);
static void	cleanup(int);
static void	usage();

/* Globals */
static char *buf[2];
static int type;
static volatile int aborted;
static long blockSize;
static int64_t numBlocks;

#ifdef USE_PTHREADS
static pthread_mutex_t lock;
static pthread_cond_t less;
static pthread_cond_t more;
static volatile int nblocks;
#else
static int ctl1[2], ctl2[2];
#endif

int
main(int argc, char **argv)
{
	int c;
	int64_t numWritten;
	int64_t i;
	float duration;
	void *buffer;
	struct timeval tpstart, tpend;
#ifdef USE_PTHREADS
	pthread_t tid;
	pthread_attr_t attr;
#else
	pid_t pid;
#endif

	/* set defaults */
	blockSize = 512;
	numBlocks = 1024;
	type = ALPHADATA;

	while ((c = getopt(argc, argv, "arb:c:")) != EOF) {
		switch (c) {
		case 'a':
			type = ALPHADATA;
			break;
		case 'r':
			type = RANDDATA;
			break;
		case 'b':
			blockSize = getnum(optarg);
			break;
		case 'c':
			numBlocks = getnum(optarg);
			break;
		case '?':
			usage();
			exit(1);
		}
	}

	/* Double buffering, get twice the block size of memory */
#ifdef USE_PTHREADS
	/* Just use a normal malloc for threads */
	if ((buffer = malloc(blockSize * 2)) == NULL) {
		fprintf(stderr, "malloc for %ld bytes failed.\n",
		    blockSize * 2);
		exit(1);
	}
#else
	/* Shared memory for processes */
	buffer = getshm(blockSize * 2);
#endif
	buf[0] = buffer;
	buf[1] = (char *)buffer + blockSize;
	aborted = 0;
#ifdef USE_PTHREADS
	nblocks = 0;
	MYASSERT(pthread_mutex_init(&lock, NULL) == 0,
	    "pthread_mutex_init failed");
	MYASSERT(pthread_cond_init(&less, NULL) == 0,
	    "pthread_cond_init failed");
	MYASSERT(pthread_cond_init(&more, NULL) == 0,
	    "pthread_cond_init failed");
	MYASSERT(pthread_attr_init(&attr) == 0,
	    "pthread_attr_init failed");
	MYASSERT(pthread_attr_setdetachstate(&attr,
	    PTHREAD_CREATE_DETACHED) == 0,
	    "pthread_attr_setdetachstate failed");
	MYASSERT(pthread_create(&tid, &attr, &makeBlocks, NULL) == 0,
	    "pthread_create failed");
	MYASSERT(pthread_attr_destroy(&attr) == 0,
	    "pthread_attr_destroy failed");
#else
	MYASSERT(pipe(&ctl1[0]) == 0, "pipe failed");
	MYASSERT(pipe(&ctl2[0]) == 0, "pipe failed");
	if ((pid = fork()) == -1) {
		perror("fork failed");
		_exit(1);
	}
	if (pid == 0) {
		signal(SIGINT, SIG_IGN);
		makeBlocks(NULL);
		_exit(0);
	}
#endif
	signal(SIGINT, &cleanup);
	MYASSERT(gettimeofday(&tpstart, NULL) == 0, "gettimeofday failed");
	for (i = 0; !aborted && (numBlocks == 0 || i < numBlocks); i++) {
		/* wait for block of data */
#ifdef USE_PTHREADS
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		while (nblocks == 0)
			MYASSERT(pthread_cond_wait(&more, &lock) == 0,
			    "pthread_cond_wait failed");
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock failed");
#else
		MYASSERT(read(ctl1[0], &c, 1) == 1, "read on pipe failed");

		/* tell child to make another */
		MYASSERT(write(ctl2[1], &c, 1) == 1, "Write on pipe failed");
#endif
		/* write it */
		numWritten = write(1, buf[i & 1], blockSize);
		if (numWritten != blockSize) {
			if (numWritten > 0) {
				fprintf(stderr, "Short write: "
				    "%lld bytes: %s\n",
				    (int64_t)numWritten, strerror(errno));
			} else
				perror("Write failed");
			fprintf(stderr, "%lld bytes, %lld full blocks "
			    "written.\n",
			    (int64_t)i * blockSize +
			    (numWritten > 0) ? numWritten : 0,
			    (int64_t)i);
			break;
		}
#ifdef USE_PTHREADS
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		nblocks--;
		MYASSERT(pthread_cond_signal(&less) == 0,
		    "pthread_cond_signal failed");
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock failed");
#endif
		
	}
	MYASSERT(gettimeofday(&tpend, NULL) == 0, "gettimeofday failed");
	duration = tpend.tv_sec + tpend.tv_usec / 1000000.0 -
	    tpstart.tv_sec - tpstart.tv_usec / 1000000.0;
	fprintf(stderr, "%lld bytes written in %.3f secs (%.3f KB/sec)\n",
	    (int64_t)i * blockSize,
	    duration, (float)i * blockSize / duration / 1024.0);
	if (aborted)
#ifdef USE_PTHREADS
		MYASSERT(pthread_cancel(tid) == 0, "pthread_cancel failed");
#else
		kill(pid, SIGTERM);
#endif

	return 0;
}

static void *
makeBlocks(void *dummy)
{
#ifndef USE_PTHREADS
	char c;
#endif
	int64_t i;

	SRAND(time(NULL));
	for (i = 0; numBlocks == 0 || i < numBlocks; i++) {
#ifdef USE_PTHREADS
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		while (nblocks > 1)
			MYASSERT(pthread_cond_wait(&less, &lock) == 0,
			    "pthread_cond_wait failed");
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock");
#endif
		initblock(buf[i & 1], blockSize, type, i);
#ifdef USE_PTHREADS
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		nblocks++;
		MYASSERT(pthread_cond_signal(&more) == 0,
		    "pthread_cond_signal failed");
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock");
#else
		MYASSERT(write(ctl1[1], &c, 1) == 1, "Write on pipe failed");
		MYASSERT(read(ctl2[0], &c, 1) == 1, "Read on pipe failed");
#endif
	}
	return NULL;
}

static void
cleanup(int sig)
{
	aborted = 1;
}

static void
usage()
{
	fprintf(stderr, "fblckgen version " VERSION ".\n"
	    "Copyright Paul Ripke $Date: 2003/07/24 12:50:23 $\n");
#ifdef USE_PTHREADS
	fprintf(stderr, "Built to use pthreads.\n\n");
#else   
	fprintf(stderr, "Built to use multiple processes.\n\n");
#endif  
	fprintf(stderr, "Usage: fblckgen [-a | -r] [-b bytes] "
	    "[-c count]\n\n");
	fprintf(stderr, "  -a          Write blocks of a repeating ASCII "
	    "string (compresses well)\n");
	fprintf(stderr, "  -r          Write blocks of binary 'random' data "
	    "(shouldn't compress)\n");
	fprintf(stderr, "  -b bytes    Set write blocksize\n");
	fprintf(stderr, "  -c count    Number of blocks to write "
	    "(zero for infinite)\n\n");
	fprintf(stderr, "Compiled defaults:\n");
	fprintf(stderr, "    fblckgen -a -b 1s -c 1k\n\n");
	fprintf(stderr, "Numeric arguments take an optional "
	    "letter multiplier:\n");
	fprintf(stderr, "  s:        Sectors (x 512)\n");
	fprintf(stderr, "  k:        kibi (x 1024 or 2^10)\n");
	fprintf(stderr, "  m:        mebi (x 1048576 or 2^20)\n");
	fprintf(stderr, "  g:        gibi (x 2^30)\n");
	fprintf(stderr, "  t:        tebi (x 2^40)\n");
	fprintf(stderr, "  p:        pebi (x 2^50)\n");
	fprintf(stderr, "  e:        exbi (x 2^60)\n");
}

