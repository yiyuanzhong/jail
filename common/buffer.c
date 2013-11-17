#include <assert.h>
#include <string.h>

#include "buffer.h"

void buffer_initialize(buffer_t *buffer)
{
    assert(buffer);
    buffer->buflen = 0;
}

size_t buffer_get_buffer_space(buffer_t *buffer)
{
    assert(buffer);
    assert(buffer->buflen <= sizeof(buffer->buffer));
    return sizeof(buffer->buffer) - buffer->buflen;
}

void *buffer_get_buffer(buffer_t *buffer)
{
    assert(buffer);
    return buffer->buffer + buffer->buflen;
}

int buffer_get_message(buffer_t *buffer, message_t **msg)
{
    message_t *m;

    assert(buffer);
    assert(msg);

    if (buffer->buflen < sizeof(message_t)) {
        return 1;
    }

    m = (message_t *)buffer->buffer;
    if (m->length > sizeof(buffer->buffer)) {
        return -1;
    } else if (m->length > buffer->buflen) {
        return 1;
    }

    *msg = m;
    return 0;
}

void buffer_consume_message(buffer_t *buffer, const message_t *msg)
{
    size_t length;

    assert(buffer);
    assert(msg);

    length = msg->length;
    if (length == buffer->buflen) {
        buffer->buflen = 0;
        return;
    }

    memmove(buffer->buffer, buffer->buffer + length, buffer->buflen - length);
    buffer->buflen -= length;
}

void buffer_push_buffer(buffer_t *buffer, size_t size)
{
    assert(size <= buffer_get_buffer_space(buffer));
    buffer->buflen += size;
}
