# WisprFlow daemon - links against whisper.cpp shared libs in ../build/bin
WHISPER_DIR ?= $(CURDIR)/../whisper.cpp
BUILD_DIR  ?= $(CURDIR)/../build
LIB_DIR    ?= $(BUILD_DIR)/bin
BIN_DIR    ?= $(CURDIR)/../bin

CC      ?= gcc
CFLAGS   = -O2 -g -Wall -Wextra -std=gnu11 \
           -I$(WHISPER_DIR)/include -I$(WHISPER_DIR)/ggml/include
# rpath: build tree first, then the installed lib location next to the binary
LDFLAGS  = -L$(LIB_DIR) -Wl,-rpath,$(LIB_DIR) -Wl,-rpath,'$$ORIGIN/../lib/wispr-flow'
LDLIBS   = -lwhisper -lggml -lpulse-simple -lpulse -lpthread -lm

# CUDA backend, if the build tree contains it
ifneq ($(wildcard $(LIB_DIR)/libggml-cuda.so),)
LDLIBS   += -lggml-cuda -lcublas -lcublasLt -lcudart
LDFLAGS  += -L/opt/cuda/lib64 -Wl,-rpath,/opt/cuda/lib64
endif

SRCS = main.c config.c hotkey.c audio.c transcribe.c insert.c wf_proc.c uinput_keys.c wf_socket.c
OBJS = $(SRCS:.c=.o)

all: $(BIN_DIR)/wispr-flow $(BIN_DIR)/wispr-inject

$(BIN_DIR)/wispr-flow: $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

$(BIN_DIR)/wispr-inject: inject_main.o uinput_keys.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ inject_main.o uinput_keys.o

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) inject_main.o $(BIN_DIR)/wispr-flow $(BIN_DIR)/wispr-inject

.PHONY: all clean
