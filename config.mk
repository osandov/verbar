PREFIX = /usr/local
CFLAGS = -std=gnu99 -pedantic -Wall -Werror -D_GNU_SOURCE -O2 `pkg-config --cflags x11`
LDFLAGS = -lmnl -lpulse `pkg-config --libs x11`
