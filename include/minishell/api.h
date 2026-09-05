#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MINISHELL_ABI_VERSION 0x00000001u

#if defined(__GNUC__)
#define MINI_IMPORT __attribute__((visibility("default")))
#else
#define MINI_IMPORT
#endif

typedef int32_t mini_result_t;

typedef struct {
    uint32_t struct_size;
    void (*write)(const char *text);
} mini_system_api_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    const mini_system_api_t *system;
} mini_api_t;

/*
 * Runtime import resolved by MiniShell's ELF loader.
 *
 * An application calls this once to obtain the resident service table. The
 * application then talks to MiniShell through function pointers in mini_api_t.
 * This is the Task 0 binding experiment; ABI v1 is not frozen yet.
 */
MINI_IMPORT const mini_api_t *mini_api_get(void);

#ifdef __cplusplus
}
#endif
