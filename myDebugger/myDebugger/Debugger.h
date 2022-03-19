//Author: E. Kelly
//Date Started: 05/03/2022
//Ver. 0.1
//Proj Description: A collection of debugging/benchmarking tools for C/C++

//Note: this project uses code from external sources, it is not all original
//https://stackoverflow.com/questions/31391914/timing-in-an-elegant-way-in-c
//https://stackoverflow.com/questions/63166/how-to-determine-cpu-and-memory-consumption-from-inside-a-process
//https://docs.microsoft.com/en-us/windows/win32/perfctrs/writing-performance-data-to-a-log-file?redirectedfrom=MSDN

#pragma once

#include <iostream> //basic IO
#include <stdint.h> //used for clock cycle benchmarking
#include <intrin.h>
#include <chrono> //time benchmarking
#include <tuple> //for memory containers

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

#pragma region timing
    //Find clock cycles
#ifdef _WIN32 //  Windows
    uint64_t clocks() { return __rdtsc(); }
#else //  Linux/GCC
    uint64_t clocks() {
        unsigned int lo, hi;
        __asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
        return ((uint64_t)hi << 32) | lo;
    }
#endif

    typedef std::pair<uint64_t, std::chrono::steady_clock::time_point> timer;

    //Benchmarks a function
    template<typename Duration = std::chrono::microseconds, typename F, typename ... Args> typename Duration::rep benchmark(F&& fun, Args&&... args) {
        const timer beg = { clocks(), std::chrono::high_resolution_clock::now() };
        std::forward<F>(fun)(std::forward<Args>(args)...);
        return std::chrono::duration_cast<Duration>(std::chrono::high_resolution_clock::now() - beg.second).count();
    }

    //returns a benchmarker object with current clock cycles and time
    inline timer getBench() { return { clocks(), std::chrono::steady_clock::now() }; }

    //prints total clock cycles and nanoseconds since the benchmark passed
    template<typename Duration = std::chrono::microseconds> inline void endBench(timer start) { //fix time output to duration
        std::string type = type_name<Duration>();
        if (type == "class std::chrono::duration<__int64,struct std::ratio<1,1000> >") type = "milliseconds"; //TODO: clean this up
        else if (type == "class std::chrono::duration<__int64,struct std::ratio<1,1000000000> >") type = "nanoseconds";
        else if (type == "class std::chrono::duration<__int64,struct std::ratio<1,1> >") type = "seconds";
        else if (type == "class std::chrono::duration<__int64,struct std::ratio<1,1000000> >") type = "microseconds";
        else if (type == "class std::chrono::duration<int,struct std::ratio<60,1> >") type = "minutes";
        else if (type == "class std::chrono::duration<int,struct std::ratio<3600,1> >") type = "hours";
        std::cout << "\nClock cycles: " << clocks() - start.first << ", " << type << ": " << std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start.second).count() << "\n";
    }
#pragma endregion timing

#pragma region Memory/CPU
    struct memory {
        unsigned long long virtTotal, virtUsed, virtProg;
        unsigned long long ramTotal, ramUsed, ramProg;
        double cpuTotal, cpuProg;
    };
#ifdef _MSC_VER
#include "windows.h"
#include "psapi.h" //gets info on current process: "process status API"

#include <pdh.h>
#include <pdhmsg.h>

#pragma comment(lib,"pdh.lib")

    //cpu stuff
    static PDH_HQUERY cpuQuery;
    static PDH_HCOUNTER cpuTotal;
    static ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
    static int numProcessors;
    static HANDLE self;

    void initCPU() {
        PDH_STATUS a = PdhOpenQuery(NULL, NULL, &cpuQuery);
        PDH_STATUS i = PdhAddEnglishCounter(cpuQuery, L"\\Processor(_Total)\\% Processor Time", NULL, &cpuTotal);
        PdhCollectQueryData(cpuQuery);

        SYSTEM_INFO sysInfo;
        FILETIME ftime, fsys, fuser;

        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;

        GetSystemTimeAsFileTime(&ftime);
        memcpy(&lastCPU, &ftime, sizeof(FILETIME));

        self = GetCurrentProcess();
        GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
        memcpy(&lastSysCPU, &fsys, sizeof(FILETIME));
        memcpy(&lastUserCPU, &fuser, sizeof(FILETIME));
    }

    double getCPU() {
        FILETIME ftime, fsys, fuser;
        ULARGE_INTEGER now, sys, user;

        GetSystemTimeAsFileTime(&ftime);
        memcpy(&now, &ftime, sizeof(FILETIME));

        GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
        memcpy(&sys, &fsys, sizeof(FILETIME));
        memcpy(&user, &fuser, sizeof(FILETIME));
        double percent = (numProcessors > 0 && now.QuadPart - lastCPU.QuadPart != 0) ? ((((sys.QuadPart - lastSysCPU.QuadPart) + (user.QuadPart - lastUserCPU.QuadPart)) / (now.QuadPart - lastCPU.QuadPart)) / numProcessors) * 100 : -0.1;
        lastCPU = now;
        lastUserCPU = user;
        lastSysCPU = sys;

        return percent;
    }

    memory getData() {
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        PROCESS_MEMORY_COUNTERS_EX pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));

        PDH_FMT_COUNTERVALUE counterVal;
        PdhCollectQueryData(cpuQuery);
        PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);

        return { memInfo.ullTotalPageFile, memInfo.ullTotalPageFile - memInfo.ullAvailPageFile, pmc.PrivateUsage, memInfo.ullTotalPhys, memInfo.ullTotalPhys - memInfo.ullAvailPhys, pmc.WorkingSetSize, counterVal.doubleValue, getCPU() };
    }

    void compareData(memory pastData) {
        memory curData = getData();
        std::cout << "Virtual Memory consumption: " << static_cast<long>(curData.virtProg - pastData.virtProg) * 100.f / curData.virtTotal
            << "%\nRAM consumption: " << static_cast<long>(curData.ramProg - pastData.ramProg) * 100.f / curData.ramTotal << "%\n";
        if (curData.cpuProg > 0 && pastData.cpuProg > 0) std::cout << "CPU usage: " << curData.cpuProg - pastData.cpuProg << "%\n";
    }

    void printDiag() {
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        PROCESS_MEMORY_COUNTERS_EX pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));

        std::cout << "Virtual Memory\n\tUsing: " << pmc.PrivateUsage * 100.f / memInfo.ullAvailPageFile << "% of available.\n\tSystem using: " << (memInfo.ullTotalPageFile - memInfo.ullAvailPageFile) * 100.f / memInfo.ullTotalPageFile
            << "% of total.\nRAM\n\tUsing: " << pmc.WorkingSetSize * 100.f / memInfo.ullAvailPhys << "% of available.\n\tSystem using: " << (memInfo.ullTotalPhys - memInfo.ullAvailPhys) * 100.f / memInfo.ullTotalPhys << "% of total.\n";
        PDH_FMT_COUNTERVALUE counterVal;

        PdhCollectQueryData(cpuQuery);
        PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);
        if (counterVal.doubleValue > 0) std::cout << "CPU\n\tUsing: " << getCPU() << "%\n\tSystem using: " << counterVal.doubleValue << "%\n";
    }
#else

#endif
#pragma endregion Memory/CPU 
}