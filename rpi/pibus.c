/*
 * pibus Copyright (c) 2013,2014 Peter Zelezny
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
	int hw_version = 0;
	int gpio_number = 18;
	const char *port = "/dev/ttyAMA0";
	int server_port = 55537;
	char *startup = NULL;
	int cdcinterval = 0;
	bool gpio_changed = FALSE;
	int idle_timeout = 300;
	bool aux = FALSE;
	bool handle_nextprev = FALSE;
	bool rotary_opposite = FALSE;
	bool z4_keymap = FALSE;

	mainloop_init();

	while ((opt = getopt(argc, argv, "c:g:p:s:t:v:z:abhmnorV")) != -1)
	{
		switch (opt)
		{
			case 'a':
				aux = 1;
				break;
			case 'b':
				bluetooth = 1;
				break;
			case 'c':
				cdcinterval = atoi(optarg);
				break;
			case 'g':
				gpio_number = atoi(optarg);
				gpio_changed = TRUE;
				break;
			case 'm':
				mk3 = 0;
				break;
			case 'n':
				handle_nextprev = TRUE;
				break;
			case 'o':
				rotary_opposite = TRUE;
				break;
			case 'p':
				server_port = atoi(optarg);
				break;
			case 'r':
				camera = 0;
				break;
			case 's':
				startup = strdup(optarg);
				break;
			case 't':
				idle_timeout = atoi(optarg);
				break;
			case 'v':
				hw_version = atoi(optarg);
				break;
			case 'V':
				printf("%s ["__DATE__"]\n", argv[0]);
				exit(0);
			case 'z':
				if (atoi(optarg) == 4)
				{
					z4_keymap = TRUE;
				}
				break;
			case 'h':
			default:
				fprintf(stderr,
					"Usage: %s [flags] [serial-port]\n"
					"\n"
					"Flags:\n"
					"\t-a           Use AUX input instead of CD changer (V4 boards only)\n"
					"\t-b           Car has bluetooth, don't use Phone and Speak buttons\n"
					"\t-c <time>    Force CDC-info replies every <time> seconds\n"
					"\t-g <number>  GPIO number to use for IBUS line monitor (0 = Use TH3122)\n"
					"\t-m           Do not do MK3 style CDC announcements\n"
					"\t-n           Handle Next/Prev buttons directly (only for some older radios)\n"
					"\t-o           Make rotary dial direction opposite\n"
					"\t-p           TCP server port number (default: 55537)\n"
					"\t-r           Do not switch to camera in reverse gear\n"
					"\t-s <string>  Send extra string to IBUS at startup\n"
					"\t-t <seconds> Set the idle timeout in seconds (V4 boards only, default 300)\n"
					"\t-v <number>  Set PiBUS hardware version\n"
					"\t-z4          Use alternative Z4 keymap\n"
					"\t-V           Show version information\n"
					"\n",
					argv[0]);
				return -1;
		}
	}

	if (argc > optind)
	{
		port = argv[optind];
	}

	if (!gpio_changed && hw_version >= 4)
	{
		gpio_number = 17;
	}

	if (gpio_init() != 0)
	{
		fprintf(stderr, "Can't init gpio\r\n");
		return -4;
	}

	if (ibus_init(port, startup, bluetooth, camera, mk3, cdcinterval, gpio_number, idle_timeout, hw_version, aux, handle_nextprev, rotary_opposite, z4_keymap, server_port) != 0)
	{
		return -2;
	}

	if (keyboard_init() != 0)
	{
		fprintf(stderr, "Can't open keyboard\r\n");
		return -3;
	}

	mainloop();

	gpio_cleanup();
	ibus_cleanup();
	keyboard_cleanup();

	return 0;
}
