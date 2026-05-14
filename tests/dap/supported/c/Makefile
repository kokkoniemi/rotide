CC ?= cc
CFLAGS ?= -std=c11 -O0 -g3 -Wall -Wextra -Wpedantic
LDFLAGS ?=

TARGET := rotide_dap_sample
SRC := rotide_dap_sample.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS) -pthread

clean:
	rm -f $(TARGET)

