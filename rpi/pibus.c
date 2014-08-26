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
	int gpio_number = 18;
	const char *port = "/dev/ttyAMA0";
	char *startup = NULL;
	int cdcinterval = 0;

	mainloop_init();

	while ((opt = getopt(argc, argv, "c:g:s:bhmr")) != -1)
	{
		switch (opt)
		{
			case 'b':
				bluetooth = 1;
				break;
			case 'c':
				cdcinterval = atoi(optarg);
				break;
			case 'g':
				gpio_number = atoi(optarg);
				break;
			case 'm':
				mk3 = 0;
				break;
			case 'r':
				camera = 0;
				break;
			case 's':
				startup = strdup(optarg);
				break;
			case 'h':
			default:
				fprintf(stderr,
					"Usage: %s [flags] [serial-port]\n"
					"\n"
					"Flags:\n"
					"\t-b           Car has bluetooth, don't use Phone and Speak buttons\n"
					"\t-c <time>    Force CDC-info replies every <time> seconds\n"
					"\t-g <number>  GPIO number to use for IBUS line monitor (0 = Use TH3122)\n"
					"\t-m           Do not do MK3 style CDC announcements\n"
					"\t-r           Do not switch to camera in reverse gear\n"
					"\t-s <string>  Send extra string to IBUS at startup\n"
					"\n",
					argv[0]);
				return -1;
		}
	}

	if (argc > optind)
	{
		port = argv[optind];
	}

	if (ibus_init(port, startup, bluetooth, camera, mk3, cdcinterval, gpio_number) != 0)
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
