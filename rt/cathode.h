/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __CATHODE_H__
#define __CATHODE_H__

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct slice {
    void   *ptr;
    int64_t size;
} slice_t;

typedef struct dynarr {
    void   *ptr;
    int64_t size;
    int64_t capacity;
} dynarr_t;

typedef struct array {
    void   *ptr;
    int64_t size;
} array_t;

typedef struct enum_def {
    uint64_t num_values;
    struct {
        uint64_t value;
        int64_t  tag_len;
    } values[];
} enum_def_t;

size_t dynarr_append(dynarr_t *arr, slice_t const slice, size_t elem_size);
void   dynarr_clear(dynarr_t *arr);
void   dynarr_free(dynarr_t *arr);

size_t      utf32_length_for_utf8_slice(slice_t slice);
size_t      utf32_length_for_cstring(char const *c_string);
size_t      utf8_length_for_utf32_slice(slice_t slice);
slice_t     to_utf8(slice_t utf32);
slice_t     to_utf32(slice_t utf8);
slice_t     cstring_to_string(char const *cstring);
char const *string_to_cstring(slice_t string);

// Generated code calls these by the cathode$ names via QBE IR
extern size_t            cathode$fputs(int fd, wchar_t const *ptr, int64_t len);
extern size_t            cathode$fendln(int fd);
extern size_t            cathode$fputln(int fd, wchar_t const *ptr, int64_t len);
extern size_t            cathode$puts(wchar_t const *ptr, int64_t len);
extern size_t            cathode$endln(void);
extern size_t            cathode$putln(wchar_t const *ptr, int64_t len);
extern size_t            cathode$eputs(wchar_t const *ptr, int64_t len);
extern size_t            cathode$eputln(wchar_t const *ptr, int64_t len);
extern void              cathode$assert(bool assertion, wchar_t const *ptr, int64_t len);
[[noreturn]] extern void cathode$abort(wchar_t const *ptr, int64_t len);
extern size_t            cathode$putint(int64_t i);
extern size_t            cathode$putuint(uint64_t i);
extern int               cathode$enum_tag(enum_def_t *enum_def, uint64_t value, slice_t *ret);

#define ALIGNAT(bytes, align) ((bytes + (align - 1)) & ~(align - 1))
#define AS_SLICE(dynarr) (*(slice_t *) (&dynarr))

#ifdef __cplusplus
}
#endif

#endif /* __CATHODE_H__ */
