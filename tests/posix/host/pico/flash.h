#include "pico.h"
int flash_safe_execute(void (*func)(void *), void *param, uint32_t enter_exit_timeout_ms);
