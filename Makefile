.PHONY:all clean directories

MKDIR_P = mkdir -p

##
PWD_DIR=$(shell pwd)
SERVER_DIR=$(PWD_DIR)/Crane
CLILIB_DIR=$(PWD_DIR)/libCrane
INC_DIR=$(PWD_DIR)/include
BIN_DIR=$(PWD_DIR)/bin

CXX ?= g++
CPPFLAGS ?= -std=c++14 -g -I$(INC_DIR)  -L$(PWD_DIR)/bin
LIBS ?= -pthread

##
export PWD_DIR CXX CPPFLAGS LIBS SERVER_DIR CLILIB_DIR INC_DIR BIN_DIR

##
all: directories server client

directories: ${BIN_DIR} ${LIB_DIR}

${BIN_DIR}:
	${MKDIR_P} ${BIN_DIR}

server: client
	$(MAKE) -C $(SERVER_DIR)
	
client: directories
	$(MAKE) -C $(CLILIB_DIR)

##
clean:
	$(MAKE)  -C $(SERVER_DIR) clean
	$(MAKE)  -C $(CLILIB_DIR) clean
	rm -rf ${BIN_DIR}
	rm -rf ${LIB_DIR}

