CC ?= cc
SRCDIR = src
WEBDIR = web
EMCC ?= emcc

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

CFLAGS = -O3 -I$(SRCDIR) $(OPUS_INC)
LDFLAGS = $(OPUS_LIB) -lopus -lm

SRC_NOPUS = $(SRCDIR)/main.c $(SRCDIR)/common.c $(SRCDIR)/files.c $(SRCDIR)/list.c
OBJ_NOPUS = $(SRC_NOPUS:.c=.o)
TARGET_NOPUS = nopus

SRC_CAPCOM = $(SRCDIR)/create_capcom_opus.c $(SRCDIR)/common.c $(SRCDIR)/files.c $(SRCDIR)/list.c
OBJ_CAPCOM = $(SRC_CAPCOM:.c=.o)
TARGET_CAPCOM = create_capcom_opus

SRC_WASM = $(SRCDIR)/nopus_wasm.c $(SRCDIR)/common.c $(SRCDIR)/files.c $(SRCDIR)/list.c
TARGET_WASM = $(WEBDIR)/nopus-web.js
WASM_LDFLAGS = -lopus -lm \
	-sALLOW_MEMORY_GROWTH=1 \
	-sENVIRONMENT=web \
	-sEXPORTED_FUNCTIONS='["_malloc","_free","_nopus_make_wav","_nopus_make_opus","_nopus_make_capcom_opus","_nopus_make_capcom_wav","_nopus_result_ptr","_nopus_result_size","_nopus_result_free"]' \
	-sNO_EXIT_RUNTIME=1 \
	-sSINGLE_FILE=1

all: $(TARGET_NOPUS) $(TARGET_CAPCOM)

wasm: $(TARGET_WASM)

$(TARGET_NOPUS): $(OBJ_NOPUS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_CAPCOM): $(OBJ_CAPCOM)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_WASM): $(SRC_WASM)
	mkdir -p $(WEBDIR)
	$(EMCC) $(CFLAGS) -o $@ $^ $(WASM_LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ_NOPUS) $(OBJ_CAPCOM) $(TARGET_NOPUS) $(TARGET_CAPCOM) $(TARGET_WASM)
