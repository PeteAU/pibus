CC = arm-bcm2708hardfp-linux-gnueabi-gcc
STRIP = arm-bcm2708hardfp-linux-gnueabi-strip

all:
	$(CC) -Wall -O2 -ggdb mainloop.c slist.c pibus.c ibus.c ibus-send.c keyboard.c gpio.c -o pibus -lrt
	cp pibus pibus.debug
	$(STRIP) -R .comment pibus
