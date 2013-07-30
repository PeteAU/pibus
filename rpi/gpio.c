#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define BCM2708_PERI_BASE        0x20000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */
#define GPIO_PIN_LEVEL0_OFFSET	((0x34/4))
#define BLOCK_SIZE (4*1024)


// I/O access
static volatile unsigned int *gpio;


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
		GPIO_BASE         //Offset to GPIO peripheral
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

int gpio_read(int gpio_number)
{
#ifdef __i386__
	return 1;
#else
	/* read GPIO 0-31 */
	return ((*(gpio + GPIO_PIN_LEVEL0_OFFSET)) & (1 << gpio_number)) ? 1 : 0;
#endif
}

void gpio_cleanup()
{

}
