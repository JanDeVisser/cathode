/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <rt/cathode.h>

int cathode$enum_tag(enum_def_t *enum_def, uint64_t value, slice_t *ret)
{
    uint64_t str_offset = 0;
    int64_t  str_len = 0;
    for (int ix = 0; ix < enum_def->num_values; ++ix) {
        if (enum_def->values[ix].value == value) {
            str_len = enum_def->values[ix].tag_len;
            break;
        }
        str_offset += (enum_def->values[ix].tag_len + 1) * sizeof(wchar_t);
    }
    if (str_len == 0) {
        cathode$abort(L"Invalid enum value", 18);
    }

    uint8_t *ptr = ((uint8_t *) enum_def) + sizeof(uint64_t) + enum_def->num_values * (sizeof(int64_t) + sizeof(uint64_t)) + str_offset;
    ret->ptr = ptr;
    ret->size = str_len;
    return 0;
}
