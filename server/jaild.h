#ifndef __SERVER_H__
#define __SERVER_H__

extern int child_main(int c);

extern uid_t       g_args_uid;
extern gid_t       g_args_gid;
extern const char *g_args_name;
extern const char *g_args_shell;

#endif /* __SERVER_H__ */
