#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>
#include "mainloop.h"
#include "slist.h"


static SList *tmr_list;		  /* timer list */
static int tmr_list_count;
static SList *se_list;			  /* socket event list */
static int se_list_count;
static int done = FALSE;		  /* finished ? */


uint64_t mainloop_get_millisec(void)
{
#if 0
	struct timeval now;
	gettimeofday(&now, NULL);
	return (now.tv_sec * 1000) + (now.tv_usec / 1000);
#else
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec * 1000) + (now.tv_nsec / 1000000);
#endif
}

void mainloop_timeout_remove(int tag)
{
	timerevent *te;
	SList *list;

	list = tmr_list;
	while (list)
	{
		te = (timerevent *) list->data;
		if (te->tag == tag)
		{
			tmr_list = slist_remove(tmr_list, te);
			free(te);
			return;
		}
		list = list->next;
	}
}

int mainloop_timeout_add(int interval, timer_callback callback, void *userdata)
{
	timerevent *te = malloc(sizeof (timerevent));

	tmr_list_count++;	/* this overflows at 2.2Billion, who cares!! */

	te->tag = tmr_list_count;
	te->interval = interval;
	te->callback = callback;
	te->userdata = userdata;

	te->next_call = mainloop_get_millisec() + te->interval;

	tmr_list = slist_prepend(tmr_list, te);

	return te->tag;
}

/*void mainloop_timeout_override_nextcall(int tag, uint64_t next_call)
{
	timerevent *te;
	SList *list;

	list = tmr_list;
	while (list)
	{
		te = (timerevent *) list->data;
		if (te->tag == tag)
		{
			te->next_call = next_call;
			return;
		}
		list = list->next;
	}
}*/

void mainloop_input_remove(int tag)
{
	socketevent *se;
	SList *list;

	list = se_list;
	while (list)
	{
		se = (socketevent *) list->data;
		if (se->tag == tag)
		{
			se_list = slist_remove(se_list, se);
			free(se);
			return;
		}
		list = list->next;
	}
}

int mainloop_input_add(int sok, int flags, socket_callback func, void *data)
{
	socketevent *se = malloc(sizeof(socketevent));

	se_list_count++;	/* this overflows at 2.2Billion, who cares!! */

	se->tag = se_list_count;
	se->sok = sok;
	se->rread = flags & FIA_READ;
	se->wwrite = flags & FIA_WRITE;
	se->eexcept = flags & FIA_EX;
	se->callback = func;
	se->userdata = data;
	se_list = slist_prepend(se_list, se);

	return se->tag;
}

void mainloop_init(void)
{
	tmr_list = NULL;
	se_list = NULL;

	tmr_list_count = 0;
	se_list_count = 0;
}

void mainloop(void)
{
	struct timeval timeout;
	socketevent *se;
	timerevent *te;
	int nfds;
	fd_set rd, wd, ex;
	SList *list;
	uint64_t shortest, delay;
	uint64_t ms;

	while (!done)
	{
		nfds = 0;
		FD_ZERO(&rd);
		FD_ZERO(&wd);
		FD_ZERO(&ex);

		list = se_list;
		while (list)
		{
			se = (socketevent *) list->data;
			if (se->rread)
				FD_SET(se->sok, &rd);
			if (se->wwrite)
				FD_SET(se->sok, &wd);
			if (se->eexcept)
				FD_SET(se->sok, &ex);
			if (se->sok > nfds)
				nfds = se->sok;
			list = list->next;
		}

		/* find the shortest timeout event */
		shortest = 0;
		list = tmr_list;
		while (list)
		{
			te = (timerevent *) list->data;
			if (te->next_call < shortest || shortest == 0)
				shortest = te->next_call;
			list = list->next;
		}

		ms = mainloop_get_millisec();
		if (shortest > ms)
		{
			delay = shortest - ms;
			timeout.tv_sec = delay / 1000;
			timeout.tv_usec = (delay % 1000) * 1000;
		}
		else
		{
			timeout.tv_sec = 0;
			timeout.tv_usec = 0;
		}

		select(nfds + 1, &rd, &wd, &ex, &timeout);

		/* set all checked flags to false */
		list = se_list;
		while (list)
		{
			se = (socketevent *) list->data;
			se->checked = 0;
			list = list->next;
		}

		/* check all the socket callbacks */
		list = se_list;
		while (list)
		{
			se = (socketevent *) list->data;
			se->checked = 1;
			if (se->rread && FD_ISSET(se->sok, &rd))
			{
				se->callback(FIA_READ, se->userdata);
			}
			else if (se->wwrite && FD_ISSET(se->sok, &wd))
			{
				se->callback(FIA_WRITE, se->userdata);
			}
			else if (se->eexcept && FD_ISSET(se->sok, &ex))
			{
				se->callback(FIA_EX, se->userdata);
			}
			list = se_list;
			if (list)
			{
				se = (socketevent *) list->data;
				while (se->checked)
				{
					list = list->next;
					if (!list)
						break;
					se = (socketevent *) list->data;
				}
			}
		}

		/* now check our list of timeout events, some might need to be called! */
		ms = mainloop_get_millisec();
do_timers:
		list = tmr_list;
		while (list)
		{
			te = (timerevent *) list->data;
			list = list->next;
			if (ms >= te->next_call)
			{
				/* if the callback returns 0, it must be removed */
				if (te->callback(te->userdata) == 0)
				{
					mainloop_timeout_remove(te->tag);
					goto do_timers;
				}
				te->next_call = ms + te->interval;
			}
		}

	}
}

void mainloop_exit(void)
{
	done = TRUE;
}

