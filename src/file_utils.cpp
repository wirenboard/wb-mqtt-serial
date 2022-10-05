#include "file_utils.h"

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <iomanip>
#include <experimental/filesystem>

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

void IterateDir(const std::string& dirName, std::function<bool(const std::string&)> fn)
{
    try {
        const std::experimental::filesystem::path dirPath{dirName};

        for (const auto& entry: std::experimental::filesystem::directory_iterator(dirPath)) {
            const auto filenameStr = entry.path().filename().string();
            if (fn(filenameStr)) {
                return;
            }
        }
    } catch (std::experimental::filesystem::filesystem_error const& ex) {
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
