CROSS_PREFIX =
# CROSS_PREFIX = i686-mingw32-pc-
CPP          = $(CROSS_PREFIX)g++
CC           = $(CROSS_PREFIX)gcc
WINDRES      = $(CROSS_PREFIX)windres
DLLWRAP      = $(CROSS_PREFIX)dllwrap
RM           = erase
# RM           = rm
BIN          = xmp-scrobbler.dll
OBJ          = xmp-scrobbler.o libscrobbler/md5.o libscrobbler/scrobbler.o cachemanager.o data.o dialog.o
LIBS         = -L"D:/Development/Libraries/i686" -mwindows -s -lcurl -lws2_32 -lgdi32 -lz
# LIBS         = -L"D:/Development/Libraries/NativeO3" -mwindows -s -lcurl -lws2_32 -lgdi32 -lz
# LIBS         = -L/home/xander/devenv/lib -mwindows -s -lcurl_ssl -lssl -lcrypto -lws2_32 -lgdi32 -lz
CXXINCS      = -I"D:/Development/Include"
# CXXINCS      = -I/home/xander/devenv/include
CXXFLAGS     = $(CXXINCS) -DCURL_STATICLIB -O3 -march=i686 -mtune=generic -mwindows

all: xmp-scrobbler.dll

clean:
	${RM} $(OBJ) $(BIN)

$(BIN): $(OBJ)
	$(DLLWRAP) -static-libgcc --def xmpdsp.def --driver-name $(CROSS_PREFIX)c++ -Wl,--enable-stdcall-fixup $(OBJ) $(LIBS) -o $(BIN)

xmp-scrobbler.o: xmp-scrobbler.cpp
	$(CPP) -c xmp-scrobbler.cpp -o xmp-scrobbler.o $(CXXFLAGS)

libscrobbler/md5.o: libscrobbler/md5.c
	$(CPP) -c libscrobbler/md5.c -o libscrobbler/md5.o $(CXXFLAGS)

libscrobbler/scrobbler.o: libscrobbler/scrobbler.cpp
	$(CPP) -c libscrobbler/scrobbler.cpp -o libscrobbler/scrobbler.o $(CXXFLAGS)

cachemanager.o: cachemanager.cpp
	$(CPP) -c cachemanager.cpp -o cachemanager.o $(CXXFLAGS)

data.o: data.cpp
	$(CPP) -c data.cpp -o data.o $(CXXFLAGS)

dialog.o: xmp-scrobbler.rc
	$(WINDRES) -i xmp-scrobbler.rc --input-format=rc -o dialog.o -O coff
