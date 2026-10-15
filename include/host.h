#ifndef _HOST_H_
#define _HOST_H_

char *host_install_root(void);
int host_run(const char *const *argv);
char *host_temporary_file(const char *extension);
int host_file_exists(const char *path);
const char *host_executable_suffix(void);

#endif
