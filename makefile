EMU_CC      = gcc
EMU_SRC     = src/emulator.c
EMU_FLAGS   = -std=c99 -Wall -Wextra -pedantic -O3 -Wgnu-case-range
EMU_OUT     = bin/emu
EMU_DEBUG   = -DSTEP
EMU_LIBS    = -L/usr/local/lib -I/usr/local/include -lSDL2

COM_CC    = g++
COM_SRC   = src/compiler.cpp
COM_FLAGS = -Wall -Wextra -pedantic -O3 -std=c++11
COM_OUT   = bin/com
COM_DEBUG = -DDEBUG

default:
	$(EMU_CC) $(EMU_SRC) $(EMU_FLAGS) -o $(EMU_OUT) $(EMU_LIBS)
	$(EMU_CC) $(EMU_SRC) $(EMU_FLAGS) $(EMU_DEBUG) $(EMU_LIBS) -o $(EMU_OUT)-debug
	$(COM_CC) $(COM_SRC) $(COM_FLAGS) -o $(COM_OUT)
	$(COM_CC) $(COM_SRC) $(COM_FLAGS) $(COM_DEBUG) -o $(COM_OUT)-debug
