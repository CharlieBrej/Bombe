
if NO_STEAM
STEAM_FLAGS = -DNO_STEAM
STEAM_LDADD =
else
STEAM_FLAGS = 
STEAM_LDADD =  -lsteam_api
endif

EXTRA_FLAGS =
EXTRA_LDADD = 
EXTRA_LD_FLAGS = 
EXTRA_SRC = 

if LINUX
    EXTRA_FLAGS += -DTARGET_LINUX 
    EXTRA_SRC += clip/image.cpp
    EXTRA_LDADD += -lX11 -lpng
endif

if MACOS
    EXTRA_FLAGS += -DTARGET_MACOS
    EXTRA_SRC += clip/clip.cpp clip/image.cpp  clip/clip_osx.mm
    EXTRA_LD_FLAGS += -framework Cocoa
endif

bin_PROGRAMS = Bombe GridGenerator BombeServer

Bombe_SOURCES =     main.cpp \
                    Grid.cpp Grid.h \
                    Misc.cpp Misc.h \
                    GameState.cpp GameState.h \
                    SaveState.cpp SaveState.h \
                    LevelSet.cpp LevelSet.h \
                    Compress.cpp Compress.h \
                    ImgClipBoard.cpp ImgClipBoard.h \
                    $(EXTRA_SRC)

Bombe_CXXFLAGS = @CXXFLAGS@ @SDL2_CFLAGS@ @SDL2_image_CFLAGS@ @SDL2_mixer_CFLAGS@ @SDL2_net_CFLAGS@ @SDL2_ttf_CFLAGS@ @ZSTD_CFLAGS@ @Z3_CFLAGS@ -I. $(STEAM_FLAGS) $(EXTRA_FLAGS)
Bombe_LDADD=@SDL2_LIBS@ @SDL2_image_LIBS@ @SDL2_mixer_LIBS@ @SDL2_net_LIBS@ @SDL2_ttf_LIBS@ @ZSTD_LIBS@ @Z3_LIBS@ $(STEAM_LDADD) $(EXTRA_LDADD) -lpthread
Bombe_LDFLAGS=-L. $(EXTRA_LD_FLAGS)

GridGenerator_SOURCES =     grid_generator.cpp \
                    Grid.cpp Grid.h \
                    Misc.cpp Misc.h \
                    SaveState.cpp SaveState.h \
                    LevelSet.cpp LevelSet.h \
                    Compress.cpp Compress.h

GridGenerator_CXXFLAGS = @CXXFLAGS@ @ZSTD_CFLAGS@ @Z3_CFLAGS@ $(EXTRA_FLAGS)
GridGenerator_LDADD=@ZSTD_LIBS@ @Z3_LIBS@ $(EXTRA_LDADD) -lpthread
GridGenerator_LDFLAGS=-L. $(EXTRA_LD_FLAGS)

BombeServer_SOURCES =   BombeServer.cpp BombeServer.h \
                        SaveState.cpp SaveState.h \
                        Compress.cpp Compress.h
                        

BombeServer_CXXFLAGS = @CXXFLAGS@  $(EXTRA_FLAGS)  @libcurl_CFLAGS@
BombeServer_LDADD=@ZSTD_LIBS@ $(EXTRA_LDADD) -lpthread @libcurl_LIBS@
BombeServer_LDFLAGS=-L. $(EXTRA_LD_FLAGS)


CXXFLAGS = -std=c++2a
