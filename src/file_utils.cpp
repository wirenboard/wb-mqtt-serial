#include "file_utils.h"
#include "log.h"
#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <unistd.h>

#define LOG(logger) ::logger.Log() << "[file utils] "

TNoDirError::TNoDirError(const std::string& msg): std::runtime_error(msg)
{}

bool TryOpen(const std::vector<std::string>& fnames, std::ifstream& file)
{
    for (auto& fname: fnames) {
        file.open(fname);
        if (file.is_open()) {
            return true;
        }
        file.clear();
    }
    return false;
}

void WriteToFile(const std::string& fileName, const std::string& value)
{
    std::ofstream f;
    OpenWithException(f, fileName);
    f << value;
}

namespace
{
    bool SyncFile(const std::string& fileName)
    {
        int fd = open(fileName.c_str(), O_RDONLY);
        if (fd < 0) {
            return false;
        }
        int res = fsync(fd);
        close(fd);
        return res == 0;
    }
}

void WriteFileAtomically(const std::string& fileName, const std::string& content)
{
    std::filesystem::path path(fileName);
    if (std::filesystem::is_symlink(path)) {
        path = std::filesystem::canonical(path);
    }
    auto tmpFileName = path.string() + ".tmp";
    try {
        {
            std::ofstream file;
            OpenWithException(file, tmpFileName);
            file << content;
            file.flush();
            if (!file.good()) {
                throw std::runtime_error("Failed to write file: " + tmpFileName);
            }
        }
        // Closing the file leaves the data in the kernel cache, without the sync the renamed file
        // may be empty after a power loss
        if (!SyncFile(tmpFileName)) {
            throw std::runtime_error("Failed to sync file: " + tmpFileName);
        }
        std::filesystem::rename(tmpFileName, path);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(tmpFileName, ec);
        throw;
    }
    // The rename is in the kernel cache too, without the sync the old file may come back after a power loss.
    // Nothing is known about the disk after a failed sync, the file is replaced for the readers anyway
    auto dirName = path.parent_path().empty() ? std::string(".") : path.parent_path().string();
    if (!SyncFile(dirName)) {
        LOG(Warn) << "Failed to sync directory " << dirName << ", " << path.string()
                  << " may be old after a power loss";
    }
}

void IterateDir(const std::string& dirName, std::function<bool(const std::string&)> fn)
{
    try {
        const std::filesystem::path dirPath{dirName};

        for (const auto& entry: std::filesystem::directory_iterator(dirPath)) {
            const auto filenameStr = entry.path().filename().string();
            if (fn(filenameStr)) {
                return;
            }
        }
    } catch (std::filesystem::filesystem_error const& ex) {
        throw TNoDirError(ex.what());
    }
}

std::string IterateDirByPattern(const std::string& dirName,
                                const std::string& pattern,
                                std::function<bool(const std::string&)> fn,
                                bool sort)
{
    if (sort) {
        std::vector<std::string> files;
        IterateDir(dirName, [&](const auto& name) {
            if (name.find(pattern) != std::string::npos) {
                files.push_back(dirName + "/" + name);
            }
            return false;
        });
        std::sort(files.begin(), files.end());
        for (const auto& fileName: files) {
            if (fn(fileName)) {
                return fileName;
            }
        }
        return std::string();
    }
    std::string res;
    IterateDir(dirName, [&](const auto& name) {
        if (name.find(pattern) != std::string::npos) {
            std::string d(dirName + "/" + name);
            if (fn(d)) {
                res = d;
                return true;
            }
        }
        return false;
    });
    return res;
}
