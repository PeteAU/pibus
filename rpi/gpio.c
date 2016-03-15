#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "gpio.h"

#define V1_BCM2708_PERI_BASE        0x20000000
#define V1_GPIO_BASE                (V1_BCM2708_PERI_BASE + 0x200000) /* GPIO controller */

#define V2_BCM2708_PERI_BASE        0x3F000000
#define V2_GPIO_BASE                (V2_BCM2708_PERI_BASE + 0x200000) /* GPIO controller */

#define GPIO_PIN_L0_FSEL0_OFFSET	(0)		/* GPFSEL0 */
#define GPIO_PIN_L0_FSEL1_OFFSET	((0x04/4))	/* GPFSEL1 */
#define GPIO_PIN_L0_SET_OFFSET		((0x1C/4))	/* GPSET0 */
#define GPIO_PIN_L0_CLR_OFFSET		((0x28/4))	/* GPCLR0 */
#define GPIO_PIN_L0_READ_OFFSET		((0x34/4))	/* GPLEV0 */

#define GPIO_INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define GPIO_OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))

#define GPIO_PUD	*(gpio+37)	/* Pull up/down */
#define GPIO_PUDCLK0	*(gpio+38)	/* Pull up/down clock */
#define GPIO_PUDCLK1	*(gpio+39)	/* Pull up/down clock */

#define UART_FR_OFFSET			((0x1018/4))
#define UART_FR_RXFE_BIT		0x10

#define BLOCK_SIZE (8*1024)


// I/O access
static volatile unsigned int *gpio;


static int pi_v2()
{
	char buf[512];
	int fd;
	int v2 = 0;

	fd = open("/proc/cpuinfo", O_RDONLY);
	if (fd != -1)
	{
		if (read(fd, buf, sizeof (buf)) > 10)
		{
			buf[sizeof(buf) - 1] = 0;
			/* Either of these are not an old V1 Pi */
			if (strstr(buf, "ARMv7") || strstr(buf, "ARMv8"))
			{
				v2 = 1;
			}
		}
		close(fd);
	}

	return v2;
}

int gpio_init()
{
#ifdef __i386__
	return 0;
#else
	void *gpio_map;
	int mem_fd;

	if ((mem_fd = open("/dev/mem", O_RDWR | O_SYNC) ) < 0)
	{
		printf("can't open /dev/mem \n");
		exit(-1);
	}

	gpio_map = mmap(
		NULL,             //Any adddress in our space will do
		BLOCK_SIZE,       //Map length
		PROT_READ|PROT_WRITE,// Enable reading & writting to mapped memory
		MAP_SHARED,       //Shared with other processes
		mem_fd,           //File to map
		pi_v2() ? V2_GPIO_BASE : V1_GPIO_BASE         //Offset to GPIO peripheral
	);

	close(mem_fd); //No need to keep mem_fd open after mmap

	if (gpio_map == MAP_FAILED)
	{
		printf("mmap error %p\n", gpio_map);//errno also set!
		exit(-1);
	}

	// Always use volatile pointer!
	gpio = (volatile unsigned int *)gpio_map;

	return 0;
#endif
}

#ifndef __i386__

void gpio_set_input(int gpio_number)
{
	GPIO_INP_GPIO(gpio_number);
}

void gpio_set_output(int gpio_number)
{
	GPIO_INP_GPIO(gpio_number);
	GPIO_OUT_GPIO(gpio_number);
}

int gpio_read(int gpio_number)
{
	/* read GPIO 0-31 */
	return ((*(gpio + GPIO_PIN_L0_READ_OFFSET)) & (1 << gpio_number)) ? 1 : 0;
}

void gpio_write(int gpio_number, int value)
{
	/* write GPIO 0-31 */
	if (value)
	{
		*(gpio + GPIO_PIN_L0_SET_OFFSET) = (1 << gpio_number);
	}
	else
	{
		*(gpio + GPIO_PIN_L0_CLR_OFFSET) = (1 << gpio_number);
	}
}

void gpio_set_pull(int gpio_number, pull_type pt)
{
	GPIO_PUD = pt;
	usleep(1000);

	GPIO_PUDCLK0 = (1 << gpio_number);
	usleep(1000);

	GPIO_PUD = 0;
	GPIO_PUDCLK0 = 0;
	usleep(1000);
}

#else

void gpio_set_input(int gpio_number)
{
}

void gpio_set_output(int gpio_number)
{
}

int gpio_read(int gpio_number)
{
	return 1;
}

void gpio_write(int gpio_number, int value)
{
}

void gpio_set_pull(int gpio_number, pull_type pt)
{
}

#endif

void gpio_cleanup()
{

}

int uart_rx_fifo_empty()
{
#ifdef __i386__
	return 1;
#else
	return ((*(gpio + UART_FR_OFFSET)) & UART_FR_RXFE_BIT) ? 1 : 0;
#endif
}
