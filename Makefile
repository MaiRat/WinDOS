# Makefile for WinDOS NE-file parser
#
# Targets:
#   all      - build the parser library and test binaries
#   test     - build and run all unit tests
#   clean    - remove build artefacts
#
# Toolchain: Open Watcom C compiler (wcc) targeting 16-bit real-mode DOS.
# Install Open Watcom from https://github.com/open-watcom/open-watcom-v2
#
# wcc flags used:
#   -ml   : large memory model (separate far code and data segments)
#   -za99 : enable C99 language extensions
#   -wx   : enable all warnings
#   -d2   : full symbolic debug information
#   -i=   : include search path (Watcom syntax; no space before path)

CC      := wcc
CFLAGS  := -ml -za99 -wx -d2 -i=$(CURDIR)/src
LD      := wlink
LDFLAGS := system dos option quiet

SRC_DIR   := src
TEST_DIR  := tests
BUILD_DIR := build

PARSER_SRC  := $(SRC_DIR)/ne_parser.c
PARSER_OBJ  := $(BUILD_DIR)/ne_parser.obj

LOADER_SRC  := $(SRC_DIR)/ne_loader.c
LOADER_OBJ  := $(BUILD_DIR)/ne_loader.obj

RELOC_SRC   := $(SRC_DIR)/ne_reloc.c
RELOC_OBJ   := $(BUILD_DIR)/ne_reloc.obj

MODULE_SRC  := $(SRC_DIR)/ne_module.c
MODULE_OBJ  := $(BUILD_DIR)/ne_module.obj

IMPEXP_SRC  := $(SRC_DIR)/ne_impexp.c
IMPEXP_OBJ  := $(BUILD_DIR)/ne_impexp.obj

TEST_SRC         := $(TEST_DIR)/test_ne_parser.c
TEST_OBJ         := $(BUILD_DIR)/test_ne_parser.obj
TEST_BIN         := $(BUILD_DIR)/test_ne_parser.exe

LOADER_TEST_SRC  := $(TEST_DIR)/test_ne_loader.c
LOADER_TEST_OBJ  := $(BUILD_DIR)/test_ne_loader.obj
LOADER_TEST_BIN  := $(BUILD_DIR)/test_ne_loader.exe

RELOC_TEST_SRC   := $(TEST_DIR)/test_ne_reloc.c
RELOC_TEST_OBJ   := $(BUILD_DIR)/test_ne_reloc.obj
RELOC_TEST_BIN   := $(BUILD_DIR)/test_ne_reloc.exe

MODULE_TEST_SRC  := $(TEST_DIR)/test_ne_module.c
MODULE_TEST_OBJ  := $(BUILD_DIR)/test_ne_module.obj
MODULE_TEST_BIN  := $(BUILD_DIR)/test_ne_module.exe

IMPEXP_TEST_SRC  := $(TEST_DIR)/test_ne_impexp.c
IMPEXP_TEST_OBJ  := $(BUILD_DIR)/test_ne_impexp.obj
IMPEXP_TEST_BIN  := $(BUILD_DIR)/test_ne_impexp.exe

.PHONY: all test clean

all: $(TEST_BIN) $(LOADER_TEST_BIN) $(RELOC_TEST_BIN) $(MODULE_TEST_BIN) $(IMPEXP_TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PARSER_OBJ): $(PARSER_SRC) $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(LOADER_OBJ): $(LOADER_SRC) $(SRC_DIR)/ne_loader.h $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(RELOC_OBJ): $(RELOC_SRC) $(SRC_DIR)/ne_reloc.h $(SRC_DIR)/ne_loader.h $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(MODULE_OBJ): $(MODULE_SRC) $(SRC_DIR)/ne_module.h $(SRC_DIR)/ne_loader.h $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(IMPEXP_OBJ): $(IMPEXP_SRC) $(SRC_DIR)/ne_impexp.h $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(TEST_OBJ): $(TEST_SRC) $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(LOADER_TEST_OBJ): $(LOADER_TEST_SRC) $(SRC_DIR)/ne_loader.h $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(RELOC_TEST_OBJ): $(RELOC_TEST_SRC) $(SRC_DIR)/ne_reloc.h $(SRC_DIR)/ne_loader.h $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(MODULE_TEST_OBJ): $(MODULE_TEST_SRC) $(SRC_DIR)/ne_module.h $(SRC_DIR)/ne_loader.h $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(IMPEXP_TEST_OBJ): $(IMPEXP_TEST_SRC) $(SRC_DIR)/ne_impexp.h $(SRC_DIR)/ne_parser.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(TEST_BIN): $(TEST_OBJ) $(PARSER_OBJ) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) name $@ file $(TEST_OBJ),$(PARSER_OBJ)

$(LOADER_TEST_BIN): $(LOADER_TEST_OBJ) $(PARSER_OBJ) $(LOADER_OBJ) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) name $@ file $(LOADER_TEST_OBJ),$(PARSER_OBJ),$(LOADER_OBJ)

$(RELOC_TEST_BIN): $(RELOC_TEST_OBJ) $(PARSER_OBJ) $(LOADER_OBJ) $(RELOC_OBJ) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) name $@ file $(RELOC_TEST_OBJ),$(PARSER_OBJ),$(LOADER_OBJ),$(RELOC_OBJ)

$(MODULE_TEST_BIN): $(MODULE_TEST_OBJ) $(PARSER_OBJ) $(LOADER_OBJ) $(MODULE_OBJ) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) name $@ file $(MODULE_TEST_OBJ),$(PARSER_OBJ),$(LOADER_OBJ),$(MODULE_OBJ)

$(IMPEXP_TEST_BIN): $(IMPEXP_TEST_OBJ) $(PARSER_OBJ) $(IMPEXP_OBJ) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) name $@ file $(IMPEXP_TEST_OBJ),$(PARSER_OBJ),$(IMPEXP_OBJ)

test: $(TEST_BIN) $(LOADER_TEST_BIN) $(RELOC_TEST_BIN) $(MODULE_TEST_BIN) $(IMPEXP_TEST_BIN)
	@echo "--- Running NE parser tests ---"
	$(TEST_BIN)
	@echo "--- Running NE loader tests ---"
	$(LOADER_TEST_BIN)
	@echo "--- Running NE relocation tests ---"
	$(RELOC_TEST_BIN)
	@echo "--- Running NE module table tests ---"
	$(MODULE_TEST_BIN)
	@echo "--- Running NE import/export resolution tests ---"
	$(IMPEXP_TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)
