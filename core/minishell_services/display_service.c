#include "services_internal.h"

static bool s_available;
static mini_text_display_api_t s_text_api;
static mini_display_api_t s_display_api;

static mini_result_t display_get_info(mini_text_display_info_t *out_info)
{
    const minishell_services_port_t *port = minishell_services_port();
    const uint32_t v0_size = MINI_FIELD_END(mini_text_display_info_t, rows);
    if ((s_display_api.capabilities & MINI_DISPLAY_CAP_TEXT) == 0u) return MINI_ERR_UNSUPPORTED;
    if (out_info == NULL || out_info->struct_size < v0_size) return MINI_ERR_INVALID;
    uint32_t columns = 0u;
    uint32_t rows = 0u;
    mini_result_t result = port->display_text_get_info(port->ctx, &columns, &rows);
    if (result != MINI_OK) return result;
    if (columns == 0u || rows == 0u) return MINI_ERR_NOT_READY;
    out_info->columns = columns;
    out_info->rows = rows;
    return MINI_OK;
}

static mini_result_t query_geometry(uint32_t *out_columns, uint32_t *out_rows)
{
    const minishell_services_port_t *port = minishell_services_port();
    mini_result_t result = port->display_text_get_info(port->ctx, out_columns, out_rows);
    if (result != MINI_OK) return result;
    if (*out_columns == 0u || *out_rows == 0u) return MINI_ERR_NOT_READY;
    return MINI_OK;
}

static mini_result_t display_clear(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    if ((s_display_api.capabilities & MINI_DISPLAY_CAP_TEXT) == 0u) return MINI_ERR_UNSUPPORTED;
    return port->display_text_clear(port->ctx);
}

static mini_result_t display_clear_at(uint32_t row, uint32_t column, uint32_t rows, uint32_t columns)
{
    const minishell_services_port_t *port = minishell_services_port();
    if ((s_display_api.capabilities & MINI_DISPLAY_CAP_TEXT) == 0u) return MINI_ERR_UNSUPPORTED;
    uint32_t max_columns = 0u;
    uint32_t max_rows = 0u;
    mini_result_t result = query_geometry(&max_columns, &max_rows);
    if (result != MINI_OK) return result;
    if (row >= max_rows || column >= max_columns) return MINI_ERR_INVALID;
    if (rows == 0u || columns == 0u) return MINI_OK;
    if (rows > max_rows - row) rows = max_rows - row;
    if (columns > max_columns - column) columns = max_columns - column;
    return port->display_text_clear_at(port->ctx, row, column, rows, columns);
}

static mini_result_t display_write_at(uint32_t row, uint32_t column, const char *text, uint32_t byte_count)
{
    const minishell_services_port_t *port = minishell_services_port();
    if ((s_display_api.capabilities & MINI_DISPLAY_CAP_TEXT) == 0u) return MINI_ERR_UNSUPPORTED;
    if (text == NULL && byte_count > 0u) return MINI_ERR_INVALID;
    uint32_t max_columns = 0u;
    uint32_t max_rows = 0u;
    mini_result_t result = query_geometry(&max_columns, &max_rows);
    if (result != MINI_OK) return result;
    if (row >= max_rows || column >= max_columns) return MINI_ERR_INVALID;
    if (byte_count == 0u) return MINI_OK;
    uint32_t available = max_columns - column;
    if (byte_count > available) byte_count = available;
    return port->display_text_write_at(port->ctx, row, column, text, byte_count);
}

static mini_result_t display_write_at_attr(uint32_t row, uint32_t column,
                                           const char *text, uint32_t byte_count,
                                           uint32_t attributes)
{
    const minishell_services_port_t *port = minishell_services_port();
    const uint32_t known_attributes = MINI_TEXT_ATTR_INVERSE;
    if ((s_display_api.capabilities & MINI_DISPLAY_CAP_TEXT) == 0u) return MINI_ERR_UNSUPPORTED;
    if ((attributes & ~known_attributes) != 0u) return MINI_ERR_INVALID;
    if (attributes == MINI_TEXT_ATTR_NONE) {
        return display_write_at(row, column, text, byte_count);
    }
    if (port->display_text_write_at_attr == NULL) return MINI_ERR_UNSUPPORTED;
    if (text == NULL && byte_count > 0u) return MINI_ERR_INVALID;

    uint32_t max_columns = 0u;
    uint32_t max_rows = 0u;
    mini_result_t result = query_geometry(&max_columns, &max_rows);
    if (result != MINI_OK) return result;
    if (row >= max_rows || column >= max_columns) return MINI_ERR_INVALID;
    if (byte_count == 0u) return MINI_OK;
    uint32_t available = max_columns - column;
    if (byte_count > available) byte_count = available;
    return port->display_text_write_at_attr(port->ctx, row, column, text,
                                            byte_count, attributes);
}

static mini_result_t display_present(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    return port->display_present(port->ctx);
}

void minishell_display_service_configure(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    s_available = port->display_present != NULL;
    s_text_api.struct_size = sizeof(s_text_api);
    s_text_api.get_info = display_get_info;
    s_text_api.clear = display_clear;
    s_text_api.clear_at = display_clear_at;
    s_text_api.write_at = display_write_at;
    s_text_api.write_at_attr = port->display_text_write_at_attr != NULL
                                   ? display_write_at_attr
                                   : NULL;
    s_display_api.struct_size = sizeof(s_display_api);
    s_display_api.capabilities = 0u;
    s_display_api.text = NULL;
    s_display_api.present = display_present;
    bool text_available = port->display_text_get_info != NULL && port->display_text_clear != NULL &&
                          port->display_text_clear_at != NULL && port->display_text_write_at != NULL;
    if (s_available && text_available && (port->display_capabilities & MINI_DISPLAY_CAP_TEXT) != 0u) {
        s_display_api.capabilities |= MINI_DISPLAY_CAP_TEXT;
        s_display_api.text = &s_text_api;
    }
}

bool minishell_display_service_available(void) { return s_available; }
const mini_display_api_t *minishell_display_service_api(void) { return &s_display_api; }
