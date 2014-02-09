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

#ifndef __CMDLINE_H__
#define __CMDLINE_H__

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>

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

/**
 * @retval 0 if succeeded, -1 on error.
 * @warning Calling this method might overwrite the original ARGV.
 * @param fmt can be NULL so that the original process name is restored, however arguments
 *            are not restored, only argv[0].
 * @sa cmdline_setup()
 */
extern int cmdline_set_process_name(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

#endif /* __CMDLINE_H__ */
