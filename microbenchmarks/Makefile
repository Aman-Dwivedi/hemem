HEMEMDIR = $(CURDIR)/../src/

CC = gcc
CFLAGS = -g -Wall -O3
#CFLAGS = -g3 -Wall -O0
INCLUDES = -I../linux/usr/include
LIBS = -lm -lpthread

default: gups-pebs gups-random gups-hotset-move gups

all: gups-pebs gups-random gups-simple gups-lru gups-lru-swap gups-hotset-move #gups

gups-random: gups-random.o
	$(CC) $(CFLAGS) $(INCLUDES) -o gups-random gups-random.o zipf.o $(LIBS) -L$(HEMEMDIR) -lhemem

gups-small: gups-small.o
	$(CC) $(CFLAGS) $(INCLUDES) -o gups-small gups-small.o ../src/timer.o $(LIBS)

gups-sequential: gups-sequential.o
	$(CC) $(CFLAGS) $(INCLUDES) -o gups-sequential gups-sequential.o zipf.o $(LIBS) -L$(HEMEMDIR) -lhemem-sequential

gups-stride-pages: gups-stride-pages.o
	$(CC) $(CFLAGS) $(INCLUDES) -o gups-stride-pages gups-stride-pages.o zipf.o $(LIBS) -L$(HEMEMDIR) -lhemem-stride

gups: gups.o
	$(CC) $(CFLAGS) $(INCLUDES) -o gups gups.o zipf.o ../src/timer.o $(LIBS)

gups-pebs: gups.o
	$(CC) $(CFLAGS) $(INCLUDES) -o gups-pebs gups.o zipf.o $(LIBS) -L$(HEMEMDIR) -lhemem -DHEMEM

gups-hotset-move: gups-hotset-move.o
	$(CC) $(CFLAGS) $(INCLUDES) -o gups-hotset-move gups-hotset-move.o zipf.o $(LIBS) -L$(HEMEMDIR) -lhemem

gups-simple: gups.o
	$(CC) $(CFLAGS) $(INCLUDES) -o gups-simple gups.o zipf.o $(LIBS) -L$(HEMEMDIR) -lhemem-simple

gups-lru: gups.o
	$(CC) $(CFLAGS) $(INCLUDES) -o gups-lru gups.o zipf.o $(LIBS) -L$(HEMEMDIR) -lhemem-lru

gups-lru-swap: gups.o
	$(CC) $(CFLAGS) $(INCLUDES) -o gups-lru-swap gups.o zipf.o $(LIBS) -L$(HEMEMDIR) -lhemem-lru-swap

gups-random.o: gups-random.c zipf.c gups.h
	$(CC) $(CFLAGS) $(INCLUDES) -c gups-random.c zipf.c

gups-sequential.o: gups-sequential.c zipf.c gups.h
	$(CC) $(CFLAGS) $(INCLUDES) -c gups-sequential.c zipf.c

gups-stride-pages.o: gups-stride-pages.c zipf.c gups.h
	$(CC) $(CFLAGS) $(INCLUDES) -c gups-stride-pages.c zipf.c

gups.o: gups.c zipf.c  gups.h
	$(CC) $(CFLAGS) $(INCLUDES) -c gups.c zipf.c

gups-hotset-move.o: gups-hotset-move.c zipf.c gups.h
	$(CC) $(CFLAGS) $(INCLUDES) -c gups-hotset-move.c zipf.c

clean:
	$(RM) *.o gups gups-hotset-move gups-lru-swap gups-lru gups-simple gups-random gups-pebs
