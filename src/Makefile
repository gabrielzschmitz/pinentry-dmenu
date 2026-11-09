# pinentry-dmenu - dmenu-like stupid pin entry
# See LICENSE file for copyright and license details.

include config.mk

SRC = pinentry-dmenu.c drw.c util.c
OBJ = ${SRC:.c=.o}
OBJ_PIN = pinentry/pinentry.o pinentry/util.o pinentry/password-cache.o pinentry/argparse.o pinentry/secmem.o

all: options pinentry-dmenu

options:
	@echo pinentry-dmenu build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

config.h: config.def.h
	@cp config.def.h $@

${OBJ}: config.h config.mk drw.h util.h

pinentry:
	$(MAKE) -C pinentry

pinentry-dmenu: pinentry pinentry-dmenu.o drw.o util.o
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${OBJ_PIN} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f pinentry-dmenu ${OBJ} config.h
	$(MAKE) -C pinentry/ clean

install: all
	@mkdir -p ${DESTDIR}${PREFIX}/bin ${DESTDIR}${MANPREFIX}/man1
	@install -m 755 pinentry-dmenu ${DESTDIR}${PREFIX}/bin/pinentry-dmenu
	@sed "s/VERSION/${VERSION}/g;s/DATE/${DATE}/g;s/BUGREPORT/${BUGREPORT}/g" < pinentry-dmenu.1 > ${DESTDIR}${MANPREFIX}/man1/pinentry-dmenu.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/pinentry-dmenu.1

uninstall:
	@rm -f ${DESTDIR}${PREFIX}/bin/pinentry-dmenu ${DESTDIR}${MANPREFIX}/man1/pinentry-dmenu.1

.PHONY: all options clean install uninstall pinentry
