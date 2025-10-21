// Globals.cpp
#include "Globals.h"

std::atomic<bool> tab_pressed(false);
std::atomic<bool> down_pressed(false);
std::atomic<bool> up_pressed(false);
std::atomic<bool> done(false);

#ifndef _WIN32
struct termios g_orig_termios;
#endif
