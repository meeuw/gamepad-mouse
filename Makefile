TARGET = gamepad-mouse
LIBS = -ludev -lm
CC = gcc
CFLAGS = -O2 -g -Wall -Wextra

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

install:
	install -D gamepad-mouse $(DESTDIR)/usr/bin/gamepad-mouse

clean:
	-rm -f *.o
	-rm -f $(TARGET)
