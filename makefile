CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
TARGET = myfind

all: $(TARGET)

$(TARGET): myfind.o
	$(CC) $(CFLAGS) -o $(TARGET) myfind.o

myfind.o: myfind.cpp
	$(CC) $(CFLAGS) -c myfind.cpp

clean:
	rm -f *.o $(TARGET)