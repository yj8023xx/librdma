# Makefile template for shared library

CC = gcc -g -D_GNU_SOURCE -O2 # C compiler
RDMA_DIR := $(abspath $(CURDIR)/../build/)
INCLUDES  := -I$(CURDIR)/../src

LDLIBS = -lrdmacm -lrdma -lm -pthread -lrt
LDFLAGS += -Wl,-rpath=$(abspath $(RDMA_DIR)) -L$(RDMA_DIR)

SRCS = helloworld.c latency.c srq.c multicast.c
OBJS = $(SRCS:.c=.o)
BIN = $(SRCS:.c=)

.PHONY: clean

all: $(BIN)

$(BIN):$(OBJS)
	$(CC) $@.o ${LDFLAGS} ${LDLIBS} -o $@
	rm -f $@.o

%.o: %.c
	$(CC) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(BIN)