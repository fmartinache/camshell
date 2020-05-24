CC=gcc

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
