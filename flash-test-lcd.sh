#!/bin/sh

set -x -e

avr-gcc -c -gdwarf-2 -std=gnu99 -Os -fsigned-char -fshort-enums    -Wno-attributes -mmcu=at90usb1286 -Wall -Werror -otest-lcd.o -Iqp-nano/include -I. -DV='"v0.1-34-gfdc6369"' -DD='"2013-06-01 22:27:16 WST"'   -c -o test-lcd.o test-lcd.c

avr-gcc -gdwarf-2 -Os -mmcu=at90usb1286 -o test-lcd.elf -Wl,-Map,dclock.map,--cref test-lcd.o

avr-objcopy -j .text -j .data -O ihex test-lcd.elf test-lcd.hex

avrdude -p usb1286 -B 20 -P /dev/ttyACM0 -c stk500v2 -U lfuse:w:$(head -1 fuse-lfuse):m -U hfuse:w:$(head -1 fuse-hfuse):m -U efuse:w:$(head -1 fuse-efuse):m -U flash:w:test-lcd.hex 

