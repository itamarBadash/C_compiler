#ifndef _PREPROCESSOR_H_
#define _PREPROCESSOR_H_
#include "token_buf.h"

int pp_run(token_buf *out, const char *source);
int pp_run_ex(token_buf *out, const char *source, const char *filename, const char **include_dirs,
              int include_dir_count);
int pp_run_file(token_buf *out, const char *path, const char **include_dirs, int include_dir_count);
char *pp_splice_lines(const char *source);
#endif //_PREPROCESSOR_H_
