PREFIX = /usr/local
CFLAGS = -std=gnu99 -pedantic -Wall -Werror -D_GNU_SOURCE -O2 -I/usr/include/libnl3 `pkg-config --cflags x11`
LDFLAGS = -lmnl -lnl-3 -lnl-genl-3 -lpulse `pkg-config --libs x11`
