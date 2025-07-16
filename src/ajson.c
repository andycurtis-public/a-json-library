// SPDX-FileCopyrightText: 2019-2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0

#include "a-json-library/ajson.h"

#include "a-memory-library/aml_pool.h"

#include "the-macro-library/macro_sort.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define AJSON_NATURAL_NUMBER_CASE                                            \
  '1' : case '2' : case '3' : case '4' : case '5' : case '6' : case '7'        \
      : case '8' : case '9'

#define AJSON_NUMBER_CASE '0' : case AJSON_NATURAL_NUMBER_CASE

#define AJSON_SPACE_CASE 32 : case 9 : case 13 : case 10

#ifdef AJSON_DEBUG
#define AJSON_BAD_CHARACTER                                                  \
  line = __LINE__;                                                             \
  goto bad_character

#define AJSON_KEYED_START_STRING                                             \
  line2 = __LINE__;                                                            \
  goto keyed_start_string

#define AJSON_KEYED_ADD_STRING                                               \
  line2 = __LINE__;                                                            \
  goto keyed_add_string

#define AJSON_KEYED_NEXT_DIGIT                                               \
  line2 = __LINE__;                                                            \
  goto keyed_next_digit

#define AJSON_KEYED_DECIMAL_NUMBER                                           \
  line2 = __LINE__;                                                            \
  goto keyed_decimal_number

#define AJSON_START_KEY                                                      \
  line2 = __LINE__;                                                            \
  goto start_key

#define AJSON_START_VALUE                                                    \
  line2 = __LINE__;                                                            \
  goto start_value

#define AJSON_START_STRING                                                   \
  line2 = __LINE__;                                                            \
  goto start_string

#define AJSON_ADD_STRING                                                     \
  line2 = __LINE__;                                                            \
  goto add_string

#define AJSON_NEXT_DIGIT                                                     \
  line2 = __LINE__;                                                            \
  goto next_digit

#define AJSON_DECIMAL_NUMBER                                                 \
  line2 = __LINE__;                                                            \
  goto decimal_number
#else
#define AJSON_BAD_CHARACTER goto bad_character

#define AJSON_KEYED_START_STRING goto keyed_start_string

#define AJSON_KEYED_ADD_STRING goto keyed_add_string

#define AJSON_KEYED_NEXT_DIGIT goto keyed_next_digit

#define AJSON_KEYED_DECIMAL_NUMBER goto keyed_decimal_number

#define AJSON_START_KEY goto start_key

#define AJSON_START_VALUE goto start_value

#define AJSON_START_STRING goto start_string

#define AJSON_ADD_STRING goto add_string

#define AJSON_NEXT_DIGIT goto next_digit

#define AJSON_DECIMAL_NUMBER goto decimal_number
#endif

/**
 * ajson_copy_valid_utf8
 *
 * Copies bytes from 'src' (length 'len') to 'dest',
 * skipping any invalid UTF-8 sequences.
 * Returns a pointer to the position after the last valid
 * byte written. (It does NOT automatically write a '\0'.)
 */
char *ajson_copy_valid_utf8(char *dest, const char *src, size_t len)
{
    size_t i = 0;
    char *d = dest;

    while (i < len) {
        unsigned char c = (unsigned char)src[i];

        // 1) Single-byte ASCII (0x00..0x7F):
        if (c < 0x80) {
            *d++ = c;
            i++;
        }
        // 2) 2-byte sequence (110xxxxx 10xxxxxx):
        else if ((c & 0xE0) == 0xC0 && (i + 1) < len) {
            unsigned char c2 = (unsigned char)src[i + 1];
            if ((c2 & 0xC0) == 0x80) {
                *d++ = c;
                *d++ = c2;
                i += 2;
            } else {
                // Invalid second byte -> skip 'c'.
                i++;
            }
        }
        // 3) 3-byte sequence (1110xxxx 10xxxxxx 10xxxxxx):
        else if ((c & 0xF0) == 0xE0 && (i + 2) < len) {
            unsigned char c2 = (unsigned char)src[i + 1];
            unsigned char c3 = (unsigned char)src[i + 2];
            if (((c2 & 0xC0) == 0x80) &&
                ((c3 & 0xC0) == 0x80))
            {
                *d++ = c;
                *d++ = c2;
                *d++ = c3;
                i += 3;
            } else {
                i++;
            }
        }
        // 4) 4-byte sequence (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx):
        else if ((c & 0xF8) == 0xF0 && (i + 3) < len) {
            unsigned char c2 = (unsigned char)src[i + 1];
            unsigned char c3 = (unsigned char)src[i + 2];
            unsigned char c4 = (unsigned char)src[i + 3];
            if (((c2 & 0xC0) == 0x80) &&
                ((c3 & 0xC0) == 0x80) &&
                ((c4 & 0xC0) == 0x80))
            {
                *d++ = c;
                *d++ = c2;
                *d++ = c3;
                *d++ = c4;
                i += 4;
            } else {
                i++;
            }
        }
        else {
            // Not a valid UTF-8 start byte -> skip it.
            i++;
        }
    }

    return d;  // Return the pointer to the *end* of what we wrote
}

/**
 * Appends the contents of 'src' (length 'len') to the aml_buffer 'bh',
 * skipping bytes that do not form valid UTF-8.
 */
static void ajson_buffer_append_valid_utf8(aml_buffer_t *bh,
                                           const char *src,
                                           size_t len)
{
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)src[i];

        // ASCII bytes [0..0x7F] are always valid.
        if (c < 0x80) {
            aml_buffer_appendc(bh, c);
            i++;
        }
        // 2-byte sequence?  110xxxxx 10xxxxxx
        else if ((c & 0xE0) == 0xC0 && (i + 1) < len) {
            unsigned char c2 = (unsigned char)src[i+1];
            if ((c2 & 0xC0) == 0x80) {
                aml_buffer_appendc(bh, c);
                aml_buffer_appendc(bh, c2);
                i += 2;
            } else {
                // Invalid second byte; skip the first one.
                i++;
            }
        }
        // 3-byte sequence?  1110xxxx 10xxxxxx 10xxxxxx
        else if ((c & 0xF0) == 0xE0 && (i + 2) < len) {
            unsigned char c2 = (unsigned char)src[i+1];
            unsigned char c3 = (unsigned char)src[i+2];
            if (((c2 & 0xC0) == 0x80) && ((c3 & 0xC0) == 0x80)) {
                aml_buffer_appendc(bh, c);
                aml_buffer_appendc(bh, c2);
                aml_buffer_appendc(bh, c3);
                i += 3;
            } else {
                // Something invalid in the continuation bytes
                i++;
            }
        }
        // 4-byte sequence?  11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        else if ((c & 0xF8) == 0xF0 && (i + 3) < len) {
            unsigned char c2 = (unsigned char)src[i+1];
            unsigned char c3 = (unsigned char)src[i+2];
            unsigned char c4 = (unsigned char)src[i+3];
            if (((c2 & 0xC0) == 0x80) &&
                ((c3 & 0xC0) == 0x80) &&
                ((c4 & 0xC0) == 0x80)) {
                aml_buffer_appendc(bh, c);
                aml_buffer_appendc(bh, c2);
                aml_buffer_appendc(bh, c3);
                aml_buffer_appendc(bh, c4);
                i += 4;
            } else {
                i++;
            }
        }
        else {
            // If none of the above matched, it's invalid. Skip it.
            i++;
        }
    }
}

/**
 * ajson_strip_invalid_utf8_inplace
 *
 * Modifies the given buffer 'str' of length 'len' so that
 * it contains only valid UTF-8 bytes. Invalid bytes are dropped.
 * Returns the new length (number of valid bytes).
 *
 * The buffer *is* mutated in-place, and a final '\0' is *not* appended
 * automatically (unless you want to do so yourself).
 */
size_t ajson_strip_invalid_utf8_inplace(char *str, size_t len)
{
    size_t in_i = 0;    // read index
    size_t out_i = 0;   // write index

    while (in_i < len) {
        unsigned char c = (unsigned char)str[in_i];

        // 1) ASCII range [0..0x7F]
        if (c < 0x80) {
            str[out_i++] = c;
            in_i++;
        }
        // 2) 2-byte sequence?
        else if ((c & 0xE0) == 0xC0 && (in_i + 1) < len) {
            unsigned char c2 = (unsigned char)str[in_i + 1];
            if ((c2 & 0xC0) == 0x80) {
                // valid 2-byte
                str[out_i++] = c;
                str[out_i++] = c2;
                in_i += 2;
            } else {
                // invalid -> skip first byte
                in_i++;
            }
        }
        // 3) 3-byte sequence?
        else if ((c & 0xF0) == 0xE0 && (in_i + 2) < len) {
            unsigned char c2 = (unsigned char)str[in_i + 1];
            unsigned char c3 = (unsigned char)str[in_i + 2];
            if (((c2 & 0xC0) == 0x80) && ((c3 & 0xC0) == 0x80)) {
                str[out_i++] = c;
                str[out_i++] = c2;
                str[out_i++] = c3;
                in_i += 3;
            } else {
                in_i++;
            }
        }
        // 4) 4-byte sequence?
        else if ((c & 0xF8) == 0xF0 && (in_i + 3) < len) {
            unsigned char c2 = (unsigned char)str[in_i + 1];
            unsigned char c3 = (unsigned char)str[in_i + 2];
            unsigned char c4 = (unsigned char)str[in_i + 3];
            if (((c2 & 0xC0) == 0x80) &&
                ((c3 & 0xC0) == 0x80) &&
                ((c4 & 0xC0) == 0x80)) {
                str[out_i++] = c;
                str[out_i++] = c2;
                str[out_i++] = c3;
                str[out_i++] = c4;
                in_i += 4;
            } else {
                in_i++;
            }
        }
        else {
            // invalid start byte
            in_i++;
        }
    }

    // If you *want* to ensure NUL-termination, do so here (optional):
    // str[out_i] = '\0';

    return out_i;
}


static void ajson_dump_object_to_buffer(aml_buffer_t *bh, _ajsono_t *a);
static void ajson_dump_array_to_buffer(aml_buffer_t *bh, _ajsona_t *a);

void ajson_dump_to_buffer(aml_buffer_t *bh, ajson_t *a) {
  if (a->type >= AJSON_STRING) {
    if (a->type == AJSON_STRING) {
      aml_buffer_appendc(bh, '\"');
	  ajson_buffer_append_valid_utf8(bh, a->value, a->length);
      // aml_buffer_append(bh, a->value, a->length);
      aml_buffer_appendc(bh, '\"');
    } else
      aml_buffer_append(bh, a->value, a->length);
  } else if (a->type == AJSON_OBJECT) {
    ajson_dump_object_to_buffer(bh, (_ajsono_t *)a);
  } else if (a->type == AJSON_ARRAY) {
    ajson_dump_array_to_buffer(bh, (_ajsona_t *)a);
  } else if (a->type == AJSON_BINARY) {
    aml_buffer_append(bh, "nb", 2);
    uint32_t len = a->length;
    aml_buffer_append(bh, &len, sizeof(len));
    aml_buffer_append(bh, a->value, a->length);
  }
}

static void ajson_dump_object_to_buffer(aml_buffer_t *bh, _ajsono_t *a) {
  aml_buffer_appendc(bh, '{');
  ajsono_t *n = a->head;
  while (n) {
    ajsono_t *next = n->next;
    while (next && next->value == NULL)
      next = next->next;
    aml_buffer_appendc(bh, '\"');
    aml_buffer_appends(bh, n->key);
    aml_buffer_append(bh, "\":", 2);
    ajson_dump_to_buffer(bh, n->value);
    if (next)
      aml_buffer_appendc(bh, ',');
    n = next;
  }

  aml_buffer_appendc(bh, '}');
}

static void ajson_dump_array_to_buffer(aml_buffer_t *bh, _ajsona_t *a) {
  aml_buffer_appendc(bh, '[');
  ajsona_t *n = a->head;
  while (n) {
    ajsona_t *next = n->next;
    while (next && next->value == NULL)
      next = next->next;
    ajson_dump_to_buffer(bh, n->value);
    if (next)
      aml_buffer_appendc(bh, ',');
    n = next;
  }
  aml_buffer_appendc(bh, ']');
}

static size_t ajson_dump_object_estimate(_ajsono_t *a);
static size_t ajson_dump_array_estimate(_ajsona_t *a);

static size_t _ajson_dump_estimate(ajson_t *a) {
  size_t size = 0;
  if (a->type >= AJSON_STRING) {
    if (a->type == AJSON_STRING) {
      size += 2 + a->length;
    } else
      size += a->length;
  } else if (a->type == AJSON_OBJECT) {
    size += ajson_dump_object_estimate((_ajsono_t *)a);
  } else if (a->type == AJSON_ARRAY) {
    size += ajson_dump_array_estimate((_ajsona_t *)a);
  } else if (a->type == AJSON_BINARY) {
    size += 2 + sizeof(uint32_t) + a->length;
  }
  return size;
}

static size_t ajson_dump_object_estimate(_ajsono_t *a) {
  size_t size = 1;
  ajsono_t *n = a->head;
  while (n) {
    ajsono_t *next = n->next;
    while (next && next->value == NULL)
      next = next->next;
    size += strlen(n->key) + 3;
    size += _ajson_dump_estimate(n->value);
    if (next)
      size++;
    n = next;
  }
  return size+1;
}

static size_t ajson_dump_array_estimate(_ajsona_t *a) {
  size_t size = 1;
  ajsona_t *n = a->head;
  while (n) {
    ajsona_t *next = n->next;
    while (next && next->value == NULL)
      next = next->next;
    size += _ajson_dump_estimate(n->value);
    if (next)
      size++;
    n = next;
  }
  return size+1;
}

size_t ajson_dump_estimate(ajson_t *a) {
  return _ajson_dump_estimate(a)+1;
}

static char *ajson_dump_object_to_memory(char *s, _ajsono_t *a);
static char *ajson_dump_array_to_memory(char *s, _ajsona_t *a);

static char *_ajson_dump_to_memory(char *s, ajson_t *a) {
    if (a->type >= AJSON_STRING) {
        if (a->type == AJSON_STRING) {
            *s++ = '"';
			s = ajson_copy_valid_utf8(s, a->value, a->length);
            *s++ = '"';
        } else {
            memcpy(s, a->value, a->length);
            s += a->length;
        }
    } else if (a->type == AJSON_OBJECT) {
        s = ajson_dump_object_to_memory(s, (_ajsono_t *)a);
    } else if (a->type == AJSON_ARRAY) {
        s = ajson_dump_array_to_memory(s, (_ajsona_t *)a);
    } else if (a->type == AJSON_BINARY) {
        *s++ = '"';
        memcpy(s, a->value, a->length);
        s += a->length;
        *s++ = '"';
    }
    return s;
}

static char *ajson_dump_object_to_memory(char *s, _ajsono_t *a) {
    *s++ = '{';
    ajsono_t *n = a->head;
    int first = 1;
    while (n) {
        ajsono_t *next = n->next;
        while (next && next->value == NULL)
            next = next->next;

        if (!first) {
            *s++ = ',';
        }
        first = 0;

        *s++ = '"';
        size_t key_len = strlen(n->key);
        memcpy(s, n->key, key_len);
        s += key_len;
        *s++ = '"';
        *s++ = ':';

        s = _ajson_dump_to_memory(s, n->value);
        n = next;
    }
    *s++ = '}';
    return s;
}

static char *ajson_dump_array_to_memory(char *s, _ajsona_t *a) {
    *s++ = '[';
    ajsona_t *n = a->head;
    int first = 1;
    while (n) {
        ajsona_t *next = n->next;
        while (next && next->value == NULL)
            next = next->next;

        if (!first) {
            *s++ = ',';
        }
        first = 0;

        s = _ajson_dump_to_memory(s, n->value);
        n = next;
    }
    *s++ = ']';
    return s;
}

char *ajson_dump_to_memory(char *s, ajson_t *a) {
    s = _ajson_dump_to_memory(s, a);
    *s = 0;
    return s;
}

// deprecated
char *ajson_dump_using_pool(aml_pool_t *pool, ajson_t *a) {
    size_t size = ajson_dump_estimate(a);
    char *s = aml_pool_alloc(pool, size);
    ajson_dump_to_memory(s, a);
    return s;
}

char *ajson_stringify(aml_pool_t *pool, ajson_t *a) {
    size_t size = ajson_dump_estimate(a);
    char *s = aml_pool_alloc(pool, size);
    ajson_dump_to_memory(s, a);
    return s;
}


static void ajson_dump_object(FILE *out, _ajsono_t *a);
static void ajson_dump_array(FILE *out, _ajsona_t *a);

void ajson_dump(FILE *out, ajson_t *a) {
  if (a->type >= AJSON_STRING) {
    if (a->type == AJSON_STRING)
      fprintf(out, "\"%s\"", a->value);
    else
      fprintf(out, "%s", a->value);
  } else if (a->type == AJSON_OBJECT) {
    ajson_dump_object(out, (_ajsono_t *)a);
  } else if (a->type == AJSON_ARRAY) {
    ajson_dump_array(out, (_ajsona_t *)a);
  } else if (a->type == AJSON_BINARY) {
    fprintf(out, "b");
    uint32_t len = a->length;
    fwrite(&len, sizeof(len), 1, out);
    fwrite(a->value, a->length, 1, out);
  }
}

static void ajson_dump_object(FILE *out, _ajsono_t *a) {
  fprintf(out, "{");
  ajsono_t *n = a->head;
  while (n) {
    ajsono_t *next = n->next;
    while (next && next->value == NULL)
      next = next->next;

    fprintf(out, "\"%s\":", n->key);
    ajson_dump(out, n->value);
    if (next)
      fprintf(out, ",");
    n = next;
  }

  fprintf(out, "}");
}

static void ajson_dump_array(FILE *out, _ajsona_t *a) {
  fprintf(out, "[");
  ajsona_t *n = a->head;
  while (n) {
    ajsona_t *next = n->next;
    while (next && next->value == NULL)
      next = next->next;

    ajson_dump(out, n->value);
    if (next)
      fprintf(out, ",");
    n = next;
  }
  fprintf(out, "]");
}

static int unicode_to_utf8(char *dest, char **src) {
  char *s = *src;
  unsigned int ch;
  int c = *s++;
  if (c >= '0' && c <= '9')
    ch = c - '0';
  else if (c >= 'A' && c <= 'F')
    ch = c - 'A' + 10;
  else if (c >= 'a' && c <= 'f')
    ch = c - 'a' + 10;
  else
    return -1;

  c = *s++;
  if (c >= '0' && c <= '9')
    ch = (ch << 4) + c - '0';
  else if (c >= 'A' && c <= 'F')
    ch = (ch << 4) + c - 'A' + 10;
  else if (c >= 'a' && c <= 'f')
    ch = (ch << 4) + c - 'a' + 10;
  else
    return -1;

  c = *s++;
  if (c >= '0' && c <= '9')
    ch = (ch << 4) + c - '0';
  else if (c >= 'A' && c <= 'F')
    ch = (ch << 4) + c - 'A' + 10;
  else if (c >= 'a' && c <= 'f')
    ch = (ch << 4) + c - 'a' + 10;
  else
    return -1;

  c = *s++;
  if (c >= '0' && c <= '9')
    ch = (ch << 4) + c - '0';
  else if (c >= 'A' && c <= 'F')
    ch = (ch << 4) + c - 'A' + 10;
  else if (c >= 'a' && c <= 'f')
    ch = (ch << 4) + c - 'a' + 10;
  else
    return -1;

  if (ch >= 0xD800 && ch <= 0xDBFF) {
    if (*s != '\\' || s[1] != 'u')
      return -1;
    s += 2;
    unsigned int ch2;
    c = *s++;
    if (c >= '0' && c <= '9')
      ch2 = c - '0';
    else if (c >= 'A' && c <= 'F')
      ch2 = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
      ch2 = c - 'a' + 10;
    else
      return -1;

    c = *s++;
    if (c >= '0' && c <= '9')
      ch2 = (ch2 << 4) + c - '0';
    else if (c >= 'A' && c <= 'F')
      ch2 = (ch2 << 4) + c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
      ch2 = (ch2 << 4) + c - 'a' + 10;
    else
      return -1;

    c = *s++;
    if (c >= '0' && c <= '9')
      ch2 = (ch2 << 4) + c - '0';
    else if (c >= 'A' && c <= 'F')
      ch2 = (ch2 << 4) + c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
      ch2 = (ch2 << 4) + c - 'a' + 10;
    else
      return -1;

    c = *s++;
    if (c >= '0' && c <= '9')
      ch2 = (ch2 << 4) + c - '0';
    else if (c >= 'A' && c <= 'F')
      ch2 = (ch2 << 4) + c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
      ch2 = (ch2 << 4) + c - 'a' + 10;
    else
      return -1;
    if (ch2 < 0xDC00 || ch2 > 0xDFFF)
      return -1;
    ch = ((ch - 0xD800) << 10) + (ch2 - 0xDC00) + 0x10000;
  }

  *src = s;

  if (ch < 0x80) {
    dest[0] = (char)ch;
    return 1;
  }
  if (ch < 0x800) {
    dest[0] = (ch >> 6) | 0xC0;
    dest[1] = (ch & 0x3F) | 0x80;
    return 2;
  }
  if (ch < 0x10000) {
    dest[0] = (ch >> 12) | 0xE0;
    dest[1] = ((ch >> 6) & 0x3F) | 0x80;
    dest[2] = (ch & 0x3F) | 0x80;
    return 3;
  }
  if (ch < 0x110000) {
    dest[0] = (ch >> 18) | 0xF0;
    dest[1] = ((ch >> 12) & 0x3F) | 0x80;
    dest[2] = ((ch >> 6) & 0x3F) | 0x80;
    dest[3] = (ch & 0x3F) | 0x80;
    return 4;
  }
  return 0;
}

static inline char *_ajson_decode(aml_pool_t *pool, char **eptr, char *s, char *p,
                                    size_t length) {
  char *sp;
  char *res = (char *)aml_pool_alloc(pool, length + 1);
  size_t pos = p - s;
  memcpy(res, s, pos);
  char *rp = res + pos;
  char *ep = s + length;
  while (p < ep) {
    int ch = *p++;
    if (ch != '\\')
      *rp++ = ch;
    else {
      ch = *p++;
      switch (ch) {
      case '\"':
        *rp++ = '\"';
        break;
      case '\\':
        *rp++ = '\\';
        break;
      case '/':
        *rp++ = '/';
        break;
      case 'b':
        *rp++ = 8;
        break;
      case 'f':
        *rp++ = 12;
        break;
      case 'n':
        *rp++ = 10;
        break;
      case 'r':
        *rp++ = 13;
        break;
      case 't':
        *rp++ = 9;
        break;
      case 'u':
        sp = p - 2;
        ch = unicode_to_utf8(rp, &p);
        if (ch == -1) {
          p = sp;
          *rp++ = *p++;
          *rp++ = *p++;
          *rp++ = *p++;
          *rp++ = *p++;
          *rp++ = *p++;
          *rp++ = *p++;
        } else
          rp += ch;
        break;
      }
    }
  }
  *rp = 0;
  *eptr = rp;
  return res;
}

char *_ajson_encode(aml_pool_t *pool, char *s, char *p, size_t length) {
  char *res = (char *)aml_pool_alloc(pool, (length * 6) + 3);  // More space for unicode escapes
  char *wp = res;
  memcpy(res, s, p - s);
  wp += (p - s);
  char *ep = s + length;
  while (p < ep) {
    switch (*p) {
    case '\"': *wp++ = '\\'; *wp++ = '\"'; p++; break;
    case '\\': *wp++ = '\\'; *wp++ = '\\'; p++; break;
    case '/':  *wp++ = '\\'; *wp++ = '/';  p++; break;
    case '\b': *wp++ = '\\'; *wp++ = 'b';  p++; break;
    case '\f': *wp++ = '\\'; *wp++ = 'f';  p++; break;
    case '\n': *wp++ = '\\'; *wp++ = 'n';  p++; break;
    case '\r': *wp++ = '\\'; *wp++ = 'r';  p++; break;
    case '\t': *wp++ = '\\'; *wp++ = 't';  p++; break;

    default:
      if ((unsigned char)(*p) < 0x20) {
        sprintf(wp, "\\u%04X", (unsigned char)(*p));
        wp += 6;
        p++;
      } else {
        *wp++ = *p++;
      }
      break;
    }
  }
  *wp = 0;
  return res;
}

char *ajson_encode(aml_pool_t *pool, char *s, size_t length) {
  char *p = s;
  char *ep = p + length;
  if(*ep != 0) {
    s = aml_pool_dup(pool, s, length+1);
    s[length] = 0;
    p = s;
    ep = p + length;
  }
  while (p < ep) {
    if ((unsigned char)(*p) < 0x20 || *p == '\"' || *p == '\\' || *p == '/' || *p <= 0)
       return _ajson_encode(pool, s, p, length);
    p++;
  }
  return s;
}

char *ajson_decode(aml_pool_t *pool, char *s, size_t length) {
  char *p = s;
  char *ep = p + length;
  for (;;) {
    if (p == ep)
      return s;
    else if (*p == '\\')
      break;
    p++;
  }
  char *eptr = NULL;
  return _ajson_decode(pool, &eptr, s, p, length);
}

char *ajson_decode2(size_t *rlen, aml_pool_t *pool, char *s, size_t length) {
  char *p = s;
  char *ep = p + length;
  for (;;) {
    if (p == ep) {
      *rlen = length;
      return s;
    }
    else if (*p == '\\')
      break;
    p++;
  }
  char *eptr = NULL;
  char *r = _ajson_decode(pool, &eptr, s, p, length);
  *rlen = eptr - r;
  return r;
}


void ajson_dump_error_to_buffer(aml_buffer_t *bh, ajson_t *j) {
  ajson_error_t *err = (ajson_error_t *)j;
  char *p = err->source;
  char *ep = err->error_at;
  int row = 1;
  char *srow = p;
  while (p < ep) {
    if (*p == '\\') {
      p += 2;
      continue;
    } else if (*p == '\n') {
      p++;
      row++;
      srow = p;
    } else
      p++;
  }
#ifdef AJSON_DEBUG
  aml_buffer_appendf(bh,
                    "Error at row %d, column: %ld (%ld bytes into json) "
                    "thrown from %d/%d\n",
                    row, (p - srow) + 1, ep - err->source, err->line,
                    err->line2);
#else
  aml_buffer_appendf(bh, "Error at row %d, column: %ld (%ld bytes into json)\n",
                    row, (p - srow) + 1, ep - err->source);
#endif
}

void ajson_dump_error(FILE *out, ajson_t *j) {
  ajson_error_t *err = (ajson_error_t *)j;
  char *p = err->source;
  char *ep = err->error_at;
  int row = 1;
  char *srow = p;
  while (p < ep) {
    if (*p == '\\') {
      p += 2;
      continue;
    } else if (*p == '\n') {
      p++;
      row++;
      srow = p;
    } else
      p++;
  }
#ifdef AJSON_DEBUG
  fprintf(out,
          "Error at row %d, column: %ld (%ld bytes into json) thrown from "
          "%d/%d\n",
          row, (p - srow) + 1, ep - err->source, err->line, err->line2);
#else
  fprintf(out, "Error at row %d, column: %ld (%ld bytes into json)\n", row,
          (p - srow) + 1, ep - err->source);
#endif
}

ajson_t *ajson_parse(aml_pool_t *pool, char *p, char *ep) {
#ifdef AJSON_DEBUG
  int line, line2 = 0;
#endif
  char *sp = p;
  char ch;
  char *key = NULL;
  char *stringp = NULL;
  ajsona_t *anode;
  _ajsono_t *obj;
  _ajsona_t *arr = NULL, *arr2;
  ajson_t *j;
  ajson_t *res = NULL;
  _ajsono_t *root = NULL;

  int data_type;
  uint32_t string_length;

  if (p >= ep) {
    p++;
    AJSON_BAD_CHARACTER;
  }

  if (*p != '{')
    goto start_value;
  root = (_ajsono_t *)aml_pool_zalloc(pool, sizeof(_ajsono_t));
  root->type = AJSON_OBJECT;
  root->pool = pool;
  p++;
  res = (ajson_t *)root;

start_key:;
  if (p >= ep) {
    AJSON_BAD_CHARACTER;
  }
  ch = *p++;
  switch (ch) {
  case '\"':
    key = p;
    goto get_end_of_key;
  case AJSON_SPACE_CASE:
    AJSON_START_KEY;
  case '}':
    // if (mode == AJSON_RDONLY)
#ifdef AJSON_FILL_TEST
#ifdef AJSON_SORT_TEST
    _ajsono_fill(root);
#else
    _ajsono_fill_tree(root);
#endif
#endif
    root = (_ajsono_t *)root->parent;
    if (!root)
      return res;

    ch = *p;
    if (root->type == AJSON_OBJECT) {
      goto look_for_key;
    } else {
      arr = (_ajsona_t *)root;
      goto look_for_next_object;
    }
  default:
    AJSON_BAD_CHARACTER;
  };

get_end_of_key:;
  while (p < ep && *p != '\"')
    p++;
  if (p < ep) {
    if (p[-1] == '\\') {
        // if odd number of \, then skip "
        char *backtrack = p-1;
        while(*backtrack == '\\')
            backtrack--;
        if(((p-backtrack) & 1) == 0) {
            p++;
            goto get_end_of_key;
        }
    }
    *p = 0;
  }
  p++;
  while (p < ep && *p != ':')
    p++;
  p++;

start_key_object:;
  if (p >= ep) {
    AJSON_BAD_CHARACTER;
  }
  ch = *p++;
  switch (ch) {
  case '\"':
    stringp = p;
    data_type = AJSON_STRING;
    AJSON_KEYED_START_STRING;
  case AJSON_SPACE_CASE:
    goto start_key_object;
  case '{':
    obj = (_ajsono_t *)aml_pool_zalloc(pool, sizeof(_ajsono_t));
    obj->type = AJSON_OBJECT;
    obj->pool = pool;
    // obj->parent = (ajson_t *)root;
    ajsono_append((ajson_t *)root, key, (ajson_t *)obj, false);

    root = obj;
    AJSON_START_KEY;
  case '[':
    arr = (_ajsona_t *)aml_pool_zalloc(pool, sizeof(_ajsona_t));
    arr->type = AJSON_ARRAY;
    arr->pool = pool;
    // arr->parent = (ajson_t *)root;
    ajsono_append((ajson_t *)root, key, (ajson_t *)arr, false);
    root = (_ajsono_t *)arr;
    AJSON_START_VALUE;
  case '-':
    stringp = p - 1;
    ch = *p++;
    if (ch >= '1' && ch <= '9') {
      AJSON_KEYED_NEXT_DIGIT;
    } else if (ch == '0') {
      ch = *p;
      if (p + 1 < ep) {
        if (ch != '.') {
          stringp = (char *)"0";
          data_type = AJSON_ZERO;
          string_length = 1;
          AJSON_KEYED_ADD_STRING;
        } else {
          p++;
          AJSON_KEYED_DECIMAL_NUMBER;
        }
      } else {
        stringp = (char *)"0";
        data_type = AJSON_ZERO;
        string_length = 1;
        AJSON_KEYED_ADD_STRING;
      }
    }
    AJSON_BAD_CHARACTER;
  case '0':
    stringp = p - 1;
    ch = *p;
    if (p + 1 < ep) {
      if (ch != '.') {
        stringp = (char *)"0";
        data_type = AJSON_ZERO;
        string_length = 1;
        AJSON_KEYED_ADD_STRING;
      } else {
        p++;
        AJSON_KEYED_DECIMAL_NUMBER;
      }
    } else {
      stringp = (char *)"0";
      data_type = AJSON_ZERO;
      string_length = 1;
      AJSON_KEYED_ADD_STRING;
    }
  case AJSON_NATURAL_NUMBER_CASE:
    stringp = p - 1;
    AJSON_KEYED_NEXT_DIGIT;
  case 't':
    if (p + 3 >= ep) {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'r') {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'u') {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'e') {
      AJSON_BAD_CHARACTER;
    }
    stringp = (char *)"true";
    data_type = AJSON_TRUE;
    string_length = 4;
    ch = *p;
    AJSON_KEYED_ADD_STRING;
  case 'f':
    if (p + 4 >= ep) {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'a') {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'l') {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 's') {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'e') {
      AJSON_BAD_CHARACTER;
    }
    stringp = (char *)"false";
    ch = *p;
    data_type = AJSON_FALSE;
    string_length = 5;
    AJSON_KEYED_ADD_STRING;
  case 'n':
    ch = *p++;
    if (ch != 'u') {
      if (ch == 'b') {
        if (p + 3 >= ep) {
          AJSON_BAD_CHARACTER;
        }
        string_length = *(uint32_t *)(p);
        p += 4;
        if (p + string_length >= ep) {
          AJSON_BAD_CHARACTER;
        }
        data_type = AJSON_BINARY;
        stringp = p;
        p += string_length;
        ch = *p;
        AJSON_KEYED_ADD_STRING;
      }
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'l') {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'l') {
      AJSON_BAD_CHARACTER;
    }
    stringp = (char *)"null";
    ch = *p;
    data_type = AJSON_NULL;
    string_length = 4;
    AJSON_KEYED_ADD_STRING;
  default:
    AJSON_BAD_CHARACTER;
  };

keyed_start_string:;
  while (p < ep && *p != '\"')
    p++;
  if (p >= ep) {
    AJSON_BAD_CHARACTER;
  }
  if (p[-1] == '\\') {
    // if odd number of \, then skip "
    char *backtrack = p-1;
    while(*backtrack == '\\')
        backtrack--;
    if(((p-backtrack) & 1) == 0) {
        p++;
        goto keyed_start_string;
    }
  }
  *p = 0;
  string_length = p - stringp;
  p++;
  ch = *p;

keyed_add_string:;
  j = (ajson_t *)aml_pool_alloc(pool, sizeof(ajson_t));
  // j->parent = (ajson_t *)root;
  j->type = data_type;
#ifdef AJSON_DECODE_TEST
  if (data_type == AJSON_STRING)
    j->value = ajson_decode(pool, stringp, string_length);
  else
    j->value = stringp;
#else
  j->value = stringp;
#endif
  j->length = string_length;
  ajsono_append((ajson_t *)root, key, j, false);

look_for_key:;
  switch (ch) {
  case ',':
    if (p >= ep) {
      AJSON_BAD_CHARACTER;
    }
    p++;
    AJSON_START_KEY;
  case '}':
// if (mode == AJSON_RDONLY)
#ifdef AJSON_FILL_TEST
#ifdef AJSON_SORT_TEST
    _ajsono_fill(root);
#else
    _ajsono_fill_tree(root);
#endif
#endif
    root = (_ajsono_t *)root->parent;
    if (!root)
      return res;

    p++;
    ch = *p;
    if (root->type == AJSON_OBJECT) {
      goto look_for_key;
    } else {
      arr = (_ajsona_t *)root;
      goto look_for_next_object;
    }

  case AJSON_SPACE_CASE:
    p++;
    ch = *p;
    goto look_for_key;
  default:
    AJSON_BAD_CHARACTER;
  };

keyed_next_digit:;
  ch = *p++;
  while ((ch >= '0' && ch <= '9')) {
    ch = *p++;
  }

  if (ch == '.') {
    // Transition to decimal number parsing
    AJSON_KEYED_DECIMAL_NUMBER;
  }

  if (ch == 'e' || ch == 'E') {
    // Handle optional + or - sign after e/E
    ch = *p++;
    if (ch == '+' || ch == '-') {
      ch = *p++;
    }

    // Ensure valid digits after e/E
    if (ch < '0' || ch > '9') {
      AJSON_BAD_CHARACTER; // Invalid character after e/E
    }

    // Parse exponent digits
    while (ch >= '0' && ch <= '9') {
      ch = *p++;
    }
  }

  // Step back to handle the end of the number properly
  p--;
  *p = 0; // Null-terminate the string

  // At this point, the number is fully parsed
  data_type = AJSON_NUMBER;
  string_length = p - stringp;
  AJSON_KEYED_ADD_STRING;

keyed_decimal_number:;
  ch = *p++;

  // Ensure there is at least one digit after the decimal point
  if (ch < '0' || ch > '9') {
    AJSON_BAD_CHARACTER;
  }

  // Parse digits after the decimal point
  while (ch >= '0' && ch <= '9') {
    ch = *p++;
  }

  // Check for scientific notation (e/E)
  if (ch == 'e' || ch == 'E') {
    // Transition to handle scientific notation
    ch = *p++;
    if (ch == '+' || ch == '-') {
      // Handle optional + or - sign
      ch = *p++;
    }

    // Ensure valid digits after e/E
    if (ch < '0' || ch > '9') {
      AJSON_BAD_CHARACTER; // Invalid character after e/E
    }

    // Parse digits in the exponent
    while (ch >= '0' && ch <= '9') {
      ch = *p++;
    }
  }

  // Finalize the number parsing
  p--;           // Step back to handle the next character
  *p = 0;        // Null-terminate the string
  data_type = AJSON_DECIMAL; // Assign the type
  string_length = p - stringp; // Calculate the length
  AJSON_KEYED_ADD_STRING;      // Add the number to the JSON structure

start_value:;
  if (p >= ep) {
    AJSON_BAD_CHARACTER;
  }
  ch = *p++;
  switch (ch) {
  case '\"':
    stringp = p;
    data_type = AJSON_STRING;
    AJSON_START_STRING;
  case AJSON_SPACE_CASE:
    AJSON_START_VALUE;
  case '{':
    anode = (ajsona_t *)aml_pool_zalloc(pool, sizeof(ajsona_t) +
                                                   sizeof(_ajsono_t));
    obj = (_ajsono_t *)(anode + 1);
    anode->value = (ajson_t *)obj;
    if (arr) {
      arr->num_entries++;
      if (!arr->head)
        arr->head = arr->tail = anode;
      else {
        anode->previous = arr->tail;
        arr->tail->next = anode;
        arr->tail = anode;
      }
    } else
      res = (ajson_t *)obj;
    obj->type = AJSON_OBJECT;
    obj->pool = pool;

    obj->parent = (ajson_t *)arr;
    root = obj;
    AJSON_START_KEY;
  case '[':
    anode = (ajsona_t *)aml_pool_zalloc(pool, sizeof(ajsona_t) +
                                                   sizeof(_ajsona_t));
    arr2 = (_ajsona_t *)(anode + 1);
    anode->value = (ajson_t *)arr2;
    if (arr) {
      arr->num_entries++;
      if (!arr->head)
        arr->head = arr->tail = anode;
      else {
        anode->previous = arr->tail;
        arr->tail->next = anode;
        arr->tail = anode;
      }
    } else
      res = (ajson_t *)arr2;
    arr2->type = AJSON_ARRAY;
    arr2->pool = pool;
    arr2->parent = (ajson_t *)arr;
    arr = arr2;
    root = (_ajsono_t *)arr;
    AJSON_START_VALUE;
  case ']':
// if (mode == AJSON_RDONLY)
#ifdef AJSON_FILL_TEST
    _ajsona_fill(arr);
#endif
    root = (_ajsono_t *)arr->parent;
    if (!root)
      return res;
    ch = *p;
    if (root->type == AJSON_OBJECT) {
      goto look_for_key;
    } else {
      arr = (_ajsona_t *)root;
      goto look_for_next_object;
    }
  case '-':
    stringp = p - 1;
    ch = *p++;
    if (ch >= '1' && ch <= '9') {
      AJSON_NEXT_DIGIT;
    } else if (ch == '0') {
      ch = *p;
      if (p + 1 < ep) {
        if (ch != '.') {
          stringp = (char *)"0";
          data_type = AJSON_ZERO;
          string_length = 1;
          AJSON_ADD_STRING;
        } else {
          p++;
          AJSON_DECIMAL_NUMBER;
        }
      } else {
        stringp = (char *)"0";
        data_type = AJSON_ZERO;
        string_length = 1;
        AJSON_ADD_STRING;
      }
    }
    AJSON_BAD_CHARACTER;
  case '0':
    stringp = p - 1;
    ch = *p;
    if (p + 1 < ep) {
      if (ch != '.') {
        stringp = (char *)"0";
        data_type = AJSON_ZERO;
        string_length = 1;
        AJSON_ADD_STRING;
      } else {
        p++;
        AJSON_DECIMAL_NUMBER;
      }
    } else {
      stringp = (char *)"0";
      data_type = AJSON_ZERO;
      string_length = 1;
      AJSON_ADD_STRING;
    }
  case AJSON_NATURAL_NUMBER_CASE:
    stringp = p - 1;
    AJSON_NEXT_DIGIT;
  case 't':
    ch = *p++;
    if (ch != 'r') {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'u') {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'e') {
      AJSON_BAD_CHARACTER;
    }
    stringp = (char *)"true";
    data_type = AJSON_TRUE;
    string_length = 4;
    ch = *p;
    AJSON_ADD_STRING;
  case 'f':
    ch = *p++;
    if (ch != 'a') {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'l') {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 's') {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'e') {
      AJSON_BAD_CHARACTER;
    }
    stringp = (char *)"false";
    data_type = AJSON_FALSE;
    string_length = 5;
    ch = *p;
    AJSON_ADD_STRING;
  case 'n':
    ch = *p++;
    if (ch != 'u') {
      if (ch == 'b') {
        if (p + 3 >= ep) {
          AJSON_BAD_CHARACTER;
        }
        string_length = *(uint32_t *)(p);
        p += 4;
        if (p + string_length >= ep) {
          AJSON_BAD_CHARACTER;
        }
        data_type = AJSON_BINARY;
        stringp = p;
        p += string_length;
        ch = *p;
        AJSON_ADD_STRING;
      }

      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'l') {
      AJSON_BAD_CHARACTER;
    }
    ch = *p++;
    if (ch != 'l') {
      AJSON_BAD_CHARACTER;
    }
    stringp = (char *)"null";
    data_type = AJSON_NULL;
    string_length = 4;
    ch = *p;
    AJSON_ADD_STRING;

  default:
    AJSON_BAD_CHARACTER;
  };

start_string:;
  while (p < ep && *p != '\"')
    p++;
  if (p >= ep) {
    AJSON_BAD_CHARACTER;
  }
  if (p[-1] == '\\') {
    // if odd number of \, then skip "
    char *backtrack = p-1;
    while(*backtrack == '\\')
        backtrack--;
    if(((p-backtrack) & 1) == 0) {
        p++;
        goto start_string;
    }
  }
  *p = 0;
  string_length = p - stringp;
  p++;
  ch = *p;

add_string:;
  anode = (ajsona_t *)aml_pool_zalloc(pool,
                                       sizeof(ajsona_t) + sizeof(ajson_t));
  j = anode->value = (ajson_t *)(anode + 1);
  j->type = data_type;
#ifdef AJSON_DECODE_TEST
  if (data_type == AJSON_STRING)
    j->value = ajson_decode(pool, stringp, string_length);
  else
    j->value = stringp;
#else
  j->value = stringp;
#endif
  j->length = string_length;
  j->parent = (ajson_t *)arr;

  if (!arr)
    return j;

  arr->num_entries++;
  if (!arr->head)
    arr->head = arr->tail = anode;
  else {
    anode->previous = arr->tail;
    arr->tail->next = anode;
    arr->tail = anode;
  }

look_for_next_object:;
  switch (ch) {
  case ',':
    if (p >= ep) {
      AJSON_BAD_CHARACTER;
    }
    p++;
    AJSON_START_VALUE;
  case ']':
// if (mode == AJSON_RDONLY)
#ifdef AJSON_FILL_TEST
    _ajsona_fill(arr);
#endif
    root = (_ajsono_t *)arr->parent;
    if (!root)
      return res;
    p++;
    ch = *p;
    if (root->type == AJSON_OBJECT) {
      goto look_for_key;
    } else {
      arr = (_ajsona_t *)root;
      goto look_for_next_object;
    }
  case AJSON_SPACE_CASE:
    p++;
    ch = *p;
    goto look_for_next_object;
  default:
    AJSON_BAD_CHARACTER;
  };

next_digit:;
  ch = *p++;

  // Parse digits before encountering a period or e/E
  while (ch >= '0' && ch <= '9') {
    ch = *p++;
  }

  if (ch == '.') {
    // Handle the decimal portion
    ch = *p++;
    if (ch < '0' || ch > '9') {
      AJSON_BAD_CHARACTER; // Invalid number if no digits after the decimal
    }

    while (ch >= '0' && ch <= '9') {
      ch = *p++;
    }
  }

  // Check for scientific notation (e/E)
  if (ch == 'e' || ch == 'E') {
    // Transition to handle scientific notation
    ch = *p++;
    if (ch == '+' || ch == '-') {
      // Handle optional + or - sign
      ch = *p++;
    }

    // Ensure valid digits after e/E
    if (ch < '0' || ch > '9') {
      AJSON_BAD_CHARACTER; // Invalid character after e/E
    }

    // Parse digits in the exponent
    while (ch >= '0' && ch <= '9') {
      ch = *p++;
    }
  }

  // Finalize the number parsing
  p--;           // Step back to handle the next character
  *p = 0;        // Null-terminate the string
  data_type = AJSON_NUMBER; // Assign the type
  string_length = p - stringp; // Calculate the length
  AJSON_ADD_STRING;           // Add the number to the JSON structure

decimal_number:;
  ch = *p++;

  // Ensure there is at least one digit after the decimal point
  if (ch < '0' || ch > '9') {
    AJSON_BAD_CHARACTER;
  }

  // Parse digits after the decimal point
  while (ch >= '0' && ch <= '9') {
    ch = *p++;
  }

  // Check for scientific notation (e/E)
  if (ch == 'e' || ch == 'E') {
    // Transition to handle scientific notation
    ch = *p++;
    if (ch == '+' || ch == '-') {
      // Handle optional + or - sign
      ch = *p++;
    }

    // Ensure valid digits after e/E
    if (ch < '0' || ch > '9') {
      AJSON_BAD_CHARACTER; // Invalid character after e/E
    }

    // Parse digits in the exponent
    while (ch >= '0' && ch <= '9') {
      ch = *p++;
    }
  }

  // Finalize the number parsing
  p--;           // Step back to handle the next character
  *p = 0;        // Null-terminate the string
  data_type = AJSON_DECIMAL; // Assign the type
  string_length = p - stringp; // Calculate the length
  AJSON_ADD_STRING;           // Add the number to the JSON structure

bad_character:;
  ajson_error_t *err =
      (ajson_error_t *)aml_pool_alloc(pool, sizeof(ajson_error_t));
  err->type = AJSON_ERROR;
#ifdef AJSON_DEBUG
  err->line = line;
  err->line2 = line2;
#else
  err->line = 0;
  err->line2 = 0;
#endif
  err->error_at = p;
  err->source = sp;
  return (ajson_t *)err;
}

static inline bool ajson_compare(const ajsono_t **a, const ajsono_t **b) {
  return strcmp((*a)->key, (*b)->key) < 0;
}

macro_sort(__ajson_sort, ajsono_t *, ajson_compare);

void _ajsono_fill(_ajsono_t *o) {
  o->root = (macro_map_t *)aml_pool_alloc(
      o->pool, (sizeof(ajsono_t *) * (o->num_entries + 1)));
  ajsono_t **base = (ajsono_t **)o->root;
  ajsono_t **awp = base;
  ajsono_t *n = o->head;
  while (n) {
    *awp++ = n;
    n = n->next;
  }
  o->num_sorted_entries = awp - base;
  if (o->num_sorted_entries)
    __ajson_sort(base, o->num_sorted_entries);
  else
    o->root = NULL;
}
