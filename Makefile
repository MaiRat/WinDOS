# Makefile for WinDOS NE-file parser
#
# Targets:
#   all      - build the parser library and test binary
#   test     - build and run all unit tests
#   clean    - remove build artefacts

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter \
           -g -I$(CURDIR)/src
LDFLAGS :=

SRC_DIR   := src
TEST_DIR  := tests
BUILD_DIR := build

PARSER_SRC  := $(SRC_DIR)/ne_parser.c
PARSER_OBJ  := $(BUILD_DIR)/ne_parser.o

TEST_SRC    := $(TEST_DIR)/test_ne_parser.c
TEST_BIN    := $(BUILD_DIR)/test_ne_parser

.PHONY: all test clean

all: $(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PARSER_OBJ): $(PARSER_SRC) $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_BIN): $(TEST_SRC) $(PARSER_OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TEST_BIN)
	@echo "--- Running NE parser tests ---"
	$(TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)
