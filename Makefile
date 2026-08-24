CC = gcc
CPPFLAGS =
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g
LDFLAGS =
LDLIBS =

TARGET = pdftext
ASAN_TARGET := $(TARGET)-asan

SRC = \
	src/main.c \
	src/reader.c \
	src/lexer.c \
	src/parser.c \
	src/object.c

.PHONY: all clean test asan

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

test: $(TARGET)
	@./$(TARGET) tests/numbers.txt >/dev/null

asan: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address,undefined
asan: $(ASAN_TARGET)
	@./$(ASAN_TARGET) tests/numbers.txt >/dev/null

$(ASAN_TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	$(RM) $(TARGET) $(ASAN_TARGET)
