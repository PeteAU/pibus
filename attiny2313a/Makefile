#avrdude -v -v -c usbasp -p attiny2313 -U flash:w:ibus.hex
#avrdude -v -v -c usbasp -p attiny2313 -U lfuse:w:0x7d:m -U hfuse:w:0x9f:m -U efuse:w:0xff:m

CC = avr-gcc
OBJDUMP = avr-objdump
OBJCOPY = avr-objcopy
STRIP = avr-strip

all:
	$(CC) -Wall -fshort-enums -fpack-struct -fwhole-program -ffreestanding -funsigned-char -Os -mmcu=attiny2313a ibus.c -o ibus.o -Wa,-ahls=ibus.lst
	$(OBJCOPY) -j .text -j .data -O ihex ibus.o ibus.hex
	$(OBJCOPY) -j .text -j .data -O binary ibus.o ibus.bin
	$(OBJDUMP) -D ibus.o > ibus-dump.txt
