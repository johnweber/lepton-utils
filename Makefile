PREFIX ?= /usr/bin

all: leptbmp fblept leptgraypng leptcam

leptcam: leptcam.o
	${CC} -o leptcam leptcam.c leptsci.o -lpthread -Wall

leptgraypng: leptgraypng.c leptsci.o
	${CC} -o leptgraypng leptgraypng.c leptsci.o -lpng -Wall

leptbmp: leptbmp.c leptsci.o

fblept: fblept.c leptsci.o

leptsci.o: leptsci.c

install: all
	install -d $(DESTDIR)$(PREFIX)
	install -m 0755 fblept $(DESTDIR)$(PREFIX)
	install -m 0755 leptbmp $(DESTDIR)$(PREFIX)
	install -m 0755 leptgraypng $(DESTDIR)$(PREFIX)
	install -m 0755 leptcam $(DESTDIR)$(PREFIX)

clean:
	rm -f leptcam *.o fblept leptbmp leptgraypng
