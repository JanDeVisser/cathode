/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <rt/cathode.h>

size_t dynarr_append(dynarr_t *arr, slice_t const slice, size_t elem_size)
{
    size_t sz = ALIGNAT(elem_size, 8);
    assert(arr != NULL);
    if (arr->ptr == NULL) {
        arr->ptr = malloc(16 * sz);
        assert(arr->ptr != NULL);
        arr->size = 0;
        arr->capacity = 16;
    }
    size_t const required = arr->size + slice.size + 1;
    if (arr->capacity < required) {
        int64_t new_cap = arr->capacity * (int64_t) 1.6;
        while (new_cap < required) {
            new_cap *= (int64_t) 1.6;
        }
        void *p = realloc(arr->ptr, new_cap * sz);
        assert(p);
        arr->ptr = p;
        arr->capacity = new_cap;
        memset(arr->ptr + arr->size, 0, (arr->capacity - arr->size) * sz);
    }
    memcpy(arr->ptr + arr->size, slice.ptr, slice.size * sz);
    arr->size += slice.size;
    return arr->size;
}

void dynarr_clear(dynarr_t *arr)
{
    arr->size = 0;
}

void dynarr_free(dynarr_t *arr)
{
    if (arr->ptr) {
        free(arr->ptr);
    }
    arr->ptr = NULL;
    arr->size = 0;
    arr->capacity = 0;
}
