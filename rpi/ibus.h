int ibus_init(const char *port, char *startup, bool bluetooth, bool camera, bool mk3, int cdc_info_interval, int gpio_number, int hw_version);
void ibus_log(char *fmt, ...);
void ibus_dump_hex(FILE *out, const unsigned char *data, int length, bool check_the_sum);
void ibus_mainloop(void);
void ibus_cleanup(void);
