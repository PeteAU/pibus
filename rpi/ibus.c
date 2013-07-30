#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <linux/input.h>
#include <sys/select.h>
#include <stdint.h>
#include <errno.h>

#include "keyboard.h"
#include "gpio.h"
#include "mainloop.h"
#include "ibus-send.h"

#define SOURCE 0
#define LENGTH 1
#define DEST 2
#define DATA 3


static struct
{
	bool have_time;
	bool have_date;
	bool playing;
	bool send_window_open;
	bool keyboard_blocked;

	uint64_t last_byte;
	int bufPos;
	unsigned char buf[64];
	int ifd;

	char hhmm[32];
}
ibus =
{
	.have_time = FALSE,
	.have_date = FALSE,
	.playing = FALSE,
	.send_window_open = FALSE,
	.keyboard_blocked = TRUE,

	.last_byte = 0,
	.bufPos = 0,
	.buf = {0,},
	.ifd = -1,

	.hhmm = {0,},
};


FILE *flog;


static void power_off(void)
{
	fflush(flog);
	fclose(flog);
	flog = NULL;

	system("/bin/sync");
	sleep(1);
	system("/sbin/poweroff");
}

static void ibus_handle_poweroff(const unsigned char *msg, int length)
{
	power_off();
}

static void dump_hex(FILE *out, const unsigned char *data, int length)
{
	int i;

	for (i = 0; i < length; i++)
	{
		fprintf(out, "%02x ", data[i]);
	}

	fprintf(out, "\n");
}

static void ibus_handle_date(const unsigned char *msg, int length)
{
	char buf[64];

	if (length < 16)
	{
		return;
	}

	if (ibus.have_time && !ibus.have_date)
	{
		snprintf(buf, sizeof(buf), "date -s \"%c%c%c%c-%c%c-%c%c %s\"", msg[12], msg[13], msg[14], msg[15], msg[9], msg[10], msg[6], msg[7], ibus.hhmm);
		system(buf);

		fprintf(flog, "date: %s\n", buf);

		ibus.have_date = TRUE;
	}
}

static void ibus_handle_time(const unsigned char *msg, int length)
{
/*
11/5/2009 4:13:52 PM.251:  A4 05 80 41 01 01 60
11/5/2009 4:13:52 PM.251:  ACM  --> IKE : On-board computer data request: Time: current value request
11/5/2009 4:13:52 PM.281:  80 0C FF 24 01 00 20 34 3A 30 38 50 4D 6D
11/5/2009 4:13:52 PM.281:  IKE  --> LOC : Update Text:  Layout=Time  Fld0,EndTx=" 4:08PM"
11/5/2009 4:13:52 PM.291:  A4 05 80 41 02 01 63
11/5/2009 4:13:52 PM.291:  ACM  --> IKE : On-board computer data request: Date: current value request
11/5/2009 4:13:52 PM.332:  80 0F FF 24 02 00 2D 2D 2F 2D 2D 2F 32 30 30 32 56
11/5/2009 4:13:52 PM.332:  IKE  --> LOC : Update Text:  Layout=Date  Fld0,EndTx="--/--/2002"

1/26/2010 5:04:11 PM.213:  3B 05 80 41 01 01 FF
1/26/2010 5:04:11 PM.213:  GT   --> IKE : On-board computer data request: Time: current value request
1/26/2010 5:04:11 PM.233:  3B 05 80 41 02 01 FC
1/26/2010 5:04:11 PM.233:  GT   --> IKE : On-board computer data request: Date: current value request
1/26/2010 5:04:11 PM.253:  80 0C E7 24 01 00 20 35 3A 30 34 50 4D 78
1/26/2010 5:04:11 PM.253:  IKE  --> ANZV: Update Text:  Layout=Time  Fld0,EndTx=" 5:04PM"
1/26/2010 5:04:11 PM.303:  80 0F E7 24 02 00 30 31 2F 32 36 2F 32 30 31 30 48
1/26/2010 5:04:11 PM.303:  IKE  --> ANZV: Update Text:  Layout=Date  Fld0,EndTx="01/26/2010"
*/
//	char buf[32];
	int hour;

	if (length < 13)
	{
		return;
	}

	hour = atoi((const char *)msg + 6);

	if (msg[11] == 'P')
	{
		hour += 12;
	}

	snprintf(ibus.hhmm, sizeof(ibus.hhmm), "%02d:%c%c", hour, msg[9], msg[10]);
	ibus.have_time = TRUE;

//	sprintf(buf, "date -s %s", ibus.hhmm);
//	system(buf);

	fprintf(flog, "time: %s\n", ibus.hhmm);
}

static void ibus_handle_rotary(const unsigned char *msg, int length)
{
	int i, key;

	if (length < 5 || ibus.keyboard_blocked)
	{
		return;
	}

	switch (msg[4] & 0xF0)
	{
		case 0x80:
			key = KEY_UP;
			break;

		case 0x00:
			key = KEY_DOWN;
			break;

		default:
			return;
	}

	for (i = 0; i < (msg[4] & 0x0F); i++)
	{
		keyboard_generate(key);
	}
}

static void ibus_handle_outsidekey(const unsigned char *msg, int length)
{
	ibus.keyboard_blocked = TRUE;
}

static void ibus_handle_screen(const unsigned char *msg, int length)
{
	if (length > 5)
	{
		fprintf(flog, "\033[31munknown screen 0x%02X\033[m\n", msg[4]);
	}
}


RODATA not_playing[]   = "\x18\x0a\x68\x39\x00\x02\x00\x01\x00\x01\x04\x45";
RODATA start_playing[] = "\x18\x0a\x68\x39\x02\x09\x00\x01\x00\x01\x04\x4c";
RODATA pause_playing[] = "\x18\x0a\x68\x39\x01\x0c\x00\x01\x00\x01\x04\x4a";

static void cdchanger_handle_inforeq(const unsigned char *msg, int length)
{
	if (ibus.playing)
	{
		/* This un-mutes the line-in */
		ibus_send(ibus.ifd, start_playing, 12);
	}
	else
	{
		ibus_send(ibus.ifd, not_playing, 12);
	}
}

static void cdchanger_handle_cdcmode(const unsigned char *msg, int length)
{
	//RODATA flash_led[] = "\xc8\x04\xe7\x2b\x30\x30";
	//ibus_send(ibus.ifd, flash_led, 6);

	//RODATA rpi_msg[] = "\x68\x12\x3b\x23\x62\x10Raspberry Pi\x20\x67";
	// 52 61 73 70 62 65 72 72 79 20 50 69
	//ibus_send(ibus.ifd, rpi_msg, 20);

	ibus.keyboard_blocked = FALSE;
	ibus.playing = TRUE;
}

static void cdchanger_handle_stop(const unsigned char *msg, int length)
{
	ibus_send(ibus.ifd, not_playing, 12);
	ibus.playing = FALSE;
}

static void cdchanger_handle_pause(const unsigned char *msg, int length)
{
	ibus_send(ibus.ifd, pause_playing, 12);
	ibus.playing = FALSE;
}

static void cdchanger_handle_start(const unsigned char *msg, int length)
{
	ibus_send(ibus.ifd, start_playing, 12);
	ibus.playing = TRUE;
}

static void cdchanger_handle_diskchange(const unsigned char *msg, int length)
{
	if (length != 7 || msg[6] != (0x4b ^ msg[5]))
	{
		return;
	}

	ibus_send(ibus.ifd, start_playing, 12);
}

static void cdchanger_handle_poll(const unsigned char *msg, int length)
{
	RODATA cdc_im_here[] = "\x18\x04\xFF\x02\x00\xE1";

	ibus_send(ibus.ifd, cdc_im_here, 6);
}



static const struct
{
	int match_length;
	char *ibusmsg;
	char *desc;
	char *command;
	unsigned int key;
	void (*function)(const unsigned char *msg, int length);
}
events[] =
{
	//{5, "\x50\x03\xC8\x01\x9A", "r/t", NULL, KEY_TAB},
	{6, "\xF0\x04\xFF\x48\x07\x44", "clock", NULL, KEY_ESC},
	//{6, "\xF0\x04\x68\x48\x10\xC4", "left", NULL, 0},
	//{6, "\xF0\x04\x68\x48\x00\xD4", "right", NULL, 0},
	{6, "\xF0\x04\x3B\x48\x05\x82", "enter", NULL, KEY_ENTER},
	{6, "\xF0\x04\x68\x48\x14\xC0", "<>", NULL, KEY_TAB},
	{4, "\xF0\x04\x3B\x49", "rotary", NULL, 0, ibus_handle_rotary},

	{6, "\xF0\x04\x68\x48\x11\xC5", "1", NULL, KEY_SPACE},
	{6, "\xF0\x04\x68\x48\x02\xD6", "4", NULL, KEY_I},

	{6, "\xF0\x04\x68\x48\x01\xD5", "2", NULL, KEY_Z},
	{6, "\xF0\x04\x68\x48\x13\xC7", "5", NULL, KEY_X},

	{6, "\xF0\x04\x68\x48\x12\xC6", "3", NULL, KEY_LEFT},
	{6, "\xF0\x04\x68\x48\x03\xD7", "6", NULL, KEY_RIGHT},

	{6, "\xF0\x04\x68\x48\x23\xF7", "mode", NULL, 0, ibus_handle_outsidekey},
	{6, "\xF0\x04\xFF\x48\x34\x77", "menu", NULL, 0, ibus_handle_outsidekey},
	{6, "\xF0\x04\x68\x48\x31\xE5", "FM", NULL, 0, ibus_handle_outsidekey},
	{6, "\xF0\x04\x68\x48\x21\xF5", "AM", NULL, 0, ibus_handle_outsidekey},
	{6, "\x68\x04\x3b\x46\x02\x13", "screen-mainmenu", NULL, 0, ibus_handle_outsidekey},
	{6, "\x68\x04\x3b\x46\x01\x10", "screen-none", NULL, 0},
	{6, "\x68\x04\x3b\x46\x04\x15", "screen-toneoff", NULL, 0},
	{6, "\x68\x04\x3b\x46\x08\x19", "screen-selectoff", NULL, 0},
	{6, "\x68\x04\x3b\x46\x0C\x1d", "screen-toneselectoff", NULL, 0},
	{4, "\x68\x04\x3b\x46", "screen-unknown", NULL, 0, ibus_handle_screen},

	{6, "\x50\x04\xc8\x3b\x80\x27", "speak", NULL, KEY_SPACE},
	{7, "\x44\x05\xBF\x74\x00\xFF\x75", "immobilized", NULL, 0},

	{5, "\x80\x0C\xFF\x24\x01", "time", NULL, 0, ibus_handle_time},
	{5, "\x80\x0F\xFF\x24\x02", "date", NULL, 0, ibus_handle_date},

	{5, "\x68\x03\x18\x01\x72", "cd-poll", NULL, 0, cdchanger_handle_poll},
	{7, "\x68\x05\x18\x38\x00\x00\x4d", "cd-info",  NULL, 0, cdchanger_handle_inforeq},
	{7, "\x68\x05\x18\x38\x01\x00\x4c", "cd-stop",  NULL, 0, cdchanger_handle_stop},
	{7, "\x68\x05\x18\x38\x02\x00\x4f", "cd-pause", NULL, 0, cdchanger_handle_pause},
	{7, "\x68\x05\x18\x38\x03\x00\x4e", "cd-start", NULL, 0, cdchanger_handle_start},
	{5, "\x68\x05\x18\x38\x06",         "cd-change",NULL, 0, cdchanger_handle_diskchange},
	{7, "\x68\x05\x18\x38\x0a\x01\x46", "cd-prev",  NULL, KEY_COMMA, cdchanger_handle_start},
	{7, "\x68\x05\x18\x38\x0a\x00\x47", "cd-next",  NULL, KEY_DOT, cdchanger_handle_start},
	{20,"\x68\x12\x3b\x23\x62\x10\x43\x44\x43\x20\x31\x2d\x30\x34\x20\x20\x20\x20\x20\x4c", "CDC 1-04", NULL, 0, cdchanger_handle_cdcmode},
};


static void ibus_handle_message(const unsigned char *msg, int length)
{
	int i;

	dump_hex(flog, msg, length);

	for (i = 0; i < sizeof(events) / sizeof(events[0]); i++)
	{
		if (events[i].match_length > length)
		{
			continue;
		}

		if (memcmp(msg, events[i].ibusmsg, events[i].match_length) == 0)
		{
			if (events[i].key && !ibus.keyboard_blocked)
			{
				keyboard_generate(events[i].key);
			}

			fprintf(flog, "ibus event: \033[32m%s\033[m\n", events[i].desc);

			if (events[i].command != NULL)
			{
				system(events[i].command);
			}

			if (events[i].function != NULL)
			{
				events[i].function(msg, length);
			}

			return;
		}
	}

	ibus_remove_from_queue(msg, length);
}

static void ibus_read(int condition, void *unused)
{
	unsigned char c;
	uint64_t now = mainloop_get_millisec();
	int r;

	while (1)
	{
		if ((r = read(ibus.ifd, &c, 1)) != 1)
		{
			if (r == -1)
			{
				int e = errno;
				if (e != EWOULDBLOCK)
				{
					printf("ifd=%d e=%d %s\n", ibus.ifd, e, strerror(e));
					exit(1);
				}
			}
			return;
		}

		if (now - ibus.last_byte > 100)
		{
			ibus.bufPos = 0;
		}
		ibus.last_byte = now;

		ibus.buf[ibus.bufPos] = c;
		if (ibus.bufPos < (sizeof(ibus.buf) - 1))
		{
			ibus.bufPos++;
		}

		ibus.send_window_open = FALSE;

		if (ibus.bufPos >= 4 && ibus.buf[LENGTH] + 2 == ibus.bufPos)
		{
			ibus_handle_message(ibus.buf, ibus.bufPos);
			ibus.bufPos = 0;
			ibus.send_window_open = TRUE;
		}
	}

}

/* every 50ms */

static int ibus_tick(void *unused)
{
	static int i = 0;
	static int j = 0;

	i++;
	if (i >= 5)
	{
		i = 0;
		/* 4 minute idle timeout */
		if (mainloop_get_millisec() - ibus.last_byte > 240000)
		{
			fprintf(flog, "idle timeout\n");
			power_off();
		}
	}

	j++;
	if (j >= 400)
	{
		j = 0;
		/* flush log & announce CD-changer every 20s */
		fflush(flog);
		RODATA cdc_announce[] = "\x18\x04\xFF\x02\x01\xE0";
		ibus_send(ibus.ifd, cdc_announce, 6);
	}

	ibus_service_queue(ibus.ifd, ibus.send_window_open);

	return 1;
}

int ibus_init(const char *port)
{
	struct termios newtio;

	ibus.ifd = open(port, O_RDWR | O_NOCTTY);
	if (ibus.ifd == -1)
	{
		fprintf(stderr, "Can't open ibus [%s] %s\n", port, strerror(errno));
		return -1;
	}

	memset(&newtio, 0, sizeof(newtio)); /* clear struct for new port settings */
 	newtio.c_cflag = B9600 | CS8 | CLOCAL | CREAD | PARENB;
	newtio.c_iflag = IGNPAR | IGNBRK;
	newtio.c_oflag = 0;
	newtio.c_lflag = 0;

	newtio.c_cc[VTIME] = 0;   /* inter-character timer unused */
	newtio.c_cc[VMIN] = 0;    /* !blocking read until 1 chars received */

	tcflush(ibus.ifd, TCIFLUSH);
	tcsetattr(ibus.ifd, TCSANOW, &newtio);

#ifdef __i386__
	flog = fopen("./ibus.txt", "a");
#else
	flog = fopen("/storage/ibus.txt", "a");
#endif
	if (flog == NULL)
	{
		fprintf(stderr, "Cannot write to log: %s\n", strerror(errno));
		close(ibus.ifd);
		ibus.ifd = -1;
		return -2;
	}

	fprintf(flog, "startup fd=%d ----------------------\n", ibus.ifd);
	fflush(flog);

	ibus.last_byte = mainloop_get_millisec();

	mainloop_input_add(ibus.ifd, FIA_READ, ibus_read, NULL);
	mainloop_timeout_add(50, ibus_tick, NULL);

	return 0;
}

void ibus_cleanup(void)
{
	/*if (ibus.ifd != -1)
	{
		close(ibus.ifd);
		ibus.ifd = -1;
	}*/
}
