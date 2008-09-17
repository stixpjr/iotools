/* $Id: fblckgen.c,v 1.10 2008/06/17 10:37:59 stix Exp $ */

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

#include "iotools.h"
#include "common.h"

static char const rcsid[] = "$Id: fblckgen.c,v 1.10 2008/06/17 10:37:59 stix Exp $";

/* Prototypes */
static void	*makeBlocks(void *);
static void	*status(void *);
static void	cleanup(int);
static void	usage();

/* Globals */
static char *buf[2];
static int type;
static int flAborted;
static int flFinished;
static long blockSize;
static int64_t numBlocks;
static int64_t blocksWritten;
static int outfd;

#ifdef USE_PTHREADS
static pthread_mutex_t lock;
static pthread_cond_t less;
static pthread_cond_t more;
static int nblocks;
#else
static int ctl1[2], ctl2[2];
#endif

int
main(int argc, char **argv)
{
	int c;
	int flQuiet;
	int flVerbose;
	int64_t numWritten;
	float duration;
	void *buffer;
	struct timeval tpstart, tpend;
#ifdef USE_PTHREADS
	pthread_t tid;
	pthread_attr_t attr;
	pthread_t status_tid;
#else
	pid_t pid;
#endif
	/* close off stdout, so stdio doesn't play with it */
	outfd = dup(STDOUT_FILENO);
	if (outfd < 0) {
		perror("dup on stdout failed");
		exit(1);
	}
	close(STDOUT_FILENO);

	/* set defaults */
	blockSize = 512;
	numBlocks = 1024;
	type = ALPHADATA;
	flQuiet = 0;
	flVerbose = 0;

	while ((c = getopt(argc, argv, "arb:c:qv")) != EOF) {
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
		case 'q':
			flQuiet = 1;
			break;
		case 'v':
			flVerbose = 1;
			break;
		case '?':
		default:
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
	flAborted = 0;
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
	if (flVerbose) {
		MYASSERT(pthread_create(&status_tid, NULL, &status, NULL) == 0,
		    "pthread_create failed");
	}
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
	for (blocksWritten = 0; !flAborted &&
	    (numBlocks == 0 || blocksWritten < numBlocks); blocksWritten++) {
		/* tell child to make another */
		/* and wait for block of data */
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

		MYASSERT(write(ctl2[1], &c, 1) == 1, "Write on pipe failed");
#endif
		/* write it */
		numWritten = write(outfd, buf[blocksWritten & 1], blockSize);
		if (numWritten != blockSize) {
			if (numWritten > 0) {
				fprintf(stderr, "Short write: "
				    "%" PRId64 " bytes\n", (int64_t)numWritten);
			} else
				perror("Write failed");
			break;
		}
#ifndef USE_PTHREADS
		if (flVerbose)
			statusLine(blocksWritten * blockSize / 1024,
				numBlocks * blockSize / 1024,
				"KiB", "KiB/s");
#endif
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
	flFinished = 1;
#ifdef USE_PTHREADS
	if (flVerbose)
		pthread_join(status_tid, NULL);
#endif
	if (flAborted)
		fprintf(stderr, "Transfer aborted.\n");
	if (!flQuiet) {
		duration = tpend.tv_sec - tpstart.tv_sec +
		    (tpend.tv_usec - tpstart.tv_usec) / 1000000.0;
		fprintf(stderr, "%" PRId64
		    " bytes written in %.3f secs (%.3f KiB/sec)\n",
		    (int64_t)blocksWritten * blockSize,
		    duration, (float)blocksWritten * blockSize / duration / 1024.0);
	}
	if (flAborted)
#ifdef USE_PTHREADS
		pthread_cond_signal(&less);
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
	for (i = 0; (numBlocks == 0 || i < numBlocks) && !flAborted; i++) {
#ifdef USE_PTHREADS
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		while (nblocks > 1 && !flAborted)
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

static void *
status(void *dummy)
{
	while (!flAborted && !flFinished) {
		statusLine(blocksWritten * blockSize / 1024, numBlocks * blockSize / 1024,
		    "KiB", "KiB/s");
		usleep(STATUS_UPDATE_TIME);
	}
	fputc('\n', stderr);
	return 0;
}

static void
cleanup(int sig)
{
	flAborted = 1;
}

static void
usage()
{
	fprintf(stderr, "fblckgen version " PACKAGE_VERSION ".\n"
	    "Copyright Paul Ripke $Date: 2008/06/17 10:37:59 $\n");
#ifdef USE_PTHREADS
	fprintf(stderr, "Built to use pthreads.\n\n");
#else   
	fprintf(stderr, "Built to use multiple processes.\n\n");
#endif  
	fprintf(stderr, "Usage: fblckgen [-a | -r] [-q] [-v] [-b bytes] "
	    "[-c count]\n\n");
	fprintf(stderr, "  -a          Write blocks of a repeating ASCII "
	    "string (compresses well)\n");
	fprintf(stderr, "  -r          Write blocks of binary 'random' data "
	    "(shouldn't compress)\n");
	fprintf(stderr, "  -b bytes    Set write blocksize\n");
	fprintf(stderr, "  -c count    Number of blocks to write "
	    "(zero for infinite)\n");
	fprintf(stderr, "  -q          Quiet operation\n");
	fprintf(stderr, "  -v          Display progress line\n\n");
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
