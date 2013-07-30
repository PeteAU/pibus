struct _SList
{
	void *data;
	struct _SList *next;
};

typedef struct _SList SList;

SList *slist_append(SList *list, void *data);
SList* slist_prepend(SList *list, void *data);
SList* slist_remove(SList *list, const void *data);

