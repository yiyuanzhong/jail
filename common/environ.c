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

#include "environ.h"

#include <string.h>

typedef struct {
    const char *what;
    int prefix;
} good_t;

static good_t GOOD[] = {
    {"SSH_CONNECTION",      0},
    {"SSH_CLIENT",          0},
    {"SSH_TTY",             0},
    {"TERM",                0},
    {"LANG",                0},
    {"LC_",                 1},
};

int environ_permitted(const char *env)
{
    good_t *p;
    size_t i;
    size_t l;

    for (i = 0, p = GOOD; i < sizeof(GOOD) / sizeof(*GOOD); ++i, ++p) {
        l = strlen(p->what);
        if (memcmp(env, p->what, l)) {
            continue;
        }

        if (p->prefix) {
            return 1;
        }

        if (env[l] == '=') {
            return 1;
        }
    }

    return 0;
}
