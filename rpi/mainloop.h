#define FIA_READ 1
#define FIA_WRITE 2
#define FIA_EX 4

#define TRUE 1
#define FALSE 0

#define RODATA static const unsigned char

typedef int bool;

typedef void (*socket_callback) (int condition, void *user_data);
typedef int (*timer_callback) (void *user_data);

struct socketeventRec
{
	socket_callback callback;
	void *userdata;
	int sok;
	int tag;
	int rread:1;
	int wwrite:1;
	int eexcept:1;
	int checked:1;
};

typedef struct socketeventRec socketevent;


struct timerRec
{
	timer_callback callback;
	void *userdata;
	int interval;
	int tag;
	uint64_t next_call;	/* milliseconds */
};

typedef struct timerRec timerevent;


void mainloop_init(void);
uint64_t mainloop_get_millisec(void);
void mainloop(void);

void mainloop_timeout_remove(int tag);
int mainloop_timeout_add(int interval, timer_callback callback, void *userdata);
void mainloop_timeout_override_nextcall(int tag, uint64_t next_call);

void mainloop_input_remove(int tag);
int mainloop_input_add(int sok, int flags, socket_callback func, void *data);

