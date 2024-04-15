# Default to the host architecture
ARCH ?= $(shell uname -m)

ifeq ($(ARCH),x86_64)
	CC := gcc
else ifeq ($(ARCH),arm)
	CC := arm-linux-gnueabihf-gcc
	CFLAGS += -marm
else ifeq ($(ARCH),aarch64)
	CC := aarch64-linux-gnu-gcc
endif

LIB_DIRECTORY := -L/usr/lib/$(ARCH)-linux-gnu
INC_DIRECTORY := -I/usr/include/bluetooth
LIB := -lbluetooth
CFLAGS += -Wall -fPIC

.c.o:
	$(CC) $(INC_DIRECTORY) $(CFLAGS) -c $<

all:
	make tool
	make lib

tool: bletool.o
	$(CC) -o bletool $(INC_DIRECTORY) $(LIB_DIRECTORY) $< $(LIB)

lib: bletool.o
	$(CC) $(CFLAGS) -shared -o libbletool.so $< $(LIB_DIRECTORY) $(LIB)
	install -m 644 libbletool.so ../

clean:
	rm -f bletool bletool.o libbletool.o libbletool.so
