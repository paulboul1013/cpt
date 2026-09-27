CC = gcc
CPPFLAGS =
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g
LDFLAGS =
LDLIBS =

TARGET = pdftext
ASAN_TARGET := $(TARGET)-asan
OWNERSHIP_TARGET := tests/ownership-test
OWNERSHIP_ASAN_TARGET := tests/ownership-test-asan
ERROR_TARGET := tests/error-test
ERROR_ASAN_TARGET := tests/error-test-asan
LEXER_TARGET := tests/lexer-test
LEXER_ASAN_TARGET := tests/lexer-test-asan
OBJECT_TARGET := tests/object-test
OBJECT_ASAN_TARGET := tests/object-test-asan
LIMITS_TARGET := tests/limits-test
LIMITS_ASAN_TARGET := tests/limits-test-asan
READER_TARGET := tests/reader-test
READER_ASAN_TARGET := tests/reader-test-asan
READER_SRC := tests/reader_test.c
ASAN_CFLAGS = $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer
ASAN_LDFLAGS = $(LDFLAGS) -fsanitize=address,undefined

SRC = \
	src/main.c \
	src/reader.c \
	src/lexer.c \
	src/parser.c \
	src/object.c \
	src/error.c \
	src/limits.c
HDR = \
	src/reader.h \
	src/lexer.h \
	src/parser.h \
	src/object.h \
	src/error.h \
	src/bytes.h \
	src/limits.h
TEST_INPUT = tests/numbers.txt
OWNERSHIP_SRC = tests/ownership_test.c
ERROR_SRC = tests/error_test.c
LEXER_SRC = tests/lexer_test.c
OBJECT_SRC = tests/object_test.c
LIMITS_SRC = tests/limits_test.c

define CHECK_NUMBERS_OUTPUT
	@expected=$$(printf '%s\n' 'file size: 20 bytes' 'INT 123' 'INT 456' 'INT -42' 'INT 88' 'INT 999'); \
	actual=$$(./$(1) $(TEST_INPUT)); \
	if [ "$$actual" != "$$expected" ]; then \
		printf 'build gate output mismatch for %s\n' "$(1)" >&2; \
		printf '%s\n' "$$actual" >&2; \
		exit 1; \
	fi
endef

.PHONY: all clean test asan $(TARGET) $(ASAN_TARGET)

all: $(TARGET)

$(TARGET): $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(SRC) $(LDLIBS) -o $@

test: $(TARGET) $(OWNERSHIP_TARGET) $(ERROR_TARGET) $(LEXER_TARGET) $(OBJECT_TARGET) $(LIMITS_TARGET) $(READER_TARGET)
	$(call CHECK_NUMBERS_OUTPUT,$(TARGET))
	./tests/run-fixtures.sh ./$(TARGET)
	./$(OWNERSHIP_TARGET)
	./$(ERROR_TARGET)
	./$(LEXER_TARGET)
	./$(OBJECT_TARGET)
	./$(LIMITS_TARGET)
	./$(READER_TARGET)

asan: $(ASAN_TARGET) $(OWNERSHIP_ASAN_TARGET) $(ERROR_ASAN_TARGET) $(LEXER_ASAN_TARGET) $(OBJECT_ASAN_TARGET) $(LIMITS_ASAN_TARGET) $(READER_ASAN_TARGET)
	$(call CHECK_NUMBERS_OUTPUT,$(ASAN_TARGET))
	./tests/run-fixtures.sh ./$(ASAN_TARGET)
	./$(OWNERSHIP_ASAN_TARGET)
	./$(ERROR_ASAN_TARGET)
	./$(LEXER_ASAN_TARGET)
	./$(OBJECT_ASAN_TARGET)
	./$(LIMITS_ASAN_TARGET)
	./$(READER_ASAN_TARGET)

$(ASAN_TARGET): $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(ASAN_CFLAGS) $(ASAN_LDFLAGS) $(SRC) $(LDLIBS) -o $@

$(OWNERSHIP_TARGET): $(OWNERSHIP_SRC) $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(OWNERSHIP_SRC) src/reader.c src/lexer.c src/parser.c src/object.c src/error.c src/limits.c $(LDLIBS) -o $@

$(OWNERSHIP_ASAN_TARGET): $(OWNERSHIP_SRC) $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(ASAN_CFLAGS) $(ASAN_LDFLAGS) $(OWNERSHIP_SRC) src/reader.c src/lexer.c src/parser.c src/object.c src/error.c src/limits.c $(LDLIBS) -o $@

$(ERROR_TARGET): $(ERROR_SRC) src/error.c src/error.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(ERROR_SRC) src/error.c $(LDLIBS) -o $@

$(ERROR_ASAN_TARGET): $(ERROR_SRC) src/error.c src/error.h
	$(CC) $(CPPFLAGS) $(ASAN_CFLAGS) $(ASAN_LDFLAGS) $(ERROR_SRC) src/error.c $(LDLIBS) -o $@

$(LEXER_TARGET): $(LEXER_SRC) $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(LEXER_SRC) src/reader.c src/lexer.c src/error.c src/limits.c $(LDLIBS) -o $@

$(LEXER_ASAN_TARGET): $(LEXER_SRC) $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(ASAN_CFLAGS) $(ASAN_LDFLAGS) $(LEXER_SRC) src/reader.c src/lexer.c src/error.c src/limits.c $(LDLIBS) -o $@

$(OBJECT_TARGET): $(OBJECT_SRC) $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(OBJECT_SRC) src/reader.c src/lexer.c src/parser.c src/object.c src/error.c src/limits.c $(LDLIBS) -o $@

$(OBJECT_ASAN_TARGET): $(OBJECT_SRC) $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(ASAN_CFLAGS) $(ASAN_LDFLAGS) $(OBJECT_SRC) src/reader.c src/lexer.c src/parser.c src/object.c src/error.c src/limits.c $(LDLIBS) -o $@

$(LIMITS_TARGET): $(LIMITS_SRC) $(SRC) $(HDR) src/limits.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(LIMITS_SRC) src/reader.c src/lexer.c src/parser.c src/object.c src/error.c src/limits.c $(LDLIBS) -o $@

$(LIMITS_ASAN_TARGET): $(LIMITS_SRC) $(SRC) $(HDR) src/limits.h
	$(CC) $(CPPFLAGS) $(ASAN_CFLAGS) $(ASAN_LDFLAGS) $(LIMITS_SRC) src/reader.c src/lexer.c src/parser.c src/object.c src/error.c src/limits.c $(LDLIBS) -o $@

$(READER_TARGET): $(READER_SRC) src/reader.c src/reader.h src/error.c src/error.h src/limits.c src/limits.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(READER_SRC) src/reader.c src/error.c src/limits.c $(LDLIBS) -o $@

$(READER_ASAN_TARGET): $(READER_SRC) src/reader.c src/reader.h src/error.c src/error.h src/limits.c src/limits.h
	$(CC) $(CPPFLAGS) $(ASAN_CFLAGS) $(ASAN_LDFLAGS) $(READER_SRC) src/reader.c src/error.c src/limits.c $(LDLIBS) -o $@

clean:
	$(RM) $(TARGET) $(ASAN_TARGET) $(OWNERSHIP_TARGET) $(OWNERSHIP_ASAN_TARGET) $(ERROR_TARGET) $(ERROR_ASAN_TARGET) $(LEXER_TARGET) $(LEXER_ASAN_TARGET) $(OBJECT_TARGET) $(OBJECT_ASAN_TARGET) $(LIMITS_TARGET) $(LIMITS_ASAN_TARGET) $(READER_TARGET) $(READER_ASAN_TARGET)
