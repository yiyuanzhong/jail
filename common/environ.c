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
