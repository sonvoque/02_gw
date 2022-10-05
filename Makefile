#CFLAGS=-g -Wall -W -I../../..
CC=/usr/bin/gcc
CP=/usr/bin/cp
CFLAGS=-g -Wall -W 

all: 
	gcc -o main main.c util.c aes_lib.c `mysql_config --cflags --libs`


util.o: util.c
	gcc -o util.o util.c

aes_lib.o: aes_lib.c
	gcc -o aes_lib.o aes_lib.c

#net.o: ../net.c
net.o: net.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<

#noerr.o: ../../noerr/noerr.c
noerr.o: noerr.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<

