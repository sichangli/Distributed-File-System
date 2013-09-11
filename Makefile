CC = g++
CFLAGS = -Wall -g
LDFLAGS = -g

all: tcp_server tcp_client

tcp_server: tcp_server.o CachedFile.o Cache.o

tcp_client: tcp_client.o

tcp_server.o: tcp_server.cpp CachedFile.h Cache.h
	$(CC) $(CFLAGS) -c tcp_server.cpp

tcp_client.o: tcp_client.cpp
	$(CC) $(CFLAGS) -c tcp_client.cpp

CachedFile.o: CachedFile.h CachedFile.cpp
	$(CC) $(CFLAGS) -c CachedFile.cpp

Cache.o: Cache.h Cache.cpp
	$(CC) $(CFLAGS) -c Cache.cpp

.PHONY: clean
clean:
	rm -f *.o tcp_server tcp_client
