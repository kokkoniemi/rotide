CC ?= cc
CPPFLAGS ?= -I.
CFLAGS ?= -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wundef -fno-common -pedantic -std=c2x
LDFLAGS ?=
SANITIZER_CFLAGS ?= -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
SANITIZER_LDFLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer

SRCS = rotide.c terminal.c buffer.c output.c input.c alloc.c save_syscalls.c
OBJS = $(SRCS:.c=.o)
TEST_SRCS = tests/rotide_tests.c tests/test_helpers.c tests/alloc_test_hooks.c tests/save_syscalls_test_hooks.c
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_BIN = tests/rotide_tests

rotide: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_OBJS) terminal.o buffer.o output.o input.o alloc.o save_syscalls.o
	$(CC) $(LDFLAGS) $^ -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

test-sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) $(SANITIZER_CFLAGS)" \
		LDFLAGS="$(LDFLAGS) $(SANITIZER_LDFLAGS)" test

.PHONY: clean test test-sanitize
clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TEST_BIN) rotide
