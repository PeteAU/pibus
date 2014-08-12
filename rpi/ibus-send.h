
void ibus_service_queue(int ifd, bool can_send, int gpio_number);
void ibus_remove_from_queue(const unsigned char *msg, int length);
void ibus_send(int ifd, const unsigned char *msg, int length);

