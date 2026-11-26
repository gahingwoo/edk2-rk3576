# The PD and DisplayPort Alt Mode arithmetic of Rk3576Fusb302Lib (UsbPdDp.h) on the host. `make run`.
CFLAGS ?= -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS += -std=gnu11 -Wall -Wextra -Werror
HDR = ../../../Silicon/Rockchip/RK3576/Library/Rk3576Fusb302Lib/UsbPdDp.h

usbpd-host: test.c $(HDR)
	$(CC) $(CFLAGS) -o $@ test.c

run: usbpd-host
	./usbpd-host

clean:
	rm -f usbpd-host
