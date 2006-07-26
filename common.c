/* $Id: common.c,v 1.1 2003/07/17 23:50:49 stix Exp stix $ */

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

#include "iotools.h"
#include "common.h"

static char const rcsid[] = "$Id: common.c,v 1.1 2003/07/17 23:50:49 stix Exp stix $";

/*
 * getnum:
 * Read an quad-int (int64_t) with optional letter multiplier.
 */
int64_t
getnum(char *c)
{
	int64_t result = 0;

	while (isdigit((int)*c)) {
		result = result * 10 + *c - '0';
		c++;
	}
	if (tolower((int)*c) == 's')		/* sectors */
		result *= 512;
	else if (tolower((int)*c) == 'k')	/* kibi */
		result *= 1024;
	else if (tolower((int)*c) == 'm')	/* mebi */
		result *= 1048576LL;
	else if (tolower((int)*c) == 'g')	/* gibi */
		result *= 1073741824LL;
	else if (tolower((int)*c) == 't')	/* tebi */
		result *= 1099511627776LL;
	else if (tolower((int)*c) == 'p')	/* pebi */
		result *= 1125899906842624LL;
	else if (tolower((int)*c) == 'e')	/* exbi */
		result *= 1152921504606846976LL;

	return result;
}

/*
 * initblock:
 * Initialise a block of memory with pseudo-random data or ascii sequence.
 * ascii sequence cycles, no matter the block size.
 * We try to do it quickly!
 */
void
initblock(char *buf, long blockSize, dataType type, int64_t blockNum)
{
	int i, c, len;
	static char ascii[('~' - ' ' + 1) * 44] = { '\0' };

	switch (type) {
	case ALPHADATA:
		/*
		 * Init ascii on first call.  We push the block generation
		 * through memcpy, since single byte generation is
		 * hideously slow on some CPUs.
		 */
		if (ascii[0] == '\0')
			for (i = 0; i < sizeof(ascii); i++)
				ascii[i] = i % ('~' - ' ' + 1) + ' ';
		i = 0;
		c = blockNum * blockSize % sizeof(ascii);
		len = blockSize - i < sizeof(ascii) - c ?
		    blockSize - i : sizeof(ascii) - c;
		memcpy(&buf[i], &ascii[c], len);
		i += len;
		while (i < blockSize) {
			len = blockSize - i < sizeof(ascii) ?
			    blockSize - i : sizeof(ascii);
			memcpy(&buf[i], &ascii, len);
			i += len;
		}
		break;
	case RANDDATA:
		/*
		 * Do random numbers two bytes at a time.  We take the
		 * MS bytes, since they might be a little more "random",
		 * given the brain-damaged algorithm.
		 */
		for (i = 0; i < (blockSize >> 1); i++, buf += 2)
			*(short *)buf = RAND() >> 16;
		if ((i << 1) < blockSize)
			*++buf = RAND() >> 8;
		break;
	default:
		fprintf(stderr, "Bad type: %d.\n", type);
		exit(1);
	}
}

/*
 * getshm:
 * Two methods, either SYSV or mmap(2) style. Take your pick
 */

#ifdef USE_SYSVSHM

void
*getshm(long size)
{
	int shmid;
	void *buf;

	shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0644);
	if (shmid == -1) {
		perror("shmget failed");
		exit(1);
	}
	buf = shmat(shmid, NULL, 0);
	if ((long)buf == -1) {
		perror("shmat failed");
		exit(1);
	}
	/* mark as deleted, so we don't have to worry about cleaning up */
	if (shmctl(shmid, IPC_RMID, NULL) == -1) {
		perror("shmctl failed");
		exit(1);
	}
	return buf;
}

#else

void
*getshm(long size)
{
	void *buf;
#ifndef MAP_ANONYMOUS
	int fd;

	fd = open("/dev/zero", O_RDWR);
	if (fd == -1) {
		perror("Failed to open /dev/zero");
		exit(1);
	}
	buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
	    MAP_SHARED, fd, 0);
	close(fd);
#else
	buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
	    MAP_SHARED | MAP_ANON, -1, 0);
#endif
	if ((long)buf == -1) {
		perror("mmap failed");
		exit(1);
	}
	return buf;
}

#endif /* !USE_SYSVSHM */
