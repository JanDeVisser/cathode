/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <rt/cathode.h>

size_t utf32_length_for_utf8_slice(slice_t slice)
{
    size_t ret = 0;
    for (size_t ix = 0; ix < slice.size; ++ret) {
        uint8_t first_byte = ((char *) slice.ptr)[ix];

        int num_bytes;
        if ((first_byte & 0x80) == 0) {
            // Single byte character (0xxxxxxx)
            num_bytes = 1;
        } else if ((first_byte & 0xE0) == 0xC0) {
            // Two byte character (110xxxxx 10xxxxxx)
            num_bytes = 2;
        } else if ((first_byte & 0xF0) == 0xE0) {
            // Three byte character (1110xxxx 10xxxxxx 10xxxxxx)
            num_bytes = 3;
        } else if ((first_byte & 0xF8) == 0xF0) {
            // Four byte character (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
            num_bytes = 4;
        } else {
            // Invalid UTF-8 byte, skip it
            num_bytes = 1;
            continue;
        }
        ix += num_bytes;
    }
    return ret;
}

size_t utf32_length_for_cstring(char const *c_string)
{
    return utf32_length_for_utf8_slice((slice_t) {
        .ptr = (void *) c_string,
        .size = strlen(c_string) });
}

size_t utf8_length_for_utf32_slice(slice_t slice)
{
    size_t utf8_pos = 0;

    size_t ret = 0;
    for (size_t i = 0; i < slice.size; i++) {
        uint32_t code_point = ((wchar_t *) slice.ptr)[i];

        // Validate the code point
        if (code_point > 0x10FFFF) {
            code_point = 0xFFFD; // Replacement character for invalid code points
        }

        // Encode the code point in UTF-8
        if (code_point <= 0x7F) {
            // Single byte: 0xxxxxxx
            ++ret;
        } else if (code_point <= 0x7FF) {
            // Two bytes: 110xxxxx 10xxxxxx
            ret += 2;
        } else if (code_point <= 0xFFFF) {
            // Three bytes: 1110xxxx 10xxxxxx 10xxxxxx
            ret += 3;
        } else {
            // Four bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            ret += 4;
        }
    }
    return ret;
}

//
// Error Handling and Edge Cases
//
// 1. **Invalid sequences**: The algorithm checks for valid continuation bytes
// and uses the Unicode replacement character (U+FFFD) for invalid sequences.
//
// 2. **Byte order mark (BOM)**: The algorithm doesn't handle BOMs. If needed,
// you should check for a BOM at the beginning of the input and handle
// endianness accordingly.
//
// 3. **Memory allocation**: The function allocates memory for the worst case
// (one UTF-32 character per UTF-8 byte), which is more than needed for most
// text.
//
// 4. **Out-of-range code points**: Valid Unicode code points must be in the
// range 0x0000 - 0x10FFFF. The algorithm should reject code points outside this
// range.
//
slice_t to_utf8(slice_t utf32)
{
    slice_t ret = { 0 };
    ret.size = utf8_length_for_utf32_slice(utf32);
    ret.ptr = malloc(ret.size + 1);
    ((char *) ret.ptr)[ret.size] = 0;

    size_t utf8_pos = 0;
    for (size_t i = 0; i < utf32.size; i++) {
        uint32_t code_point = ((wchar_t *) utf32.ptr)[i];

        // Validate the code point
        if (code_point > 0x10FFFF) {
            code_point = 0xFFFD; // Replacement character for invalid code points
        }

        // Encode the code point in UTF-8
        if (code_point <= 0x7F) {
            // Single byte: 0xxxxxxx
            ((char *) ret.ptr)[utf8_pos++] = (uint8_t) code_point;
        } else if (code_point <= 0x7FF) {
            // Two bytes: 110xxxxx 10xxxxxx
            ((char *) ret.ptr)[utf8_pos++] = 0xC0 | (code_point >> 6);
            ((char *) ret.ptr)[utf8_pos++] = 0x80 | (code_point & 0x3F);
        } else if (code_point <= 0xFFFF) {
            // Three bytes: 1110xxxx 10xxxxxx 10xxxxxx
            ((char *) ret.ptr)[utf8_pos++] = 0xE0 | (code_point >> 12);
            ((char *) ret.ptr)[utf8_pos++] = 0x80 | ((code_point >> 6) & 0x3F);
            ((char *) ret.ptr)[utf8_pos++] = 0x80 | (code_point & 0x3F);
        } else {
            // Four bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            ((char *) ret.ptr)[utf8_pos++] = 0xF0 | (code_point >> 18);
            ((char *) ret.ptr)[utf8_pos++] = 0x80 | ((code_point >> 12) & 0x3F);
            ((char *) ret.ptr)[utf8_pos++] = 0x80 | ((code_point >> 6) & 0x3F);
            ((char *) ret.ptr)[utf8_pos++] = 0x80 | (code_point & 0x3F);
        }
    }
    return ret;
}

//
// Error Handling and Edge Cases
//
// 1. **Invalid sequences**: The algorithm checks for valid continuation bytes
// and uses the Unicode replacement character (U+FFFD) for invalid sequences.
//
// 2. **Byte order mark (BOM)**: The algorithm doesn't handle BOMs. If needed,
// you should check for a BOM at the beginning of the input and handle
// endianness accordingly.
//
// 3. **Memory allocation**: The function allocates memory for the worst case
// (one UTF-32 character per UTF-8 byte), which is more than needed for most
// text.
//
// 4. **Out-of-range code points**: Valid Unicode code points must be in the
// range 0x0000 - 0x10FFFF. The algorithm should reject code points outside this
// range.
//
slice_t to_utf32(slice_t utf8)
{
    slice_t ret = { 0 };
    ret.size = utf32_length_for_utf8_slice(utf8);
    ret.ptr = malloc(sizeof(wchar_t) * (ret.size + 1));
    ((wchar_t *) ret.ptr)[ret.size] = 0;
    size_t utf8_pos = 0;
    size_t utf32_pos = 0;

    while (utf8_pos < utf8.size) {
        uint32_t code_point = 0;
        uint8_t  first_byte = ((char *) utf8.ptr)[utf8_pos++];

        // Determine the number of bytes in this character
        int num_bytes;
        if ((first_byte & 0x80) == 0) {
            // Single byte character (0xxxxxxx)
            code_point = first_byte;
            num_bytes = 1;
        } else if ((first_byte & 0xE0) == 0xC0) {
            // Two byte character (110xxxxx 10xxxxxx)
            code_point = first_byte & 0x1F;
            num_bytes = 2;
        } else if ((first_byte & 0xF0) == 0xE0) {
            // Three byte character (1110xxxx 10xxxxxx 10xxxxxx)
            code_point = first_byte & 0x0F;
            num_bytes = 3;
        } else if ((first_byte & 0xF8) == 0xF0) {
            // Four byte character (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
            code_point = first_byte & 0x07;
            num_bytes = 4;
        } else {
            // Invalid UTF-8 byte, skip it
            continue;
        }

        // Process continuation bytes (if any)
        for (int i = 1; i < num_bytes && utf8_pos < utf8.size; i++) {
            uint8_t byte = ((char *) utf8.ptr)[utf8_pos++];
            if ((byte & 0xC0) != 0x80) {
                // Invalid continuation byte
                code_point = 0xFFFD; // Replacement character
                break;
            }
            // Add the 6 bits from the continuation byte
            code_point = (code_point << 6) | (byte & 0x3F);
        }

        // Store the code point in UTF-32 format
        ((wchar_t *) ret.ptr)[utf32_pos++] = code_point;
    }

    return ret;
}

slice_t cstring_to_string(char const *cstring)
{
    return to_utf32((slice_t) { (void *) cstring, strlen(cstring) });
}

char const *string_to_cstring(slice_t string)
{
    return to_utf8(string).ptr;
}
