/*
 * Copyright (c) 2013-2014 By Peter Zelezny.
 *
 * For personal individual use only. Other uses require written express permission.
 *
 */

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
#include <inttypes.h>
#include <errno.h>
#include <stdarg.h>
#include <linux/serial.h>
#include <pwd.h>

#include "keyboard.h"
#include "gpio.h"
#include "mainloop.h"
#include "ibus-send.h"
#include "ibus.h"
#include "server.h"
#include "log.h"

#define SOURCE 0
#define LENGTH 1
#define DEST 2
#define DATA 3

/* Based on PiBUS V4.01 hardware */

#define GPIO_NSLP_CTL		22
#define GPIO_PIN17_CTL		23
#define GPIO_LED_CTL		24
#define GPIO_RELAY_CTL		27


typedef enum
{
	VIDEO_SRC_BMW = 0,
	VIDEO_SRC_PI = 1,
	VIDEO_SRC_CAMERA = 2,
	VIDEO_SRC_LAST = 2
}
videoSource_t;

static struct
{
	bool have_time;
	bool have_date;
	bool playing;
	bool keyboard_blocked;
	bool bluetooth;
	bool have_camera;
	bool cdc_announce;
	bool set_gps_time;
	bool aux;
	bool handle_nextprev;
	bool rotary_opposite;
	bool z4_keymap;

	uint64_t last_byte;
	int bufPos;
	unsigned char buf[192];
	char *port_name;
	int ifd;
	int ifd_tag;
	int read_msgs;
	int bytes_read;
	int cdc_info_tag;
	int cdc_info_interval;
	int cdc_timeouts;
	int gpio_number;
	int idle_timeout;
	int hw_version;
	int num_time_requests;

	videoSource_t videoSource;

	struct
	{
		int year;
		int month;
		int day;
		int hours;
		int minutes;
		int seconds;
	}
	datetime;
}
ibus =
{
	.have_time = FALSE,
	.have_date = FALSE,
	.playing = FALSE,
	.keyboard_blocked = TRUE,
	.bluetooth = FALSE,
	.have_camera = TRUE,
	.cdc_announce = TRUE,
	.set_gps_time = FALSE,
	.aux = FALSE,
	.handle_nextprev = FALSE,
	.rotary_opposite = FALSE,
	.z4_keymap = FALSE,

	.last_byte = 0,
	.bufPos = 0,
	.buf = {0,},
	.port_name = NULL,
	.ifd = -1,
	.ifd_tag = -1,
	.read_msgs = 0,
	.bytes_read = 0,
	.cdc_info_tag = -1,
	.cdc_info_interval = 0,
	.cdc_timeouts = 0,
	.gpio_number = 0,
	.idle_timeout = 0,
	.hw_version = 0,
	.num_time_requests = 0,

	.videoSource = VIDEO_SRC_BMW,
};


static void power_off(void)
{
	if (ibus.hw_version >= 4)
	{
		gpio_write(GPIO_LED_CTL, 1);
	}

	log_close();

	if (ibus.ifd_tag != -1)
	{
		mainloop_input_remove(ibus.ifd_tag);
		ibus.ifd_tag = -1;
	}

	if (ibus.ifd != -1)
	{
		close(ibus.ifd);
		ibus.ifd = -1;
	}

	if (access("/storage/pibus-poweroff.sh", X_OK) == 0)
	{
		execl("/bin/sh", "sh", "-c", "/storage/pibus-poweroff.sh", (char *) 0);
		/* doesn't return */
	}

	system("/bin/sync");
	sleep(1);

	if (access("/usr/sbin/poweroff", F_OK) == 0)
		system("/usr/sbin/poweroff");
	else
		system("/sbin/poweroff");
}

/* Execute an external command in the HOMEDIR */

static void ibus_exec(const char *cmd, const char *arg)
{
	int pid;
	char path[256];
	struct passwd *pw;
	const char *homedir;

	pw = getpwuid(getuid());
	homedir = pw ? pw->pw_dir : "/storage";

	snprintf(path, sizeof(path), "%s/%s", homedir, cmd);
	path[sizeof(path) - 1] = 0;

	if (access(path, X_OK) == 0)
	{
		pid = fork();
		if (pid == 0)
		{
			execlp(path, cmd, arg, NULL);
			_exit(0);
		}
	}
}

static void ibus_set_video(videoSource_t src)
{
	switch (src)
	{
		case VIDEO_SRC_BMW:
			gpio_write(GPIO_RELAY_CTL, 0);	/* relay off */
			gpio_write(GPIO_PIN17_CTL, 0);	/* pin17 off */
			break;

		case VIDEO_SRC_PI:
			gpio_write(GPIO_RELAY_CTL, 0);	/* relay off */
			gpio_write(GPIO_PIN17_CTL, 1);	/* pin17 on */
			break;

		case VIDEO_SRC_CAMERA:
			gpio_write(GPIO_RELAY_CTL, 1);	/* relay on */
			gpio_write(GPIO_PIN17_CTL, 1);	/* pin17 on */
			break;
	}
}

static void ibus_handle_phone(const unsigned char *msg, int length)
{
	if (ibus.hw_version >= 4 && !ibus.bluetooth)
	{
		ibus.videoSource++;
		if (ibus.videoSource > VIDEO_SRC_LAST)
		{
			ibus.videoSource = 0;
		}
		ibus_set_video(ibus.videoSource);
	}
}

static void ibus_handle_ike_sensor(const unsigned char *msg, int length)
{
	if (ibus.hw_version >= 4 && ibus.have_camera)
	{
		switch (msg[5] >> 4)
		{
			case 1:	/* reverse */
				ibus_set_video(VIDEO_SRC_CAMERA);
				break;
			default:	/* any other gear */
				ibus_set_video(ibus.videoSource);
				break;
		}
	}

	switch (msg[5] >> 4)
	{
		case 1:	/* reverse */
			ibus_exec("pibus-event.sh", "gear_reverse");
			break;
		default:	/* any other gear */
			ibus_exec("pibus-event.sh", "gear_other");
			break;
	}
}

static bool ibus_good_checksum(const unsigned char *msg, int length)
{
	unsigned char sum;
	int i;

	sum = msg[0];
	for (i = 1; i < (length - 1); i++)
	{
		sum ^= msg[i];
	}

	if (sum != msg[length - 1])
	{
		return FALSE;
	}

	return TRUE;
}

static void ibus_request_time(void)
{
	/* CDChanger asks IKE for Time */
	RODATA rt[] = "\x18\x05\x80\x41\x01\x01\xDC";

	ibus_remove_tag_from_queue(TAG_TIME);
	ibus_send_with_tag(ibus.ifd, rt, 7, ibus.gpio_number, FALSE, FALSE, TAG_TIME);
}

static void ibus_request_date(void)
{
	/* CDChanger asks IKE for Date */
	RODATA rd[] = "\x18\x05\x80\x41\x02\x01\xDF";

	ibus_remove_tag_from_queue(TAG_DATE);
	ibus_send_with_tag(ibus.ifd, rd, 7, ibus.gpio_number, FALSE, FALSE, TAG_DATE);
}

static void ibus_set_time_and_date(bool change_date, bool change_time)
{
	struct tm *tm;
	time_t now;

	if (ibus.have_time && ibus.have_date)
	{
		now = time(NULL);
		tm = gmtime(&now);
		tm->tm_isdst = -1;

		if (change_time)
		{
			tm->tm_sec = ibus.datetime.seconds;
			tm->tm_min = ibus.datetime.minutes;
			tm->tm_hour = ibus.datetime.hours;
		}

		if (change_date)
		{
			tm->tm_mday = ibus.datetime.day;
			tm->tm_mon = ibus.datetime.month - 1;
			tm->tm_year = ibus.datetime.year - 1900;
		}

		now = mktime(tm);
		stime(&now);

		if (change_date && change_time)
		{
			log_msg("setting date: %04d/%02d/%02d %02d:%02d:%02d\n",
				 ibus.datetime.year, ibus.datetime.month, ibus.datetime.day,
				 ibus.datetime.hours, ibus.datetime.minutes, ibus.datetime.seconds);
		}
		else if (change_time)
		{
			log_msg("setting date: ----/--/-- %02d:%02d:%02d\n",
				 ibus.datetime.hours, ibus.datetime.minutes, ibus.datetime.seconds);
		}
	}
}

static int atoin(const unsigned char *str, int n)
{
	char buf[8];

	memcpy(buf, str, n);
	buf[n] = 0;

	return atoi(buf);
}

static void ibus_handle_date(const unsigned char *msg, int length)
{
	if (length > 15 && !ibus.have_date)
	{
		ibus.have_date = TRUE;

		ibus.datetime.day = atoin(msg + 6, 2);
		ibus.datetime.month = atoin(msg + 9, 2);
		ibus.datetime.year = atoin(msg + 12, 4);

		ibus_set_time_and_date(TRUE, TRUE);
		ibus_remove_tag_from_queue(TAG_DATE);
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
	if (length > 12 && !ibus.have_time)
	{
		ibus.have_time = TRUE;

		ibus.datetime.hours = atoin(msg + 6, 2);
		ibus.datetime.minutes = atoin(msg + 9, 2);
		ibus.datetime.seconds = 1;

		if (msg[11] == 'P')
		{
			ibus.datetime.hours += 12;
		}

		ibus_set_time_and_date(TRUE, TRUE);
		ibus_remove_tag_from_queue(TAG_TIME);
	}
}

static void ibus_handle_gps(const unsigned char *msg, int length)
{
	int gps_minutes;
	int gps_seconds;
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	int offset;

// 05.06.2011 19:52:03.334: 7F 14 C8 A2 01 00 49 34 02 10 00 10 59 31 00 02 89 00 17 52 01 D8
// 05.06.2011 19:52:03.334: NAV --> TEL : Current position: GPS fix; 49°34'2.1"N 10°59'31.0"E; Alt 289m; UTC 17:52:01

	if (length != 22)
	{
		return;
	}

	gps_minutes = ((msg[19] >> 4) * 10) + (msg[19] & 0x0F);
	gps_seconds = ((msg[20] >> 4) * 10) + (msg[20] & 0x0F);

	if (gps_minutes > 2 && gps_minutes < 57 && tm->tm_min > 2 && tm->tm_min < 57)
	{
		offset = abs(((gps_minutes * 60) + gps_seconds) - ((tm->tm_min * 60) + tm->tm_sec));

		if ((offset >= 30 && offset <= 180) || (!ibus.set_gps_time && offset <= 180))
		{
			ibus.set_gps_time = TRUE;

			ibus.datetime.hours = tm->tm_hour;
			ibus.datetime.minutes = gps_minutes;
			ibus.datetime.seconds = gps_seconds;

			ibus_set_time_and_date(FALSE, TRUE);
		}
	}
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
			key = ibus.rotary_opposite ? KEY_DOWN : KEY_UP;
			break;

		case 0x00:
			key = ibus.rotary_opposite ? KEY_UP : KEY_DOWN;
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

	if (ibus.hw_version >= 4)
	{
		ibus.videoSource = VIDEO_SRC_BMW;
		ibus_set_video(ibus.videoSource);
	}
}

static void ibus_handle_speak(const unsigned char *msg, int length)
{
	if (!ibus.keyboard_blocked && !ibus.bluetooth)
	{
		keyboard_generate(KEY_SPACE);
	}
}

static void ibus_handle_immobilized(const unsigned char *msg, int length)
{
	if (ibus.cdc_info_tag != -1)
	{
		mainloop_timeout_remove(ibus.cdc_info_tag);
		ibus.cdc_info_tag = -1;
	}
}


RODATA not_playing[]   = "\x18\x0a\x68\x39\x00\x02\x00\x01\x00\x01\x04\x45";
RODATA start_playing[] = "\x18\x0a\x68\x39\x02\x09\x00\x01\x00\x01\x04\x4c";
RODATA pause_playing[] = "\x18\x0a\x68\x39\x01\x0c\x00\x01\x00\x01\x04\x4a";

static void cdchanger_send_inforeq(void)
{
	ibus_remove_tag_from_queue(TAG_CDC);

	if (ibus.playing)
	{
		/* This un-mutes the line-in */
		ibus_send_with_tag(ibus.ifd, start_playing, 12, ibus.gpio_number, TRUE, TRUE, TAG_CDC);
	}
	else
	{
		ibus_send_with_tag(ibus.ifd, not_playing, 12, ibus.gpio_number, TRUE, TRUE, TAG_CDC);
	}

	/* No more announcements */
	ibus.cdc_announce = FALSE;
}

static int cdchanger_interval_timeout(void *unused)
{
	ibus.cdc_timeouts++;
	if (ibus.cdc_timeouts >= 5)
	{
		/* The car has stopped issuing cd-info requests for
		 * some reason and no immobilized event occured!
		 * Avoid battery drain and staying awake.
		 */
		log_msg("too many cdc interval timeouts\n");
		return 0;
	}

	log_msg("cdc interval timeout (%d s)\n", ibus.cdc_info_interval);
	cdchanger_send_inforeq();
	return 1;
}

static void cdchanger_handle_inforeq(const unsigned char *msg, int length)
{
	if (ibus.aux)
	{
		return;
	}

	ibus.cdc_timeouts = 0;
	cdchanger_send_inforeq();

	if (ibus.cdc_info_interval > 0)
	{
		if (ibus.cdc_info_tag != -1)
		{
			mainloop_timeout_remove(ibus.cdc_info_tag);
		}

		ibus.cdc_info_tag = mainloop_timeout_add(ibus.cdc_info_interval * 1000, cdchanger_interval_timeout, NULL);
	}
}

static void enter_pi_screen(const unsigned char *msg, int length)
{
	ibus.keyboard_blocked = FALSE;
	ibus.playing = TRUE;

	if (ibus.hw_version >= 4)
	{
		ibus.videoSource = VIDEO_SRC_PI;
		ibus_set_video(ibus.videoSource);
	}
}

static void cdchanger_handle_stop(const unsigned char *msg, int length)
{
	if (ibus.aux)
	{
		return;
	}

	ibus_send(ibus.ifd, not_playing, 12, ibus.gpio_number);
	ibus.playing = FALSE;

	if (ibus.cdc_info_tag != -1)
	{
		mainloop_timeout_remove(ibus.cdc_info_tag);
		ibus.cdc_info_tag = -1;
	}
}

static void cdchanger_handle_pause(const unsigned char *msg, int length)
{
	if (ibus.aux)
	{
		return;
	}

	ibus_send(ibus.ifd, pause_playing, 12, ibus.gpio_number);
	ibus.playing = FALSE;
}

static void cdchanger_handle_start(const unsigned char *msg, int length)
{
	if (ibus.aux)
	{
		return;
	}

	ibus_send(ibus.ifd, start_playing, 12, ibus.gpio_number);
	ibus.playing = TRUE;
}

static void cdchanger_handle_diskchange(const unsigned char *msg, int length)
{
	if (length != 7 || msg[6] != (0x4b ^ msg[5]))
	{
		return;
	}

	if (ibus.aux)
	{
		return;
	}

	ibus_send(ibus.ifd, start_playing, 12, ibus.gpio_number);
}

static void cdchanger_handle_poll(const unsigned char *msg, int length)
{
	if (ibus.aux)
	{
		return;
	}

	RODATA cdc_im_here[] = "\x18\x04\xFF\x02\x00\xE1";

	ibus_send(ibus.ifd, cdc_im_here, 6, ibus.gpio_number);
}

/*
	Somebody on the internet said:
	When the I-Bus wakes up, the CD player starts to announce it-self ("02 01" msg) every 30 secondes
	until the radio poll ("01"). At the first poll, the CD will send a poll response ("02 00"),
	then will respond to each next poll (every 30 secondes).
	If the CD doesn't respond to the poll, the radio considers that there is no CD Player (or not anymore).
*/
static void cdchanger_announce()
{
	if (ibus.cdc_announce)
	{
		RODATA cdc_announce[] = "\x18\x04\xFF\x02\x01\xE0";
		ibus_send(ibus.ifd, cdc_announce, 6, ibus.gpio_number);

		/* Don't do it again */
		ibus.cdc_announce = FALSE;
	}
}

static void ibus_handle_aux(const unsigned char *buf, int length)
{
	if (ibus.aux)
	{
		enter_pi_screen(buf, length);
	}
	else
	{
		ibus_handle_outsidekey(buf, length);
	}
}

static bool is_cdc_message(const unsigned char *buf, int length)
{
	/* Copied from attiny code */

	if (length == 20 &&
		buf[0] == 0x68 &&
		buf[6] == 0x43 &&
		buf[13] == 0x34 &&
		buf[19] == 0x4c)
	{
		log_msg("CDC: %s\n", "CDC 1-04");
		return TRUE;
	}

	if (length >= 16 &&
		buf[0] == 0x68 &&
		buf[6] == 0x54 &&
		buf[7] == 0x52 &&
		buf[8] == 0x20 &&
		buf[9] == 0x30 &&
		buf[10] == 0x34)
	{
		log_msg("CDC: %s\n", "TR 04");
		return TRUE;
	}

	if (length == 25 &&
		buf[0] == 0x68 &&
		buf[15] == 0x43 &&
		buf[16] == 0x44 &&
		buf[18] == 0x31 &&
		buf[20] == 0x30 &&
		buf[21] == 0x34 &&
		(buf[24] == 0x25 || buf[24] == 0x35))
	{
		log_msg("CDC: %s\n", "CD 1-04");
		return TRUE;
	}

	/* USA 1998 750iL (AlpineWhiteV12) */
	if (length == 14 &&
		memcmp(buf, "\x68\x0c\x3b\x23\xc4\x20\x43\x44\x20\x31\x2d\x30\x34\xa7", 14) == 0)
	{
		log_msg("CDC: %s\n", "US CD 1-04");
		return TRUE;
	}

	/* Norway 2004 E83 MK4 (WazKid) */
	if (length == 15 &&
		memcmp(buf, "\x68\x0d\x3b\x23\x62\x10\x43\x44\x43\x20\x31\x2d\x30\x34\x73", 15) == 0)
	{
		log_msg("CDC: %s\n", "E83 CDC 1-04");
		return TRUE;
	}

	/* Spain E39 530d MK2 (phantrax) */
	if (length == 18 &&
		memcmp(buf, "\x68\x10\x3b\x23\xc4\x30\x43\x44\x20\x31\x2d\x30\x34\x20\x20\x20\x20\xab", 18) == 0)
	{
		log_msg("CDC: %s\n", "E39 CD 1-04");
		return TRUE;
	}

	/* Germany X3 (Tom Z.) */
	if (length == 12 &&
		memcmp(buf, "\x68\x0a\x3b\x23\x62\x10\x54\x52\x20\x30\x34\x2a", 12) == 0)
	{
		log_msg("CDC: %s\n", "X3 TR 04");
		return TRUE;
	}

	{
		static bool have_bin_msg = FALSE;
		static unsigned char cdc_msg[64];
		static int cdc_len;

		struct stat st;
		int len;
		int fd;

		if (!have_bin_msg)
		{
			fd = open("/storage/pibus-cdc.bin", O_RDONLY);
			if (fd != -1)
			{
				if (fstat(fd, &st) == 0)
				{
					len = st.st_size;
					if (len > sizeof (cdc_msg))
					{
						len = sizeof (cdc_msg);
					}
					if (read(fd, cdc_msg, len) == len)
					{
						have_bin_msg = TRUE;
						cdc_len = len;
					}
				}
				close(fd);
			}
		}

		if (have_bin_msg)
		{
			if (length == cdc_len &&
			    memcmp(buf, cdc_msg, length) == 0)
			{
				log_msg("CDC: %s\n", "CDC BIN");
				return TRUE;
			}
		}
	}

	return FALSE;
}

static void ibus_handle_prev(const unsigned char *buf, int length)
{
	if (!ibus.keyboard_blocked && ibus.handle_nextprev)
	{
		keyboard_generate(KEY_COMMA);
	}
}

static void ibus_handle_next(const unsigned char *buf, int length)
{
	if (!ibus.keyboard_blocked && ibus.handle_nextprev)
	{
		keyboard_generate(KEY_DOT);
	}
}

static void ibus_l1(const unsigned char *buf, int length)
{
	if (ibus.keyboard_blocked)
	{
		return;
	}

	if (ibus.z4_keymap)
		keyboard_generate(KEY_I);
}

static void ibus_l2(const unsigned char *buf, int length)
{
}

static void ibus_l3(const unsigned char *buf, int length)
{
}

static void ibus_l4(const unsigned char *buf, int length)
{
}

static void ibus_l5(const unsigned char *buf, int length)
{
}

static void ibus_l6(const unsigned char *buf, int length)
{
}

static void ibus_handle_3(const unsigned char *buf, int length)
{
	if (ibus.keyboard_blocked)
	{
		return;
	}

	if (ibus.z4_keymap)
		keyboard_generate(KEY_X);
	else
		keyboard_generate(KEY_LEFT);
}

static void ibus_handle_4(const unsigned char *buf, int length)
{
	if (ibus.keyboard_blocked)
	{
		return;
	}

	if (ibus.z4_keymap)
		keyboard_generate(KEY_BACKSPACE);
	else
		keyboard_generate(KEY_I);
}

static void ibus_handle_5(const unsigned char *buf, int length)
{
	if (ibus.keyboard_blocked)
	{
		return;
	}

	if (ibus.z4_keymap)
		keyboard_generate(KEY_LEFT);
	else
		keyboard_generate(KEY_X);
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
	{6, "\xF0\x04\xFF\x48\x07\x44", "clock", NULL, KEY_BACKSPACE},
	{6, "\xF0\x04\x3B\x48\x05\x82", "enter", NULL, KEY_ENTER},
	{6, "\xF0\x04\x68\x48\x14\xC0", "<>", NULL, KEY_TAB},
	{4, "\xF0\x04\x3B\x49", "rotary", NULL, 0, ibus_handle_rotary},

	{6, "\xF0\x04\x68\x48\x04\xD0", "tone", NULL, 0, ibus_handle_outsidekey},
	{6, "\xF0\x04\x68\x48\x20\xF4", "select", NULL, 0, ibus_handle_outsidekey},
	{7, "\xf0\x05\xff\x47\x00\x0f\x42", "select", NULL, 0, ibus_handle_outsidekey},
	{7, "\xF0\x05\xFF\x47\x00\x38\x75", "info", NULL, 0, ibus_handle_outsidekey},

	/* Steering wheel */
	{6, "\x50\x04\x68\x3B\x01\x06", NULL, NULL, 0, ibus_handle_next},
	{6, "\x50\x04\x68\x3B\x08\x0F", NULL, NULL, 0, ibus_handle_prev},

	/* Board monitor */
	{6, "\xF0\x04\x68\x48\x00\xD4", NULL, NULL, 0, ibus_handle_next},
	{6, "\xF0\x04\x68\x48\x10\xC4", NULL, NULL, 0, ibus_handle_prev},

	{6, "\xF0\x04\x68\x48\x40\x94", "FF", NULL, KEY_RIGHT|_CTRL_BIT},
	{6, "\xF0\x04\x68\x48\x50\x84", "RR", NULL, KEY_LEFT|_CTRL_BIT},

	{6, "\xF0\x04\x68\x48\x51\x85", "L1", NULL, 0, ibus_l1},
	{6, "\xF0\x04\x68\x48\x41\x95", "L2", NULL, KEY_TAB},
//	{6, "\xF0\x04\x68\x48\x52\x86", "L3", NULL, 0, ibus_l3},
	{6, "\xF0\x04\x68\x48\x42\x96", "L4", NULL, KEY_ESC},
//	{6, "\xF0\x04\x68\x48\x53\x87", "L5", NULL, 0, ibus_l5},
//	{6, "\xF0\x04\x68\x48\x43\x97", "L6", NULL, 0, ibus_l6},

	{6, "\xF0\x04\x68\x48\x11\xC5", "1", NULL, KEY_SPACE},
	{6, "\xF0\x04\x68\x48\x02\xD6", "4", NULL, 0, ibus_handle_4},

	{6, "\xF0\x04\x68\x48\x01\xD5", "2", NULL, KEY_Z},
	{6, "\xF0\x04\x68\x48\x13\xC7", "5", NULL, 0, ibus_handle_5},

	{6, "\xF0\x04\x68\x48\x12\xC6", "3", NULL, 0, ibus_handle_3},
	{6, "\xF0\x04\x68\x48\x03\xD7", "6", NULL, KEY_RIGHT},

	{6, "\xF0\x04\x68\x48\x23\xF7", "mode", NULL, 0, ibus_handle_outsidekey},
	{6, "\xF0\x04\xFF\x48\x34\x77", "menu", NULL, 0, ibus_handle_outsidekey},
	{6, "\xF0\x04\x68\x48\x31\xE5", "FM", NULL, 0, ibus_handle_outsidekey},
	{6, "\xF0\x04\x68\x48\x21\xF5", "AM", NULL, 0, ibus_handle_outsidekey},
	{6, "\x68\x04\x3b\x46\x02\x13", "screen-mainmenu", NULL, 0, ibus_handle_outsidekey},

	{6, "\x50\x04\xc8\x3b\x80\x27", "speak", NULL, 0, ibus_handle_speak},
	{7, "\x44\x05\xBF\x74\x00\xFF\x75", "immobilized", NULL, 0, ibus_handle_immobilized},

	/* For MG Rover */
	{5, "\x80\x0C\x3B\x24\x01", "time", NULL, 0, ibus_handle_time},
	{5, "\x80\x0F\x3B\x24\x02", "date", NULL, 0, ibus_handle_date},

	/* For BMW */
	{5, "\x80\x0C\xFF\x24\x01", "time", NULL, 0, ibus_handle_time},
	{5, "\x80\x0F\xFF\x24\x02", "date", NULL, 0, ibus_handle_date},

	{5, "\x80\x0C\xE7\x24\x01", "time", NULL, 0, ibus_handle_time},
	{5, "\x80\x0F\xE7\x24\x02", "date", NULL, 0, ibus_handle_date},

	{5, "\x7F\x14\xC8\xA2\x01", NULL/*"GPS"*/, NULL, 0, ibus_handle_gps},

	{5, "\x68\x03\x18\x01\x72",         "cd-poll",  NULL, 0, cdchanger_handle_poll},
	{7, "\x68\x05\x18\x38\x00\x00\x4d", "cd-info",  NULL, 0, cdchanger_handle_inforeq},
	{7, "\x68\x05\x18\x38\x01\x00\x4c", "cd-stop",  NULL, 0, cdchanger_handle_stop},
	{7, "\x68\x05\x18\x38\x02\x00\x4f", "cd-pause", NULL, 0, cdchanger_handle_pause},
	{7, "\x68\x05\x18\x38\x03\x00\x4e", "cd-start", NULL, 0, cdchanger_handle_start},
	{5, "\x68\x05\x18\x38\x06",         "cd-change",NULL, 0, cdchanger_handle_diskchange},
	{7, "\x68\x05\x18\x38\x0a\x01\x46", "cd-prev",  NULL, KEY_COMMA, cdchanger_handle_start},
	{7, "\x68\x05\x18\x38\x0a\x00\x47", "cd-next",  NULL, KEY_DOT, cdchanger_handle_start},

	/* These are handled by the ATtiny on V2 and V3 boards */
	{6, "\xF0\x04\xFF\x48\x08\x4B", "phone", NULL, 0, ibus_handle_phone},
	{4, "\x80\x0C\xBF\x13", "IKE sensor", NULL, 0, ibus_handle_ike_sensor},
	{4, "\x80\x0A\xBF\x13", "IKE sensor", NULL, 0, ibus_handle_ike_sensor},
	{4, "\x80\x09\xBF\x13", "IKE sensor", NULL, 0, ibus_handle_ike_sensor},

	{20,"\x68\x12\x3b\x23\x62\x10\x41\x55\x58\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x5c", NULL/*"aux"*/, NULL, 0, ibus_handle_aux},
	{10,"\x68\x08\x3b\x23\x62\x10\x41\x55\x58\x46", NULL/*"aux"*/, NULL, 0, ibus_handle_aux},

#if 0
	/* The most common CDC message */
	{20,"\x68\x12\x3b\x23\x62\x10\x43\x44\x43\x20\x31\x2d\x30\x34\x20\x20\x20\x20\x20\x4c", "CDC 1-04", NULL, 0, cdchanger_handle_cdcmode},

	/* This one was seen on an Adelaide M3 (onefifty370) */
	{20,"\x68\x12\x3b\x23\x62\x10\x54\x52\x20\x30\x34\x20\x20\x20\x20\x20\x20\x20\x20\x32", "TR 04L", NULL, 0, cdchanger_handle_cdcmode},

	/* This one was seen on a Lithuanian M3 (realvtk) */
	{16,"\x68\x0e\x3b\x23\x62\x10\x54\x52\x20\x30\x34\x20\x20\x20\x20\x2e", "TR 04S", NULL, 0, cdchanger_handle_cdcmode},

	/* This one was seen on a German E39 525i 05/2001 MK3 BM24 (DK) */
	{25,"\x68\x17\x3b\x23\x62\x30\x20\x20\x07\x20\x20\x20\x20\x20\x08\x43\x44\x20\x31\x2d\x30\x34\x20\x20\x25", "CD 1-04", NULL, 0, cdchanger_handle_cdcmode},

	/* This one was seen on a Finland MK2 */
	{25,"\x68\x17\x3b\x23\x62\x20\x20\x20\x07\x20\x20\x20\x20\x20\x08\x43\x44\x20\x31\x2d\x30\x34\x20\x20\x35", "CD 1-04", NULL, 0, cdchanger_handle_cdcmode},

	/* This one was seen on a Norway E83 MK4 (WazKid) */
	{15,"\x68\x0d\x3b\x23\x62\x10\x43\x44\x43\x20\x31\x2d\x30\x34\x73", "E83 CDC 1-04", NULL, 0, cdchanger_handle_cdcmode},

	/* This one was seen on a Spain E39 530d (phantrax) */
	{18,"\x68\x10\x3b\x23\xc4\x30\x43\x44\x20\x31\x2d\x30\x34\x20\x20\x20\x20\xab", "E39-530d CD 1-04", NULL, 0, cdchanger_handle_cdcmode},phantrax
#endif
};


static void ibus_handle_message(const unsigned char *msg, int length, const char *suffix, bool recovered)
{
	int i;

	log_ibus(msg, length, suffix);

	server_handle_message(msg, length);

	/* are we entering the CDC screen? */
	if (!ibus.aux && is_cdc_message(msg, length))
	{
		enter_pi_screen(msg, length);
	}

	/* got a message from the radio */
	if (msg[0] == 0x68 && !ibus.aux)
	{
		cdchanger_announce();
	}

	ibus.read_msgs++;

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

			if (events[i].command != NULL)
			{
				system(events[i].command);
			}

			if (events[i].function != NULL)
			{
				events[i].function(msg, length);
			}

			break;
		}
	}

	if (!recovered)
	{
		ibus_remove_from_queue(msg, length);
	}
}

static void ibus_discard_bytes(int number_of_bytes)
{
	ibus.bufPos -= number_of_bytes;

	/* bug? */
	if (ibus.bufPos < 0)
	{
		ibus.bufPos = 0;
	}
	else if (ibus.bufPos)
	{
		memmove(ibus.buf, ibus.buf + number_of_bytes, ibus.bufPos);
	}
}

static bool ibus_discard_receive_buffer(void)
{
	int len;
	bool recovered = FALSE;

	while (ibus.bufPos >= 5)
	{
		/* discard the 1st byte and try again */
		ibus_discard_bytes(1);

		len = ibus.buf[LENGTH] + 2;

		while (len >= 4 && len <= ibus.bufPos && ibus_good_checksum(ibus.buf, len))
		{
			recovered = TRUE;

			ibus_handle_message(ibus.buf, len, "recover", TRUE);
			ibus_discard_bytes(len);

			if (!(ibus.bufPos >= 5))
			{
				break;
			}

			len = ibus.buf[LENGTH] + 2;
		}
	}

	ibus.bufPos = 0;

	return recovered;
}

static void ibus_read(int condition, void *unused)
{
	unsigned char c;
	uint64_t now;
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

		now = mainloop_get_millisec();

		if (now - ibus.last_byte > 64 && ibus.bufPos)
		{
			log_msg_with_hex(ibus.buf, ibus.bufPos, "ibus_read(): discard %d: ", ibus.bufPos);
			ibus_discard_receive_buffer();
		}
		ibus.last_byte = now;

		ibus.buf[ibus.bufPos] = c;
		if (ibus.bufPos < (sizeof(ibus.buf) - 1))
		{
			ibus.bufPos++;
		}

		ibus.bytes_read++;

retry:
		if (ibus.bufPos >= 4 && (ibus.buf[LENGTH] + 2) == ibus.bufPos)
		{
			if (!ibus_good_checksum(ibus.buf, ibus.bufPos))
			{
				log_ibus(ibus.buf, ibus.bufPos, "corrupt");

				while (ibus.bufPos >= 5)
				{
					/* discard the 1st byte and try again */
					ibus_discard_bytes(1);

					int len = ibus.buf[LENGTH] + 2;
					bool recovered = FALSE;

					while (len <= ibus.bufPos && ibus_good_checksum(ibus.buf, len))
					{
						recovered = TRUE;

						ibus_handle_message(ibus.buf, len, "recover", TRUE);
						ibus_discard_bytes(len);

						if (!(ibus.bufPos >= 4))
						{
							//ibus.bufPos = 0;
							break;
						}

						len = ibus.buf[LENGTH] + 2;
					}

					if (recovered)
					{
						goto retry;
					}
				}

				/* bad... improve this path! */
			}
			else
			{
				ibus_handle_message(ibus.buf, ibus.bufPos, "", FALSE);
			}

			ibus.bufPos = 0;
		}
	}

}

static int ibus_init_serial_port(bool have_log)
{
	struct termios newtio;
	struct serial_struct ser;

	if (ibus.ifd_tag != -1)
	{
		mainloop_input_remove(ibus.ifd_tag);
		ibus.ifd_tag = -1;
		close(ibus.ifd);
	}

	ibus.ifd = open(ibus.port_name, O_RDWR | O_NOCTTY | O_SYNC);
	if (ibus.ifd == -1)
	{
		if (have_log)
			log_msg("Can't open ibus [%s] %s\n", ibus.port_name, strerror(errno));
		else
			fprintf(stderr, "Can't open ibus [%s] %s\n", ibus.port_name, strerror(errno));
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

	ioctl(ibus.ifd, TIOCGSERIAL, &ser);
	ser.flags |= ASYNC_LOW_LATENCY;
	ioctl(ibus.ifd, TIOCSSERIAL, &ser);

	ibus.ifd_tag = mainloop_input_add(ibus.ifd, FIA_READ, ibus_read, NULL);

	return 0;
}

/* every one second */

static int ibus_1s_tick(void *unused)
{
	static int i = 0;

	i++;
	if (i >= 30)
	{
		i = 0;
	}

	/* flush log every 30s */
	if (i == 4)
	{
		log_flush();
	}

	/* every 15s */
	if ((i == 8 || i == 23) && ibus.num_time_requests <= 3)
	{
		if (ibus.read_msgs == 0)
		{
			log_msg("ibus idle\n");
		}
		else
		{
			if (!ibus.have_time)
			{
				ibus_request_time();
				ibus.num_time_requests++;
			}
			if (!ibus.have_date)
			{
				ibus_request_date();
			}
		}
	}

	/* 5 minute idle timeout */
	if (mainloop_get_millisec() - ibus.last_byte > ibus.idle_timeout * 1000)
	{
		log_msg("idle timeout\n");
		power_off();
	}

	return 1;
}

static void ibus_update_leds()
{
	static int i = 0;

	i++;
	if ((ibus.read_msgs && i >= 20) || i >= 60)
	{
		i = 0;
	}

	if (ibus.hw_version >= 4)
	{
		if (ibus.read_msgs)
		{
			/* single blink */
			gpio_write(GPIO_LED_CTL, (i < 2) ? 1 : 0);
		}
		else
		{
			/* double blink */
			gpio_write(GPIO_LED_CTL, ((i < 2) || (i > 5 && i < 8)) ? 1 : 0);
		}
	}
}

/* every 50ms */

static int ibus_50ms_tick(void *unused)
{
	bool can_send;
	uint64_t now;
	bool giveup;

	ibus_update_leds();

	/* this'll hardly ever happen */
	if (ibus.bufPos)
	{
		now = mainloop_get_millisec();
		if (now - ibus.last_byte > 200)
		{
			log_msg_with_hex(ibus.buf, ibus.bufPos, "ibus_50ms_tick(): discard %d: ", ibus.bufPos);
			ibus_discard_receive_buffer();
		}
	}

	if (/*ibus.gpio_number > 0*/1)
	{
		if (ibus.bytes_read == 0)
		{
			can_send = TRUE;
		}
		else
		{
			can_send = FALSE;
		}

		if (ibus_service_queue(ibus.ifd, can_send, ibus.gpio_number, &giveup))
		{
			/* Unexpected: GPIO was low or RX FIFO not empty :( */
		}

		if (giveup)
		{
			log_msg("serial port is broken - reopening\n");
			//ibus_discard_queue();
			ibus_init_serial_port(TRUE);
		}

		if (ibus.bufPos == 0 && !can_send)
		{
			/* restart the counter */
			ibus.bytes_read = 0;

			/*
			 * Now wait for ibus_50ms_tick() to be called again.
			 * If no bytes were read during this 50ms, we can transmit.
			 *
			 */
		}
	}

	return 1;
}

int ibus_send_ascii(const char *cmd)
{
	char byte[4];
	unsigned char data[256];
	int len = strlen(cmd);
	int i, j;

	if (len >= (sizeof(data) * 2))
	{
		return 1;
	}

	for (i = 0, j = 0; i < len; i += 2, j++)
	{
		byte[0] = cmd[i];
		byte[1] = cmd[i+1];
		byte[2] = 0;
		data[j] = strtoul(byte, NULL, 16);
	}

	/* length error? */
	if (data[1] + 2 != j || j < 5 || j > 255)
	{
		return 2;
	}

	ibus_send(ibus.ifd, data, j, ibus.gpio_number);

	return 0;
}

int ibus_init(const char *port, char *startup, bool bluetooth, bool camera, bool cdc_announce, int cdc_info_interval, int gpio_number, int idle_timeout, int hw_version, bool aux, bool handle_nextprev, bool rotary_opposite, bool z4_keymap, int server_port, int log_level)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	ibus.port_name = strdup(port);

	if (ibus_init_serial_port(FALSE) == -1)
	{
		return -1;
	}

	usleep(10000);

	if (ibus_init_serial_port(FALSE) == -1)
	{
		return -1;
	}

	if (log_open(ts.tv_sec, log_level) != 0)
	{
		fprintf(stderr, "Cannot write to log: %s\n", strerror(errno));
		mainloop_input_remove(ibus.ifd_tag);
		ibus.ifd_tag = -1;
		close(ibus.ifd);
		ibus.ifd = -1;
		return -2;
	}

	log_msg("startup bt=%d cam=%d anc=%d cdci=%d gpio=%d idle=%d hwv=%d aux=%d hnp=%d rop=%d [" __DATE__ "]\n", bluetooth, camera, cdc_announce, cdc_info_interval, gpio_number, idle_timeout, hw_version, aux, handle_nextprev, rotary_opposite);
	log_flush();

	ibus.last_byte = mainloop_get_millisec();
	ibus.bluetooth = bluetooth;
	ibus.have_camera = camera;
	ibus.cdc_announce = cdc_announce;
	ibus.cdc_info_interval = cdc_info_interval;
	ibus.gpio_number = gpio_number;
	ibus.idle_timeout = idle_timeout;
	ibus.hw_version = hw_version;
	ibus.aux = aux;
	ibus.handle_nextprev = handle_nextprev;
	ibus.rotary_opposite = rotary_opposite;
	ibus.z4_keymap = z4_keymap;

	mainloop_timeout_add(50, ibus_50ms_tick, NULL);
	mainloop_timeout_add(1000, ibus_1s_tick, NULL);

	/* gpio 15 is the UART RX, don't change its direction. */
	if (gpio_number != 15 && gpio_number != 0)
	{
		/* The IBUS monitor pin must be an input (should already be) */
		gpio_set_input(gpio_number);

		if (hw_version >= 4)
		{
			gpio_set_pull(gpio_number, PULL_UP);
		}
	}

	if (hw_version >= 4)
	{
		/* These are factory defaults, but just make sure */
		gpio_set_pull(GPIO_NSLP_CTL, PULL_DOWN);
		gpio_set_pull(GPIO_PIN17_CTL, PULL_DOWN);
		gpio_set_pull(GPIO_LED_CTL, PULL_DOWN);
		gpio_set_pull(GPIO_RELAY_CTL, PULL_DOWN);

		gpio_write(GPIO_NSLP_CTL, 1);	/* Wake up the transceiver */
		gpio_write(GPIO_PIN17_CTL, 0);
		gpio_write(GPIO_LED_CTL, 1);
		gpio_write(GPIO_RELAY_CTL, 0);

		gpio_set_output(GPIO_NSLP_CTL);
		gpio_set_output(GPIO_PIN17_CTL);
		gpio_set_output(GPIO_LED_CTL);
		gpio_set_output(GPIO_RELAY_CTL);
	}
	else if (bluetooth || (!camera))
	{
		unsigned char set[] = "\xd7\x04\xd8\x70\x00\x00";

		if (bluetooth)
		{
			/* Tell the ATtiny to ignore the Phone button */
			set[4] |= 1;
		}

		if (!camera)
		{
			/* Tell the ATtiny to ignore reverse gear */
			set[4] |= 2;
		}

		set[5] = set[0] ^ set[1] ^ set[2] ^ set[3] ^ set[4];
		ibus_send(ibus.ifd, set, 6, ibus.gpio_number);
	}

	if (startup)
	{
		ibus_send_ascii(startup);
		log_flush();
		free(startup);
	}

	if (server_port)
	{
		server_init(server_port);
	}

	return 0;
}

void ibus_cleanup(void)
{
	/*if (ibus.ifd != -1)
	{
		close(ibus.ifd);
		ibus.ifd = -1;
	}*/

	server_cleanup();
}

