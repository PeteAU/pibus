void log_msg(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_msg_with_hex(const unsigned char *data, int length, char *fmt, ...) __attribute__((format(printf, 3, 4)));
void log_ibus(const unsigned char *data, int length, const char *suffix);
int log_open(time_t start, int level);
void log_flush();
void log_close();
