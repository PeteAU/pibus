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
static bool port_good = FALSE;



typedef struct
{
	unsigned char msg[232];
	int length;
	int countdown;
	bool sync;
	int tag;
	int transmit_count;
}
packet;



/* called every 50ms */

bool ibus_service_queue(int ifd, bool can_send, int gpio_number, bool *giveup)
{
	SList *list;
	packet *pkt;
	int i;
	int packets_sent;

	*giveup = FALSE;

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
		return FALSE;
	}

	if (pkt_list)
	{
		/* Only send if GPIO 15 (UART RX) is high (idle state) */
		if (!gpio_read(15))
		{
			ibus_log("service_queue(): ibus/gpio busy - waiting\n");
			return TRUE;
		}

		/* If the RX FIFO contains bytes, don't send now! */
		if (!uart_rx_fifo_empty())
		{
			ibus_log("service_queue(): rx fifo not empty - waiting\n");
			return TRUE;
		}
	}

	i = 0;
	packets_sent = 0;
	list = pkt_list;
	while (list)
	{
		pkt = list->data;

		if (pkt->sync && i != 0)
		{
			/* must send all previous items first (including echo-back) */
			break;
		}

		if (pkt->countdown == 0)
		{
			ibus_log("service_queue(%d): ", pkt->length);
			ibus_dump_hex(flog, pkt->msg, pkt->length, NULL);
			write(ifd, pkt->msg, pkt->length);

			pkt->transmit_count++;
			if (pkt->transmit_count > 3 && !port_good)
			{
				pkt->transmit_count = 1;
				*giveup = TRUE;
				return FALSE;
			}

			/* send again if it doesn't echo back within 0.5 seconds (10 * 50ms) */
			pkt->countdown = 10;
			packets_sent++;
		}

		i++;
		list = list->next;
	}

	if (packets_sent)
	{
		fsync(ifd);
	}

	return FALSE;
}

void ibus_discard_queue(void)
{
	SList *list = pkt_list;
	packet *pkt;

	while (list)
	{
		pkt = list->data;
		pkt_list = slist_remove(pkt_list, pkt);
		free (pkt);
		list = pkt_list;
	}
}

void ibus_remove_from_queue(const unsigned char *msg, int length)
{
	SList *list = pkt_list;
	packet *pkt;

	while (list)
	{
		pkt = list->data;
		if (pkt->length == length && memcmp(pkt->msg, msg, length) == 0)
		{
			port_good = TRUE;
			ibus_log("remove_queue(%d): success - dequeued\n", length);
			pkt_list = slist_remove(pkt_list, pkt);
			free (pkt);
			return;
		}
		list = list->next;
	}
}

void ibus_remove_tag_from_queue(int tag)
{
	SList *list;
	packet *pkt;

start:
	list = pkt_list;
	while (list)
	{
		pkt = list->data;
		if (pkt->tag == tag)
		{
			pkt_list = slist_remove(pkt_list, pkt);
			free (pkt);
			goto start;
		}
		list = list->next;
	}
}

static unsigned char ibus_calc_sum(const unsigned char *msg, int length)
{
	int i;
	unsigned char sum;

	sum = msg[0];
	for (i = 1; i < (length - 1); i++)
	{
		sum ^= msg[i];
	}

	return sum;
}

static void ibus_add_to_queue(const unsigned char *msg, int length, int countdown, bool sync, bool prepend, int tag)
{
	packet *pkt;

	pkt = malloc(sizeof(packet));
	memcpy(pkt->msg, msg, length);
	pkt->msg[length - 1] = ibus_calc_sum(pkt->msg, length);
	pkt->length = length;
	pkt->countdown = countdown;
	pkt->sync = sync;
	pkt->tag = tag;
	pkt->transmit_count = 0;

	if (prepend)
		pkt_list = slist_prepend(pkt_list, pkt);
	else
		pkt_list = slist_append(pkt_list, pkt);
}

void ibus_send(int ifd, const unsigned char *msg, int length, int gpio_number)
{
	ibus_log("send(%d): %02x %02x %02x queued\n", length, msg[0], msg[1], msg[2]);

	if (gpio_number > 0)
	{
		ibus_add_to_queue(msg, length, 1, FALSE, FALSE, 0);
	}
}

void ibus_send_with_tag(int ifd, const unsigned char *msg, int length, int gpio_number, bool sync, bool prepend, int tag)
{
	ibus_log("send(%d): %02x %02x %02x queued (tag=%d)\n", length, msg[0], msg[1], msg[2], tag);

	if (gpio_number > 0)
	{
		ibus_add_to_queue(msg, length, 1, sync, prepend, tag);
	}
}
