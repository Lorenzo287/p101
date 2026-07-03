CC ?= gcc
CFLAGS ?= -std=gnu11 -Wall -Wextra -pedantic -O3
LDFLAGS ?= -lm

ifeq ($(OS),Windows_NT)
EXE := .exe
else
EXE :=
endif

TARGET := p101$(EXE)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): p101.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	$(RM) $(TARGET)

test: $(TARGET)
	powershell -NoProfile -ExecutionPolicy Bypass -File smoke.ps1
