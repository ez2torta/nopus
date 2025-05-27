CC = gcc
CFLAGS = -O3
LDFLAGS = -lopus -lm

SRC_NOPUS = main.c common.c files.c list.c
OBJ_NOPUS = $(SRC_NOPUS:.c=.o)
TARGET_NOPUS = nopus

SRC_CAPCOM = create_capcom_opus.c common.c files.c list.c
OBJ_CAPCOM = $(SRC_CAPCOM:.c=.o)
TARGET_CAPCOM = create_capcom_opus

all: $(TARGET_NOPUS) $(TARGET_CAPCOM)

$(TARGET_NOPUS): $(OBJ_NOPUS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_CAPCOM): $(OBJ_CAPCOM)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ_NOPUS) $(OBJ_CAPCOM) $(TARGET_NOPUS) $(TARGET_CAPCOM)
