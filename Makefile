CC ?= cc
SRCDIR = src
WEBDIR = web
EMCC ?= emcc
PKG_CONFIG ?= pkg-config
WASM_PKG_CONFIG_PATH ?=
WASM_CHECK_LOG_LINES ?= 20

# Detect OS and set opus include/lib paths for macOS (Homebrew) or Linux
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	# macOS (try both Homebrew locations)
	OPUS_INC = -I/opt/homebrew/include -I/usr/local/include
	OPUS_LIB = -L/opt/homebrew/lib -L/usr/local/lib
else
	# Linux (default locations)
	OPUS_INC =
	OPUS_LIB =
endif

HOST_OPUS_CFLAGS_RAW ?= $(shell $(PKG_CONFIG) --cflags opus 2>/dev/null)
HOST_OPUS_LIBS ?= $(shell $(PKG_CONFIG) --libs opus 2>/dev/null)
WASM_OPUS_CFLAGS_RAW ?= $(shell PKG_CONFIG_PATH="$(WASM_PKG_CONFIG_PATH)" $(PKG_CONFIG) --cflags opus 2>/dev/null)
WASM_OPUS_LIBS ?= $(shell PKG_CONFIG_PATH="$(WASM_PKG_CONFIG_PATH)" $(PKG_CONFIG) --libs opus 2>/dev/null)
# Some opus.pc files expose -I.../include/opus even though this project includes
# headers as <opus/opus.h>, so also add the parent include directory when needed.
HOST_OPUS_PARENT_INC = $(patsubst -I%/opus,-I%,$(filter -I%/opus,$(HOST_OPUS_CFLAGS_RAW)))
WASM_OPUS_PARENT_INC = $(patsubst -I%/opus,-I%,$(filter -I%/opus,$(WASM_OPUS_CFLAGS_RAW)))
HOST_OPUS_CFLAGS = $(HOST_OPUS_CFLAGS_RAW) $(HOST_OPUS_PARENT_INC)
WASM_OPUS_CFLAGS = $(WASM_OPUS_CFLAGS_RAW) $(WASM_OPUS_PARENT_INC)

CFLAGS = -O3 -I$(SRCDIR) $(OPUS_INC) $(HOST_OPUS_CFLAGS)
LDFLAGS = $(OPUS_LIB) $(HOST_OPUS_LIBS) -lm
WASM_CFLAGS = -O3 -I$(SRCDIR) $(WASM_OPUS_CFLAGS)

SRC_NOPUS = $(SRCDIR)/main.c $(SRCDIR)/common.c $(SRCDIR)/files.c $(SRCDIR)/list.c
OBJ_NOPUS = $(SRC_NOPUS:.c=.o)
TARGET_NOPUS = nopus

SRC_CAPCOM = $(SRCDIR)/create_capcom_opus.c $(SRCDIR)/common.c $(SRCDIR)/files.c $(SRCDIR)/list.c
OBJ_CAPCOM = $(SRC_CAPCOM:.c=.o)
TARGET_CAPCOM = create_capcom_opus

SRC_WASM = $(SRCDIR)/nopus_wasm.c $(SRCDIR)/common.c $(SRCDIR)/files.c $(SRCDIR)/list.c
TARGET_WASM = $(WEBDIR)/nopus-web.js
WASM_LDFLAGS = $(WASM_OPUS_LIBS) -lm \
	-sALLOW_MEMORY_GROWTH=1 \
	-sENVIRONMENT=web \
	-sEXPORTED_FUNCTIONS='["_malloc","_free","_nopus_make_wav","_nopus_make_opus","_nopus_make_capcom_opus","_nopus_make_capcom_wav","_nopus_result_ptr","_nopus_result_size","_nopus_result_free"]' \
	-sNO_EXIT_RUNTIME=1 \
	-sSINGLE_FILE=1

.PHONY: all wasm clean check-wasm-opus

all: $(TARGET_NOPUS) $(TARGET_CAPCOM)

wasm: $(TARGET_WASM)

$(TARGET_NOPUS): $(OBJ_NOPUS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_CAPCOM): $(OBJ_CAPCOM)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

check-wasm-opus:
	@tmp_dir="$${TMPDIR:-/tmp}"; \
	tmp_src=""; \
	tmp_out=""; \
	tmp_log=""; \
	trap 'rm -f "$$tmp_src" "$$tmp_out" "$$tmp_log"' EXIT INT TERM; \
	tmp_src="$$(mktemp "$$tmp_dir/nopus-wasm-opus-XXXXXX.c")" || exit 1; \
	tmp_out="$$(mktemp "$$tmp_dir/nopus-wasm-opus-XXXXXX.js")" || exit 1; \
	tmp_log="$$(mktemp "$$tmp_dir/nopus-wasm-opus-XXXXXX.log")" || exit 1; \
	printf '%s\n' \
		'#include <opus/opus.h>' \
		'int main(void) {' \
		'    int error = 0;' \
		'    OpusEncoder* encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &error);' \
		'    return encoder == NULL ? error : 0;' \
		'}' > "$$tmp_src"; \
	if ! $(EMCC) $(WASM_CFLAGS) "$$tmp_src" -o "$$tmp_out" $(WASM_LDFLAGS) >"$$tmp_log" 2>&1; then \
		echo "Error: make wasm requires a libopus build compiled for Emscripten."; \
		echo "The host package 'libopus-dev' is not enough because emcc cannot link the native library."; \
		echo "Build libopus with emconfigure/emmake and then run:"; \
		echo "  make wasm WASM_PKG_CONFIG_PATH=/absolute/path/to/lib/pkgconfig"; \
		echo "or pass WASM_OPUS_CFLAGS=... WASM_OPUS_LIBS=..."; \
		echo "--- emcc output ---"; \
		sed -n "1,$(WASM_CHECK_LOG_LINES)p" "$$tmp_log"; \
		exit 1; \
	fi

$(TARGET_WASM): $(SRC_WASM) | check-wasm-opus
	mkdir -p $(WEBDIR)
	$(EMCC) $(WASM_CFLAGS) -o $@ $^ $(WASM_LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ_NOPUS) $(OBJ_CAPCOM) $(TARGET_NOPUS) $(TARGET_CAPCOM) $(TARGET_WASM)
