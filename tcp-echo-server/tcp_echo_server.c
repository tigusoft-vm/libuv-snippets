#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/**
 * Our tcp server object.
 */
uv_tcp_t server;
uv_tcp_t *client;
uv_stream_t *g_stream;
uv_timer_t gc_req;

/**
 * Shared reference to our event loop.
 */
uv_loop_t * loop;

/**
 * Function declarations.
 */
uv_buf_t alloc_buffer(uv_handle_t * handle, size_t size);
void connection_cb(uv_stream_t * server, int status);
void read_cb(uv_stream_t * stream, ssize_t nread, uv_buf_t buf);
void timer_cb(uv_timer_t* handle);

/////////////////////////////////////////////////////////////////////
typedef struct uv_buff_circular {
	uv_buf_t *buffs; // array of buffers
	size_t nbuffs; // number of elements in buffs
	int size; // current size
	// private
	uv_buf_t *current_element; // last element
	size_t single_buff_size;
} uv_buff_circular;

//private functions

/**
 * Call free for buff
 */
static void free_buff(uv_buf_t *buff) {
	assert(buff != NULL);
	free(buff->base);
	buff->base = NULL;
	buff->len = 0;
}

/**
 * Move current_element pointer to next element
 */
static void move_internal_pointer(uv_buff_circular * const circular_buff) {
	assert(circular_buff != NULL);
	// Move 'current_element' pointer
	// check if current_element == last element
	if (circular_buff->current_element == &circular_buff->buffs[circular_buff->nbuffs -1]) {
		circular_buff->current_element = &circular_buff->buffs[0];
	}
	else {
		circular_buff->current_element++;
	}
}

// public functions

/**
 * @param circular_buff Must be allocated in caller.
 * @param nbufs Number of buffers
 */
void buff_circular_init(uv_buff_circular *circular_buff, size_t nbufs) {
	assert(circular_buff != NULL);
	circular_buff->buffs = (uv_buf_t *)malloc(sizeof(uv_buf_t) * nbufs);
	circular_buff->nbuffs = nbufs;
	circular_buff->current_element = &circular_buff->buffs[nbufs -1];
	circular_buff->size = 0;
}

/**
 * Move data from @param buff to @param circular_buff
 * @param circular_buff Initialized by caller (buff_circular_init).
 * @param buff Initialized by caller. Clean in this function (.base = NULL, .len=0).
 * @return 0 if success
 */
int buff_circular_push(uv_buff_circular * const circular_buff, uv_buf_t * const buff) {
	if (circular_buff == NULL) {
		return 1;
	}
	if (buff == NULL) {
		return 2;
	}

	assert(circular_buff->size <= circular_buff->nbuffs);
	if (circular_buff->size == circular_buff->nbuffs) { // buffer if full
		return 3;
	}

	// move 'current_element' pointer to next slot
	move_internal_pointer(circular_buff);

	// move element
	circular_buff->current_element->len = buff->len;
	buff->len = 0;
	circular_buff->current_element->base = buff->base;
	buff->base = NULL;

	// increment size
	if (circular_buff->size < circular_buff->nbuffs) {
		circular_buff->size++;
	}
	return 0;
}

/**
 * Move data from @param circular_buff to @param buff
 * @param circular_buff Initialized by caller (buff_circular_init).
 * @param buff Out pointer. Must be empty (.base = NULL, .len=0).
 * @return 0 if success
 */
int buff_circular_pop(uv_buff_circular *circular_buff, uv_buf_t * const buff) {
	if (circular_buff == NULL) {
		return 1;
	}
	if (buff == NULL) {
		return 2;
	}

	assert(buff->base == NULL);
	assert(buff->len == 0);

	size_t pop_element_index = circular_buff->nbuffs;
	size_t last_element_index = circular_buff->current_element - circular_buff->buffs;

	uv_buf_t *pop_ptr = circular_buff->current_element;
	for (int i = 0; i < circular_buff->size - 1; ++i) {
		if (pop_ptr == &circular_buff->buffs[0]) {
			pop_ptr += circular_buff->nbuffs - 1; // last element in array
		}
		else {
			pop_ptr--;
		}
	}

	assert(pop_element_index <= circular_buff->nbuffs);

	// move buffer
	buff->base = pop_ptr->base;
	pop_ptr->base = NULL;
	buff->len = pop_ptr->len;
	pop_ptr->len = 0;

	circular_buff->size--;

	return 0;
}

/**
 * Call free() for evry element.
 * Deallocate internal array.
 */
void buff_circular_deinit(uv_buff_circular *circular_buff) {
	if (circular_buff == NULL) {
		return;
	}

	for (int i = 0; i < circular_buff->nbuffs; ++i) {
		if (circular_buff->buffs[i].base != NULL)
			free_buff(&circular_buff->buffs[i]);
	}
	free(circular_buff->buffs);
	circular_buff->current_element = NULL;
	circular_buff->buffs = NULL;
	circular_buff->nbuffs = 0;
	circular_buff->size = 0;
}

/////////////////////////////////////////////////////////////////////

void test_buff_circular() {
	printf ("test_buff_circular\n");
	uv_buff_circular circular_buff;
	const size_t buff_size = 5;
	const size_t number_of_buffs = 10;
	buff_circular_init(&circular_buff, number_of_buffs);

	for (int i = 0; i < number_of_buffs; ++i) {
		uv_buf_t buff;
		buff.base = (char*)malloc(sizeof(char) * buff_size);
		buff.len = buff_size;
		for (int j = 0; j < buff_size; ++j) {
			buff.base[j] = 'a';
		}
		buff_circular_push(&circular_buff, &buff);
		free(buff.base);
	}

	for (int i = 0; i < circular_buff.nbuffs; ++i) {
		uv_buf_t buff2;
		buff2.base = NULL;
		buff2.len = 0;
		buff_circular_pop(&circular_buff, &buff2);
		printf("pop buffer %d\n", i);
		for (int j = 0; j < buff2.len; ++j) {
			printf("%c", buff2.base[j]);
		}
		printf("\n");
		free(buff2.base);
	}


	for (int i = 0; i < number_of_buffs; ++i) {
		uv_buf_t buff;
		buff.base = (char*)malloc(sizeof(char) * buff_size);
		buff.len = buff_size;
		for (int j = 0; j < buff_size; ++j) {
			buff.base[j] = 'b';
		}
		buff_circular_push(&circular_buff, &buff);
		free(buff.base);
	}

	for (int i = 0; i < circular_buff.nbuffs; ++i) {
		uv_buf_t buff2;
		buff2.base = NULL;
		buff2.len = 0;
		buff_circular_pop(&circular_buff, &buff2);
		printf("pop buffer %d\n", i);
		for (int j = 0; j < buff2.len; ++j) {
			printf("%c", buff2.base[j]);
		}
		printf("\n");
		free(buff2.base);
	}

	buff_circular_deinit(&circular_buff);
	printf ("end of test\n");
}

uv_buff_circular buff_circular;


int main() {

	test_buff_circular();
	return 0;
	g_stream = NULL;
	const int port = 3000;
	const char *host = "127.0.0.1";
	printf("Starting the test echo server. Connect to me, host %s on port %d\n" , host, port);

	/* dynamically allocate a new client stream object on conn */
	client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));

    loop = uv_default_loop();

	buff_circular_init(&buff_circular, 5);
	uv_timer_init(loop, &gc_req);
	uv_timer_start(&gc_req, (uv_timer_cb)timer_cb, 0, 2000);

    /* convert a humanreadable ip address to a c struct */
    struct sockaddr_in addr = uv_ip4_addr(host, port);

    /* initialize the server */
    uv_tcp_init(loop, &server);
    /* bind the server to the address above */
    uv_tcp_bind(&server, addr);
    
    /* let the server listen on the address for new connections */
    int r = uv_listen((uv_stream_t *) &server, 128, connection_cb);

    if (r) {
        return fprintf(stderr, "Error on listening: %s.\n", 
                uv_strerror(uv_last_error(loop)));
    }

    /* execute all tasks in queue */
    uv_run(loop, UV_RUN_DEFAULT);
	buff_circular_deinit(&buff_circular);
	if (client != NULL)
		free(client);
	return 0;
}

/**
 * Callback which is executed on each new connection.
 */
void connection_cb(uv_stream_t * server, int status) {
    /* if status not zero there was an error */
    if (status == -1) {
        fprintf(stderr, "Error on listening: %s.\n", 
            uv_strerror(uv_last_error(loop)));
    }

    /* initialize the new client */
    uv_tcp_init(loop, client);

    /* now let bind the client to the server to be used for incomings */
    if (uv_accept(server, (uv_stream_t *) client) == 0) {
        /* start reading from stream */
        int r = uv_read_start((uv_stream_t *) client, alloc_buffer, read_cb);

        if (r) {
            fprintf(stderr, "Error on reading client stream: %s.\n", 
                    uv_strerror(uv_last_error(loop)));
        }
    } else {
        /* close client stream on error */
        uv_close((uv_handle_t *) client, NULL);
    }
}

/**
 * Callback which is executed on each readable state.
 */
void read_cb(uv_stream_t * stream, ssize_t nread, uv_buf_t buf) {
    /* if read bytes counter -1 there is an error or EOF */
    if (nread == -1) {
        if (uv_last_error(loop).code != UV_EOF) {
            fprintf(stderr, "Error on reading client stream: %s.\n", 
                    uv_strerror(uv_last_error(loop)));
        }

        uv_close((uv_handle_t *) stream, NULL);
    }

    assert(nread<=buf.len); // this should be impossible, uv should never return it

	if (buf.base[0] == 'z') {
		uv_stop(loop);
	}

	printf("READ buffer: ");
    for (size_t i=0; i<nread; ++i) {
    	unsigned char c = buf.base[i];
    	if (i) printf(",");
    	if ( (c>=32) && (c<=127) ) printf("%c",c);
    	else printf("(%u)", (unsigned int)c);
    }
    printf(" nread=%llu ", (unsigned long long)nread);
    printf(" len=%llu\n", (unsigned long long)buf.len);

    uv_buf_t write_buf = uv_buf_init((char *) malloc(nread), nread);
	write_buf.len = nread;
	memset(write_buf.base, 0, write_buf.len);
	memcpy(write_buf.base, buf.base, nread);

	printf("push msg to circular buffer\n");
	buff_circular_push(&buff_circular, &write_buf);
	printf("circular buffer size: %llu\n", (unsigned long long)buff_circular.size);
	g_stream = stream;

//	/* write sync the incoming buffer to the socket */
//    uv_buf_t write_buf = uv_buf_init((char *) malloc(nread), nread);
//	write_buf.len = nread;
//	memcpy(write_buf.base, buf.base, nread);
//    /* dynamically allocate memory for a new write task */
//    uv_write_t * req = (uv_write_t *) malloc(sizeof(uv_write_t));
//    int r = uv_write(req, stream, &write_buf, 1, NULL);
//
//    if (r) {
//        fprintf(stderr, "Error on writing client stream: %s.\n",
//                uv_strerror(uv_last_error(loop)));
//    }

    /* free the remaining memory */
	//uv_stop(loop);
    free(buf.base);
//	free(write_buf.base);
//	free(req);
}

/**
 * Allocates a buffer which we can use for reading.
 */
uv_buf_t alloc_buffer(uv_handle_t * handle, size_t size) {
        return uv_buf_init((char *) malloc(size), size);
}


void timer_cb(uv_timer_t* handle) {
	printf("timer_cb\n");
	if (g_stream == NULL) {
		return;
	}
	if (buff_circular.size == 0) {
		return;
	}
	/* write sync the incoming buffer to the socket */
    uv_buf_t write_buf = uv_buf_init((char *) malloc(65536), 65536);
	write_buf.len = 65536;
	buff_circular_pop(&buff_circular, &write_buf);
	printf("write_buf.len: %llu\n", (unsigned long long)write_buf.len);
    /* dynamically allocate memory for a new write task */
    uv_write_t * req = (uv_write_t *) malloc(sizeof(uv_write_t));
    int r = uv_write(req, g_stream, &write_buf, 1, NULL);

    if (r) {
        fprintf(stderr, "Error on writing client stream: %s.\n",
                uv_strerror(uv_last_error(loop)));
    }

    /* free the remaining memory */
	free(write_buf.base);
	//free(req);
}