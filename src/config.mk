# pinentry-dmenu settings
DATE      = $$(date +'%B %Y')
VERSION   = 0.1
BUGREPORT = https:\/\/github.com\/gabrielzschmitz\/pinentry-dmenu

PREFIX    = /usr/local
MANPREFIX = ${PREFIX}/share/man
ICONPREFIX = ${PREFIX}/share/icons
ICONPATH ?= ${ICONPREFIX}/hicolor/256x256/apps/pinentry-dmenu.png

X11INC ?= /usr/X11R6/include
X11LIB ?= /usr/X11R6/lib
PKG_CONFIG ?= pkg-config
XINERAMALIBS = -lXinerama
XINERAMAFLAGS = -DXINERAMA
FREETYPELIBS = -lfontconfig -lXft
FREETYPEINC ?= /usr/include/freetype2

INCS = -I${X11INC} -I${FREETYPEINC} `${PKG_CONFIG} --cflags imlib2 xft xinerama libconfig libassuan gpgme gpg-error`
LIBS = -L${X11LIB} -lX11 ${XINERAMALIBS} ${FREETYPELIBS} `${PKG_CONFIG} --libs imlib2 xft xinerama libconfig libassuan gpgme gpg-error` -lm

CPPFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -DVERSION=\"${VERSION}\" -DICONPATH=\"${ICONPATH}\" ${XINERAMAFLAGS} -DPACKAGE_VERSION=\"${VERSION}\" -DPACKAGE_BUGREPORT=\"${BUGREPORT}\"
CFLAGS += -std=c99 -pedantic -Wall -Os ${INCS} ${CPPFLAGS}
LDFLAGS += ${LIBS}

CC ?= cc
