/* $Id: iohammer.c,v 1.6 2003/07/28 01:42:21 stix Exp $ */

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

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#ifdef __APPLE__
#include <dev/disk.h>
#endif

#ifdef BSD4_4
#include <sys/disklabel.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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

static char const rcsid[] = "$Id: iohammer.c,v 1.6 2003/07/28 01:42:21 stix Exp $";

/* Prototypes */
static void	*doIO(void *);
static void	cleanup(int);
static void	usage();
static void	openfile(int **fds, char *name, int64_t *size,
		    int threads, int access);

/* Globals */
static int ignore, threads, type, writeLim, *fds;
static volatile int aborted;
static long blockSize;
static int64_t iolimit, fileBlocks;
static volatile int64_t numio, numWrites;

#ifdef USE_PTHREADS
static pthread_mutex_t lock;
static pthread_cond_t cond;
#else
static int *pipe_ctl_r, *pipe_ctl_w, *pipe_cnt_r, *pipe_cnt_w;
#endif

int
main(int argc, char **argv)
{
	int c, i, unformatted, writePct;
	int64_t fileSize;
	float secs;
	struct timeval startTime, endTime;
	char fileName[PATH_MAX];
#ifdef USE_PTHREADS
	pthread_t *tid;
	pthread_attr_t attr;
#else
	char tok;
	int fdmax, p[2];
	pid_t *pid;
	fd_set rdset;
	struct timeval tmout;
#endif

	/* Set defaults */
	blockSize = 512;
	strncpy(fileName, ".", sizeof(fileName)-1);
	fileSize = 1048576L;
	ignore = 0;
	numio = 0;
	threads = 8;
	type = ALPHADATA;
	unformatted = 0;
	writePct = 0;

	aborted = 0;

	while ((c = getopt(argc, argv, "raiub:c:w:t:s:f:?")) != EOF) {
		switch (c) {
		case 'a':
			type = ALPHADATA;
			break;
		case 'b':
			blockSize = getnum(optarg);
			break;
		case 'c':
			iolimit = getnum(optarg);
			break;
		case 'f':
			strncpy(fileName, optarg, sizeof(fileName));
			break;
		case 'i':
			ignore = 1;
			break;
		case 'r':
			type = RANDDATA;
			break;
		case 's':
			fileSize = getnum(optarg);
			break;
		case 't':
			threads = atoi(optarg);
			if (threads <= 0) {
				fprintf(stderr, "Invalid number of "
				    "threads: %d\n", threads);
				usage();
				exit(1);
			}
			break;
		case 'u':
			unformatted = 1;
			break;
		case 'w':
			writePct = atoi(optarg);
			if (writePct > 100)
				writePct = 100;
			break;
		case '?':
		default:
			usage();
			exit(1);
		}
	}

	openfile(&fds, fileName, &fileSize, threads, writePct == 0 ?
	    O_RDONLY : O_RDWR);
	if (!unformatted) {
		printf("Size %lld: ", fileSize);
		fflush(stdout);
	}

	writeLim = (writePct << 10) / 100;
	fileBlocks = fileSize / blockSize;
	if (iolimit > 0 && threads > iolimit)
		threads = iolimit;

	signal(SIGINT, &cleanup);

#ifdef USE_PTHREADS
	MYASSERT((tid = malloc(threads * sizeof(pthread_t))) != NULL,
	    "malloc failed");
	MYASSERT(pthread_mutex_init(&lock, NULL) == 0,
	    "pthread_mutex_init failed");
	MYASSERT(pthread_cond_init(&cond, NULL) == 0,
	    "pthread_cond_init failed");
	MYASSERT(pthread_attr_init(&attr) == 0,
	    "pthread_attr_init failed");
	MYASSERT(pthread_attr_setdetachstate(&attr,
	    PTHREAD_CREATE_DETACHED) == 0,
	    "pthread_attr_setdetachstate failed");
	for (i = 0; i < threads; i++) {
		MYASSERT(pthread_create(&tid[i], &attr, &doIO, (void *)i) == 0,
		    "pthread_create failed");
	}
	MYASSERT(pthread_attr_destroy(&attr) == 0,
	    "pthread_attr_destroy failed");

	MYASSERT(gettimeofday(&startTime, NULL) == 0, "gettimeofday failed");

	/* wait for the threads to finish */
	MYASSERT(pthread_mutex_lock(&lock) == 0,
	    "pthread_mutex_lock failed");
	while ((iolimit == 0 || numio < iolimit) && !aborted)
		MYASSERT(pthread_cond_wait(&cond, &lock) == 0,
		    "pthread_cond_wait failed");
	MYASSERT(pthread_mutex_unlock(&lock) == 0,
	    "pthread_mutex_unlock failed");
#else
	MYASSERT((pid = malloc(threads * sizeof(pid_t))) != NULL,
	    "malloc failed");
	MYASSERT((pipe_ctl_r = (int *) malloc(threads * sizeof(int))) != NULL,
	    "malloc failed");
	MYASSERT((pipe_ctl_w = (int *) malloc(threads * sizeof(int))) != NULL,
	    "malloc failed");
	MYASSERT((pipe_cnt_r = (int *) malloc(threads * sizeof(int))) != NULL,
	    "malloc failed");
	MYASSERT((pipe_cnt_w = (int *) malloc(threads * sizeof(int))) != NULL,
	    "malloc failed");
	for (i = 0; i < threads; i++) {
		MYASSERT(pipe((int *) &p) == 0, "pipe failed");
		pipe_ctl_r[i] = p[0];
		pipe_ctl_w[i] = p[1];
		MYASSERT(pipe((int *) &p) == 0, "pipe failed");
		pipe_cnt_r[i] = p[0];
		pipe_cnt_w[i] = p[1];
		switch (pid[i] = fork()) {
		case -1:
			perror("fork failed");
			exit(1);
			break;
		case 0:		/* child */
			signal(SIGINT, SIG_IGN);
			close(pipe_ctl_w[i]);
			close(pipe_cnt_r[i]);
			doIO((void *)i);
			_exit(0);
		default:	/* parent */
			close(pipe_ctl_r[i]);
			close(pipe_cnt_w[i]);
			break;
		}
		
	}

	/* kick start all the children */
	tok = 1;
	fdmax = 0;
	MYASSERT(gettimeofday(&startTime, NULL) == 0, "gettimeofday failed");
	for (i = 0; i < threads && (iolimit == 0 ||
	    numio < iolimit); i++) {
		MYASSERT(write(pipe_ctl_w[i], &tok, 1) == 1,
		    "write to pipe failed");	
		if (fdmax < pipe_cnt_r[i])
			fdmax = pipe_cnt_r[i];
	}
	fdmax++;
	while ((iolimit == 0 || numio < iolimit) && !aborted) {
		FD_ZERO(&rdset);
		for (i = 0; i < threads; i++)
			if (pipe_cnt_r[i] >= 0)
				FD_SET(pipe_cnt_r[i], &rdset);
		tmout.tv_sec = 10;
		tmout.tv_usec = 0;
		switch(select(fdmax, &rdset, NULL, NULL, &tmout)) {
		case 0:
			break;
		case -1:
			if (errno != EINTR) {
				aborted = 1;
				perror("select call failed");
			}		/* fallthru */
		default:
			for (i = 0; i < threads; i++)
				if (pipe_cnt_r[i] >= 0 &&
				    FD_ISSET(pipe_cnt_r[i], &rdset)) {
					MYASSERT(read(pipe_cnt_r[i], &tok, 1)
					    == 1,
					    "read on count pipe failed");
					numio++;
					if (tok == 1)
						numWrites++;
					if (iolimit == 0 || numio +
					    threads <= iolimit)
						tok = 1;
					else
						tok = 0;
					MYASSERT(write(pipe_ctl_w[i], &tok, 1)
					    == 1,
					    "write to control pipe failed");
					if (tok == 0)
						pipe_cnt_r[i] = -1;
				}
		}
	}
#endif
	MYASSERT(gettimeofday(&endTime, NULL) == 0, "gettimeofday failed");
	secs = endTime.tv_sec + endTime.tv_usec / 1000000.0
	    - startTime.tv_sec - startTime.tv_usec / 1000000.0;
	if (unformatted) {
		printf("%lld\t%d\t%ld\t%d\t%lld\t%lld\t%f\t%f\n", fileSize,
		    threads, blockSize, writePct, numio, numWrites, secs,
		    numio / secs);
	} else {
		printf("%.3f secs, %lld IOs, %lld writes, ", secs, numio,
		    numWrites);
		printf("%.1f IOs/sec, %.2f ms average seek\n", numio / secs,
		    secs / numio * 1000.0);
	}

	if (aborted)
		for (i = 0; i < threads; i++)
#ifdef USE_PTHREADS
			pthread_cancel(tid[i]);
#else
			kill(pid[i], SIGTERM);
#endif
	exit(0);
}

static void *
doIO(void *arg)
{
	char tok;
	int writeFlag, tid;
	long seed;
	off_t pos, ioRet;
	struct timeval tmout;
	char *buf;

	tid = (int)arg;
	if ((buf = malloc(blockSize)) == NULL) {
		fprintf(stderr, "malloc for %ld bytes failed.", blockSize);
		exit(1);
	}
	MYASSERT(gettimeofday(&tmout, NULL) == 0, "gettimeofday failed");
	writeFlag = 0;
#ifdef USE_PTHREADS
	seed = tmout.tv_usec ^ tmout.tv_sec ^ (long)&seed;
#else
	seed = tmout.tv_usec ^ tmout.tv_sec ^ getpid();
#endif
	SRAND(seed);
	srandom(seed);
	for (;;) {
		pos = random() % fileBlocks * blockSize;
		if ((RAND() & 0x03ff) < writeLim) {	/* write */
			writeFlag = 1;
			initblock(buf, blockSize, type, 1);
		} else
			writeFlag = 0;
#ifndef USE_PTHREADS
		MYASSERT(read(pipe_ctl_r[tid], &tok, 1) == 1,
		    "pipe read failed");
		if (tok == 0) {
			_exit(0);	/* finished */
		}
#endif
		if ((ioRet = lseek(fds[tid], pos, SEEK_SET)) == -1) {
			perror("lseek failed");
			exit(1);
		}
		if (writeFlag) {
			ioRet = write(fds[tid], buf, blockSize);
			tok = 1;
		} else {
			ioRet = read(fds[tid], buf, blockSize);
			tok = 0;
		}
		if (ioRet != blockSize) {
			fprintf(stderr, "%s I/O failed, offset %lld: %d (%s)\n",
			    writeFlag ? "write" : "read",
			    (int64_t)pos, errno, strerror(errno));
			if (!ignore) {
				aborted = 1;
#ifdef USE_PTHREADS
				pthread_exit(0);
#else
				_exit(1);
#endif
			}
		}
#ifdef USE_PTHREADS
		MYASSERT(pthread_mutex_lock(&lock) == 0,
		    "pthread_mutex_lock failed");
		numio++;
		if (writeFlag)
			numWrites++;
		if (aborted || (iolimit > 0 && numio + threads >= iolimit + 1))
			break;		/* finished */
		MYASSERT(pthread_mutex_unlock(&lock) == 0,
		    "pthread_mutex_unlock failed");
#else
		MYASSERT(write(pipe_cnt_w[tid], &tok, 1) == 1,
		    "write to pipe failed");
#endif
	}
#ifdef USE_PTHREADS
	MYASSERT(pthread_cond_signal(&cond) == 0,
	    "pthread_cond_signal failed");
	MYASSERT(pthread_mutex_unlock(&lock) == 0,
	    "pthread_mutex_unlock failed");
#endif
	return NULL;
}


static void
openfile(int **fds, char *name, int64_t *size, int threads, int access)
{
	struct stat sb;
	int fd, i, isTemp;
	int64_t fileSize;
	char *blck;
#ifdef __APPLE__
	long blocks, blkSize;
#endif

	isTemp = fd = 0;

	if (stat(name, &sb) != 0) {
		fprintf(stderr, "File/device/directory '%s' not found.\n",
		    name);
		exit(1);
	}
	if (S_ISDIR(sb.st_mode)) {
		if (*size == 0) {
			fprintf(stderr, "Size must be specified for "
			    "temporary files\n");
			exit(1);
		}
		isTemp = 1;
		strcat(name, "/iohammer.XXXXXX");
		fd = mkstemp(name);
		if (fd < 0) {
			fprintf(stderr, "Failed to create file '%s': %s\n",
			    name, strerror(errno));
			exit(1);
		}
		fprintf(stderr, "Using temporary file '%s'.\n", name);
	} else if (!S_ISREG(sb.st_mode) && !S_ISCHR(sb.st_mode) &&
	    !S_ISBLK(sb.st_mode)) {
		fprintf(stderr, "Unknown file type: %08x\n", sb.st_mode);
		exit(1);
	}
	*fds = malloc(sizeof(int) * threads);
	if (*fds == NULL) {
		fprintf(stderr, "malloc failed.\n");
		exit(1);
	}
	for (i = 0; i < threads; i++) {
		if (((*fds)[i] = open(name, access)) < 0) {
			fprintf(stderr, "Failed to open fd %d to file/device "
			    "'%s': %s\n", i, name, strerror(errno));
			exit(1);
		}
	}
	if (isTemp) {
		unlink(name);
		/*
		 * write blocks of zeros to allocate blocks on disk upto
		 * the size requested
		 */
		blck = malloc(65536);
		if (blck == NULL) {
			fprintf(stderr, "malloc failed: %s\n", strerror(errno));
			exit(1);
		}
		bzero(blck, 65536);
		/* start with 64k blocks, for speed */
		for (i = 0; i < (*size >> 16); i++) {
			if (write(fd, blck, 65536) != 65536) {
				fprintf(stderr, "Write failed: %s\n",
				    strerror(errno));
				exit(1);
			}
		}
		/* then individual bytes until we're there */
		for (i = 0; i < (*size - ((*size >> 16) << 16)); i++) {
			if (write(fd, blck, 1) != 1) {
				fprintf(stderr, "Write failed: %s\n",
				    strerror(errno));
				exit(1);
			}
		}
		fsync(fd);
		free(blck);
	} else {
		/* Find the size of the file/device */
		fileSize = sb.st_size;
		if (*size == 0)
			*size = fileSize;
		/* We have to do it the other way... */
		if (*size == 0)
			*size = lseek((*fds)[0], 0, SEEK_END);
#ifdef __APPLE__
		/* Have another go... */
		if (*size == 0) {
			if (ioctl((*fds)[0], DKIOCNUMBLKS, &blocks) == -1) {
				fprintf(stderr, "ioctl(DKIOCNUMBLKS) failed: "
				    "%s\n", strerror(errno));
				exit(1);
			}
			if (ioctl((*fds)[0], DKIOCBLKSIZE, &blkSize) == -1) {
				fprintf(stderr, "ioctl(DKIOCBLKSIZE) failed: "
				    "%s\n", strerror(errno));
				exit(1);
			}
			*size = ((int64_t)blocks) * blkSize;
		}
#endif
	}
	if (fd > 0)
		close(fd);
}

static void
cleanup(int sig)
{
	aborted = 1;
}

static void
usage()
{
	fprintf(stderr, "iohammer version " VERSION ".\n"
	    "Copyright Paul Ripke $Date: 2003/07/28 01:42:21 $\n");
#ifdef USE_PTHREADS
	fprintf(stderr, "Built to use pthreads.\n\n");
#else
	fprintf(stderr, "Built to use multiple processes.\n\n");
#endif
	fprintf(stderr, "Usage: iohammer [-a | -r] [-i] [-b size] "
	    "[-c count] [-w write%%]\n");
	fprintf(stderr, "                [-t threads] [-s size] "
	    "[-f file/dir/dev]\n\n");
	fprintf(stderr, "  -a          Write blocks of a repeating ASCII "
	    "string\n");
	fprintf(stderr, "  -r          Write blocks of binary 'random' data\n");
	fprintf(stderr, "  -i          Ignore I/O errors and continue\n");
	fprintf(stderr, "  -b bytes    Set write blocksize\n");
	fprintf(stderr, "  -c count    Number of blocks to read/write "
	    "(zero for infinite)\n");
	fprintf(stderr, "  -w write%%   Integer percentage of operations to be "
	    "writes\n");
	fprintf(stderr, "  -t threads  Number of threads to do I/O\n");
	fprintf(stderr, "  -u          Unformatted output. Write tab-separted "
	    "figures\n");
	fprintf(stderr, "  -s size     Size of file/device to create/use\n");
	fprintf(stderr, "              Specify '0' to attempt to find the "
	    "size of file/device\n");
	fprintf(stderr, "  -f file     Name of file (must exist), directory "
	    "or device\n");
	fprintf(stderr, "              If directory, a temporary file is "
	    "created\n\n");
	fprintf(stderr, "Unformatted output, order is:\n");
	fprintf(stderr, "  size, threads, blocksize, write-pct, count, "
	    "writes, seconds, rate\n\n");
	fprintf(stderr, "Compiled defaults:\n");
	fprintf(stderr, "    iohammer -a -b 1s -c 0 -w 0 -s 1m -f .\n\n");
	fprintf(stderr, "  Numeric arguments take an optional "
	    "letter multiplier:\n");
	fprintf(stderr, "    s:        Sectors (x 512)\n");
	fprintf(stderr, "    k:        kibi (x 1024 or 2^10)\n");
	fprintf(stderr, "    m:        mebi (x 1048576 or 2^20)\n");
	fprintf(stderr, "    g:        gibi (x 2^30)\n");
	fprintf(stderr, "    t:        tebi (x 2^40)\n");
	fprintf(stderr, "    p:        pebi (x 2^50)\n");
	fprintf(stderr, "    e:        exbi (x 2^60)\n");
}
