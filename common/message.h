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

#ifndef __MESSAGE_H__
#define __MESSAGE_H__

#include <pty.h>
#include <stdint.h>

typedef enum {
    TypeForkSocket,
    TypeForkPipe,
    TypeForkTty,
    TypeWinsize,
    TypeStdIn,
    TypeStdOut,
    TypeStdErr,
    TypeStatus,
} type_t;

#pragma pack(push)
#pragma pack(1)

typedef struct {
    uint16_t        type;
    uint16_t        length;
    char            body[1];
} message_t;

typedef struct {
    uint16_t        type;
    uint16_t        length;
    struct winsize  winp;
} message_winsize_t;

typedef struct {
    uint16_t        type;
    uint16_t        length;
    struct termios  termp;
    struct winsize  winp;
    char            env[1];
} message_fork_tty_t;

typedef struct {
    uint16_t        type;
    uint16_t        length;
    char            env[1];
} message_fork_pipe_t;

typedef message_fork_pipe_t message_fork_socket_t;

typedef struct {
    uint16_t        type;
    uint16_t        length;
    int             status;
} message_status_t;

typedef message_t message_stdin_t;
typedef message_t message_stdout_t;
typedef message_t message_stderr_t;

#pragma pack(pop)

#endif /* __MESSAGE_H__ */
