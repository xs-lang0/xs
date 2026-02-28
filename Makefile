CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wno-unused-parameter -std=c11 -Isrc -Isrc/tls/bearssl
LDFLAGS = -lm -lpthread

# Feature flags (all enabled by default)
XSC_ENABLE_VM      ?= 1
XSC_ENABLE_JIT     ?= 1
XSC_ENABLE_PLUGINS ?= 1
XSC_ENABLE_SANDBOX ?= 1
XSC_ENABLE_TRACER  ?= 1
XSC_ENABLE_LSP     ?= 1
XSC_ENABLE_DAP     ?= 1

# Platform detection
ifeq ($(OS),Windows_NT)
  CFLAGS  += -D__USE_MINGW_ANSI_STDIO=1
  TARGET  = xs.exe
else
  UNAME := $(shell uname -s)
  ifeq ($(UNAME),Linux)
    ifneq ($(wildcard /usr/include/x86_64-linux-gnu),)
      CFLAGS += -isystem /usr/include/x86_64-linux-gnu
    endif
    ifneq ($(wildcard /usr/include),)
      CFLAGS += -isystem /usr/include
    endif
  endif
  TARGET = xs
endif

# Core sources (always built)
CORE_SRCS = src/core/value.c \
            src/core/ast.c \
            src/core/lexer.c \
            src/core/env.c \
            src/core/parser.c \
            src/core/xs_bigint.c

RUNTIME_SRCS = src/runtime/interp.c \
               src/runtime/builtins.c \
               src/runtime/error.c \
               src/runtime/stdlib.c

REPL_SRCS = src/repl/repl.c

LINT_SRCS = src/lint/lint.c

TYPES_EXTRA_SRCS = src/types/inference.c

EMBED_SRCS = src/xs_embed.c

DIAG_SRCS = src/diagnostic/diagnostic.c \
            src/diagnostic/render.c \
            src/diagnostic/colorize.c \
            src/diagnostic/explain.c

# Always-compiled tool sources (main.c references these unconditionally)
TOOL_SRCS = src/fmt/fmt.c \
            src/doc/docgen.c \
            src/pkg/pkg.c \
            src/profiler/profiler.c \
            src/coverage/coverage.c \
            src/optimizer/optimizer.c \
            src/ir/ir.c

MAIN_SRCS = src/main.c

SEMA_SRCS = src/types/types.c \
            src/semantic/exhaust.c \
            src/semantic/symtable.c \
            src/semantic/resolve.c \
            src/semantic/typecheck.c \
            src/semantic/cache.c \
            src/semantic/sema.c

TLS_SRCS = $(wildcard src/tls/*.c) $(wildcard src/tls/bearssl/**/*.c) $(wildcard src/tls/bearssl/*.c)

SRCS = $(CORE_SRCS) $(RUNTIME_SRCS) $(REPL_SRCS) $(LINT_SRCS) $(TYPES_EXTRA_SRCS) $(EMBED_SRCS) $(DIAG_SRCS) $(TOOL_SRCS) $(MAIN_SRCS) $(SEMA_SRCS) $(TLS_SRCS)

# Conditional sources
ifeq ($(XSC_ENABLE_VM),1)
  CFLAGS += -DXSC_ENABLE_VM
  SRCS += $(wildcard src/vm/*.c)
endif

ifeq ($(XSC_ENABLE_JIT),1)
  CFLAGS += -DXSC_ENABLE_JIT
  SRCS += $(wildcard src/jit/*.c)
endif

ifeq ($(XSC_ENABLE_PLUGINS),1)
  CFLAGS += -DXSC_ENABLE_PLUGINS
  LDFLAGS += -ldl
  SRCS += $(wildcard src/plugins/*.c)
endif

ifeq ($(XSC_ENABLE_SANDBOX),1)
  CFLAGS += -DXSC_ENABLE_SANDBOX
endif

ifeq ($(XSC_ENABLE_TRACER),1)
  CFLAGS += -DXSC_ENABLE_TRACER
  SRCS += $(wildcard src/tracer/*.c)
endif

ifeq ($(XSC_ENABLE_LSP),1)
  CFLAGS += -DXSC_ENABLE_LSP
  SRCS += $(wildcard src/lsp/*.c)
endif

ifeq ($(XSC_ENABLE_DAP),1)
  CFLAGS += -DXSC_ENABLE_DAP
  SRCS += $(wildcard src/dap/*.c)
endif

XSC_ENABLE_EFFECTS  ?= 1
XSC_ENABLE_TRANSPILER ?= 1
XSC_ENABLE_FMT      ?= 1
XSC_ENABLE_PKG      ?= 1
XSC_ENABLE_PROFILER ?= 1
XSC_ENABLE_COVERAGE ?= 1
XSC_ENABLE_DOC      ?= 1

ifeq ($(XSC_ENABLE_EFFECTS),1)
  CFLAGS += -DXSC_ENABLE_EFFECTS
  SRCS += $(wildcard src/effects/*.c)
endif

ifeq ($(XSC_ENABLE_TRANSPILER),1)
  CFLAGS += -DXSC_ENABLE_TRANSPILER
  SRCS += $(wildcard src/transpiler/*.c)
endif

ifeq ($(XSC_ENABLE_FMT),1)
  CFLAGS += -DXSC_ENABLE_FMT
endif

ifeq ($(XSC_ENABLE_PKG),1)
  CFLAGS += -DXSC_ENABLE_PKG
endif

ifeq ($(XSC_ENABLE_PROFILER),1)
  CFLAGS += -DXSC_ENABLE_PROFILER
endif

ifeq ($(XSC_ENABLE_COVERAGE),1)
  CFLAGS += -DXSC_ENABLE_COVERAGE
endif

ifeq ($(XSC_ENABLE_DOC),1)
  CFLAGS += -DXSC_ENABLE_DOC
endif

OBJS = $(SRCS:.c=.o)

# Targets
.PHONY: all clean debug release test install wasm wasi

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

debug: CFLAGS = -g -O0 -Wall -Wextra -Wno-unused-parameter -std=c11 -Isrc -Isrc/tls/bearssl \
                -fsanitize=address -fsanitize=undefined -DDEBUG \
                $(foreach f,VM JIT PLUGINS SANDBOX TRACER LSP DAP EFFECTS TRANSPILER FMT PKG PROFILER COVERAGE DOC,-DXSC_ENABLE_$(f))
debug: LDFLAGS += -fsanitize=address -fsanitize=undefined
debug: clean $(TARGET)

release: CFLAGS = -O3 -Wall -Wextra -Wno-unused-parameter -std=c11 -Isrc -Isrc/tls/bearssl \
                  -DNDEBUG -flto \
                  $(foreach f,VM JIT PLUGINS SANDBOX TRACER LSP DAP EFFECTS TRANSPILER FMT PKG PROFILER COVERAGE DOC,-DXSC_ENABLE_$(f))
release: LDFLAGS += -flto -s
release: clean $(TARGET)

test: $(TARGET)
	@bash tests/run.sh

install: release
	install -m 755 $(TARGET) /usr/local/bin/xs

wasm:
	@echo "WASM target not yet implemented (see Phase N)"

wasi:
	@echo "WASI target not yet implemented (see Phase N)"

clean:
	rm -f $(OBJS) $(TARGET)
	find src -name '*.o' -delete
