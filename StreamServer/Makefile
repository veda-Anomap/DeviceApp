.SUFFIXES : .cpp .o

OBJ = main.o StreamController.o QtCommServer.o RtspServer.o InternalClient.o
CXX = aarch64-linux-gnu-g++

SYSROOT = $(HOME)/rpi_root
CFLAGS += --sysroot=$(SYSROOT) \
          -I$(SYSROOT)/usr/include/gstreamer-1.0 \
          -I$(SYSROOT)/usr/include/glib-2.0 \
          -I$(SYSROOT)/usr/lib/aarch64-linux-gnu/glib-2.0/include \
          -I$(SYSROOT)/usr/lib/aarch64-linux-gnu/gstreamer-1.0/include \
          -Iinclude \
          -I../common

LDFLAGS += --sysroot=$(SYSROOT) \
           -L$(SYSROOT)/usr/lib/aarch64-linux-gnu \
           -L$(SYSROOT)/lib/aarch64-linux-gnu \
           -Wl,-rpath-link=$(SYSROOT)/usr/lib/aarch64-linux-gnu \
           -Wl,-rpath-link=$(SYSROOT)/lib/aarch64-linux-gnu \
           -lgstrtspserver-1.0 -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 \
           -lpthread

.PHONY: clean all

all: stream_server

stream_server: $(OBJ)
	$(CXX) -o $@ $(OBJ) $(LDFLAGS)

%.o: src/%.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o stream_server
