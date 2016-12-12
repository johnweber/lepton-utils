PREFIX ?= /usr/bin

CC ?= gcc

# Debugging
ifdef DEBUG
CFLAGS = -DDEBUG -O0 -g
endif

all: leptbmp fblept leptgraypng leptcam

leptcam: leptcam.c leptsci.c palettes.c
	${CC} ${CFLAGS} -o leptcam leptcam.c leptsci.c palettes.c -lpthread ${LDFLAGS}

leptgraypng: leptgraypng.c leptsci.c
	${CC} ${CFLAGS} -o leptgraypng leptgraypng.c leptsci.c -lpng ${LDFLAGS}

leptbmp: leptbmp.c leptsci.o

fblept: fblept.c leptsci.o

install: all
	install -d $(DESTDIR)$(PREFIX)
	install -m 0755 fblept $(DESTDIR)$(PREFIX)
	install -m 0755 leptbmp $(DESTDIR)$(PREFIX)
	install -m 0755 leptgraypng $(DESTDIR)$(PREFIX)
	install -m 0755 leptcam $(DESTDIR)$(PREFIX)

clean:
	rm -f leptcam *.o fblept leptbmp leptgraypng
