#include <fstream>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <stdio.h>

#include "testlog.h"

std::string TLoggedFixture::BaseDir = ".";

TLoggedFixture::~TLoggedFixture() {}

void TLoggedFixture::TearDown()
{
    std::string file_name = GetLogFileName(".out");
    if (IsOk()) {
        if (remove(file_name.c_str()) < 0) {
            if (errno != ENOENT)
                FAIL() << "failed to remove .out file: " << file_name;
        }
        return;
    }
    std::ofstream f(file_name);
    if (!f.is_open())
        FAIL() << "cannot create file for failing logged test: " << file_name;
    else {
        f << Contents.str();
        ADD_FAILURE() << "test diff: " << GetLogFileName();
    }
}

bool TLoggedFixture::IsOk()
{
    std::ifstream f(GetLogFileName());
    if (!f)
        return !Contents.tellp();

    std::stringstream buf;
    buf << f.rdbuf();
    return buf.str() == Contents.str();
}

std::stringstream& TLoggedFixture::Indented()
{
    int num_spaces = IndentBasicOffset * IndentLevel;
    while (num_spaces--)
        Contents << ' ';
    return Contents;
}

std::string TLoggedFixture::GetLogFileName(const std::string& suffix) const
{
    const ::testing::TestInfo* const test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    return GetDataFilePath(std::string(test_info->test_case_name()) +
                           "." + test_info->name() +
                           ".dat" + suffix);
}

void TLoggedFixture::SetExecutableName(const std::string& file_name)
{
    char* s = strdup(file_name.c_str());
    BaseDir = dirname(s);
    free(s);
}

std::string TLoggedFixture::GetDataFilePath(const std::string& relative_path)
{
    return BaseDir + "/" + relative_path;
}
