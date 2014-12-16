#define TAG_MENU	1
#define TAG_CDC		2
#define TAG_TIME	3
#define TAG_DATE	4

bool ibus_service_queue(int ifd, bool can_send, int gpio_number);
void ibus_remove_from_queue(const unsigned char *msg, int length);
void ibus_remove_tag_from_queue(int tag);
void ibus_send(int ifd, const unsigned char *msg, int length, int gpio_number);
void ibus_send_with_tag(int ifd, const unsigned char *msg, int length, int gpio_number, bool sync, bool prepend, int tag);
