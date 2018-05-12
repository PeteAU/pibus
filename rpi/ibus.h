int ibus_init(const char *port, char *startup, bool bluetooth, bool camera, bool cdc_announce, int cdc_info_interval, int gpio_number, int idle_timeout, int hw_version, int input, bool handle_nextprev, bool rotary_opposite, bool z4_keymap, int server_port, int log_level, int coolant_warning);
void ibus_log(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ibus_dump_hex(FILE *out, const unsigned char *data, int length, const char *suffix);
int ibus_send_ascii(const char *cmd);
void ibus_cleanup(void);
