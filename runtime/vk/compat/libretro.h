/* The few libretro names parallel-psx's renderer still uses (logging, the HD texture tracker's debug
 * keys), so it builds outside a libretro core. Written for this port. */
#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

enum retro_log_level { RETRO_LOG_DEBUG = 0, RETRO_LOG_INFO, RETRO_LOG_WARN, RETRO_LOG_ERROR };
typedef void (*retro_log_printf_t)(enum retro_log_level level, const char *fmt, ...);
typedef int16_t (*retro_input_state_t)(unsigned port, unsigned device, unsigned index, unsigned id);

#define RETRO_DEVICE_KEYBOARD 3
enum retro_key { RETROK_QUOTE = 39, RETROK_SEMICOLON = 59, RETROK_LEFTBRACKET = 91, RETROK_RIGHTBRACKET = 93 };
