CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wundef -fno-common -pedantic -std=c2x

SRCS = rotide.c terminal.c buffer.c output.c input.c
OBJS = $(SRCS:.c=.o)

rotide: $(OBJS)
	$(CC) $(OBJS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(OBJS) rotide
