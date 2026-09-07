#pragma once

#include <stddef.h>
#include <stdint.h>

#include "minishell_services.h"

#define MINI_FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

const minishell_services_port_t *minishell_services_port(void);
uint64_t minishell_memory_limit_bytes(void);
uint64_t minishell_storage_limit_bytes(void);

const mini_system_api_t *minishell_system_service_api(void);
const mini_memory_api_t *minishell_memory_service_api(void);
const mini_fs_api_t *minishell_filesystem_service_api(void);
const mini_time_location_api_t *minishell_time_location_service_api(void);
const mini_display_api_t *minishell_display_service_api(void);
const mini_input_api_t *minishell_input_service_api(void);

void minishell_memory_service_configure(void);
void minishell_memory_service_app_begin(void);
void minishell_memory_service_app_end(void);

void minishell_filesystem_service_configure(void);
void minishell_filesystem_service_app_begin(void);
void minishell_filesystem_service_app_end(void);

void minishell_time_location_service_configure(void);
void minishell_display_service_configure(void);
void minishell_input_service_configure(void);
void minishell_input_service_app_begin(void);
void minishell_input_service_app_end(void);

bool minishell_memory_service_available(void);
bool minishell_filesystem_service_available(void);
bool minishell_time_location_service_available(void);
bool minishell_display_service_available(void);
bool minishell_input_service_available(void);
