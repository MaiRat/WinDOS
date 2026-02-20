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

LOADER_SRC  := $(SRC_DIR)/ne_loader.c
LOADER_OBJ  := $(BUILD_DIR)/ne_loader.o

RELOC_SRC   := $(SRC_DIR)/ne_reloc.c
RELOC_OBJ   := $(BUILD_DIR)/ne_reloc.o

MODULE_SRC  := $(SRC_DIR)/ne_module.c
MODULE_OBJ  := $(BUILD_DIR)/ne_module.o

TEST_SRC         := $(TEST_DIR)/test_ne_parser.c
TEST_BIN         := $(BUILD_DIR)/test_ne_parser

LOADER_TEST_SRC  := $(TEST_DIR)/test_ne_loader.c
LOADER_TEST_BIN  := $(BUILD_DIR)/test_ne_loader

RELOC_TEST_SRC   := $(TEST_DIR)/test_ne_reloc.c
RELOC_TEST_BIN   := $(BUILD_DIR)/test_ne_reloc

MODULE_TEST_SRC  := $(TEST_DIR)/test_ne_module.c
MODULE_TEST_BIN  := $(BUILD_DIR)/test_ne_module

.PHONY: all test clean

all: $(TEST_BIN) $(LOADER_TEST_BIN) $(RELOC_TEST_BIN) $(MODULE_TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PARSER_OBJ): $(PARSER_SRC) $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(LOADER_OBJ): $(LOADER_SRC) $(SRC_DIR)/ne_loader.h $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(RELOC_OBJ): $(RELOC_SRC) $(SRC_DIR)/ne_reloc.h $(SRC_DIR)/ne_loader.h $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(MODULE_OBJ): $(MODULE_SRC) $(SRC_DIR)/ne_module.h $(SRC_DIR)/ne_loader.h $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_BIN): $(TEST_SRC) $(PARSER_OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(LOADER_TEST_BIN): $(LOADER_TEST_SRC) $(PARSER_OBJ) $(LOADER_OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(RELOC_TEST_BIN): $(RELOC_TEST_SRC) $(PARSER_OBJ) $(LOADER_OBJ) $(RELOC_OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(MODULE_TEST_BIN): $(MODULE_TEST_SRC) $(PARSER_OBJ) $(LOADER_OBJ) $(MODULE_OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TEST_BIN) $(LOADER_TEST_BIN) $(RELOC_TEST_BIN) $(MODULE_TEST_BIN)
	@echo "--- Running NE parser tests ---"
	$(TEST_BIN)
	@echo "--- Running NE loader tests ---"
	$(LOADER_TEST_BIN)
	@echo "--- Running NE relocation tests ---"
	$(RELOC_TEST_BIN)
	@echo "--- Running NE module table tests ---"
	$(MODULE_TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)
