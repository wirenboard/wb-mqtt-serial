#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class TFilesWatcher
{
public:
    enum class TEvent
    {
        CloseWrite,
        Delete
    };

    typedef std::function<void(std::string fileName, TFilesWatcher::TEvent event)> TCallback;

    TFilesWatcher(const std::string& path, TCallback callback);
    ~TFilesWatcher();

private:
    std::string Path;
    TCallback Callback;
    std::atomic<bool> Running;
    std::thread WatcherThread;
};
