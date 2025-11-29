#include "utils.hpp"

u64 hash_str(const std::string &s)
{
    // portable hash
    size_t h = std::hash<std::string>{}(s);
    return (u64)h;
}

// Check if id is in (start, end] (clockwise)
bool in_interval_inclusive(u64 start, u64 end, u64 id)
{
    if (start < end)
        return id > start && id <= end;
    if (start > end) // wrap around
        return id > start || id <= end;
    else // start == end: interval is entire ring (single node)
        return true;
}

// Check if id is in (start, end) (clockwise)
bool in_interval_open(u64 start, u64 end, u64 id)
{
    if (start < end)
        return id > start && id < end;
    if (start > end) // wrap around
        return id > start || id < end;
    else // start == end: interval is entire ring except start
        return id != start;
}