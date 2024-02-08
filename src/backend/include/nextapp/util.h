#pragma once

#include <string>
#include <cstddef>
#include <locale>

namespace nextapp {

template <class T, class V>
concept range_of = std::ranges::range<T> && std::is_same_v<V, std::ranges::range_value_t<T>>;


std::string getEnv(const char *name, std::string def = {});

template <range_of<char> T>
std::string toUpper(const T& input)
{
    static const std::locale loc{"C"};
    std::string v;
    v.reserve(input.size());

    for(const auto ch : input) {
        v.push_back(std::toupper(ch, loc));
    }

    return v;
}

} // ns
