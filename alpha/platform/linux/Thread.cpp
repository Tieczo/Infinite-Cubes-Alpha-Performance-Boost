// StartThread and the Win32 handle functions of windows.h for the native Linux build: threads are
// std::threads, files are stdio streams.
#include "Thread.h"

#include <atomic>
#include <string>
#include <sys/resource.h>
#include <unistd.h>

namespace {

struct ThreadHandle : CompatHandle {
    std::thread thread;
    std::atomic<pid_t> id{0}; // the kernel's thread id, set when the thread starts

    ~ThreadHandle() override
    {
        if (thread.joinable())
            thread.detach();
    }
    DWORD wait(DWORD milliseconds) override
    {
        if (milliseconds != INFINITE)
            return WAIT_FAILED;
        if (thread.joinable() && thread.get_id() != std::this_thread::get_id())
            thread.join();
        return WAIT_OBJECT_0;
    }
};

struct FileHandle : CompatHandle {
    FILE *file;
    explicit FileHandle(FILE *file) : file(file) {}
    ~FileHandle() override { fclose(file); }
};

}

HANDLE StartThread(const std::function<void()> &function)
{
    ThreadHandle *handle = new ThreadHandle;
    handle->thread = std::thread([handle, function]() {
        handle->id = gettid();
        function();
    });
    return handle;
}

// Below-normal threads get a higher nice value (the scheduler gives them less time); raising the
// priority above normal is not allowed for normal users and is ignored.
BOOL SetThreadPriority(HANDLE thread, int priority)
{
    ThreadHandle *handle = static_cast<ThreadHandle *>(thread);
    while (!handle->id)
        std::this_thread::yield();
    if (priority >= 0)
        return TRUE;
    return setpriority(PRIO_PROCESS, handle->id, -priority * 5) == 0;
}

HANDLE CreateFileW(const wchar_t *name, DWORD, DWORD, void *, DWORD, DWORD, HANDLE)
{
    std::string path;
    for (const wchar_t *c = name; *c; c++)
        path += *c == L'\\' ? '/' : (char)*c;
    FILE *file = fopen(path.c_str(), "rb");
    return file ? new FileHandle(file) : INVALID_HANDLE_VALUE;
}

DWORD SetFilePointer(HANDLE file, LONG distance, LONG *, DWORD method)
{
    FILE *stream = static_cast<FileHandle *>(file)->file;
    if (fseek(stream, distance, (int)method) != 0)
        return INVALID_SET_FILE_POINTER;
    return (DWORD)ftell(stream);
}

BOOL ReadFile(HANDLE file, void *buffer, DWORD size, DWORD *read, void *)
{
    size_t count = fread(buffer, 1, size, static_cast<FileHandle *>(file)->file);
    if (read)
        *read = (DWORD)count;
    return TRUE;
}
