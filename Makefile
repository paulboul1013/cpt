CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -g

TARGET = main

SRC = \
	src/main.c \
	src/reader.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)