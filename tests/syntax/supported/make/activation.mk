# activation fixture
CC := cc

all: rotide

rotide: main.o
	$(CC) -o $@ $<
