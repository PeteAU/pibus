int gpio_init();
void gpio_set_input(int gpio_number);
void gpio_set_output(int gpio_number);
int gpio_read(int gpio_number);
void gpio_write(int gpio_number, int value);
void gpio_cleanup();
