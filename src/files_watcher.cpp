#include "files_watcher.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "log.h"

#define LOG(logger) logger.Log() << "[file watcher] "

TFilesWatcher::TFilesWatcher(const std::vector<std::string>& paths, TFilesWatcher::TCallback callback)
    : Callback(callback),
      Running(true)
{
    WatcherThread = std::thread([this, paths] {
        int fd = inotify_init();
        if (fd == -1) {
            LOG(Error) << "inotify_init failed: " << strerror(errno);
            return;
        }

        std::unordered_map<int, std::string> wdToPath;

        for (const auto& path: paths) {
            int wd = inotify_add_watch(fd, path.c_str(), IN_CLOSE_WRITE | IN_DELETE);
            if (wd == -1) {
                LOG(Error) << "inotify_add_watch failed: " << strerror(errno);
                close(fd);
                return;
            } else {
                wdToPath.emplace(wd, path);
            }
        }

        struct pollfd fds[1];
        fds[0].fd = fd;
        fds[0].events = POLLIN;

        while (Running) {
            int ret = poll(fds, 1, 1000);
            if (ret == -1) {
                LOG(Error) << "poll failed: " << strerror(errno);
                break;
            }

            if (ret == 0) {
                continue;
            }

            if (fds[0].revents & POLLIN) {
                char buf[4096];
                ssize_t len = read(fd, buf, sizeof(buf));
                if (len == -1) {
                    LOG(Error) << "read failed: " << strerror(errno);
                    break;
                }

                if (len == 0) {
                    break;
                }

                for (char* ptr = buf; ptr < buf + len;) {
                    struct inotify_event* event = (struct inotify_event*)ptr;
                    auto it = wdToPath.find(event->wd);
                    if (it != wdToPath.end()) {
                        auto fullPath = it->second + "/" + event->name;
                        switch (event->mask) {
                            case IN_CLOSE_WRITE: {
                                {
                                    LOG(Debug) << "file modified: " << fullPath;
                                    Callback(fullPath, TFilesWatcher::TEvent::CloseWrite);
                                    break;
                                }
                                case IN_DELETE: {
                                    LOG(Debug) << "file deleted: " << fullPath;
                                    Callback(fullPath, TFilesWatcher::TEvent::Delete);
                                    break;
                                }
                            }
                        }
                    }
                    ptr += sizeof(struct inotify_event) + event->len;
                }
            }
        }

        for (auto path: wdToPath) {
            inotify_rm_watch(fd, path.first);
        }
        close(fd);
    });
}

TFilesWatcher::~TFilesWatcher()
{
    Running = false;
    WatcherThread.join();
}
