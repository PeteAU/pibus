/*
 * pibus Copyright (c) 2013 Peter Zelezny
 * All Rights Reserved
 */

#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "keyboard.h"
#include "mainloop.h"
#include "ibus.h"
#include "gpio.h"



int main(int argc, char **argv)
{
	int opt;
	bool bluetooth = FALSE;
	bool mk3 = TRUE;
	bool camera = TRUE;
	const char *port = "/dev/ttyAMA0";

	mainloop_init();

	while ((opt = getopt(argc, argv, ":bhmr")) != -1)
	{
		switch (opt)
		{
			case 'b':
				bluetooth = 1;
				break;
			case 'm':
				mk3 = 0;
				break;
			case 'r':
				camera = 0;
				break;
			case 'h':
			default:
				fprintf(stderr,
					"Usage: %s [flags] [serial-port]\n"
					"\n"
					"Flags:\n"
					"\t-b   Car has bluetooth, don't use Phone and Speak buttons\n"
					"\t-m   Do not do MK3 style CDC announcements\n"
					"\t-r   Do not switch to camera in reverse gear\n"
					"\n",
					argv[0]);
				return -1;
		}
	}

	if (argc > optind)
	{
		port = argv[optind];
	}

	if (ibus_init(port, bluetooth, camera, mk3) != 0)
	{
		return -2;
	}

	if (keyboard_init() != 0)
	{
		fprintf(stderr, "Can't open keyboard\r\n");
		return -3;
	}

	if (gpio_init() != 0)
	{
		fprintf(stderr, "Can't init gpio\r\n");
		return -4;
	}

	mainloop();

	gpio_cleanup();
	ibus_cleanup();
	keyboard_cleanup();

	return 0;
}
