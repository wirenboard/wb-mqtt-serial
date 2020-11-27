#pragma once

#include <fstream>
#include <functional>
#include <vector>
#include <stdexcept>

/**
 * @brief Try to open one of files listed in fnames.
 *
 * @param fnames Vector of filenames to try to open
 * @param file Reference to ifstream of openeded file.
 * @return true One of files is open succesfuly
 * @return false Nothing is open
 */
bool TryOpen(const std::vector<std::string>& fnames, std::ifstream& file);

/**
 * @brief Open file. Throw exception on failure.
 *
 * @tparam T ifstream or ofstream
 * @param f Stream to open
 * @param fileName Name of file to open
 */
template <class T> void OpenWithException(T& f, const std::string& fileName)
{
    f.open(fileName);
    if (!f.is_open()) {
        throw std::runtime_error("Can't open file:" + fileName);
    }
}

/**
 * @brief Write a string to file
 *
 * @param fileName Name of file
 * @param value Value to write
 */
void WriteToFile(const std::string& fileName, const std::string& value);

/**
 * @brief Exception class thrown on open directory failure.
 */
class TNoDirError : public std::runtime_error
{
public:
    TNoDirError(const std::string& msg);
};

/**
 * @brief Iterate over entries of the folder.
 *
 * @param dirName Folder to iterate over
 * @param fn The function calls fn with every folder entry. If fn returns true, iteration stops.
 */
void IterateDir(const std::string& dirName, std::function<bool(const std::string&)> fn);

/**
 * @brief Iterate over entries of the folder. If entry name contains pattern, execute fn with the
 * entry.
 *
 * @param dirName Folder to iterate over
 * @param pattern String what should be a part of folder entry to call fn
 * @param fn The function calls fn with matching folder entry. If fn returns true, iteration stops.
 * @return std::string Folder entry for which fn returned true or empty string
 */
std::string IterateDirByPattern(const std::string&                      dirName,
                                const std::string&                      pattern,
                                std::function<bool(const std::string&)> fn);
