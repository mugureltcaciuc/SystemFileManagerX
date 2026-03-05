// Globals.h
#ifndef GLOBALS_H
#define GLOBALS_H

#ifndef _WIN32
#include <termios.h>  // <- needed for struct termios
#endif

#include <atomic>

extern std::atomic<bool> esc_pressed;
extern std::atomic<bool> tab_pressed;
extern std::atomic<bool> down_pressed;
extern std::atomic<bool> up_pressed;
extern std::atomic<bool> done;

extern struct termios g_orig_termios;
#endif // GLOBALS_H
