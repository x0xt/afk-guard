CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lpthread -lm
TARGET  = afk-guard

$(TARGET): afk-guard.c
	$(CC) $(CFLAGS) -o $(TARGET) afk-guard.c $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
