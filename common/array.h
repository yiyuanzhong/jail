#ifndef __ARRAY_H__
#define __ARRAY_H__

#include <stdio.h>

typedef struct {
    char **array;
    size_t count;
    size_t capacity;
} array_t;

/* Either array_initialize(3) or array_parse(3) before calling anything else. */
extern void array_initialize(array_t *array);
extern int array_push(array_t *array, char *str);

/* If you array_push(3)ed. */
extern void array_release(array_t *array);

extern ssize_t array_parse(array_t *array, void *buffer, size_t length);

/* If you array_parse(3)ed. */
extern void array_cleanup(array_t *array);

/* How many elements in there? */
extern size_t array_get_count(array_t *array);
extern char *array_get_element(array_t *array, size_t index);

/* How many bytes does it need to serialize to buffer? */
extern size_t array_get_size(array_t *array);
extern ssize_t array_serialize(array_t *array, void *buffer, size_t length);

#endif /* __ARRAY_H__ */
