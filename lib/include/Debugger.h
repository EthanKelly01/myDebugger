#pragma once

//Author: E. Kelly
//Date Started: 05/03/2022
//Ver. 0.1
//Proj Description: A collection of debugging/benchmarking tools for C/C++

//Note: this project uses code from external sources, it is not all original
//https://stackoverflow.com/questions/31391914/timing-in-an-elegant-way-in-c
//https://stackoverflow.com/questions/63166/how-to-determine-cpu-and-memory-consumption-from-inside-a-process

#include <iostream> //basic IO
#include <stdint.h> //used for clock cycle benchmarking
#include <intrin.h>
#include <chrono> //time benchmarking

namespace Debugger {

#pragma region type_name
    //returns the demangled type name of the variable x
    //call `type_name<decltype(x)>()`
    template <class T> std::string type_name() {
        typedef typename std::remove_reference<T>::type TR;
        std::unique_ptr<char, void(*)(void*)> own(
#ifndef _MSC_VER
            abi::__cxa_demangle(typeid(TR).name(), nullptr, nullptr, nullptr),
#else
            nullptr,
#endif
            std::free);
        std::string r = own != nullptr ? own.get() : typeid(TR).name();
        if (std::is_const<TR>::value) r += " const";
        if (std::is_volatile<TR>::value) r += " volatile";
        if (std::is_lvalue_reference<T>::value) r += "&";
        else if (std::is_rvalue_reference<T>::value) r += "&&";
        return r;
    }
#pragma endregion type_name

#pragma region clock_cycles
    //Find clock cycles
#ifdef _WIN32 //  Windows
    uint64_t rdtsc() { return __rdtsc(); }
#else //  Linux/GCC
    uint64_t rdtsc() {
        unsigned int lo, hi;
        __asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
        return ((uint64_t)hi << 32) | lo;
    }
#endif
#pragma endregion clock_cycles

    typedef std::pair<uint64_t, std::chrono::steady_clock::time_point> timer;

    //Benchmarks a function
    template<typename Duration = std::chrono::microseconds, typename F, typename ... Args> typename Duration::rep benchmark(F&& fun, Args&&... args) {
        const timer beg = { rdtsc(), std::chrono::high_resolution_clock::now() };
        std::forward<F>(fun)(std::forward<Args>(args)...);
        return std::chrono::duration_cast<Duration>(std::chrono::high_resolution_clock::now() - beg.second).count();
    }

    //returns a benchmarker object with current clock cycles and time
    inline timer getBench() { return { rdtsc(), std::chrono::steady_clock::now() }; }

    //prints total clock cycles and nanoseconds since the benchmark passed
    template<typename Duration = std::chrono::microseconds> inline void endBench(timer start) { //fix time output to duration
        std::string type = type_name<Duration>();
        if (type == "class std::chrono::duration<__int64,struct std::ratio<1,1000> >") type = "milliseconds"; //TODO: clean this up
        else if (type == "class std::chrono::duration<__int64,struct std::ratio<1,1000000000> >") type = "nanoseconds";
        else if (type == "class std::chrono::duration<__int64,struct std::ratio<1,1> >") type = "seconds";
        else if (type == "class std::chrono::duration<__int64,struct std::ratio<1,1000000> >") type = "microseconds";
        else if (type == "class std::chrono::duration<int,struct std::ratio<60,1> >") type = "minutes";
        else if (type == "class std::chrono::duration<int,struct std::ratio<3600,1> >") type = "hours";
        std::cout << "\nClock cycles: " << rdtsc() - start.first << ", " << type << ": " << std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start.second).count() << "\n";
    }
}