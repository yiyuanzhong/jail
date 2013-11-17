#include "array.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void array_initialize(array_t *array)
{
    assert(array);

    array->capacity = 0;
    array->array = NULL;
    array->count = 0;
}

void array_release(array_t *array)
{
    size_t i;

    assert(array);

    for (i = 0; i < array->count; ++i) {
        free(array->array[i]);
    }

    free(array->array);
}

void array_cleanup(array_t *array)
{
    assert(array);
    free(array->array);
}

/** <0 if invalid, or size of array. */
static ssize_t array_sanity_test(char *array, size_t size)
{
    ssize_t result;
    size_t len;
    char *p;

    result = 0;
    p = array;
    len = 0;

    while (*p) {
        while (*p++) {
            ++len;
            if (len >= size) {
                return -1;
            }
        }

        ++len;
        if (len >= size) {
            return -1;
        }

        ++result;
    }

    return result;
}

ssize_t array_parse(array_t *array, void *buffer, size_t size)
{
    ssize_t count;
    ssize_t i;
    char *arr;
    char *p;

    assert(array);
    assert(buffer);

    if (size == 0) {
        return -1;
    }

    arr = (char *)buffer;
    count = array_sanity_test(arr, size);
    if (count < 0) {
        return -1;
    }

    array->count = (size_t)count;
    array->array = (char **)malloc(sizeof(char *) * count);
    if (!array->array) {
        return -1;
    }

    for (i = 0, p = arr; i < count; ++i) {
        array->array[i] = p;

        /* Skip a whole string including trailing zero. */
        while (*p++);
    }

    return p - arr + 1;
}

size_t array_get_count(array_t *array)
{
    assert(array);
    return array->count;
}

char *array_get_element(array_t *array, size_t index)
{
    assert(array);
    if (index >= array->count) {
        return NULL;
    }

    return array->array[index];
}

size_t array_get_size(array_t *array)
{
    size_t size;
    size_t i;

    assert(array);
    size = 0;

    for (i = 0; i < array->count; ++i) {
        size += strlen(array->array[i]);
        size += 1;
    }

    size += 1;
    return size;
}

ssize_t array_serialize(array_t *array, void *buffer, size_t length)
{
    size_t size;
    size_t now;
    char *arr;
    size_t i;
    char *p;

    assert(array);

    arr = (char *)buffer;
    size = 0;
    p = arr;

    for (i = 0; i < array->count; ++i) {
        now = strlen(array->array[i]);
        now += 1;
        size += now;
        if (size > length) {
            return -1;
        }

        memcpy(p, array->array[i], now);
        p += now;
    }

    size += 1;
    if (size > length) {
        return -1;
    }

    *p = '\0';
    return (ssize_t)size;
}

int array_push(array_t *array, char *str)
{
    size_t s;
    char **a;
    char *p;

    assert(array);
    p = strdup(str);
    if (!p) {
        return -1;
    }

    if (array->count == array->capacity) {
        if (array->count == 0) {
            s = 10;
        } else {
            s = array->count * 2;
        }

        a = (char **)realloc(array->array, sizeof(char *) * s);
        if (!a) {
            free(p);
            return -1;
        }

        array->capacity = s;
        array->array = a;
    }

    array->array[array->count] = p;
    array->count += 1;
    return 0;
}
