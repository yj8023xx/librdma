########
#  All modules' directories in src and build
########
SRC_DIR   := src
BUILD_DIR := build

########
#  Source and Object files in their module directories
########
SRC := $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.*))

CC = gcc -g -D_GNU_SOURCE -O2 # C compiler

INCLUDE := $(CURDIR)/include

RDMA_FLAGS += -fvisibility=default

LDFLAGS += -Wl,-rpath=/usr/local/lib -L/usr/local/lib -Wl,-rpath=/usr/lib -L/usr/lib
LDLIBS = -lrdmacm -libverbs -lpthread

########
#  Phony targets
########
.PHONY: all checkdirs clean

all: checkdirs librdma

########
#  Create dirs recursively
########
checkdirs:
	@mkdir -p $(BUILD_DIR)

clean:
	@rm -rf $(BUILD_DIR)

########
#  Targets
########
#build/libmlfs.a: $(OBJ)
librdma: $(SRC)
	$(CC) -fPIC -shared -o $(BUILD_DIR)/librdma.so $(SRC) $(RDMA_FLAGS) $(INCLUDES) $(LDFLAGS) $(LDLIBS)