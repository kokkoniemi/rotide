# comment
CC := cc
.PHONY: all clean
all: rotide
	@$(CC) -o $@ main.o
include config.mk
