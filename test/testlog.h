#pragma once

#include <sstream>
#include <string>
#include <functional>
#include <gtest/gtest.h>

class TTestLogItem
{
    typedef std::function<void(const std::string&)> TAction;
public:
    TTestLogItem(const TTestLogItem&) {}
    TTestLogItem& operator =(const TTestLogItem&) { return *this; }

    ~TTestLogItem()
    {
        if (Contents.tellp())
            Action(Contents.str());
    }

    template<typename T> TTestLogItem& operator <<(const T& value)
    {
        Contents << value;
        return *this;
    }

private:
    friend class TLoggedFixture;
    TTestLogItem(const TAction& action): Action(action) {}
    TAction Action;
    std::stringstream Contents;
};

template<> inline TTestLogItem& TTestLogItem::operator <<(const std::vector<uint8_t>& value)
{
    std::ios::fmtflags f(Contents.flags());
    Contents << std::hex << std::uppercase << std::setfill('0');
    bool first = true;
    for (uint8_t b: value) {
        if (first)
            first = false;
        else
            Contents << " ";
        Contents << std::setw(2) << int(b);
    }
    Contents.flags(f);
    return *this;
}

class TLoggedFixture: public ::testing::Test
{
public:
    virtual ~TLoggedFixture();
    TTestLogItem Emit()
    {
        return TTestLogItem([this](const std::string& s) {
                Indented() << s << std::endl;
            });
    }
    TTestLogItem Note()
    {
        return TTestLogItem([this](const std::string& s) {
                Indented() << ">>> "  << s << std::endl;
            });
    }

    void TearDown();
    static void SetExecutableName(const std::string& file_name);
    static std::string GetDataFilePath(const std::string& relative_path);

private:
    bool IsOk();
    std::stringstream& Indented();
    std::string GetLogFileName(const std::string& suffix = "") const;
    std::stringstream Contents;
    int IndentLevel = 0;
    static std::string BaseDir;
    friend class TTestLogIndent;
    const int IndentBasicOffset = 4;
};

class TTestLogIndent
{
public:
    TTestLogIndent(TLoggedFixture& fixture): Fixture(fixture)
    {
        ++Fixture.IndentLevel;
    }
    ~TTestLogIndent()
    {
        --Fixture.IndentLevel;
    }
private:
    TLoggedFixture& Fixture;
};
