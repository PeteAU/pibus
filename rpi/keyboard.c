#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <time.h>
#include "keyboard.h"


static int kfd;


int keyboard_init(void)
{
	struct uinput_user_dev uidev;
	int i;

	kfd = open("/dev/uinput", O_WRONLY);
	if (kfd < 0)
		return -1;

	if (ioctl(kfd, UI_SET_EVBIT, EV_KEY) < 0)
		return -2;
	if (ioctl(kfd, UI_SET_EVBIT, EV_SYN) < 0)
		return -2;

	for (i = 1; i < 255; i++)
	{
		if (ioctl(kfd, UI_SET_KEYBIT, i) < 0)
			return -3;
	}

	memset(&uidev, 0, sizeof(uidev));
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "uinput-ibus");
	uidev.id.bustype = BUS_USB;
	uidev.id.vendor  = 0x1;
	uidev.id.product = 0x1;
	uidev.id.version = 1;

	if (write(kfd, &uidev, sizeof(uidev)) < 0)
		return -4;

	if (ioctl(kfd, UI_DEV_CREATE) < 0)
		return -5;

	return 0;
}

static int keyboard_generate_down(unsigned short key)
{
	struct input_event ev;

	memset(&ev, 0, sizeof(struct input_event));
	ev.type = EV_KEY;
	ev.code = key;
	ev.value = 1;
	if (write(kfd, &ev, sizeof(struct input_event)) < 0)
		return -1;

	return 0;
}

static int keyboard_generate_up(unsigned short key)
{
	struct input_event ev;

	memset(&ev, 0, sizeof(struct input_event));
	ev.type = EV_KEY;
	ev.code = key;
	ev.value = 0;
	if (write(kfd, &ev, sizeof(struct input_event)) < 0)
		return -1;

	return 0;
}

int keyboard_generate(unsigned short key)
{
	struct input_event ev;
	unsigned short mod = 0;

	if (key & _CTRL_BIT)
	{
		key &= ~(_CTRL_BIT);
		mod = KEY_LEFTCTRL;
	}

	if (mod)
	{
		if (keyboard_generate_down(mod) < 0)
			return -1;
	}

	if (keyboard_generate_down(key) < 0)
		return -1;

	if (keyboard_generate_up(key) < 0)
		return -1;

	if (mod)
	{
		if (keyboard_generate_up(mod) < 0)
			return -1;
	}

	memset(&ev, 0, sizeof(struct input_event));
	ev.type = EV_SYN;
	ev.code = 0;
	ev.value = 0;
	if (write(kfd, &ev, sizeof(struct input_event)) < 0)
		 return -3;

	fdatasync(kfd);

	return 0;
}

void keyboard_cleanup(void)
{
	ioctl(kfd, UI_DEV_DESTROY);

	close(kfd);
	kfd = -1;
}
