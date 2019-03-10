.PHONY: inline_hook ${BIN_DIR}/libCrane.so

MYPWD_DIR=$(shell pwd)
CXX ?= g++
override CXXFLAGS += -fPIC -I$(MYPWD_DIR)/third_party/include -L$(MYPWD_DIR)/third_party/bin

${BIN_DIR}/libCrane.so: third_party/bin/libPFishHook.a hooks.o managing.o CraneStatus.o Clipboard.o
	$(CXX) -fPIC $(CXXFLAGS) -shared -o ${BIN_DIR}/libCrane.so  -Wl,--start-group $^ $(LIBS) -ldl -Wl,--end-group 

third_party/bin/libPFishHook.a: directories
	make -C $(MYPWD_DIR)/third_party/PFishHook LIBPATH=$(MYPWD_DIR)/third_party/zydis/build INCLUDE=$(MYPWD_DIR)/third_party/zydis/include OUT_DIR=$(MYPWD_DIR)/third_party/bin OUT_INC_DIR=$(MYPWD_DIR)/third_party/include

clean:
	rm -f *.o
	rm -f ${BIN_DIR}/libCrane.so
remake: clean ${BIN_DIR}/libCrane.so


directories:
	mkdir -p $(MYPWD_DIR)/third_party
	mkdir -p $(MYPWD_DIR)/third_party/bin
	mkdir -p $(MYPWD_DIR)/third_party/include
