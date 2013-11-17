/*
 * Sailor Utility - Common utilities for Project Sailor
 * Copyright 2013, Baidu Inc.  All rights reserved.
 * Author: yiyuanzhong@gmail.com (Yiyuan Zhong)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Baidu Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * $Author: zhongyiyuan01 $
 * $Date: 2013-05-04 14:07:24 +0800 (Sat, 04 May 2013) $
 * $Revision: 156527 $
 * @brief
 *
 **/

#ifndef __SAILOR_UTILITY_CMDLINE_H__
#define __SAILOR_UTILITY_CMDLINE_H__

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

#endif /* __SAILOR_UTILITY_CMDLINE_H__ */
