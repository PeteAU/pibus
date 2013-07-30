#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "gpio.h"
#include "mainloop.h"
#include "ibus-send.h"
#include "slist.h"


extern FILE *flog;

static SList *pkt_list = NULL;



typedef struct
{
	unsigned char msg[24];
	int length;
	int countdown;
}
packet;


static void dump_hex(FILE *out, const unsigned char *data, int length)
{
	int i;

	for (i = 0; i < length; i++)
	{
		fprintf(out, "%02x ", data[i]);
	}

	fprintf(out, "\n");
}

/* called every 50ms */

void ibus_service_queue(int ifd, bool can_send)
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

	/* Only send if GPIO 18 is high */
	if (pkt_list && !gpio_read(18))
	{
		fprintf(flog, "ibus_service_queue(): ibus/gpio busy - waiting\n");
		return;
	}

	list = pkt_list;
	while (list)
	{
		pkt = list->data;
		if (pkt->countdown == 0)
		{
			fprintf(flog, "ibus_service_queue(%d): ", pkt->length);
			dump_hex(flog, pkt->msg, pkt->length);
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
				fprintf(flog, "ibus_remove_queue(%d): success - dequeued\n", length);
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

void ibus_send(int ifd, const unsigned char *msg, int length)
{
	unsigned char sum;
	int i;

	fprintf(flog, "ibus_send(%d): %02x %02x %02x queued\n", length, msg[0], msg[1], msg[2]);

	sum = msg[0];
	for (i = 1; i < (length - 1); i++)
	{
		sum ^= msg[i];
	}

	if (sum != msg[length -1])
	{
		fprintf(flog, "ibus_send: \033[31mbad checksum\033[m\n");
	}

	ibus_add_to_queue(msg, length, 1);
}
