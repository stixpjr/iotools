/* $Id: mbdd.c,v 1.12 2013/02/21 11:09:24 stix Exp $ */

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

static char const rcsid[] = "$Id: mbdd.c,v 1.12 2013/02/21 11:09:24 stix Exp $";

/* Prototypes */
static void	*reader(void *);
static void	*writer(void *);
static void	*status(void *);
static void	cleanup(int);
static void	usage();

/* Globals */
static char **buf;
static long bufSize, partialReads, maxBlocks, numBlocks;
static int flAborted, flFinished, numBufs;
static size_t remainder;
static int destCount;
static char **destNames;
static int64_t *totalWritten;
static unsigned long *bufSum, *bufSamples;
static int *outfds;

static pthread_mutex_t lock;
static pthread_cond_t less;
static pthread_cond_t *more;
static int *fullBufs;

int
main(int argc, char **argv)
{
	int c, i, flQuiet, stdoutcopy;
	double duration;
	struct timeval tpstart, tpend;
	pthread_t reader_tid;
	pthread_t *writer_tids;
	pthread_attr_t attr;
	pthread_t status_tid;
	int flVerbose = 0;

	/* close off stdout, so stdio doesn't play with it */
	stdoutcopy = dup(STDOUT_FILENO);
	if (stdoutcopy < 0) {
		perror("dup on stdout failed");
		exit(1);
	}
	close(STDOUT_FILENO);

	/* set defaults */
	bufSize = 64*1024;
	numBufs = 16;
	flQuiet = 0;
	destCount = 1;
	maxBlocks = 0;

	while ((c = getopt(argc, argv, "b:c:n:qsv")) != EOF) {
		switch (c) {
		case 'b':
			bufSize = getnum(optarg);
			break;
		case 'c':
			maxBlocks = getnum(optarg);
			break;
		case 'n':
			numBufs = getnum(optarg);
			break;
		case 'q':
			flQuiet = 1;
			break;
		case 's':
			destCount = 0;
			close(stdoutcopy);
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

	if (maxBlocks < 0) {
		fprintf(stderr, "Block count must be >= 0\n");
		exit(1);
	}

	if (numBufs < 2) {
		fprintf(stderr, "Buffer count must be > 1\n");
		exit(1);
	}

	/* Open additional destinations */

	argv += optind;
	argc -= optind;
	if (destCount + argc <= 0) {
		fprintf(stderr, "No destinations specified.\n");
		exit(1);
	}
	outfds = malloc((sizeof *outfds) * (destCount + argc));
	destNames = malloc((sizeof *destNames) * (destCount + argc));
	if (outfds == NULL ||
	    destNames == NULL) {
		fprintf(stderr, "malloc failed.\n");
		exit(1);
	}

	if (destCount == 1) {
		outfds[0] = stdoutcopy;
		destNames[0] = "stdout";
	}
	while (*argv) {
		outfds[destCount] = open(*argv, O_WRONLY|O_CREAT, 0666);
		if (outfds[destCount] < 0) {
			fprintf(stderr, "Unable to open '%s' for write: %s\n", *argv,
			    strerror(errno));
			exit(1);
		}
		destNames[destCount] = *argv;
		destCount++;
		argv++;
	}

	/* Allocate all the arrays that are sized per destination */

	totalWritten = malloc(sizeof(*totalWritten) * destCount);
	bufSamples = malloc(sizeof(*bufSamples) * destCount);
	bufSum = malloc(sizeof(*bufSum) * destCount);
	fullBufs = malloc(sizeof(*fullBufs) * destCount);
	writer_tids = malloc(sizeof(*writer_tids) * destCount);
	more = malloc(sizeof(*more) * destCount);
	if (totalWritten == NULL ||
	    bufSamples == NULL ||
	    bufSum == NULL ||
	    fullBufs == NULL ||
	    writer_tids == NULL ||
	    more == NULL) {
		fprintf(stderr, "malloc failed.\n");
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
	flFinished = 0;
	remainder = 0;
	numBlocks = 0;
	for (i = 0; i < destCount; i++) {
		totalWritten[i] = 0;
		bufSum[i] = 0;
		bufSamples[i] = 0;
		fullBufs[i] = 0;
		MYASSERT(pthread_cond_init(&more[i], NULL) == 0,
		    "pthread_cond_init failed");
	}
	MYASSERT(pthread_mutex_init(&lock, NULL) == 0,
	    "pthread_mutex_init failed");
	MYASSERT(pthread_cond_init(&less, NULL) == 0,
	    "pthread_cond_init failed");
	MYASSERT(pthread_attr_init(&attr) == 0,
	    "pthread_attr_init failed");
	MYASSERT(pthread_attr_setdetachstate(&attr,
	    PTHREAD_CREATE_DETACHED) == 0,
	    "pthread_attr_setdetachstate failed");
	MYASSERT(pthread_create(&reader_tid, &attr, &reader, NULL) == 0,
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

	/* start the writer threads */

	for (i = 0; i < destCount; i++) {
		MYASSERT(pthread_create(&writer_tids[i], NULL, &writer,
		    (void *)i) == 0,
			"pthread_create failed");
	}

	/* wait for the writer threads to finish */

	for (i = 0; i < destCount; i++)
		pthread_join(writer_tids[i], NULL);

	MYASSERT(gettimeofday(&tpend, NULL) == 0, "gettimeofday failed");
	duration = tpend.tv_sec + tpend.tv_usec / 1000000.0 -
	    tpstart.tv_sec - tpstart.tv_usec / 1000000.0;
	if (flVerbose)
		pthread_join(status_tid, NULL);
	if (flAborted)
		fprintf(stderr, "Transfer aborted.\n");
	if (!flQuiet) {
		uint64_t totalWrittenSum;
		unsigned long bufSamplesSum, bufSumSum;

		totalWrittenSum = bufSamplesSum = bufSumSum = 0;
		for (i = 0; i < destCount; i++) {
			totalWrittenSum += totalWritten[i];
			bufSamplesSum += bufSamples[i];
			bufSumSum += bufSum[i];
		}
		
		fprintf(stderr, "%" PRId64
		    " bytes written to %d destinations in %.3lf secs (%.3lf KiB/sec)\n"
		    "%ld partial read%s, %.3lf average buffers full\n",
		    totalWrittenSum, destCount, duration,
		    duration > 0 ?
		      (double)totalWrittenSum / duration / 1024.0 : 0.0,
		    partialReads, partialReads != 1 ? "s" : "",
		    bufSamplesSum > 0 ? (double)bufSumSum / bufSamplesSum : 0.0);
	}
	if (flAborted)
		pthread_cancel(reader_tid);

	return 0;
}

int
maxFullBufs(void)
{
	int fullBufsMax;
	int i;
	
	fullBufsMax = fullBufs[0];
	for (i = 1; i < destCount; i++)
		if (fullBufs[i] > fullBufsMax)
			fullBufsMax = fullBufs[i];
	return fullBufsMax;
}

static void *
reader(void *dummy)
{
	ssize_t numRead, totalRead;
	int bufNum;
	int i;

	bufNum = 0;
	partialReads = 0;
	while (!flAborted && !flFinished) {
		totalRead = 0;
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		while (maxFullBufs() == numBufs && !flAborted)
			MYASSERT(pthread_cond_wait(&less, &lock) == 0,
			    "pthread_cond_wait failed");
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock");
		if (flAborted)
			break;
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
					for (i = 0; i < destCount; i++)
						pthread_cond_signal(&more[i]);
					return NULL;
				}
			}
			if (numRead == 0) {
				remainder = totalRead;
				flFinished = 1;
				break;
			}
			if (numRead != bufSize)
				partialReads++;
			totalRead += numRead;
		} while (!flAborted && totalRead != bufSize);
		if (++bufNum >= numBufs)
			bufNum = 0;
		if (++numBlocks == maxBlocks) {
			if (remainder == 0)
				remainder = bufSize;
			flFinished = 1;
		}
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		for (i = 0; i < destCount; i++) {
			fullBufs[i]++;
			MYASSERT(pthread_cond_signal(&more[i]) == 0,
			    "pthread_cond_signal failed");
		}
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock");
	}

	/* tell the writers to give up if we've been aborted */

	if (flAborted)
		for (i = 0; i < destCount; i++)
			pthread_cond_signal(&more[i]);
	return NULL;
}

static void *
writer(void *destNum)
{
	ssize_t numWrite, writeSize;
	int bufNum = 0;
	int tid = (int) destNum;

	while (!flAborted && !(flFinished && fullBufs[tid] == 0)) {
		/* wait for block of data */
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		while (fullBufs[tid] == 0 && !flAborted) {
			MYASSERT(pthread_cond_wait(&more[tid], &lock) == 0,
			    "pthread_cond_wait failed");
		}
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock failed");
		if (flAborted)
			break;
		/* write it */
		if (flFinished && fullBufs[tid] == 1)
			/* write the remainder, partial block */
			writeSize = remainder;
		else
			writeSize = bufSize;
		if (writeSize == 0)
			break;
		numWrite = write(outfds[tid], buf[bufNum], writeSize);
		if (numWrite != writeSize) {
			if (numWrite != -1) {
				fprintf(stderr, "%s: Short write: "
				    "%" PRId64 " bytes.\n",
				    destNames[tid],
				    (int64_t)numWrite);
				flAborted = 1;
				totalWritten[tid] += numWrite;
			} else
				perror("Write failed");
			break;
		}
		totalWritten[tid] += numWrite;
		if (++bufNum >= numBufs)
			bufNum = 0;
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		fullBufs[tid]--;
		bufSum[tid] += fullBufs[tid];
		MYASSERT(pthread_cond_signal(&less) == 0,
		    "pthread_cond_signal failed");
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock failed");
		bufSamples[tid]++;
	}

	/* tell the reader to give up if we've been aborted */

	if (flAborted)
		pthread_cond_signal(&less);
	return NULL;
}

static void *
status(void *dummy)
{
	while (!flAborted && !flFinished) {
		statusLine(totalWritten[0] / 1024.0, maxBlocks / 1024.0 * bufSize,
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
	fprintf(stderr, "mbdd version " PACKAGE_VERSION ".\n"
	    "Copyright Paul Ripke $Date: 2013/02/21 11:09:24 $\n");
	fprintf(stderr, "Multi-buffer dd\n\n");
	fprintf(stderr, "Built to use pthreads.\n\n");
	fprintf(stderr, "Usage: mbdd [-b bytes] [-c count] [-n number] [-qs]\n\n");
	fprintf(stderr, "  -b bytes    Set buffer size\n");
	fprintf(stderr, "  -c count    Maximum number of blocks read\n");
	fprintf(stderr, "  -n number   Number of buffers\n");
	fprintf(stderr, "  -q          Quiet operation\n");
	fprintf(stderr, "  -s          Suppress write to stdout\n");
	fprintf(stderr, "  -v          Display progress line\n\n");
	fprintf(stderr, "Compiled defaults:\n");
	fprintf(stderr, "    mbdd -b 64k -c 0 -n 16\n\n");
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
