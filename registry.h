#pragma once
#include <map>
#include <tuple>
#include <mutex>
#include <memory>
#include <typeindex>
#include <unordered_map>

class Registry {
public:
    // Get an instance of T constructed from the specified args.
    // If called again with the same args, returns the previously
    // created instance for these args.
    // This function is thread-safe.
    template<class T, class ...Args> static std::shared_ptr<T> Intern(Args... args)
    {
        static std::mutex Mutex;
        auto Map = InternObject<std::map<std::tuple<Args...>, std::shared_ptr<T>>>();
        std::unique_lock<std::mutex> lock(Mutex);
        std::tuple<Args...> tuple(args...);
        auto it = Map->find(tuple);
        if (it == Map->end()) {
            auto p = std::make_shared<T>(args...);
            (*Map)[tuple] = p;
            return p;
        }
        return it->second;
    }

private:
    template<class T> static T* InternObject()
    {
        // We use static var here in order to avoid the need to
        // define static data member in a .cpp file.
        // See http://stackoverflow.com/questions/11709859/how-to-have-static-data-members-in-a-header-only-library
        static std::mutex Mutex;
        static std::unordered_map<std::type_index, void*> Map;
        // TBD: lock
        std::unique_lock<std::mutex> lock(Mutex);
        auto it = Map.find(typeid(T));
        if (it == Map.end()) {
            T* r = new T();
            Map[typeid(T)] = r;
            return r;
        }
        return reinterpret_cast<T*>(it->second);
    };
};
