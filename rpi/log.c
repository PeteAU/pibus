#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <stdint.h>

#include "mainloop.h"
#include "annotate.h"
#include "log.h"


static FILE *flog;
static time_t log_start;
static int log_level;


static void log_write(char prefix_char, char *fmt, va_list args)
{
	static char buf[512];
	struct timespec ts;
	int len;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	len = sprintf(buf, "%6.6lu.%03lu ", ts.tv_sec - log_start, ts.tv_nsec / 1000000);
	if (prefix_char)
	{
		buf[len++] = prefix_char;
	}
	len += vsnprintf(buf + len, sizeof(buf) - (len + 1), fmt, args);

	buf[sizeof(buf) - 1] = '\0';
	if (len < 0 || len > (sizeof(buf) - 1))
		len = strlen (buf);

	fwrite(buf, len, 1, flog);
}

static void fwrite_hex(FILE *out, const unsigned char *data, int length)
{
	int i;

	for (i = 0; i < length; i++)
	{
		fprintf(out, "%02x", data[i]);
	}
}

void log_msg(char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	log_write('#', fmt, args);
	va_end(args);
}

void log_msg_with_hex(const unsigned char *data, int length, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	log_write('#', fmt, args);
	va_end(args);

	fwrite_hex(flog, data, length);
	fputc('\n', flog);
}

void log_ibus(const unsigned char *data, int length, const char *suffix)
{
	va_list empty;
	char annotation[512];

	if (log_level < 1)
	{
		return;
	}

	log_write(0, "", empty);
	fwrite_hex(flog, data, length);

	if (log_level)
	{
		fprintf(flog, " %s", annotate_device_to_device(data[0], data[2]));			

		if (log_level >= 2)
		{
			annotation[0] = 0;
			annotate_ibus_message(annotation, sizeof(annotation), data, length, (log_level>=3) ? TRUE : FALSE);
			fprintf(flog, " %s", annotation);
		}
	}

	if (suffix && suffix[0])
	{
		fprintf(flog, " %s\n", suffix);
	}
	else
	{
		fputc('\n', flog);
	}
}

int log_open(time_t start, int level)
{
	log_start = start;
	log_level = level;

#ifdef __i386__
	flog = fopen("./ibus.txt", "a");
#else
	{
		char logfile[256];
		struct passwd *pw;

		flog = NULL;
		pw = getpwuid(getuid());
		if (pw)
		{
			sprintf(logfile, "%s/ibus.txt", pw->pw_dir);
			flog = fopen(logfile, "a");
		}

		if (flog == NULL)
		{
			flog = fopen("/storage/ibus.txt", "a");
		}
	}
#endif
	if (flog == NULL)
	{
		return 1;
	}

	return 0;
}

void log_flush()
{
	fflush(flog);
}

void log_close()
{
	fflush(flog);
	fclose(flog);
	flog = NULL;
}

#if 0
int main()
{
	unsigned char buf[] = "\x00\x11\x22\x33\xff";
	struct timespec ts;

	flog = stdout;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	log_start = ts.tv_sec;

	log_msg("idle timeout %d\n", 99);
	log_msg_with_hex(buf, 5, "ibus_read(): discard %d: ", 7);

	log_ibus(buf, 5, "corrupt");

	return 0;
}
#endif
