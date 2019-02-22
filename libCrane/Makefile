.PHONY: inline_hook

PWD_DIR=$(shell pwd)
CXX ?= g++
CXXFLAGS ?= -std=c++14 -fPIC -g -I$(PWD_DIR)/third_party/include -L$(PWD_DIR)/third_party/bin/ -L$(PWD_DIR)/bin/
LIBS ?= -lpthread

inline_hook: directories
	make -C $(PWD_DIR)/third_party/PFishHook LIBPATH=$(PWD_DIR)/third_party/zydis/build INCLUDE=$(PWD_DIR)/third_party/zydis/include OUT_DIR=$(PWD_DIR)/third_party/bin OUT_INC_DIR=$(PWD_DIR)/third_party/include

clean:
	rm -f *.o
	rm -f bin/qtest
remake: clean bin/qtest


directories:
	mkdir -p $(PWD_DIR)/bin
	mkdir -p $(PWD_DIR)/third_party
	mkdir -p $(PWD_DIR)/third_party/bin
	mkdir -p $(PWD_DIR)/third_party/include
