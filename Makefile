CC_WIN ?= x86_64-w64-mingw32-gcc
CC     ?= gcc

all: swtd_patcher.exe

# Windows build (cross-compile with MinGW-w64, static, no runtime dependencies)
swtd_patcher.exe: src/swtd_patcher.c
	$(CC_WIN) -O2 -Wall -municode -static -s -o $@ $<

# Native build for testing the patch logic on Linux/macOS
swtd_test: src/swtd_patcher.c
	$(CC) -O2 -Wall -D_FILE_OFFSET_BITS=64 -o $@ $<

clean:
	rm -f swtd_patcher.exe swtd_test

.PHONY: all clean
