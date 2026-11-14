/* pico.h - host stand-in for the Pico SDK's pico.h (tests only). */
#ifndef HOST_PICO_H
#define HOST_PICO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef unsigned int uint;
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (4u * 1024u * 1024u)
#endif
#define PICO_OK 0
#define PICO_ERROR_TIMEOUT (-1)
extern uint8_t host_flash[];                /* the onboard flash, at XIP_BASE */
#define XIP_BASE ((uintptr_t)host_flash)
#endif
