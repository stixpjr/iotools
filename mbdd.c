/* $Id: mbdd.c,v 1.5 2008/09/17 10:17:19 stix Exp $ */

/*
 * Copyright (c) 2006 Paul Ripke. All rights reserved.
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

#ifndef USE_PTHREADS
#error "pthreads required!"
#endif

static char const rcsid[] = "$Id: mbdd.c,v 1.5 2008/09/17 10:17:19 stix Exp $";

/* Prototypes */
static void	*reader(void *);
static void	*status(void *);
static void	cleanup(int);
static void	usage();

/* Globals */
static char **buf;
static long bufSize, partialReads;
static int flAborted, flFinished, numBufs;
static size_t remainder;
static int64_t totalWritten;

static pthread_mutex_t lock;
static pthread_cond_t less;
static pthread_cond_t more;
static int fullBufs;

int
main(int argc, char **argv)
{
	int bufNum, c, i, outfd, flQuiet;
	ssize_t numWrite, writeSize;
	unsigned long bufSum, bufSamples;
	float duration;
	struct timeval tpstart, tpend;
	pthread_t tid;
	pthread_attr_t attr;
	pthread_t status_tid;
	int flVerbose = 0;

	/* close off stdout, so stdio doesn't play with it */
	outfd = dup(STDOUT_FILENO);
	if (outfd < 0) {
		perror("dup on stdout failed");
		exit(1);
	}
	close(STDOUT_FILENO);

	/* set defaults */
	bufSize = 64*1024;
	numBufs = 16;
	flQuiet = 0;

	while ((c = getopt(argc, argv, "b:n:qv")) != EOF) {
		switch (c) {
		case 'b':
			bufSize = getnum(optarg);
			break;
		case 'n':
			numBufs = getnum(optarg);
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
	if (numBufs < 2) {
		fprintf(stderr, "Buffer count must be > 2\n");
		exit(1);
	}

	/* Allocate the number of buffers */

	if ((buf = (char **)malloc(numBufs * sizeof(char *))) == NULL) {
		fprintf(stderr, "malloc for %lu bytes failed.\n",
		    (unsigned long)numBufs * sizeof(char *));
		exit(1);
	}
	for (i = 0; i < numBufs; i++)
		if ((buf[i] = (char *)malloc(bufSize)) == NULL) {
			fprintf(stderr, "malloc for %lu byte buffer failed.\n"
				"Successfully allocated %d buffers, "
				"%lu bytes\n",
			        bufSize, i, bufSize * i);
			exit(1);
		}
	flAborted = 0;
	bufNum = 0;
	totalWritten = 0;
	flFinished = 0;
	remainder = 0;
	bufSum = 0;
	bufSamples = 0;
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
	MYASSERT(pthread_create(&tid, &attr, &reader, NULL) == 0,
	    "pthread_create failed");
	MYASSERT(pthread_attr_destroy(&attr) == 0,
	    "pthread_attr_destroy failed");
	if (flVerbose) {
		MYASSERT(pthread_create(&status_tid, NULL,
			&status, NULL) == 0,
			"pthread_create failed");
	}
	signal(SIGINT, &cleanup);
	MYASSERT(gettimeofday(&tpstart, NULL) == 0, "gettimeofday failed");
	while (!flAborted && !(flFinished && fullBufs == 0)) {
		/* wait for block of data */
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		while (fullBufs == 0 && !flAborted) {
			MYASSERT(pthread_cond_wait(&more, &lock) == 0,
			    "pthread_cond_wait failed");
		}
		if (flAborted)
			break;
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock failed");
		/* write it */
		if (flFinished && fullBufs == 1)
			/* write the remainder, partial block */
			writeSize = remainder;
		else
			writeSize = bufSize;
		numWrite = write(outfd, buf[bufNum], writeSize);
		if (numWrite != writeSize) {
			if (numWrite != -1) {
				fprintf(stderr, "Short write: "
				    "%" PRId64 " bytes.\n",
				    (int64_t)numWrite);
				flAborted = 1;
				totalWritten += numWrite;
			} else
				perror("Write failed");
			break;
		}
		totalWritten += numWrite;
		if (++bufNum >= numBufs)
			bufNum = 0;
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		fullBufs--;
		bufSum += fullBufs;
		MYASSERT(pthread_cond_signal(&less) == 0,
		    "pthread_cond_signal failed");
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock failed");
		bufSamples++;
	}
	MYASSERT(gettimeofday(&tpend, NULL) == 0, "gettimeofday failed");
	duration = tpend.tv_sec + tpend.tv_usec / 1000000.0 -
	    tpstart.tv_sec - tpstart.tv_usec / 1000000.0;
	if (flVerbose)
		pthread_join(status_tid, NULL);
	if (flAborted)
		fprintf(stderr, "Transfer aborted.\n");
	if (!flQuiet) {
		fprintf(stderr, "%" PRId64
		    " bytes transferred in %.3f secs (%.3f KiB/sec)\n"
		    "%ld partial read%s, %.3f average buffers full\n",
		    totalWritten, duration,
		    duration > 0 ?
		      (float)totalWritten / duration / 1024.0 : 0.0,
		    partialReads, partialReads != 1 ? "s" : "",
		    bufSamples > 0 ? (float)bufSum / bufSamples : 0.0);
	}
	if (flAborted)
		pthread_cancel(tid);

	return 0;
}

static void *
reader(void *dummy)
{
	ssize_t numRead, totalRead;
	int bufNum;

	bufNum = 0;
	partialReads = 0;
	while (!flAborted && !flFinished) {
		totalRead = 0;
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		while (fullBufs == numBufs)
			MYASSERT(pthread_cond_wait(&less, &lock) == 0,
			    "pthread_cond_wait failed");
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock");
		do {
			numRead = read(STDIN_FILENO, &(buf[bufNum][totalRead]),
					bufSize - totalRead);
			if (numRead == -1) {
				switch (errno) {
				case EAGAIN:
				case EINTR:
					continue;
				case EBADF:
				case EFAULT:
				case EINVAL:
				default:
					perror("Read failed");
					flAborted = 1;
					pthread_cond_signal(&more);
					return NULL;
				}
			}
			if (numRead == 0) {
				flFinished = 1;
				remainder = totalRead;
				break;
			}
			if (numRead != bufSize)
				partialReads++;
			totalRead += numRead;
		} while (totalRead != bufSize);
		if (++bufNum >= numBufs)
			bufNum = 0;
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		fullBufs++;
		MYASSERT(pthread_cond_signal(&more) == 0,
		    "pthread_cond_signal failed");
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock");
	}
	return NULL;
}

static void *
status(void *dummy)
{
	while (!flAborted && !flFinished) {
		statusLine(totalWritten / 1024, 0, "KiB", "KiB/s");
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
	fprintf(stderr, "mbdd version " PACKAGE_VERSION ".\n"
	    "Copyright Paul Ripke $Date: 2008/09/17 10:17:19 $\n");
	fprintf(stderr, "Multi-buffer dd\n\n");
	fprintf(stderr, "Built to use pthreads.\n\n");
	fprintf(stderr, "Usage: mbdd [-b bytes] [-n number] [-q]\n\n");
	fprintf(stderr, "  -b bytes    Set buffer size\n");
	fprintf(stderr, "  -n number   Number of buffers\n");
	fprintf(stderr, "  -q          Quiet operation\n");
	fprintf(stderr, "  -v          Display progress line\n\n");
	fprintf(stderr, "Compiled defaults:\n");
	fprintf(stderr, "    mbdd -b 64k -n 16\n\n");
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
