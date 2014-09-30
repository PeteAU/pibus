typedef enum
{
	PULL_NONE = 0,
	PULL_DOWN = 1,
	PULL_UP = 2
} pull_type;

int gpio_init();
void gpio_set_input(int gpio_number);
void gpio_set_output(int gpio_number);
int gpio_read(int gpio_number);
void gpio_write(int gpio_number, int value);
void gpio_set_pull(int gpio_number, pull_type pt);
void gpio_cleanup();
