# Target library
lib := libfs.a

all: $(lib)

CFLAGS = -g -Wall -Wextra

$(lib): disk.o fs.o
		ar crs libfs.a fs.o disk.o

fs.o: fs.c fs.h
		gcc $(CFLAGS) -c fs.c -o fs.o

disk.o: disk.c disk.h
		gcc $(CFLAGS) -c disk.c -o disk.o

clean:
		rm -f *.o $(lib) disk.o fs.o
