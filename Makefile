EXECUTABLE=tattletail

CC = gcc
CCFLAGS = -lcurl -lpthread -lutil

all: $(EXECUTABLE)

$(EXECUTABLE):
	$(CC) $(CCFLAGS) $(EXECUTABLE).c -o $@
