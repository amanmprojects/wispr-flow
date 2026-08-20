# WisprFlow daemon - links against whisper.cpp shared libs in ../build/bin
WHISPER_DIR ?= $(CURDIR)/../whisper.cpp
BUILD_DIR  ?= $(CURDIR)/../build
LIB_DIR    ?= $(BUILD_DIR)/bin
BIN_DIR    ?= $(CURDIR)/../bin

# helper for comma-separated lists
comma := ,

# PREFIX/DESTDIR for `make install` (staged installs, packaging)
PREFIX   ?= /usr/local
DESTDIR  ?=
BINDIR   ?= $(PREFIX)/bin
LIBDIR   ?= $(PREFIX)/lib/wispr-flow

CC      ?= gcc
CFLAGS   = -O2 -g -Wall -Wextra -std=gnu11 \
           -I$(WHISPER_DIR)/include -I$(WHISPER_DIR)/ggml/include
# rpath: build tree first, then the installed lib location relative to binary
LDFLAGS  = -L$(LIB_DIR) -Wl,-rpath,$(LIB_DIR) -Wl,-rpath,'$$ORIGIN/../lib/wispr-flow'
LDLIBS   = -lwhisper -lggml -lpulse-simple -lpulse -lpthread -lm

# CUDA backend, if the build tree contains it
ifneq ($(wildcard $(LIB_DIR)/libggml-cuda.so),)
LDLIBS   += -lggml-cuda -lcublas -lcublasLt -lcudart
# Probe CUDA lib dirs in order; use first that exists. Also try pkg-config.
CUDA_LIBDIRS := /opt/cuda/lib64 /usr/local/cuda/lib64 /usr/lib/cuda/lib64 /usr/lib/x86_64-linux-gnu
CUDA_LDFLAGS :=
# pkg-config fallback for cuda libs (if available)
ifneq ($(shell pkg-config --exists cudart 2>/dev/null && echo yes),)
CUDA_LDFLAGS += $(shell pkg-config --libs-only-L cudart 2>/dev/null)
endif
# add -L and rpath for each existing CUDA libdir
CUDA_LDFLAGS += $(foreach d,$(CUDA_LIBDIRS),$(if $(wildcard $(d)/libcudart.so*),-L$(d) -Wl$(comma)-rpath$(comma)$(d),))
# fallback: if none matched but /opt/cuda/lib64 exists as dir, use it
ifeq ($(strip $(CUDA_LDFLAGS)),)
ifneq ($(wildcard /opt/cuda/lib64),)
CUDA_LDFLAGS += -L/opt/cuda/lib64 -Wl,-rpath,/opt/cuda/lib64
endif
endif
LDFLAGS  += $(CUDA_LDFLAGS)
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

install: all
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR)
	install -m755 $(BIN_DIR)/wispr-flow $(DESTDIR)$(BINDIR)/wispr-flow
	install -m755 $(BIN_DIR)/wispr-inject $(DESTDIR)$(BINDIR)/wispr-inject
	@if ls $(LIB_DIR)/libwhisper.so* >/dev/null 2>&1; then \
		install -m755 $(LIB_DIR)/libwhisper.so* $(DESTDIR)$(LIBDIR)/ 2>/dev/null || true; \
	fi
	@if ls $(LIB_DIR)/libggml.so* >/dev/null 2>&1; then \
		install -m755 $(LIB_DIR)/libggml.so* $(DESTDIR)$(LIBDIR)/ 2>/dev/null || true; \
	fi
	@if ls $(LIB_DIR)/libggml-*.so* >/dev/null 2>&1; then \
		install -m755 $(LIB_DIR)/libggml-*.so* $(DESTDIR)$(LIBDIR)/ 2>/dev/null || true; \
	fi

clean:
	rm -f $(OBJS) inject_main.o $(BIN_DIR)/wispr-flow $(BIN_DIR)/wispr-inject

.PHONY: all clean install
