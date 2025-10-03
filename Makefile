# Makefile for LocalKnowledgeBase

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
CFLAGS_DEBUG = -Wall -Wextra -g -DDEBUG -O0

TARGET = LocalKnowledgeBase
SRC = LocalKnowledgeBase.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

debug: $(SRC)
	$(CC) $(CFLAGS_DEBUG) -o $(TARGET) $(SRC)
	@echo "Built with DEBUG flags enabled"

clean:
	rm -f $(TARGET)

install-deps:
	@echo "Installing dependencies..."
	@echo "Ubuntu/Debian:"
	@echo "  sudo apt-get install libmicrohttpd-dev libcjson-dev"
	@echo "Fedora/RHEL:"
	@echo "  sudo dnf install libmicrohttpd-devel cjson-devel"
	@echo "Arch Linux:"
	@echo "  sudo pacman -S libmicrohttpd cjson"

test: $(TARGET)
	@echo "Starting C server for testing..."
	@./$(TARGET) &
	@sleep 1
	@python3 test_c_server.py || true
	@pkill -f "./$(TARGET)" || true

run: $(TARGET)
	./$(TARGET)

.PHONY: all debug clean install-deps test run
