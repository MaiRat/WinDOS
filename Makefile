# Makefile for WinDOS NE-file kernel replacement modules
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

TASK_SRC    := $(SRC_DIR)/ne_task.c
TASK_OBJ    := $(BUILD_DIR)/ne_task.obj

MEM_SRC     := $(SRC_DIR)/ne_mem.c
MEM_OBJ     := $(BUILD_DIR)/ne_mem.obj

TRAP_SRC    := $(SRC_DIR)/ne_trap.c
TRAP_OBJ    := $(BUILD_DIR)/ne_trap.obj

INTEGRATE_SRC  := $(SRC_DIR)/ne_integrate.c
INTEGRATE_OBJ  := $(BUILD_DIR)/ne_integrate.obj

FULLINTEG_SRC  := $(SRC_DIR)/ne_fullinteg.c
FULLINTEG_OBJ  := $(BUILD_DIR)/ne_fullinteg.obj

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

TASK_TEST_SRC    := $(TEST_DIR)/test_ne_task.c
TASK_TEST_OBJ    := $(BUILD_DIR)/test_ne_task.obj
TASK_TEST_BIN    := $(BUILD_DIR)/test_ne_task.exe

TRAP_TEST_SRC    := $(TEST_DIR)/test_ne_trap.c
TRAP_TEST_OBJ    := $(BUILD_DIR)/test_ne_trap.obj
TRAP_TEST_BIN    := $(BUILD_DIR)/test_ne_trap.exe

INTEGRATE_TEST_SRC  := $(TEST_DIR)/test_ne_integrate.c
INTEGRATE_TEST_OBJ  := $(BUILD_DIR)/test_ne_integrate.obj
INTEGRATE_TEST_BIN  := $(BUILD_DIR)/test_ne_integrate.exe

FULLINTEG_TEST_SRC  := $(TEST_DIR)/test_ne_fullinteg.c
FULLINTEG_TEST_OBJ  := $(BUILD_DIR)/test_ne_fullinteg.obj
FULLINTEG_TEST_BIN  := $(BUILD_DIR)/test_ne_fullinteg.exe

.PHONY: all test clean

all: $(TEST_BIN) $(LOADER_TEST_BIN) $(RELOC_TEST_BIN) $(MODULE_TEST_BIN) $(IMPEXP_TEST_BIN) $(TASK_TEST_BIN) $(TRAP_TEST_BIN) $(INTEGRATE_TEST_BIN) $(FULLINTEG_TEST_BIN)

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

$(TASK_OBJ): $(TASK_SRC) $(SRC_DIR)/ne_task.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(MEM_OBJ): $(MEM_SRC) $(SRC_DIR)/ne_mem.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(TRAP_OBJ): $(TRAP_SRC) $(SRC_DIR)/ne_trap.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(INTEGRATE_OBJ): $(INTEGRATE_SRC) $(SRC_DIR)/ne_integrate.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(FULLINTEG_OBJ): $(FULLINTEG_SRC) $(SRC_DIR)/ne_fullinteg.h | $(BUILD_DIR)
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

$(TASK_TEST_OBJ): $(TASK_TEST_SRC) $(SRC_DIR)/ne_task.h $(SRC_DIR)/ne_mem.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(TRAP_TEST_OBJ): $(TRAP_TEST_SRC) $(SRC_DIR)/ne_trap.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(INTEGRATE_TEST_OBJ): $(INTEGRATE_TEST_SRC) $(SRC_DIR)/ne_integrate.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(FULLINTEG_TEST_OBJ): $(FULLINTEG_TEST_SRC) $(SRC_DIR)/ne_fullinteg.h | $(BUILD_DIR)
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

$(TASK_TEST_BIN): $(TASK_TEST_OBJ) $(TASK_OBJ) $(MEM_OBJ) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) name $@ file $(TASK_TEST_OBJ),$(TASK_OBJ),$(MEM_OBJ)

$(TRAP_TEST_BIN): $(TRAP_TEST_OBJ) $(TRAP_OBJ) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) name $@ file $(TRAP_TEST_OBJ),$(TRAP_OBJ)

$(INTEGRATE_TEST_BIN): $(INTEGRATE_TEST_OBJ) $(INTEGRATE_OBJ) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) name $@ file $(INTEGRATE_TEST_OBJ),$(INTEGRATE_OBJ)

$(FULLINTEG_TEST_BIN): $(FULLINTEG_TEST_OBJ) $(FULLINTEG_OBJ) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) name $@ file $(FULLINTEG_TEST_OBJ),$(FULLINTEG_OBJ)

test: $(TEST_BIN) $(LOADER_TEST_BIN) $(RELOC_TEST_BIN) $(MODULE_TEST_BIN) $(IMPEXP_TEST_BIN) $(TASK_TEST_BIN) $(TRAP_TEST_BIN) $(INTEGRATE_TEST_BIN) $(FULLINTEG_TEST_BIN)
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
	@echo "--- Running NE task and memory management tests ---"
	$(TASK_TEST_BIN)
	@echo "--- Running NE exception and trap handling tests ---"
	$(TRAP_TEST_BIN)
	@echo "--- Running NE integration management tests ---"
	$(INTEGRATE_TEST_BIN)
	@echo "--- Running NE full integration tests ---"
	$(FULLINTEG_TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)

# =========================================================================
# Host (POSIX) build targets for CI validation
#
# These targets build and test using the host C compiler (cc/gcc) so that
# correctness can be verified in CI environments where Open Watcom is not
# available.  The __WATCOMC__ paths are not compiled; only the POSIX paths
# are exercised.
# =========================================================================

HOST_CC     := cc
HOST_CFLAGS := -std=c99 -Wall -Wextra -I$(CURDIR)/src

.PHONY: host-test host-clean

host-test: | $(BUILD_DIR)
	@echo "=== Building and running all tests with host compiler ==="
	@echo "--- NE parser ---"
	$(HOST_CC) $(HOST_CFLAGS) $(SRC_DIR)/ne_parser.c $(TEST_DIR)/test_ne_parser.c -o $(BUILD_DIR)/host_test_parser
	$(BUILD_DIR)/host_test_parser
	@echo "--- NE loader ---"
	$(HOST_CC) $(HOST_CFLAGS) $(SRC_DIR)/ne_parser.c $(SRC_DIR)/ne_loader.c $(TEST_DIR)/test_ne_loader.c -o $(BUILD_DIR)/host_test_loader
	$(BUILD_DIR)/host_test_loader
	@echo "--- NE relocation ---"
	$(HOST_CC) $(HOST_CFLAGS) $(SRC_DIR)/ne_parser.c $(SRC_DIR)/ne_loader.c $(SRC_DIR)/ne_reloc.c $(TEST_DIR)/test_ne_reloc.c -o $(BUILD_DIR)/host_test_reloc
	$(BUILD_DIR)/host_test_reloc
	@echo "--- NE module table ---"
	$(HOST_CC) $(HOST_CFLAGS) $(SRC_DIR)/ne_parser.c $(SRC_DIR)/ne_loader.c $(SRC_DIR)/ne_module.c $(TEST_DIR)/test_ne_module.c -o $(BUILD_DIR)/host_test_module
	$(BUILD_DIR)/host_test_module
	@echo "--- NE import/export ---"
	$(HOST_CC) $(HOST_CFLAGS) $(SRC_DIR)/ne_parser.c $(SRC_DIR)/ne_impexp.c $(TEST_DIR)/test_ne_impexp.c -o $(BUILD_DIR)/host_test_impexp
	$(BUILD_DIR)/host_test_impexp
	@echo "--- NE task/memory ---"
	$(HOST_CC) $(HOST_CFLAGS) $(SRC_DIR)/ne_task.c $(SRC_DIR)/ne_mem.c $(TEST_DIR)/test_ne_task.c -o $(BUILD_DIR)/host_test_task
	$(BUILD_DIR)/host_test_task
	@echo "--- NE trap ---"
	$(HOST_CC) $(HOST_CFLAGS) $(SRC_DIR)/ne_trap.c $(TEST_DIR)/test_ne_trap.c -o $(BUILD_DIR)/host_test_trap
	$(BUILD_DIR)/host_test_trap
	@echo "--- NE integration ---"
	$(HOST_CC) $(HOST_CFLAGS) $(SRC_DIR)/ne_integrate.c $(TEST_DIR)/test_ne_integrate.c -o $(BUILD_DIR)/host_test_integrate
	$(BUILD_DIR)/host_test_integrate
	@echo "--- NE full integration ---"
	$(HOST_CC) $(HOST_CFLAGS) $(SRC_DIR)/ne_fullinteg.c $(TEST_DIR)/test_ne_fullinteg.c -o $(BUILD_DIR)/host_test_fullinteg
	$(BUILD_DIR)/host_test_fullinteg
	@echo "=== All host tests passed ==="

host-clean:
	rm -f $(BUILD_DIR)/host_test_*
