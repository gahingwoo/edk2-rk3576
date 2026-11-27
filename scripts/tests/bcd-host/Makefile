# The BCD reader of PlatformBootManagerLib (WindowsBcd.h) on the host. `make run`.
# `make run BCD="path/to/BCD ..."` also prints what it reads from real stores.
CFLAGS ?= -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS += -std=gnu11 -Wall -Wextra -Werror
HDR = ../../../Silicon/Rockchip/Library/PlatformBootManagerLib/WindowsBcd.h

bcd-host: test.c $(HDR)
	$(CC) $(CFLAGS) -o $@ test.c

run: bcd-host
	./bcd-host $(BCD)

clean:
	rm -f bcd-host
