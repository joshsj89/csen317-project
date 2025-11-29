#pragma once
#include <cstdint>
#include <functional>
#include <string>

using u64 = uint64_t;

u64 hash_str(const std::string &s);

bool in_interval_inclusive(u64 start, u64 end, u64 id);
bool in_interval_open(u64 start, u64 end, u64 id);