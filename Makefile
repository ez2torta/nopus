
CC = gcc
SRCDIR = src

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

all: $(TARGET_NOPUS) $(TARGET_CAPCOM)

$(TARGET_NOPUS): $(OBJ_NOPUS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_CAPCOM): $(OBJ_CAPCOM)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ_NOPUS) $(OBJ_CAPCOM) $(TARGET_NOPUS) $(TARGET_CAPCOM)
