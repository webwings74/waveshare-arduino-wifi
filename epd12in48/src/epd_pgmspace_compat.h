#ifndef EPD_PGMSPACE_COMPAT_H
#define EPD_PGMSPACE_COMPAT_H

#if defined(ARDUINO_ARCH_AVR)
#include <avr/pgmspace.h>
#elif defined(__has_include)
#if __has_include(<pgmspace.h>)
#include <pgmspace.h>
#else
#include <stdint.h>
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#endif
#endif
#else
#include <stdint.h>
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#endif
#endif

#endif