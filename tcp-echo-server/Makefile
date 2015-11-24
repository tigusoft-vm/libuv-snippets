LDFLAGS = -luv

build: tcp_echo_server

clean: 
	rm -Rf *.o

tcp_echo_server:
	$(CC) --std=gnu99 -g -o tcp_echo_server.o tcp_echo_server.c $(LDFLAGS)
