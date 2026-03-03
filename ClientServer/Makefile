.SUFFIXES : .cpp .o

OBJ = main.o ClientController.o QtCommServer.o InternalClient.o SystemMonitor.o AuthManager.o
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
           -lpthread -lsqlite3 -lssl -lcrypto

.PHONY: clean all

all: client_server

client_server: $(OBJ)
	$(CXX) -o $@ $(OBJ) $(LDFLAGS)

%.o: src/%.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o client_server
