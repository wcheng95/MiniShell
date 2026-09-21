#pragma once
static inline int minicw_upper(int c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }
static inline int minicw_alnum(int c) { return (c >= '0' && c <= '9') || (minicw_upper(c) >= 'A' && minicw_upper(c) <= 'Z'); }
