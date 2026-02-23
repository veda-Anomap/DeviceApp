.SUFFIXES : .cpp .o 

OBJ1 = device_app.o RtspServer.o AppController.o DeviceManager.o QtCommServer.o
SRC1 = $(OBJ1:.o=.cpp)
CXX = aarch64-linux-gnu-g++

SYSROOT = $(HOME)/rpi_root
CFLAGS += --sysroot=$(SYSROOT) \
          -I$(SYSROOT)/usr/include/gstreamer-1.0 \
          -I$(SYSROOT)/usr/include/glib-2.0 \
          -I$(SYSROOT)/usr/lib/aarch64-linux-gnu/glib-2.0/include \
          -I$(SYSROOT)/usr/lib/aarch64-linux-gnu/gstreamer-1.0/include \
          -Iinclude

LDFLAGS += --sysroot=$(SYSROOT) \
           -L$(SYSROOT)/usr/lib/aarch64-linux-gnu \
           -L$(SYSROOT)/lib/aarch64-linux-gnu \
           -Wl,-rpath-link=$(SYSROOT)/usr/lib/aarch64-linux-gnu \
           -Wl,-rpath-link=$(SYSROOT)/lib/aarch64-linux-gnu \
           -lgstrtspserver-1.0 -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 \
           -lpthread

INC = -I include

.PHONY: clean all

all: device_app

device_app: $(OBJ1)
	$(CXX) -o $@ $(OBJ1) $(LDFLAGS)

.cpp.o:
	$(CXX) $(CFLAGS) -c $< -o $@

clean:
	rm *.o device_app 

dep: 
	gccmakedep $(INC) $(SRC1)