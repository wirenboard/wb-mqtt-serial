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

    TFilesWatcher(const std::vector<std::string>& paths, TCallback callback);
    ~TFilesWatcher();

private:
    TCallback Callback;
    std::atomic<bool> Running;
    std::thread WatcherThread;
};
