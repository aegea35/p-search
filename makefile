CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O3 -D_GNU_SOURCE 
TARGET = p_search
SRC = p_search.c
all: $(TARGET)
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)
clean:
	rm -f $(TARGET) *.o
.PHONY: all clean