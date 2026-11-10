# ==============================================================================
# Makefile - Shared Message Queues trong Linux IPC
# TV1: ipc_queue / ipc_cleanup | TV2: server / protocol / worker_pool
# TV3: client / benchmark      | TV4: build system, sanitizer, valgrind, test
# ==============================================================================

CC      := gcc
STD     := -std=gnu11
WARN    := -Wall -Wextra -Werror -pedantic
CFLAGS  := $(STD) $(WARN) -Iinclude
LDLIBS  := -lrt -pthread

BIN_DIR   := bin
OBJ_DIR   := obj
SRC_DIR   := src
TESTS_DIR := tests
INC_DIR   := include

# ---- Che do debug: make debug (ASan + UBSan) ---------------------------------
ifeq ($(DEBUG),1)
CFLAGS  += -g -O0 -fno-omit-frame-pointer -fsanitize=address,undefined
LDFLAGS += -fsanitize=address,undefined
endif

# ---- Target thuc thi ---------------------------------------------------------
SERVER    := $(BIN_DIR)/server
CLIENT    := $(BIN_DIR)/client
BENCH     := $(BIN_DIR)/benchmark
CLEANUP   := $(BIN_DIR)/ipc_cleanup
T_IPC     := $(BIN_DIR)/test_ipc_queue
T_PROTO   := $(BIN_DIR)/test_protocol
RAW_SEND  := $(BIN_DIR)/raw_send
ALL_BINS  := $(SERVER) $(CLIENT) $(BENCH) $(CLEANUP) $(T_IPC) $(T_PROTO) $(RAW_SEND)

# ---- Nhom object -------------------------------------------------------------
O_IPC    := $(OBJ_DIR)/ipc_queue.o
O_PROTO  := $(OBJ_DIR)/protocol.o
O_POOL   := $(OBJ_DIR)/worker_pool.o

.PHONY: all server client benchmark cleanup test test-unit test-ipc test-proto \
        test-integration test-chaos debug valgrind sanitize clean distclean help

all: $(ALL_BINS)

server:    $(SERVER)
client:    $(CLIENT)
benchmark: $(BENCH)
cleanup:   $(CLEANUP)

# ---- Quy tac bien dich chung (tu dong phu thuoc header qua -MMD) -------------
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/%.o: $(TESTS_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR) $(BIN_DIR):
	@mkdir -p $@

# ---- Lien ket ----------------------------------------------------------------
$(SERVER): $(O_IPC) $(O_PROTO) $(O_POOL) $(OBJ_DIR)/server.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(CLIENT): $(O_IPC) $(O_PROTO) $(OBJ_DIR)/client.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BENCH): $(O_IPC) $(O_PROTO) $(OBJ_DIR)/benchmark.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(CLEANUP): $(O_IPC) $(OBJ_DIR)/ipc_cleanup.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(T_IPC): $(O_IPC) $(OBJ_DIR)/test_ipc_queue.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(T_PROTO): $(O_IPC) $(O_PROTO) $(O_POOL) $(OBJ_DIR)/test_protocol.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# Cong cu gui message tho dung cho chaos test (TV4)
$(RAW_SEND): $(TESTS_DIR)/raw_send.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ $(LDLIBS)

# ---- Kiem thu ----------------------------------------------------------------
test-ipc: $(T_IPC)
	@echo "=== UNIT TEST: IPC CORE (TV1) ===" && ./$(T_IPC)

test-proto: $(T_PROTO)
	@echo "=== UNIT TEST: PROTOCOL & WORKER POOL (TV2) ===" && ./$(T_PROTO)

test-unit: test-ipc test-proto

test-integration: all
	@bash $(TESTS_DIR)/test_integration.sh

test-chaos: all
	@bash $(TESTS_DIR)/test_chaos.sh

# make test = unit + integration + chaos
test: test-unit test-integration test-chaos

# ---- Debug / Sanitizer / Valgrind -------------------------------------------
# make debug: build lai toan bo voi ASan + UBSan vao thu muc rieng
debug:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory all DEBUG=1
	@echo "Da build ban debug (ASan+UBSan). Chay: make test"

# make sanitize: build debug roi chay toan bo test (unit + integration + chaos)
sanitize: debug
	@$(MAKE) --no-print-directory test DEBUG=1

# make valgrind: build thuong (-g) roi chay Memcheck cho unit test + server/client
valgrind: CFLAGS += -g -O0
valgrind: clean all
	@bash $(TESTS_DIR)/run_valgrind.sh

# ---- Don dep -----------------------------------------------------------------
clean:
	rm -f $(OBJ_DIR)/*.o $(OBJ_DIR)/*.d $(ALL_BINS)

distclean: clean
	rm -rf logs

help:
	@echo "all | server | client | benchmark | cleanup"
	@echo "test | test-unit | test-integration | test-chaos"
	@echo "debug | sanitize | valgrind | clean | distclean"

-include $(wildcard $(OBJ_DIR)/*.d)
