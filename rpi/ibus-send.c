#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "gpio.h"
#include "mainloop.h"
#include "ibus.h"
#include "ibus-send.h"
#include "slist.h"


extern FILE *flog;

static SList *pkt_list = NULL;



typedef struct
{
	unsigned char msg[32];
	int length;
	int countdown;
}
packet;



/* called every 50ms */

void ibus_service_queue(int ifd, bool can_send, int gpio_number)
{
	SList *list;
	packet *pkt;

	list = pkt_list;
	while (list)
	{
		pkt = list->data;
		if (pkt->countdown)
		{
			pkt->countdown--;
		}
		list = list->next;
	}

	if (!can_send)
	{
		return;
	}

	/* Only send if GPIO 18 (pibus2/3) or GPIO 17 (pibus4) is high */
	if (pkt_list && !gpio_read(gpio_number))
	{
		ibus_log("ibus_service_queue(): ibus/gpio busy - waiting\n");
		return;
	}

	list = pkt_list;
	while (list)
	{
		pkt = list->data;
		if (pkt->countdown == 0)
		{
			ibus_log("ibus_service_queue(%d): ", pkt->length);
			ibus_dump_hex(flog, pkt->msg, pkt->length, FALSE);
			write(ifd, pkt->msg, pkt->length);
			//tcdrain(ifd);
			/* send again if it doesn't echo back within 1.4 seconds */
			pkt->countdown = 28;
		}
		//list = list->next;
		/* Only process the first item */
		break;
	}
}

void ibus_remove_from_queue(const unsigned char *msg, int length)
{
	SList *list = pkt_list;
	packet *pkt;

	while (list)
	{
		pkt = list->data;
		if (pkt->length == length)
		{
			if (memcmp(pkt->msg, msg, length) == 0)
			{
				ibus_log("ibus_remove_queue(%d): success - dequeued\n", length);
				pkt_list = slist_remove(pkt_list, pkt);
				free (pkt);
				return;
			}
		}
		list = list->next;
	}
}

static void ibus_add_to_queue(const unsigned char *msg, int length, int countdown)
{
	packet *pkt;

	pkt = malloc(sizeof(packet));
	memcpy(pkt->msg, msg, length);
	pkt->length = length;
	pkt->countdown = countdown;

	pkt_list = slist_append(pkt_list, pkt);
}

void ibus_send(int ifd, const unsigned char *msg, int length, int gpio_number)
{
	unsigned char sum;
	int i;

	ibus_log("ibus_send(%d): %02x %02x %02x queued\n", length, msg[0], msg[1], msg[2]);

	sum = msg[0];
	for (i = 1; i < (length - 1); i++)
	{
		sum ^= msg[i];
	}

	if (sum != msg[length -1])
	{
		ibus_log("ibus_send: \033[31mbad checksum\033[m\n");
	}

	if (gpio_number > 0)
	{
		ibus_add_to_queue(msg, length, 1);
	}
}
