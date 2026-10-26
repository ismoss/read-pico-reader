#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t addr;
} ip4_addr_t;

bool ip4addr_aton(const char *text, ip4_addr_t *address);
