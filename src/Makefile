CC = gcc
#APP_CFLAGS = -Wall -g3 -O0 -fPIC
#UCM_CFLAGS = -Wall -g3 -O0
#SHARED_CFLAGS = -Wall -g3 -O0
APP_CFLAGS = -Wall -O2 -g3 -fPIC
UCM_CFLAGS = -Wall -O2 -g3
SHARED_CFLAGS = -Wall -O2 -g3
#CFLAGS = -g3 -Wall -O0 -fPIC
UCM = ucm
APP = app
SHARED = shared
LDFLAGS = -shared
APP_INCLUDES := -I ../linux/usr/include/
APP_INCLUDES += -I $(SHARED)/
UCM_INCLUDES := -I ../linux/usr/include/
UCM_INCLUDES += -I ../linux/include/uapi
UCM_INCLUDES += -I $(SHARED)/
SHARED_INCLUDES := -I $(SHARED)/
LIBS = -lm -lpthread
#HEMEM_LIBS = $(LIBS) -ldl -lsyscall_intercept -L../Hoard/src -lhoard
HEMEM_LIBS = $(LIBS) -ldl -lsyscall_intercept

default: all
all:app central-manager lib-mmap-populate
app: libhemem.so libhemem-llama.so

libhemem-llama.so: hemem-app.o interpose-llama.o channel-shared-app.o channel-app.o logging-app.o
	$(CC) $(LDFLAGS) -o libhemem-llama.so hemem-app.o interpose-llama.o channel-shared-app.o channel-app.o logging-app.o  $(HEMEM_LIBS)

libhemem.so: hemem-app.o  interpose.o channel-shared-app.o channel-app.o logging-app.o
	$(CC) $(LDFLAGS) -o libhemem.so hemem-app.o interpose.o channel-shared-app.o channel-app.o logging-app.o  $(HEMEM_LIBS)

hemem-app.o: ${APP}/hemem-app.c ${APP}/hemem-app.h
	$(CC) $(APP_CFLAGS) $(APP_INCLUDES) -D ALLOC_HEMEM -c ${APP}/hemem-app.c -o hemem-app.o

interpose.o: ${APP}/interpose.c ${APP}/interpose.h
	$(CC) $(APP_CFLAGS) $(APP_INCLUDES) -c ${APP}/interpose.c

interpose-llama.o: ${APP}/interpose.c ${APP}/interpose.h
	$(CC) $(APP_CFLAGS) $(APP_INCLUDES) -D LLAMA  -o interpose-llama.o -c ${APP}/interpose.c

channel-app.o: ${APP}/channel-app.c ${APP}/channel-app.h
	$(CC) $(APP_CFLAGS) $(APP_INCLUDES) -c ${APP}/channel-app.c

central-manager: ucm.o channel-shared.o channel-ucm.o timer.o hemem-ucm.o pebs.o fifo.o spsc-ring.o logging.o
	$(CC) -o central-manager ucm.o channel-shared.o channel-ucm.o timer.o pebs.o hemem-ucm.o fifo.o spsc-ring.o logging.o -lpthread

ucm.o: ${UCM}/ucm.c
	$(CC) $(UCM_CFLAGS) $(UCM_INCLUDES) -c ${UCM}/ucm.c

hemem-ucm.o: ${UCM}/hemem-ucm.c ${UCM}/hemem-ucm.h ${UCM}/hemem-types.h
	$(CC) $(UCM_CFLAGS) $(UCM_INCLUDES) -c ${UCM}/hemem-ucm.c -lpthread

channel-shared-app.o: ${SHARED}/channel-shared.c ${SHARED}/channel-shared.h
	$(CC) $(APP_CFLAGS) $(SHARED_INCLUDES) -o channel-shared-app.o  -c ${SHARED}/channel-shared.c

channel-shared.o: ${SHARED}/channel-shared.c ${SHARED}/channel-shared.h
	$(CC) $(SHARED_CFLAGS) $(SHARED_INCLUDES) -c ${SHARED}/channel-shared.c

channel-ucm.o: ${UCM}/channel-ucm.c ${UCM}/channel-ucm.h
	$(CC) $(UCM_CFLAGS) $(UCM_INCLUDES) -c ${UCM}/channel-ucm.c

timer.o: ${UCM}/timer.c ${UCM}/timer.h
	$(CC) $(UCM_CFLAGS) $(UCM_INCLUDES) -c ${UCM}/timer.c

pebs.o: ${UCM}/pebs.c ${UCM}/pebs.h ${UCM}/hemem-types.h
	$(CC) $(UCM_CFLAGS) $(UCM_INCLUDES) -c ${UCM}/pebs.c -lpthread

fifo.o: ${UCM}/fifo.c ${UCM}/fifo.h
	$(CC) $(UCM_CFLAGS) $(UCM_INCLUDES) -c ${UCM}/fifo.c

spsc-ring.o: ${UCM}/spsc-ring.c ${UCM}/spsc-ring.h
	$(CC) $(UCM_CFLAGS) $(UCM_INCLUDES) -c ${UCM}/spsc-ring.c

logging.o: ${SHARED}/logging.c ${SHARED}/logging.h
	$(CC) $(SHARED_CFLAGS) $(SHARED_INCLUDES) -c ${SHARED}/logging.c

logging-app.o: ${SHARED}/logging.c ${SHARED}/logging.h
	$(CC) $(APP_CFLAGS) $(SHARED_INCLUDES) -o logging-app.o -c $(SHARED)/logging.c

lib-mmap-populate: lib_mmap_populate.c
	$(CC) lib_mmap_populate.c -lsyscall_intercept -fpic -shared -o libmmap_populate.so

clean:
	$(RM) *.o *.so central-manager
