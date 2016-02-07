#pragma once
#include <vector>
#include <memory>

class TExpector {
public:
    virtual ~TExpector() {}
    virtual void Expect(const std::vector<int>& request,
                        const std::vector<int>& response,
                        const char* func = 0) = 0;
};

typedef std::shared_ptr<TExpector> PExpector;

class TExpectorProvider {
public:
    virtual ~TExpectorProvider() {}
    virtual PExpector Expector() const = 0;
};
