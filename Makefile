# ==============================================================================
# Makefile - Shared Message Queues trong Linux IPC
# Tac gia: VanHuy (Thanh vien 1 - IPC Core Engine)
# ==============================================================================

CC := gcc
CFLAGS := -Wall -Wextra -Werror -pedantic -std=gnu11 -Iinclude
LDLIBS := -lrt -pthread

BIN_DIR := bin
OBJ_DIR := obj
SRC_DIR := src
TESTS_DIR := tests
INCLUDE_DIR := include

# Targets
TARGET_TEST := $(BIN_DIR)/test_ipc_queue
TARGET_CLEANUP := $(BIN_DIR)/ipc_cleanup

OBJS_COMMON := $(OBJ_DIR)/ipc_queue.o

.PHONY: all clean test valgrind dirs

all: dirs $(TARGET_TEST) $(TARGET_CLEANUP)

dirs:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR)

# Bien dich cac file object tu src/
$(OBJ_DIR)/ipc_queue.o: $(SRC_DIR)/ipc_queue.c $(INCLUDE_DIR)/ipc_queue.h | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/ipc_cleanup.o: $(SRC_DIR)/ipc_cleanup.c $(INCLUDE_DIR)/ipc_queue.h | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_ipc_queue.o: $(TESTS_DIR)/test_ipc_queue.c $(INCLUDE_DIR)/ipc_queue.h | dirs
	$(CC) $(CFLAGS) -c $< -o $@

# Lien ket cac chuong trinh thuc thi
$(TARGET_TEST): $(OBJS_COMMON) $(OBJ_DIR)/test_ipc_queue.o | dirs
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(TARGET_CLEANUP): $(OBJS_COMMON) $(OBJ_DIR)/ipc_cleanup.o | dirs
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

# Chay kiem thu
test: $(TARGET_TEST)
	@echo "=== DANG CHAY TEST SUITE IPC CORE ==="
	@./$(TARGET_TEST)

# Chay kiem tra ro ri bo nho voi Valgrind
valgrind: $(TARGET_TEST)
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET_TEST)

# Chay kiem tra bo nho voi AddressSanitizer & UndefinedBehaviorSanitizer
sanitize: dirs
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(SRC_DIR)/ipc_queue.c $(TESTS_DIR)/test_ipc_queue.c -o $(BIN_DIR)/test_asan $(LDLIBS)
	@echo "=== DANG CHAY TEST VOI ADDRESS SANITIZER ==="
	@./$(BIN_DIR)/test_asan

# Don dep cac file build
clean:
	rm -rf $(OBJ_DIR)/*.o $(TARGET_TEST) $(TARGET_CLEANUP) $(BIN_DIR)/test_asan
