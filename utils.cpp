#include "utils.hpp"

u64 hash_str(const std::string &s)
{
    // portable hash
    size_t h = std::hash<std::string>{}(s);
    return (u64)h;
}