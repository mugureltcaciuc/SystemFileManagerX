// Globals.cpp
#include "Globals.h"

std::atomic<bool> esc_pressed(false);
std::atomic<bool> tab_pressed(false);
std::atomic<bool> down_pressed(false);
std::atomic<bool> up_pressed(false);
std::atomic<bool> done(false);

struct termios g_orig_termios; //global variable to store original terminal settings
