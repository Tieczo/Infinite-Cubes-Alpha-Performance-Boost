// The part of the Win32 API the alpha uses, for the native Linux build. The basic types and COM
// declarations come from DXVK-native's windows.h (the same one its d3d9.h includes); this header adds
// critical sections, threads, timers, key codes and a few file and string helpers on top of the C++
// standard library and POSIX.
#pragma once
#include_next <windows.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <unistd.h>

#define __int64 long long
#define CALLBACK
#define INFINITE 0xffffffffu
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))

typedef unsigned long long ULONG64;

// A critical section is recursive, like Windows': a thread may enter it again.
typedef std::recursive_mutex CRITICAL_SECTION;

inline BOOL InitializeCriticalSectionAndSpinCount(CRITICAL_SECTION *, DWORD) { return TRUE; }
inline void InitializeCriticalSection(CRITICAL_SECTION *) {}
inline void DeleteCriticalSection(CRITICAL_SECTION *) {}
inline void EnterCriticalSection(CRITICAL_SECTION *section) { section->lock(); }
inline void LeaveCriticalSection(CRITICAL_SECTION *section) { section->unlock(); }
inline BOOL TryEnterCriticalSection(CRITICAL_SECTION *section) { return section->try_lock(); }

inline void Sleep(DWORD milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }

// Milliseconds since an arbitrary start, wrapping at 2^32 like timeGetTime.
inline DWORD timeGetTime()
{
    return (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
inline UINT timeBeginPeriod(UINT) { return 0; }
inline UINT timeEndPeriod(UINT) { return 0; }

inline DWORD GetLastError() { return (DWORD)errno; }

inline BOOL DeleteFileA(const char *path) { return unlink(path) == 0; }

// Handles: threads (Thread.h) and files opened by CreateFileW. CloseHandle deletes them.
struct CompatHandle {
    virtual ~CompatHandle() {}
    virtual DWORD wait(DWORD milliseconds) { return WAIT_FAILED; }
};
inline BOOL CloseHandle(HANDLE handle)
{
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return FALSE;
    delete static_cast<CompatHandle *>(handle);
    return TRUE;
}
// A null handle fails, as on Windows (the server waits for threads it may never have started).
inline DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds)
{
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return WAIT_FAILED;
    return static_cast<CompatHandle *>(handle)->wait(milliseconds);
}

#define THREAD_PRIORITY_LOWEST -2
#define THREAD_PRIORITY_BELOW_NORMAL -1
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
BOOL SetThreadPriority(HANDLE thread, int priority);

// Files, for XAudio2Engine's RIFF reader.
#define GENERIC_READ 0x80000000u
#define FILE_SHARE_READ 0x1
#define OPEN_EXISTING 3
#define FILE_BEGIN SEEK_SET
#define FILE_CURRENT SEEK_CUR
#define FILE_END SEEK_END
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? (HRESULT)(x) : (HRESULT)(((x)&0x0000FFFF) | 0x80070000))
HANDLE CreateFileW(const wchar_t *name, DWORD access, DWORD share, void *security, DWORD creation, DWORD flags,
                   HANDLE templateFile);
DWORD SetFilePointer(HANDLE file, LONG distance, LONG *distanceHigh, DWORD method);
BOOL ReadFile(HANDLE file, void *buffer, DWORD size, DWORD *read, void *overlapped);

template<size_t N> inline int strcpy_s(char (&destination)[N], const char *source)
{
    std::snprintf(destination, N, "%s", source);
    return 0;
}

// Virtual-key codes: the game's key handlers use Windows' numbering (main.cpp maps SDL keys to it).
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_NUMPAD0 0x60
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_OEM_102 0xE2
