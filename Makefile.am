AUTOMAKE_OPTIONS = foreign

srcdir	= @srcdir@
VPATH	= @srcdir@
SHELL	= /bin/sh

prefix	= @prefix@
exec_prefix = @exec_prefix@
bindir	= @bindir@
mandir	= @mandir@
mandirman1 = ${mandir}/man1

CC	= @CC@
CPPFLAGS = @CPPFLAGS@ @DEFS@
CFLAGS	= @CFLAGS@
LIBS	= @LIBS@
LDFLAGS	= @LDFLAGS@

INSTALL	= @INSTALL@

ALLPROGS = fblckgen iohammer mbdd
PROGS	= @PROGS@
man_MANS = fblckgen.1 iohammer.1 mbdd.1
SRCS	= common.c fblckgen.c iohammer.c mbdd.c
OBJS	= ${SRCS:.c=.o}

all:	${PROGS}

fblckgen: fblckgen.o common.o
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ fblckgen.o common.o ${LIBS}

iohammer: iohammer.o common.o
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ iohammer.o common.o ${LIBS}

mbdd:	mbdd.o common.o
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ mbdd.o common.o ${LIBS}

${OBJS}: iotools.h common.h config.h

.c.o:
	${CC} ${CFLAGS} ${CPPFLAGS} -c $<

install: all
	-mkdir -p ${bindir}
	@for i in ${PROGS}; do \
		echo "${INSTALL} -m 555 $$i ${bindir}/$$i"; \
		${INSTALL} -m 555 $$i ${bindir}/$$i; \
	done
	-mkdir -p ${mandirman1}
	@for i in ${MANS}; do \
		echo "${INSTALL} -m 444 $$i ${mandirman1}/$$i"; \
		${INSTALL} -m 444 $$i ${mandirman1}/$$i; \
	done

clean:
	rm -f core ${ALLPROGS} ${OBJS} *.html

distclean: clean
	rm -f Makefile config.cache config.log config.status config.h stamp-h1

rman:
	@for i in ${PROGS}; do \
		rman -f html -r '' < $$i.1 > $$i.html; \
	done
