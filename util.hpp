#pragma once

#include <cstddef>

template <class T, std::size_t N>
constexpr std::size_t countof(const T(&)[N]) noexcept
{
	return N;
}

template <class T>
constexpr std::size_t countof(const T N)
{
	return N.size();
}

#ifndef NDEBUG
void Debug(const char* format, ...);
#else
#define Debug(x) do{}while(0)
#endif
