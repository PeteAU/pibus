int ibus_init(const char *port, char *startup, bool bluetooth, bool camera, bool mk3, int cdc_info_interval, int gpio_number, int hw_version);
void ibus_log(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ibus_dump_hex(FILE *out, const unsigned char *data, int length, const char *suffix);
void ibus_mainloop(void);
void ibus_cleanup(void);
