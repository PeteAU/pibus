CC = arm-linux-gnueabihf-gcc
STRIP = arm-linux-gnueabihf-strip

all:
	$(CC) -Wall -O2 -ggdb mainloop.c slist.c pibus.c ibus.c ibus-send.c keyboard.c gpio.c server.c -o pibus -lrt
	cp pibus pibus.debug
	$(STRIP) -R .comment pibus
