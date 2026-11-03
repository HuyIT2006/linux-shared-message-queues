# ==============================================================================
# Makefile - Shared Message Queues trong Linux IPC
# Tac gia: VanHuy (Thanh vien 1 - IPC Core Engine)
# Bo sung: Thanh vien 2 (Server Architecture, Worker Pool & System Services)
# ==============================================================================

CC := gcc
CFLAGS := -Wall -Wextra -Werror -pedantic -std=gnu11 -Iinclude
LDLIBS := -lrt -pthread

BIN_DIR := bin
OBJ_DIR := obj
SRC_DIR := src
TESTS_DIR := tests
INCLUDE_DIR := include

# Targets (TV1 & TV2)
TARGET_TEST := $(BIN_DIR)/test_ipc_queue
TARGET_CLEANUP := $(BIN_DIR)/ipc_cleanup
TARGET_SERVER := $(BIN_DIR)/server
TARGET_TEST_PROTO := $(BIN_DIR)/test_protocol

OBJS_COMMON := $(OBJ_DIR)/ipc_queue.o
OBJS_SERVER := $(OBJ_DIR)/ipc_queue.o $(OBJ_DIR)/protocol.o $(OBJ_DIR)/worker_pool.o

.PHONY: all server cleanup clean test test-ipc test-proto valgrind sanitize dirs

all: dirs $(TARGET_TEST) $(TARGET_CLEANUP) $(TARGET_SERVER) $(TARGET_TEST_PROTO)

server: dirs $(TARGET_SERVER)

cleanup: dirs $(TARGET_CLEANUP)

dirs:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR)

# Bien dich cac file object tu src/ (TV1 & TV2)
$(OBJ_DIR)/ipc_queue.o: $(SRC_DIR)/ipc_queue.c $(INCLUDE_DIR)/ipc_queue.h | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/ipc_cleanup.o: $(SRC_DIR)/ipc_cleanup.c $(INCLUDE_DIR)/ipc_queue.h | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/protocol.o: $(SRC_DIR)/protocol.c $(INCLUDE_DIR)/protocol.h $(INCLUDE_DIR)/ipc_queue.h | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/worker_pool.o: $(SRC_DIR)/worker_pool.c $(INCLUDE_DIR)/worker_pool.h $(INCLUDE_DIR)/protocol.h $(INCLUDE_DIR)/ipc_queue.h | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/server.o: $(SRC_DIR)/server.c $(INCLUDE_DIR)/worker_pool.h $(INCLUDE_DIR)/protocol.h $(INCLUDE_DIR)/ipc_queue.h | dirs
	$(CC) $(CFLAGS) -c $< -o $@

# Bien dich cac file object tu tests/ (TV1 & TV2)
$(OBJ_DIR)/test_ipc_queue.o: $(TESTS_DIR)/test_ipc_queue.c $(INCLUDE_DIR)/ipc_queue.h | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_protocol.o: $(TESTS_DIR)/test_protocol.c $(INCLUDE_DIR)/protocol.h $(INCLUDE_DIR)/worker_pool.h $(INCLUDE_DIR)/ipc_queue.h | dirs
	$(CC) $(CFLAGS) -c $< -o $@

# Lien ket cac chuong trinh thuc thi
$(TARGET_TEST): $(OBJS_COMMON) $(OBJ_DIR)/test_ipc_queue.o | dirs
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(TARGET_CLEANUP): $(OBJS_COMMON) $(OBJ_DIR)/ipc_cleanup.o | dirs
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(TARGET_SERVER): $(OBJS_SERVER) $(OBJ_DIR)/server.o | dirs
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(TARGET_TEST_PROTO): $(OBJS_SERVER) $(OBJ_DIR)/test_protocol.o | dirs
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

# Chay kiem thu Protocol & Worker Pool (TV2)
test-proto: $(TARGET_TEST_PROTO)
	@echo "=== DANG CHAY TEST SUITE PROTOCOL & WORKER POOL (TV2) ==="
	@./$(TARGET_TEST_PROTO)

# Chay kiem thu IPC Core (TV1)
test-ipc: $(TARGET_TEST)
	@echo "=== DANG CHAY TEST SUITE IPC CORE (TV1) ==="
	@./$(TARGET_TEST)

# Chay toan bo kiem thu (TV1 + TV2)
test: $(TARGET_TEST) $(TARGET_TEST_PROTO)
	@echo "=== DANG CHAY TEST SUITE IPC CORE (TV1) ==="
	@./$(TARGET_TEST)
	@echo "=== DANG CHAY TEST SUITE PROTOCOL & WORKER POOL (TV2) ==="
	@./$(TARGET_TEST_PROTO)

# Chay kiem tra ro ri bo nho voi Valgrind
valgrind: $(TARGET_TEST) $(TARGET_TEST_PROTO)
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET_TEST)
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET_TEST_PROTO)

# Chay kiem tra bo nho voi AddressSanitizer & UndefinedBehaviorSanitizer
sanitize: dirs
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(SRC_DIR)/ipc_queue.c $(TESTS_DIR)/test_ipc_queue.c -o $(BIN_DIR)/test_asan $(LDLIBS)
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(SRC_DIR)/ipc_queue.c $(SRC_DIR)/protocol.c $(SRC_DIR)/worker_pool.c $(TESTS_DIR)/test_protocol.c -o $(BIN_DIR)/test_proto_asan $(LDLIBS)
	@echo "=== DANG CHAY TEST IPC CORE VOI ADDRESS SANITIZER ==="
	@./$(BIN_DIR)/test_asan
	@echo "=== DANG CHAY TEST PROTOCOL & WORKER POOL VOI ADDRESS SANITIZER ==="
	@./$(BIN_DIR)/test_proto_asan

# Don dep cac file build
clean:
	rm -rf $(OBJ_DIR)/*.o $(TARGET_TEST) $(TARGET_CLEANUP) $(TARGET_SERVER) $(TARGET_TEST_PROTO) $(BIN_DIR)/test_asan $(BIN_DIR)/test_proto_asan