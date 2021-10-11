#pragma once
#include <memory>
#include <vector>

class TExpector
{
public:
    virtual ~TExpector() = default;
    virtual void Expect(const std::vector<int>& request, const std::vector<int>& response, const char* func = 0) = 0;
};

typedef std::shared_ptr<TExpector> PExpector;

class TExpectorProvider
{
public:
    virtual ~TExpectorProvider() = default;
    virtual PExpector Expector() const = 0;
};

std::vector<int> ExpectVectorFromString(const std::string& str);