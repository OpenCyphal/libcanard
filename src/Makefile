CC = gcc
CPP = g++
all: 
	$(CC) -g -Wall canard_test.c canard.c -lpthread -std=gnu99 -o canard
crc_test:
	$(CC) -g -Wall crc_test.c -lpthread -std=gnu99 -o crc_test
clean:
	-rm $(objects) crc_test canard
