${BIN_DIR}/crane: Crane.o
	$(CXX) -fPIC $(CPPFLAGS) -o ${BIN_DIR}/crane  -Wl,--start-group $^ $(LIBS) -lCrane -ldl -Wl,--end-group -Wl,-rpath='$$ORIGIN'