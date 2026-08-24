CC = gcc
CPPFLAGS =
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g
LDFLAGS =
LDLIBS =

TARGET = pdftext
ASAN_TARGET := $(TARGET)-asan
ASAN_CFLAGS = $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer
ASAN_LDFLAGS = $(LDFLAGS) -fsanitize=address,undefined

SRC = \
	src/main.c \
	src/reader.c \
	src/lexer.c \
	src/parser.c \
	src/object.c
HDR = \
	src/reader.h \
	src/lexer.h \
	src/parser.h \
	src/object.h

.PHONY: all clean test asan $(TARGET) $(ASAN_TARGET)

all: $(TARGET)

$(TARGET): $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(SRC) $(LDLIBS) -o $@

test: $(TARGET)
	@./$(TARGET) tests/numbers.txt >/dev/null

asan: $(ASAN_TARGET)
	@./$(ASAN_TARGET) tests/numbers.txt >/dev/null

$(ASAN_TARGET): $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(ASAN_CFLAGS) $(ASAN_LDFLAGS) $(SRC) $(LDLIBS) -o $@

clean:
	$(RM) $(TARGET) $(ASAN_TARGET)
