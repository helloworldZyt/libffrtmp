TARGET=libffrtmp.so
TARGET_test=client
TARGET_lib=libffrtmp.so

CXXFLAGS_test=-std=c++11 -ggdb -O1 -fpermissive -pthread
CXXFLAGS+=-I/usr/thirdparty/include/ -L/usr/thirdparty/lib
CXXFLAGS+=-lavcodec -lavformat -lavutil
CXXFLAGS+=-lfdk-aac -lfreetype -lmp3lame -logg -lopus -lpostproc -lswresample -lvorbis -lvpx -lx264 -lx265
CXXFLAGS+=-lvorbisfile -lvorbisenc
CXXFLAGS+=$(CXXFLAGS_test)

All:$(TARGET_test)

$(TARGET):rtmp_client.cpp log.c
	g++ -shared -fpic $^ -o $@  $(CXXFLAGS)

dlib:rtmp_client.cpp log.c
	g++ -shared -fpic $^ -o $(TARGET_lib)  $(CXXFLAGS)

$(TARGET_test):$(TARGET) pull.cpp
	g++ -L. -lffrtmp $^ -o $@ $(CXXFLAGS_test) 

clean:
	$(RM) *.o $(TARGET) $(TARGET_test)

