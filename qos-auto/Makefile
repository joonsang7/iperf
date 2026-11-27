# Build libiperf first with ./configure --disable-shared --enable-static --without-openssl
IPERF_ROOT ?= ..
CC ?= cc
CFLAGS += -std=gnu11 -O2 -Wall -Wextra
sources = main.c scan.c load.c probe.c plan.c profile.c
archive = $(IPERF_ROOT)/src/.libs/libiperf.a
qos: $(sources) qos.h $(archive)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I"$(IPERF_ROOT)/src" $(sources) "$(archive)" $(LDFLAGS) -lm -pthread $(LDLIBS) -o $@
