#include "file_utils.h"

#include <cstring>
#include <dirent.h>
#include <iomanip>

TNoDirError::TNoDirError(const std::string& msg) : std::runtime_error(msg) {}

bool TryOpen(const std::vector<std::string>& fnames, std::ifstream& file)
{
    for (auto& fname : fnames) {
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
    DIR* dir = opendir(dirName.c_str());

    if (dir == NULL) {
        throw TNoDirError("Can't open directory: " + dirName);
    }

    dirent*                                 ent;
    auto                                    closeFn = [](DIR* d) { closedir(d); };
    std::unique_ptr<DIR, decltype(closeFn)> dirPtr(dir, closeFn);
    while ((ent = readdir(dirPtr.get())) != NULL) {
        if (fn(ent->d_name)) {
            return;
        }
    }
}

std::string IterateDirByPattern(const std::string&                      dirName,
                                const std::string&                      pattern,
                                std::function<bool(const std::string&)> fn)
{
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
