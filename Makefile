TARGET=libffrtmp.so
TARGET_test=client

CXXFLAGS_test=-std=c++11 -ggdb -O1 -fpermissive -pthread
CXXFLAGS_test+=-I/root/workcopy/ffmpeg_build/include -L/root/workcopy/ffmpeg_build/lib
CXXFLAGS_test+=-lavcodec -lavformat -lavutil
CXXFLAGS_test+=-lfdk-aac  -lmp3lame -logg -lopus -lpostproc -lswresample -lvorbis -lvpx -lx264 -lx265
CXXFLAGS_test+=-lvorbisfile -lvorbisenc
CXXFLAGS+=$(CXXFLAGS_test)

All:$(TARGET_test)

$(TARGET):rtmp_client.cpp log.c
	g++ -shared -fpic $^ -o $@  $(CXXFLAGS)

dlib:rtmp_client.cpp log.c
	g++ -shared -fpic $^ -o $(TARGET)  $(CXXFLAGS)

test:pull.cpp
	g++ -L. -lffrtmp $^ -o $(TARGET_test) $(CXXFLAGS_test)

$(TARGET_test):$(TARGET) pull.cpp
	g++ -L. -lffrtmp $^ -o $@ $(CXXFLAGS_test) 

clean:
	$(RM) *.o $(TARGET) $(TARGET_test)

