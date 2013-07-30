#include <stdlib.h>

#include "slist.h"


SList *slist_append(SList *list, void *data)
{
	SList *new_list;
	SList *tmp;

	new_list = malloc(sizeof(SList));
	new_list->data = data;
	new_list->next = NULL;

	if (list == NULL)
	{
		return new_list;
	}

	tmp = list;
	while (tmp)
	{
		if (!tmp->next)
		{
			break;
		}
		tmp = tmp->next;
	}

	tmp->next = new_list;

	return list;
}

SList *slist_prepend(SList *list, void *data)
{
	SList *new_list;

	new_list = malloc(sizeof(SList));
	new_list->data = data;
	new_list->next = list;

	return new_list;
}

SList *slist_remove(SList *list, const void *data)
{
	SList *tmp, *prev = NULL;

	tmp = list;
	while (tmp)
	{
		if (tmp->data == data)
		{
			if (prev)
				prev->next = tmp->next;
			else
				list = tmp->next;

			free(tmp);
			break;
		}
		prev = tmp;
		tmp = prev->next;
	}

	return list;
}
