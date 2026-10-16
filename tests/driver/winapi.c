#include <stdio.h>
#include <windows.h>

static volatile LONG counter;
static CRITICAL_SECTION section;
static int guarded;

static DWORD WINAPI worker(LPVOID parameter) {
  int i;
  for (i = 0; i < 1000; i++) {
    InterlockedIncrement(&counter);
    EnterCriticalSection(&section);
    guarded += *(int *)parameter;
    LeaveCriticalSection(&section);
  }
  return 7;
}

int main(void) {
  LARGE_INTEGER big;
  big.QuadPart = 0x0000000200000001LL;
  printf("large %lu %ld\n", (unsigned long)big.LowPart, (long)big.HighPart);
  big.u.LowPart = 5;
  printf("quad %lld\n", (long long)big.QuadPart);

  SetLastError(42);
  printf("last error %lu\n", (unsigned long)GetLastError());
  printf("length %d\n", lstrlenA("windows"));

  WCHAR wide[16];
  int units = MultiByteToWideChar(CP_UTF8, 0, "abc", -1, wide, 16);
  printf("wide %d %d\n", units, (int)wide[1]);

  SYSTEM_INFO info;
  GetSystemInfo(&info);
  printf("page %lu\n", (unsigned long)info.dwPageSize);

  LONG target = 3;
  LONG previous = InterlockedCompareExchange(&target, 9, 3);
  printf("exchange %ld %ld\n", (long)previous, (long)target);
  MemoryBarrier();
  YieldProcessor();

  InitializeCriticalSection(&section);
  int step = 2;
  HANDLE threads[2];
  int i;
  for (i = 0; i < 2; i++)
    threads[i] = CreateThread(NULL, 0, worker, &step, 0, NULL);
  WaitForMultipleObjects(2, threads, TRUE, INFINITE);
  DWORD code;
  GetExitCodeThread(threads[0], &code);
  for (i = 0; i < 2; i++)
    CloseHandle(threads[i]);
  DeleteCriticalSection(&section);
  printf("threads %ld %d exit %lu\n", (long)counter, guarded, (unsigned long)code);
  return 0;
}
