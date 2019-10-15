CC = gcc
CFLAGS = -g -Wall
LDLIBS=-lpthread -lmpdclient

OBJECTS = mpd_control.o
mpd_control: $(OBJECTS)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm $(OBJECTS) mpd_control
