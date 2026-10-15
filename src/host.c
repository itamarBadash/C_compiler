#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

static char *without_last_part(char *path) {
  char *slash = strrchr(path, '/');
  char *backslash = strrchr(path, '\\');
  if (backslash > slash)
    slash = backslash;
  if (slash)
    *slash = '\0';
  return path;
}

char *host_install_root(void) {
  char path[4096];
#ifdef _WIN32
  DWORD length = GetModuleFileNameA(NULL, path, sizeof path);
  if (length == 0 || length >= sizeof path)
    return NULL;
#else
  ssize_t length = readlink("/proc/self/exe", path, sizeof path - 1);
  if (length <= 0)
    return NULL;
  path[length] = '\0';
#endif
  without_last_part(without_last_part(path));
  char *root = (char *)malloc(strlen(path) + 1);
  if (root)
    strcpy(root, path);
  return root;
}

#ifdef _WIN32
static char *command_line(const char *const *argv) {
  size_t size = 1;
  for (int i = 0; argv[i]; i++)
    size += 2 * strlen(argv[i]) + 3;
  char *line = (char *)malloc(size);
  if (!line)
    return NULL;
  char *out = line;
  for (int i = 0; argv[i]; i++) {
    const char *arg = argv[i];
    if (i)
      *out++ = ' ';
    *out++ = '"';
    int backslashes = 0;
    for (; *arg; arg++) {
      if (*arg == '\\') {
        backslashes++;
      } else if (*arg == '"') {
        for (int k = 0; k < 2 * backslashes + 1; k++)
          *out++ = '\\';
        backslashes = 0;
      } else {
        for (int k = 0; k < backslashes; k++)
          *out++ = '\\';
        backslashes = 0;
      }
      if (*arg != '\\')
        *out++ = *arg;
    }
    for (int k = 0; k < 2 * backslashes; k++)
      *out++ = '\\';
    *out++ = '"';
  }
  *out = '\0';
  return line;
}

int host_run(const char *const *argv) {
  char *line = command_line(argv);
  if (!line)
    return -1;
  STARTUPINFOA startup;
  PROCESS_INFORMATION process;
  memset(&startup, 0, sizeof startup);
  startup.cb = sizeof startup;
  int started = CreateProcessA(argv[0], line, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process);
  free(line);
  if (!started)
    return -1;
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  CloseHandle(process.hThread);
  return (int)code;
}
#else
int host_run(const char *const *argv) {
  pid_t pid;
  if (posix_spawn(&pid, argv[0], NULL, NULL, (char *const *)argv, environ) != 0)
    return -1;
  int status;
  if (waitpid(pid, &status, 0) < 0)
    return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif

char *host_temporary_file(const char *extension) {
  static int counter;
#ifdef _WIN32
  char directory[MAX_PATH + 1];
  if (GetTempPathA(sizeof directory, directory) == 0)
    return NULL;
  int id = _getpid();
#else
  const char *directory = "/tmp/";
  int id = (int)getpid();
#endif
  size_t size = strlen(directory) + strlen(extension) + 32;
  char *path = (char *)malloc(size);
  if (path)
    snprintf(path, size, "%scc99-%d-%d%s", directory, id, counter++, extension);
  return path;
}

int host_file_exists(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file)
    return 0;
  fclose(file);
  return 1;
}

const char *host_executable_suffix(void) {
#ifdef _WIN32
  return ".exe";
#else
  return "";
#endif
}
