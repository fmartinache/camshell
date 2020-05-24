CC=gcc
#CFLAGS  = -W -Wall -ansi -pedantic -fPIC
CFLAGS  = -W -Wall
LDFLAGS = -lpthread -lm -lncurses
EXEC    = camshell
LIB     = testlib.so
OBJECTS = camshell.o ImageStreamIO.o

all: $(EXEC)

testlib.so: testlib.o
	$(CC) -o $@ $^ $(LDFLAGS)

camshell: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

clean:
	rm -rf *.o *.so
	rm -rf *~

mrproper: clean
	rm -rf $(EXEC)

# all:
# 	gcc -o ixon_server ixon_server.cpp ImageCreate.c -landor -lm -lpthread

# camshell: camshell.o ImageCreate.o
# 	gcc -o ixon_server ixon_server.o ImageCreate.o -landor -lcfitsio -lpthread -lm

# ixon_server.o: ixon_server.cpp
# 	gcc -c -Wall ixon_server.cpp

# ImageCreate.o: ImageCreate.c
# 	gcc -c ImageCreate.c

# clean:
# 	rm -rf *.o


