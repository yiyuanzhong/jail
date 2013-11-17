#ifndef __COMMON_CMDLINE_H__
#define __COMMON_CMDLINE_H__

#include <sys/types.h>
#include <stdlib.h>

#ifdef __unix__
# include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Initialize internal memory layouts to accept process name changing.
 * Make sure you call this method as early as you get ARGV from main() since other
 * modules might change the memory layout thus reduce the capability that long names
 * can be used. Might cause tiny but unavoidable memory leaks to accept long names.
 *
 * @return  The newly allocated argv array, use it instead of the original one.
 * @retval  NULL if the operation was a failure.
 *
 * @warning If your main() accepts char **env, point it to environ after the call.
 * @warning Memory leaks might occur, typically several KBs once and for all.
 */
extern char **cmdline_setup(int argc, char *argv[]);

#if HAVE_SETPROCTITLE
# define cmdline_set_process_name(...) (setproctitle(__VA_ARGS__),0)
#else
/**
 * @retval 0 if succeeded, -1 on error.
 * @warning Calling this method might overwrite the original ARGV.
 * @param fmt can be NULL so that the original process name is restored, however arguments
 *            are not restored, only argv[0].
 * @sa cmdline_setup()
 */
extern int cmdline_set_process_name(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
#endif

extern const char *cmdline_arg_name();
extern const char *cmdline_module_path();
extern const char *cmdline_module_dirname();
extern const char *cmdline_module_basename();

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __COMMON_CMDLINE_H__ */
