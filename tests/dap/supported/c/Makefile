CC ?= cc
CFLAGS ?= -std=c11 -O0 -g3 -Wall -Wextra -Wpedantic
LDFLAGS ?=

TARGET := dap_sample.out
SRCS := main.c runmode.c items.c numeric.c worker.c
OBJS := $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS) -pthread

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c items.h numeric.h runmode.h worker.h
items.o: items.c items.h
numeric.o: numeric.c numeric.h
runmode.o: runmode.c runmode.h
worker.o: worker.c worker.h

clean:
	rm -f $(TARGET) $(OBJS)
