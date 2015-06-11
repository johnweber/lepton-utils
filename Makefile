CFLAGS = -Wall

all: leptbmp fblept leptgraypng leptcam

leptcam: leptcam.o
	${CC} -o leptcam leptcam.c leptsci.o -lpthread -Wall

leptgraypng: leptgraypng.c leptsci.o
	${CC} -o leptgraypng leptgraypng.c leptsci.o -lpng -Wall

leptbmp: leptbmp.c leptsci.o

fblept: fblept.c leptsci.o

leptsci.o: leptsci.c

clean:
	rm -f leptcam *.o fblept leptbmp leptgraypng
