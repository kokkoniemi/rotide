CC ?= cc
CPPFLAGS ?= -I.
CFLAGS ?= -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wundef -fno-common -pedantic -std=c2x

SRCS = rotide.c terminal.c buffer.c output.c input.c
OBJS = $(SRCS:.c=.o)
TEST_SRCS = tests/rotide_tests.c tests/test_helpers.c
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_BIN = tests/rotide_tests

rotide: $(OBJS)
	$(CC) $(OBJS) -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_OBJS) terminal.o buffer.o output.o input.o
	$(CC) $^ -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

.PHONY: clean test
clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TEST_BIN) rotide
