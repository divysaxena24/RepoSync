# MiniDrop Makefile
# A Multithreaded Client-Server Directory Synchronization System in C

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wno-overlength-strings -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -I./common -I./client -I./server
LDFLAGS ?= -pthread -lcrypto

# Build directories
BUILD_DIR := build
BIN_DIR := bin

# Output binaries
MINIDROP_BIN := $(BIN_DIR)/minidrop
SERVER_BIN := $(BIN_DIR)/minidrop-server
CLIENT_BIN := $(BIN_DIR)/minidrop-client
TEST_PROTO_BIN := $(BIN_DIR)/test_protocol
TEST_CHECKSUM_BIN := $(BIN_DIR)/test_checksum
TEST_QUEUE_BIN := $(BIN_DIR)/test_queue

# Source files
COMMON_SRCS := common/protocol.c common/logger.c common/checksum.c common/metadata.c common/repo_engine.c
COMMON_OBJS := $(patsubst common/%.c, $(BUILD_DIR)/common/%.o, $(COMMON_SRCS))

SERVER_SRCS := server/main.c server/server.c server/connection.c server/storage.c server/receiver.c server/http_server.c
SERVER_OBJS := $(patsubst server/%.c, $(BUILD_DIR)/server/%.o, $(SERVER_SRCS))

CLIENT_SRCS := client/main.c client/client.c client/queue.c client/watcher.c client/transfer.c client/sync_engine.c
CLIENT_OBJS := $(patsubst client/%.c, $(BUILD_DIR)/client/%.o, $(CLIENT_SRCS))

APP_SRCS := app/main.c
APP_OBJS := $(BUILD_DIR)/app/main.o

# Default build target
all: $(MINIDROP_BIN) $(SERVER_BIN) $(CLIENT_BIN) test

# App target
minidrop: $(MINIDROP_BIN)

# Server target
server: $(SERVER_BIN)

# Client target
client: $(CLIENT_BIN)

# Debug build flags
debug: CFLAGS += -g -DDEBUG -O0
debug: all

# Release build flags
release: CFLAGS += -O2 -DNDEBUG
release: all

# Pattern rules for object files
$(BUILD_DIR)/common/%.o: common/%.c
	@mkdir -p $(BUILD_DIR)/common
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/server/%.o: server/%.c
	@mkdir -p $(BUILD_DIR)/server
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/client/%.o: client/%.c
	@mkdir -p $(BUILD_DIR)/client
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/app/%.o: app/%.c
	@mkdir -p $(BUILD_DIR)/app
	$(CC) $(CFLAGS) -c $< -o $@

# Main unified minidrop application
$(MINIDROP_BIN): $(APP_OBJS) $(BUILD_DIR)/server/http_server.o $(COMMON_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

# Server executable
$(SERVER_BIN): $(SERVER_OBJS) $(COMMON_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

# Client executable
$(CLIENT_BIN): $(CLIENT_OBJS) $(COMMON_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

# Unit tests
test: $(TEST_PROTO_BIN) $(TEST_CHECKSUM_BIN) $(TEST_QUEUE_BIN)
	@echo "Running protocol unit tests..."
	@./$(TEST_PROTO_BIN)
	@echo ""
	@echo "Running checksum unit tests..."
	@./$(TEST_CHECKSUM_BIN)
	@echo ""
	@echo "Running queue concurrency unit tests..."
	@./$(TEST_QUEUE_BIN)

$(TEST_PROTO_BIN): tests/test_protocol.c $(COMMON_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(COMMON_OBJS) $(LDFLAGS) -o $@

$(TEST_CHECKSUM_BIN): tests/test_checksum.c $(COMMON_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(COMMON_OBJS) $(LDFLAGS) -o $@

$(TEST_QUEUE_BIN): tests/test_queue.c client/queue.c $(COMMON_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -I./client $< client/queue.c $(COMMON_OBJS) $(LDFLAGS) -o $@

clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) $(BIN_DIR) logs/*.log

.PHONY: all debug release test clean

