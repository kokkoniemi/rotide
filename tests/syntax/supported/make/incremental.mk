objects := main.o editor.o

all: rotide

rotide: $(objects)
	$(CC) -o $@ $^
