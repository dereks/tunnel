TARGET = tunnel

CC = gcc
CFLAGS = -g -Wall

# Dynamic linking (depends on 'make install' for CyaSSL).
# There is no .deb; see Launchpad bug #624840.
LIBS = -lm -levent -levent_pthreads -lpthread -lcyassl
INCLUDES = -I../third-party/inih_r29 

#
# Static linking against CyaSSL:
#
# Link statically specifying -L with the local .libs dir and using 
# "-Wl,-Bstatic -lcyassl" in LIBS below.
##CFLAGS = -g -Wall -L../third-party/cyassl-2.9.4/src/.libs/ -L../third-party/libevent-2.0.21-stable/.libs/
#LIBS = -lm -levent -levent_pthreads -lpthread -Wl,-Bstatic -lcyassl -Wl,-Bdynamic
#INCLUDES = -I../third-party/inih_r29 -I../third-party/cyassl-2.9.4 -I../third-party/libevent-2.0.21-stable/include

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c)) ../third-party/inih_r29/ini.o
HEADERS = $(wildcard *.h) ../third-party/inih_r29/ini.h


%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) $(INCLUDES) -Wall $(LIBS) -o $@
	mv ./$(TARGET) ../

clean:
	-rm -f $(OBJECTS)
	-rm -f ../$(TARGET)

