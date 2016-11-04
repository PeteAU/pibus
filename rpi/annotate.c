// turns IBUS messages into something human readable
// Copyright(c) 2016 Peter Zelezny.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>

#include "mainloop.h"
#include "annotate.h"


static const struct
{
	int match_length;
	const char *ibusmsg;
	const char *text;
}
events[] =
{
	{5, "\x68\x03\x18\x01\x72",         "cdc-poll"},
};

static const char *wbuttons[16] =
{
	/*0*/ "none",
	/*1*/ ">",
	/*2*/ "",
	/*3*/ "",
	/*4*/ "",
	/*5*/ "",
	/*6*/ "",
	/*7*/ "",
	/*8*/ "<",
	/*9*/ "",
	/*a*/ "",
	/*b*/ "",
	/*c*/ "",
	/*d*/ "",
	/*e*/ "",
	/*f*/ ""
};

static const char *buttons[64] =
{
	/*0*/ ">",
	/*1*/ "2",
	/*2*/ "4",
	/*3*/ "6",
	/*4*/ "tone",
	/*5*/ "knob",
	/*6*/ "radio",
	/*7*/ "clock",
	/*8*/ "phone",
	/*9*/ "0x9",
	/*a*/ "0xa",
	/*b*/ "0xb",
	/*c*/ "0xc",
	/*d*/ "0xd",
	/*e*/ "0xe",
	/*f*/ "0xf",
	/*10*/ "<",
	/*11*/ "1",
	/*12*/ "3",
	/*13*/ "5",
	/*14*/ "<>",
	/*15*/ "route",
	/*16*/ "0x16",
	/*17*/ "0x17",
	/*18*/ "0x18",
	/*19*/ "0x19",
	/*1a*/ "0x1a",
	/*1b*/ "0x1b",
	/*1c*/ "0x1c",
	/*1d*/ "0x1d",
	/*1e*/ "0x1e",
	/*1f*/ "0x1f",
	/*20*/ "select",
	/*21*/ "AM",
	/*22*/ "RDS",
	/*23*/ "mode",
	/*24*/ "ejectc",
	/*25*/ "repeat",
	/*26*/ "0x26",
	/*27*/ "0x27",
	/*28*/ "0x28",
	/*29*/ "0x29",
	/*2a*/ "0x2a",
	/*2b*/ "0x2b",
	/*2c*/ "0x2c",
	/*2d*/ "0x2d",
	/*2e*/ "0x2e",
	/*2f*/ "0x2f",
	/*30*/ "display",
	/*31*/ "FM",
	/*32*/ "FP",
	/*33*/ "Dolby",
	/*34*/ "menu",
	/*35*/ "mute",
	/*36*/ "0x36",
	/*37*/ "mmcfond",
	/*38*/ "dispClose",
	/*39*/ "dispOpen",
	/*3a*/ "disp3A",
	/*3b*/ "0x3b",
	/*3c*/ "0x3c",
	/*3d*/ "0x3d",
	/*3e*/ "0x3e",
	/*3f*/ "0x3f",
};

static const char *gear[16] =
{
	/*0*/ "",
	/*1*/ "R",
	/*2*/ "1",
	/*3*/ "?",
	/*4*/ "2",
	/*5*/ "?5",
	/*6*/ "?6",
	/*7*/ "N",
	/*8*/ "D",
	/*9*/ "L",
	/*a*/ "?a",
	/*b*/ "P",
	/*c*/ "4",
	/*d*/ "3",
	/*e*/ "5",
	/*f*/ "6"
};

static const char *device(unsigned char n)
{
	static char buf[16];

	switch (n)
	{
		case 0x00: return "GM";  /* body module */
		case 0x05: return "DIA"; /* diagnostics req */
		case 0x08: return "SUN"; /* sun roof */
		case 0x18: return "CDC"; /* cd changer */
		case 0x3b: return "GT";  /* graphics driver */
		case 0x3f: return "DIA"; /* diagnostics */
		case 0x43: return "GTF"; /* rear screen */
		case 0x44: return "EWS"; /* immobilizer */
		case 0x46: return "CID"; /* central info display */
		case 0x50: return "MFL"; /* multifunction steering wheel */
		case 0x5b: return "AIR"; /* heat/air */
		case 0x60: return "PDC"; /* park distance control */
		case 0x68: return "RAD"; /* radio */
		case 0x6a: return "DSP"; /* DSP */
		case 0x72: return "SM";  /* seat memory */
		case 0x76: return "CD";  /* cd (single) */
		case 0x7f: return "NAV"; /* navigation */
		case 0x80: return "IKE"; /* instrument kluster */
		case 0xa4: return "BAG"; /* air bag */
		case 0xb0: return "SES"; /* language input */
		case 0xbb: return "JNV"; /* japan nav */
		case 0xbf: return "GLO"; /* global broadcast */
		case 0xc0: return "MID"; /* multi info display */
		case 0xc8: return "TEL"; /* telephone */
		case 0xd0: return "LCM"; /* light control module */
		case 0xe7: return "ANZ"; /* secondary display */
		case 0xe8: return "RLS"; /* rain light sensor */
		case 0xed: return "TV";  /* television */
		case 0xf0: return "BMB"; /* on board part (board monitor) */
		case 0xff: return "LOC"; /* broadcast */
	}

	sprintf(buf, "$%02x", n);
	return buf;
}

const char *annotate_device_to_device(unsigned char a, unsigned char b)
{
	static char buf[16];

	snprintf(buf, sizeof(buf), "%s>%s", device(a), device(b));
	return buf;
}

static const char *screen(unsigned char s)
{
	switch (s)
	{
		case 0: return "norm";
		case 1: return "none";
		case 2: return "radio";
		case 4: return "select";
		case 8: return "tone";
		case 12: return "clear";
		default: return "?";
	}
}

static const char *video_target(unsigned char v)
{
	switch (v)
	{
		case 1:  return "tv";
		case 2:  return "gt";
		case 3:  return "gtf";
		case 4:  return "navj";
		case 8:  return "ext";
		case 9:  return "mmc";
		case 10: return "ast";
		default: return "?";
	}
}

static const char *bc_function(unsigned char f)
{
	switch (f)
	{
		case 0x01: return "time";
		case 0x02: return "date";
		case 0x03: return "temp";
		case 0x04: return "consump1";
		case 0x05: return "consump2";
		case 0x06: return "range";
		case 0x07: return "distance";
		case 0x08: return "arrival";
		case 0x09: return "limit";
		case 0x0a: return "avspeed";
		case 0x0c: return "memo";
		case 0x1b: return "air";
		default: return "?";
	}
}

static int annotate_ike_message(char *outbuf, int max, const unsigned char *data, int length)
{
	return snprintf(outbuf, max, "gear=%s", gear[(data[5] >> 4)]);
}

static int annotate_cd_command(char *outbuf, int max, const unsigned char *data, int length, bool verbose)
{
	const char *dev = "?";
	const char *cmd = "?";

	/* XX 05 XX 38 XX XX CC */
	/* XX 06 XX 38 XX XX XX CC */

	switch (data[2])
	{
		case 0x18: dev = "cdc"; break;
		case 0x76: dev = "cd"; break;
		case 0xf0: dev = "cd"; break;
	}

	switch (data[4] & 0x7F)
	{
		case 0x00: cmd = "info"; break;
		case 0x01: cmd = "stop"; break;
		case 0x02: cmd = "pause"; break;
		case 0x03: cmd = "play"; break;
		case 0x04: cmd = data[5] ? "fwd" : "rwd"; break;
		case 0x06: cmd = "diskchange"; break;
		case 0x0a: cmd = data[5] ? "prev" : "next"; break;
	}

	return snprintf(outbuf, max, "%s-%s", dev, cmd);
}

static int annotate_cd_status(char *outbuf, int max, const unsigned char *data, int length, bool verbose)
{
	const char *dev = "?";
	const char *sta = "?";

	/* 18 0a 68 39 ... CC */

	switch (data[0])
	{
		case 0x18: dev = "cdc"; break;
		case 0x76: dev = "cd"; break;
		case 0xf0: dev = "cd"; break;
	}

	switch (data[4] & 0x7F)
	{
		case 0x00: sta = "stopped"; break;
		case 0x01: sta = "paused"; break;
		case 0x02: sta = "playing"; break;
		case 0x07: sta = "searching"; break;
		case 0x09: sta = "checking"; break;
		case 0x0b: sta = "nodisk"; break;
		case 0x0c: sta = "disk"; break;
	}

	return snprintf(outbuf, max, "%s=%s", dev, sta);
}

int annotate_ibus_message(char *outbuf, int max, const unsigned char *data, int length, bool verbose)
{
	int i;

	/* some basic fixed messages */
	for (i = 0; i < sizeof(events) / sizeof(events[0]); i++)
	{
		if (memcmp(data, events[i].ibusmsg, events[i].match_length) == 0)
		{
			return snprintf(outbuf, max, "%s", events[i].text);
		}
	}

	/* steering wheel buttons */
	if (length == 6 && data[0] == 0x50 && data[3] == 0x3b)
	{
		const char *type = "?";
		const char *button;
		const char *to;

		if (data[4] & 0x80)
		{
			button = "speak";
		}
		else
		{
			button = wbuttons[data[4] & 0x0f];
		}

		to = (data[4] & 0x40) ? ":tel" : "";

		switch (data[4] & 0x30)
		{
			case 0x00: type = "down"; break;
			case 0x10: type = "hold"; break;
			case 0x20: type = "up"; break;
		}

		return snprintf(outbuf, max, "wheel-%s=%s%s", type, button, to);
	}

	/* board monitor functions */
	if (length == 7 && data[0] == 0xf0 && data[3] == 0x47)
	{
		const char *type = "?";
		const char *button = "?";

		switch (data[5] & 0xc0)
		{
			case 0x00: type = "down"; break;
			case 0x40: type = "hold"; break;
			case 0x80: type = "up"; break;
		}

		switch (data[5] & 0x3f)
		{
			case 0x38: button = "info"; break;
			case 0x0f: button = "select"; break;
		}

		return snprintf(outbuf, max, "button-%s=%s", type, button);
	}

	/* board monitor buttons */
	if (length == 6 && data[0] == 0xf0 && data[3] == 0x48)
	{
		if (data[4] & 0x40)
			return snprintf(outbuf, max, "button-%s=%s", "hold", buttons[data[4] & 0x3f]);	
		if (data[4] & 0x80)
			return snprintf(outbuf, max, "button-%s=%s", "up", buttons[data[4] & 0x3f]);
		else
			return snprintf(outbuf, max, "button-%s=%s", "down", buttons[data[4] & 0x3f]);
	}

	/* rotary dial */
	if (length == 6 && data[0] == 0xf0 && data[3] == 0x49)
	{
		if ((data[4] & 0x0f) != 1)
			return snprintf(outbuf, max, "rotary=%s repeat=%d", (data[4] & 0x80) ? "up" : "down", (data[4] & 0x0f));
		else
			return snprintf(outbuf, max, "rotary=%s", (data[4] & 0x80) ? "up" : "down");
	}

	/* volume dial */
	if (length == 6 && memcmp(data, "\xf0\x04\x68\x32", 4) == 0)
	{
		if ((data[4] & 0xf0) != 0x10)
			return snprintf(outbuf, max, "volume=%s repeat=%d", (data[4] & 1) ? "up" : "down", ((data[4] & 0xf0) >> 4));
		else
			return snprintf(outbuf, max, "volume=%s", (data[4] & 1) ? "up" : "down");
	}

	/* cd or cdc command */
	if ((length >= 7 && (data[2] == 0xf0 || data[2] == 0x76 || data[2] == 0x18) && data[3] == 0x38))
	{
		return annotate_cd_command(outbuf, max, data, length, verbose);
	}

	/* cd or cdc status */
	if (length >= 12 && (data[0] == 0xf0 || data[0] == 0x76 || data[0] == 0x18) && data[3] == 0x39)
	{
		return annotate_cd_status(outbuf, max, data, length, verbose);
	}

	/* menu text */
	if (length > 8 && data[2] == 0x3b && (data[3] == 0xa5 || data[3] == 0x21))
	{
		return snprintf(outbuf, max, "\"%.*s\"", length - 8, data + 7);
	}
	if (length > 7 && (data[2] == 0x3b || data[2] == 0x80) && (data[3] == 0x23))
	{
		return snprintf(outbuf, max, "\"%.*s\"", length - 7, data + 6);
	}
	if (length == 22 && memcmp(data, "\x7f\x14\xc8\xa2\x01", 5) == 0)
	{
		return snprintf(outbuf, max, "gpstime=%02x:%02x:%02x", data[18], data[19], data[20]);
	}
	/*if (verbose && length >= 8 && data[0] == 0x7f && memcmp(data + 2, "\xc8\xa4", 2) == 0)
	{
		return snprintf(outbuf, max, "\"%s\"", data + 6);
	}*/
	if (length == 13 && memcmp(data, "\x7f\x0b\x80\x1f", 4) == 0)
	{
		return snprintf(outbuf, max, "time=%02x:%02x date=%02x%02x/%02x/%02x", data[5], data[6], data[10], data[11], data[9], data[7]);
	}
	if (length == 6 && memcmp(data, "\x7f\x04\xb0\xaf", 4) == 0)
	{
		const char *map = "?";
		switch (data[4] & 0x07)
		{
			case 0x01: map = "cd"; break;
			case 0x04: map = "dvd"; break;
		}
		return snprintf(outbuf, max, "map=%s", map);
	}
	if (length >= 11 && data[0] == 0x80 && data[2] == 0xbf && data[3] == 0x13)
	{
		return annotate_ike_message(outbuf, max, data, length);
	}
	if (length == 6 && memcmp(data, "\x80\x04\xbf\x11", 4) == 0)
	{
		switch (data[4])
		{
			case 0x00: return snprintf(outbuf, max, "ignition=%s", "off");
			case 0x01: return snprintf(outbuf, max, "ignition=%s", "acc");
			case 0x03: return snprintf(outbuf, max, "ignition=%s", "on");
			case 0x07: return snprintf(outbuf, max, "ignition=%s", "start");
		}
	}
	if (length == 7 && memcmp(data, "\x80\x05\xbf\x18", 4) == 0)
	{
		return snprintf(outbuf, max, "speed=%d rpm=%d", data[4] * 2, data[5] * 100);
	}
	if (length == 8 && memcmp(data, "\x80\x06\xbf\x19", 4) == 0)
	{
		int16_t coolant = (data[6] << 8) + ((signed char)data[5]);
		return snprintf(outbuf, max, "outside=%d coolant=%d", (signed char)data[4], coolant);
	}
	if (length == 9 && memcmp(data, "\x80\x07\xbf\x15", 4) == 0)
	{
		const char *model = "?";
		switch (data[4] & 0xf0)
		{
			case 0x00: model = "E38/E39H"; break;
			case 0x10: model = "E53"; break;
			case 0x20: model = "E52"; break;
			case 0x30: model = "E39B"; break;
			case 0x40: model = "E46-7"; break;
			case 0x60: model = "E46-11"; break;
			case 0xa0: model = "E83/E85"; break;
			case 0xf0: model = "E46-13"; break;
		}
		return snprintf(outbuf, max, "model=%s", model);
	}
	if (length > 7 && data[0] == 0x80 && memcmp(data + 2, "\xff\x24", 2) == 0)
	{
		return snprintf(outbuf, max, "%s=%.*s", bc_function(data[4]), data[1] - 5, data + 6);
	}
	if (length == 0xc && memcmp(data, "\x80\x0a\xbf\x17", 4) == 0)
	{
		int odo = (data[6] << 16) | (data[5] << 8) | data[4];
		int service = ((data[7] & 0x07) | data[8]) * 50;
		int days = (data[9] & 0x0f << 8) | data[10];
		return snprintf(outbuf, max, "odometer=%d service=%d days=%d", odo, service, days);
	}
	if (length == 5 && data[2] == 0x80)
	{
		switch (data[3])
		{
			case 0x10: return snprintf(outbuf, max, "get=%s", "ignition");
			case 0x12: return snprintf(outbuf, max, "get=%s", "cluster");
			case 0x14: return snprintf(outbuf, max, "get=%s", "model");
			case 0x16: return snprintf(outbuf, max, "get=%s", "service");
		}
	}
	if (length == 7 && memcmp(data + 1, "\x05\x80\x41", 3) == 0)
	{
		if (data[5] == 1)
			return snprintf(outbuf, max, "getdata=%s", bc_function(data[4]));
		else if (data[5] == 2)
			return snprintf(outbuf, max, "getstatus=%s", bc_function(data[4]));
	}
	if (length == 18 && memcmp(data, "\xd0\x10\x80\x54", 4) == 0)
	{
		int li = (((data[11] & 0x1f) << 8) | data[12]) * 10;
		int days = (data[15] << 8) | data[16];
		return snprintf(outbuf, max, "vin=%c%c%02x%02x%02x odometer=%d litres=%d days_since_service=%d", data[4], data[5], data[6], data[7], data[8], ((data[9] << 8) | data[10]) * 100, li, days);
	}
	if (length == 6 && memcmp(data, "\x68\x04\x3b\x46", 4) == 0)
	{
		if ((data[4] & 0x10) == 0)
		{
			return snprintf(outbuf, max, "screen=%s", screen(data[4]));
		}
	}
	if (length == 7 && memcmp(data, "\x3b\x05\x68\x4e", 4) == 0)
	{
		/* audio source */
		if ((data[4] & 0x80) == 0)
		{
			switch (data[4] & 0x07)
			{
				case 0: return snprintf(outbuf, max, "audio=%s", "radio");
				case 1: return snprintf(outbuf, max, "audio=%s", "tv");
				case 4: return snprintf(outbuf, max, "audio=%s", "nav");
			}
		}
	}
	if (length == 7 && memcmp(data + 1, "\x05\xf0\x4f", 3) == 0)
	{
		const char *freq = "?";
		const char *mode = "?";
		/* RGB control : ed05f04f111157 */
		switch (data[5] & 0x03)
		{
			case 1: freq = "60Hz"; break;
			case 2: freq = "50Hz"; break;
		}
		switch (data[5] & 0xf0)
		{
			case 0x00: mode = "4:3"; break;
			case 0x10: mode = "16:9"; break;
			case 0x20: mode = "8:3"; break;
			case 0x30: mode = "4:3LS"; break;
			case 0x40: mode = "4:3NLS"; break;
		}
		return snprintf(outbuf, max, "video=%s-%s-%s", video_target(data[4]&0x03), freq, mode);
	}
	if (length == 6 && memcmp(data + 1, "\x04\xf0\x4f", 3) == 0)
	{
		/* RGB control : 3b04f04f1292 */
		return snprintf(outbuf, max, "video=%s", video_target(data[4]&0x03));
	}
	if (length == 7 && memcmp(data, "\x44\x05\xbf\x74", 4) == 0)
	{
		const char *key = "?";
		const char *imm = "";

		switch (data[4])
		{
			case 0: key = "none"; break;
			case 1: key = "unlock"; break;
			case 4: key = "insert"; break;
			case 5: key = "unlock"; break;
		}
		switch (data[5])
		{
			case 0xff: key = "immobilized"; break;
		}

		return snprintf(outbuf, max, "key=%s%s", key, imm);
	}
	if (length == 6 && memcmp(data, "\x00\x04\xbf\x72", 4) == 0)
	{
		switch (data[4] & 0x70)
		{
			case 0x00: return snprintf(outbuf, max, "fob=none");
			case 0x10: return snprintf(outbuf, max, "fob=lock");
			case 0x20: return snprintf(outbuf, max, "fob=unlock");
			case 0x40: return snprintf(outbuf, max, "fob=boot");
		}
	}
	if (length == 6 && memcmp(data, "\x00\x04\xbf\x76", 4) == 0)
	{
		return snprintf(outbuf, max, "crash/alarm=%02x", data[4]);
	}
	if (length == 7 && memcmp(data, "\x00\x05\xbf\x7a", 4) == 0)
	{
		return snprintf(outbuf, max, "doors/windows");
	}
	if (length >= 6 && data[2] == 0x68 && data[3] == 0xa7)
	{
		return snprintf(outbuf, max, "tmc");
	}
	if (length == 7 && memcmp(data, "\x00\x05\xbf\x7d", 4) == 0)
	{
		return snprintf(outbuf, max, "sunroof");
	}
	if (length == 8 && memcmp(data, "\xe8\x06\x00\x58", 4) == 0)
	{
		return snprintf(outbuf, max, "wiper");
	}
	if (length == 7 && memcmp(data, "\xe8\x05\xd0\x59", 4) == 0)
	{
		return snprintf(outbuf, max, "lightsensor");
	}
	if (length == 6 && memcmp(data, "\x3b\x04\x68\x22", 4) == 0)
	{
		return snprintf(outbuf, max, "menuack=%d", data[4] + 1);
	}
	if (length == 7 && memcmp(data, "\x3b\x05\x68\x22", 4) == 0)
	{
		return snprintf(outbuf, max, "menuack=%d", data[5] + 1);
	}
	if (length == 5 && data[3] == 0x01)
	{
		return snprintf(outbuf, max, "ping");
	}
	if (length == 6 && data[3] == 0x02)
	{
		return snprintf(outbuf, max, "pong=%02x", data[4]);
	}
	if (length == 7 && data[3] == 0x02)
	{
		return snprintf(outbuf, max, "pong=%02x%02x", data[4], data[5]);
	}
	if (length == 6 && memcmp(data + 1, "\x04\xf0\x4a", 3) == 0)
	{
		switch (data[4])
		{
			case 0x90: return snprintf(outbuf, max, "getcassette");
			case 0x00: return snprintf(outbuf, max, "radioled=off");
			case 0xff: return snprintf(outbuf, max, "radioled=on");
		}
	}
	if (length == 6 && memcmp(data, "\x3b\x04\x68\x45", 4) == 0)
	{
		return snprintf(outbuf, max, "monitorstatus=%02x", data[4]);
	}
	if (length == 5 && data[2] == 0x00)
	{
		switch (data[3])
		{
			case 0x75: return snprintf(outbuf, max, "get=%s", "wiper");
			case 0x79: return snprintf(outbuf, max, "get=%s", "doors/windows");
		}
	}
	if (length == 6 && data[0] == 0x00 && data[3] == 0x77)
	{
		return snprintf(outbuf, max, "wiper=%02x", data[4]);
	}

	return 0;
}
