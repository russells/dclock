
-include make.conf

RM = rm -f
RM_RF = rm -rf

# If make.conf does not exist but we still include it in $(DEPDEPS), the .d
# files don't get made.  So exclude it if it does not exist.
ifeq ($(wildcard make.conf),make.conf)
DEPDEPS = Makefile make.conf
else
DEPDEPS = Makefile
endif

AVR_CC      ?= avr-gcc
AVR_LINK    ?= avr-gcc
AVR_OBJCOPY ?= avr-objcopy

APPNAME = dclock
PROGRAM = $(APPNAME).elf
PROGRAMMAPFILE = $(APPNAME).map
HEXPROGRAM = $(APPNAME).hex
BINPROGRAM = $(APPNAME).bin

V := $(shell ./VERSION-GEN)
D := $(shell date '+%Y-%m-%d %H:%M:%S %Z')

# Allow specification of some of the run time stuff.
ifdef SNOOZE_MINUTES
SNOOZE_MINUTES_FLAG = -DSNOOZE_MINUTES=$(SNOOZE_MINUTES)
else
SNOOZE_MINUTES_FLAG =
endif
ifdef MAX_SNOOZE_COUNT
MAX_SNOOZE_COUNT_FLAG = -DMAX_SNOOZE_COUNT=$(MAX_SNOOZE_COUNT)
else
MAX_SNOOZE_COUNT_FLAG =
endif
ifdef ALARM_SOUND_SECONDS
ALARM_SOUND_SECONDS_FLAG = -DALARM_SOUND_SECONDS=$(ALARM_SOUND_SECONDS)
else
ALARM_SOUND_SECONDS_FLAG =
endif

ALARM_FLAGS =	$(SNOOZE_MINUTES_FLAG) \
		$(MAX_SNOOZE_COUNT_FLAG) \
		$(ALARM_SOUND_SECONDS_FLAG)

# This makes the implicit .c.o rule work.
CC := $(AVR_CC)

%.d: %.c $(DEPDEPS)
	@echo DEP: $<
	@rm -f $@ $(@:.d=.u)
	@$(AVR_CC) -E -M $(CFLAGS) $< > /dev/null


# Allow the user to specify a different chip memory programmer.
AVR_PROGRAMMER      ?= stk500v2
AVR_PROGRAMMER_PORT ?= /dev/ttyACM0

LFUSE ?= $$(head -1 fuse-lfuse)
HFUSE ?= $$(head -1 fuse-hfuse)
EFUSE ?= $$(head -1 fuse-efuse)


QPN_INCDIR ?= qp-nano/include
EXTRA_LINK_FLAGS = -Wl,-Map,$(PROGRAMMAPFILE),--cref
TARGET_MCU = at90usb1286
CFLAGS  = -c -gdwarf-2 -std=gnu99 -Os -fsigned-char -fshort-enums \
	$(ALARM_FLAGS) \
	-Wno-attributes \
	-mmcu=$(TARGET_MCU) -Wall -Werror -o$@ \
	-I$(QPN_INCDIR) -I. \
	-DV='"$V"' -DD='"$D"'
LINKFLAGS = -gdwarf-2 -Os -mmcu=$(TARGET_MCU)

SRCS = dclock.c buttons.c alarm.c lcd.c serial.c bsp-avr.c \
	timekeeper.c time.c \
	timedisplay.c timesetter.c \
	twi.c twi-status.c \
	qp-nano/source/qepn.c qp-nano/source/qfn.c

SRC_OBJS = $(SRCS:.c=.o)
SRC_DEPS = $(SRCS:.c=.d)

# Version sources are distinct from the other sources so we can force version.o
# to be recompiled separately.
VERSION_SRCS = version.c
VERSION_OBJS = $(VERSION_SRCS:.c=.o)
VERSION_DEPS = $(VERSION_SRCS:.c=.d)

OBJS = $(SRC_OBJS) $(VERSION_OBJS)
DEPS = $(SRC_DEPS) $(VERSION_DEPS)

default: $(HEXPROGRAM)

.PHONY: bin
bin: $(BINPROGRAM)
$(BINPROGRAM): $(PROGRAM)
	$(AVR_OBJCOPY) -O binary $< $@

$(HEXPROGRAM): $(PROGRAM)
	$(AVR_OBJCOPY) -j .text -j .data -O ihex $< $@

$(PROGRAM): $(OBJS)
	$(AVR_LINK) $(LINKFLAGS) -o $(PROGRAM) $(EXTRA_LINK_FLAGS) \
	$(OBJS)

# Force a recompile of version.o if any other object file is recompiled.  This
# updates the startup message with the latest compilation date.
$(VERSION_OBJS): $(SRC_OBJS)


# Utility program(s).

decimal-time-conversion: decimal-time-conversion.c
	gcc -Wall -o decimal-time-conversion decimal-time-conversion.c


ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),decimallll-time-conversion)
-include $(DEPS) $(VERSION_DEPS)
endif
endif


.PHONY: tags
tags:
	etags *.[ch]
	ctags *.[ch]


# clean targets...

.PHONY: clean

clean:
	-$(RM_RF) $(OBJS) $(PROGRAM) $(HEXPROGRAM) $(PROGRAMMAPFILE) $(BINPROGRAM) $(DEPS)
	-$(RM_RF) doc
	-$(RM_RF) decimal-time-conversion

.PHONY: flash
flash: $(HEXPROGRAM)
	avrdude -p usb1286 -B 20 \
		-P $(AVR_PROGRAMMER_PORT) -c $(AVR_PROGRAMMER) \
		-U lfuse:w:$(LFUSE):m \
		-U hfuse:w:$(HFUSE):m \
		-U efuse:w:$(EFUSE):m \
		-U flash:w:$(HEXPROGRAM)

.PHONY: doc
doc:
	@mkdir -p doc
	doxygen Doxyfile
