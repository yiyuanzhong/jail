/* Copyright 2014 yiyuanzhong@gmail.com (Yiyuan Zhong)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "message.h"

typedef struct {
    size_t   buflen;
    char     buffer[65536];
} buffer_t;

extern void buffer_initialize(buffer_t *buffer);
extern void *buffer_get_buffer(buffer_t *buffer);
extern size_t buffer_get_buffer_space(buffer_t *buffer);
extern void buffer_push_buffer(buffer_t *buffer, size_t size);

/* <0 for error, 0 for OK, >0 for not yet. */
extern int buffer_get_message(buffer_t *buffer, message_t **msg);
extern void buffer_consume_message(buffer_t *buffer, const message_t *msg);

#endif /* __BUFFER_H__ */
