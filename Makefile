CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS  ?= -pthread
ifeq ($(OS),Windows_NT)
LDFLAGS  += -static -lws2_32 -lshell32 -lole32 -luuid -ladvapi32
BIN      := build/nova.exe
endif
ifeq ($(STATIC),1)
LDFLAGS  += -static-libstdc++ -static-libgcc
endif
PREFIX   ?= /usr/local
BIN      ?= build/nova
SRC      := src/main.cpp
HDR      := $(wildcard src/*.hpp)

all: $(BIN)

$(BIN): $(SRC) $(HDR)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)
	@echo "built $@"

install: $(BIN)
	install -m 755 $(BIN) $(PREFIX)/bin/nova

uninstall:
	rm -f $(PREFIX)/bin/nova

clean:
	rm -rf build dist

.PHONY: all install uninstall clean
