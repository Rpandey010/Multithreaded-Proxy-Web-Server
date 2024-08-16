CC=gcc
CFLAGS= -g -Wall 
LDFLAGS= -lpthread

all: proxy

proxy: proxy_parse.o proxy.o
	$(CC) $(CFLAGS) -o proxy proxy_parse.o proxy.o $(LDFLAGS)

proxy_parse.o: proxy_parse.c proxy_parse.h
	$(CC) $(CFLAGS) -c proxy_parse.c

proxy.o: proxy_server_with_cache.c proxy_parse.h
	$(CC) $(CFLAGS) -c proxy_server_with_cache.c -o proxy.o

clean:
	rm -f proxy *.o

tar:
	tar -cvzf ass1.tgz proxy_server_with_cache.c README Makefile proxy_parse.c proxy_parse.h