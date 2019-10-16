CC = gcc
CFLAGS = -g -Wall $(shell pkg-config --cflags --libs glib-2.0)
LDLIBS=-lpthread -lmpdclient

OBJECTS = mpd_control.o
mpd_control: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDLIBS) $^ -o $@

clean:
	rm $(OBJECTS)
	rm mpd_control
