/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

// Nintendo 3DS (devkitARM + libctru) platform layer.
//
// Everything lives on the SD card:
//   sdmc:/3ds/openrct2/        user config, saves, cache  (SpecialFolder::user*)
//   sdmc:/3ds/openrct2/data/   OpenRCT2's own data (g2.dat, language, object, sequence, ...)
//   sdmc:/3ds/openrct2/rct2/   the original RollerCoaster Tycoon 2 installation
//   sdmc:/3ds/openrct2/rct1/   (optional) RollerCoaster Tycoon 1
// If the executable was packaged with a RomFS that contains a "data" directory,
// that is preferred for the install path.

#ifdef __3DS__

    #include "../Date.h"
    #include "../Diagnostic.h"
    #include "../core/Path.hpp"
    #include "../core/String.hpp"
    #include "../localisation/Language.h"
    #include "Platform.h"

    #include <3ds.h>
    #include <pthread.h>
    #include <csignal>
    #include <malloc.h>
    #include <cstdio>
    #include <cstdlib>
    #include <exception>
    #include <unistd.h>
    #include <cstring>
    #include <ctime>
    #include <sys/iosupport.h>
    #include <sys/stat.h>
    #include <sys/time.h>

// libctru lets the application size its memory regions through weak symbols.
extern "C" {
// Main thread stack: OpenRCT2 keeps sizeable buffers on the stack (paint, formatting, bidi).
u32 __stacksize__ = 2 * 1024 * 1024;
// Heap layout. libctru's default split (app heap capped at 24 MiB, linear heap at 32 MiB,
// the rest to the app heap) is replaced so that malloc gets as much as possible: the app
// heap region is limited to 96 MiB by the kernel's address space layout, so everything
// up to that goes to the app heap and only the remainder (framebuffers, audio buffers)
// to the GPU-visible linear heap.
extern u32 __ctru_heap;
extern u32 __ctru_linear_heap;
extern u32 __ctru_heap_size;
extern u32 __ctru_linear_heap_size;
extern char* fake_heap_start;
extern char* fake_heap_end;

void __system_allocateHeaps(void)
{
    Handle reslimit = 0;
    if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE)))
        svcBreak(USERBREAK_PANIC);

    s64 maxCommit = 0, currentCommit = 0;
    ResourceLimitType reslimitType = RESLIMIT_COMMIT;
    svcGetResourceLimitLimitValues(&maxCommit, reslimit, &reslimitType, 1);
    svcGetResourceLimitCurrentValues(&currentCommit, reslimit, &reslimitType, 1);
    svcCloseHandle(reslimit);

    const u32 remaining = static_cast<u32>(maxCommit - currentCommit) & ~0xFFFu;
    const u32 heapRegion = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN; // 96 MiB
    const u32 linearMinimum = 6 * 1024 * 1024;

    u32 heapSize = remaining > linearMinimum ? remaining - linearMinimum : 0;
    if (heapSize > heapRegion)
        heapSize = heapRegion;
    u32 linearSize = remaining - heapSize;

    __ctru_heap_size = heapSize;
    __ctru_linear_heap_size = linearSize;

    if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, heapSize, MEMOP_ALLOC, static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE))))
        svcBreak(USERBREAK_PANIC);
    if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, linearSize, MEMOP_ALLOC_LINEAR, static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE))))
        svcBreak(USERBREAK_PANIC);

    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);

    fake_heap_start = reinterpret_cast<char*>(__ctru_heap);
    fake_heap_end = fake_heap_start + heapSize;
}

// libctru gives threads created without attributes (std::thread) a 32 KiB stack, which
// OpenRCT2's worker threads (object scanning, JSON parsing) overflow. The executable is
// linked with -Wl,--wrap=pthread_create so every such thread gets a larger one.
int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg);
int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg)
{
    if (attr != nullptr)
    {
        return __real_pthread_create(thread, attr, start, arg);
    }
    pthread_attr_t bigStack;
    pthread_attr_init(&bigStack);
    pthread_attr_setstacksize(&bigStack, 512 * 1024);
    int rc = __real_pthread_create(thread, &bigStack, start, arg);
    pthread_attr_destroy(&bigStack);
    return rc;
}
}

namespace OpenRCT2::Platform
{
    static constexpr const utf8* kUserRoot = "/3ds/openrct2";
    static constexpr const utf8* kSdDataPath = "/3ds/openrct2/data";
    static constexpr const utf8* kRct2Path = "/3ds/openrct2/rct2";
    static constexpr const utf8* kRct1Path = "/3ds/openrct2/rct1";

    static bool _cfguReady = false;
    static bool _romfsReady = false;

    void N3DSLogMemory(const char* tag);

    static void WaitForStart()
    {
        std::printf("Press START to return to the Homebrew Launcher.\n");
        while (aptMainLoop())
        {
            hidScanInput();
            if (hidKeysDown() & KEY_START)
                break;
            gspWaitForVBlank();
        }
    }

    // Any way the process can end short of returning from main() is routed through here so
    // the message is readable on the top screen before the Homebrew Launcher takes over.
    static void OnTerminate()
    {
        std::printf("\n*** std::terminate ***\n");
        N3DSLogMemory("at terminate");
        if (auto current = std::current_exception())
        {
            try
            {
                std::rethrow_exception(current);
            }
            catch (const std::exception& e)
            {
                std::printf("uncaught exception: %s\n", e.what());
            }
            catch (...)
            {
                std::printf("uncaught exception of unknown type\n");
            }
        }
        WaitForStart();
        _exit(134);
    }

    static void OnAbortSignal(int)
    {
        std::printf("\n*** abort() ***\n");
        WaitForStart();
        _exit(134);
    }

    static void OnExit()
    {
        std::printf("\n*** exit() called ***\n");
        WaitForStart();
    }

    // Called from main() before anything else on the console.
    void N3DSPlatformInit()
    {
        std::set_terminate(OnTerminate);
        std::signal(SIGABRT, OnAbortSignal);
        std::atexit(OnExit);
        // New 3DS: 804 MHz + L2 cache. No-op on an original 3DS.
        osSetSpeedupEnable(true);
        _romfsReady = R_SUCCEEDED(romfsInit());
        _cfguReady = R_SUCCEEDED(cfguInit());
        mkdir("/3ds", 0777);
        mkdir(kUserRoot, 0777);
    }

    // Everything printed to the top-screen console is also appended to a log file on the SD
    // card, so a run can be reported without photographing the screen.
    static u64 _startTime = 0;
    static const devoptab_t* _consoleDevoptab = nullptr;
    static devoptab_t _teeDevoptab;
    static FILE* _logFile = nullptr;

    static bool _atLineStart = true;

    static ssize_t TeeWrite(struct _reent* r, void* fd, const char* ptr, size_t len)
    {
        if (_logFile != nullptr)
        {
            // Prefix each line in the log file with the time since start, in milliseconds.
            if (_atLineStart)
            {
                std::fprintf(_logFile, "[%8lu] ", static_cast<unsigned long>(osGetTime() - _startTime));
            }
            std::fwrite(ptr, 1, len, _logFile);
            std::fflush(_logFile);
            _atLineStart = len > 0 && ptr[len - 1] == '\n';
        }
        return _consoleDevoptab->write_r(r, fd, ptr, len);
    }

    void N3DSConsoleInit()
    {
        consoleInit(GFX_BOTTOM, nullptr);
        _startTime = osGetTime();
        _logFile = std::fopen("/3ds/openrct2/openrct2.log", "w");
        _consoleDevoptab = devoptab_list[STD_OUT];
        _teeDevoptab = *_consoleDevoptab;
        _teeDevoptab.name = "tee";
        _teeDevoptab.write_r = TeeWrite;
        devoptab_list[STD_OUT] = &_teeDevoptab;
        devoptab_list[STD_ERR] = &_teeDevoptab;
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        bool isNew3DS = false;
        APT_CheckNew3DS(&isNew3DS);
        std::printf(
            "OpenRCT2 for Nintendo 3DS (%s)\nheap %lu KiB, linear heap %lu KiB, app region free %lu of %lu KiB\n",
            isNew3DS ? "New 3DS" : "Old 3DS", static_cast<unsigned long>(__ctru_heap_size / 1024),
            static_cast<unsigned long>(__ctru_linear_heap_size / 1024),
            static_cast<unsigned long>(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024),
            static_cast<unsigned long>(osGetMemRegionSize(MEMREGION_APPLICATION) / 1024));
    }

    void N3DSLogMemory(const char* tag)
    {
        struct mallinfo mi = mallinfo();
        std::printf(
            "[mem] %s: heap used %lu KiB, free %lu KiB (of %lu KiB); linear free %lu KiB (of %lu KiB)\n", tag,
            static_cast<unsigned long>(mi.uordblks / 1024), static_cast<unsigned long>((__ctru_heap_size - mi.uordblks) / 1024),
            static_cast<unsigned long>(__ctru_heap_size / 1024), static_cast<unsigned long>(linearSpaceFree() / 1024),
            static_cast<unsigned long>(__ctru_linear_heap_size / 1024));
    }

    void N3DSPlatformExit()
    {
        // Keep the top-screen log readable until the user dismisses it.
        std::printf("\nOpenRCT2 has exited normally.\n");
        WaitForStart();
        if (_cfguReady)
            cfguExit();
        if (_romfsReady)
            romfsExit();
    }

    std::string GetEnvironmentVariable(std::string_view name)
    {
        return String::toStd(getenv(std::string(name).c_str()));
    }

    std::string GetFolderPath(SpecialFolder folder)
    {
        switch (folder)
        {
            case SpecialFolder::userCache:
            case SpecialFolder::userConfig:
            case SpecialFolder::userData:
            case SpecialFolder::userHome:
                return kUserRoot;
            default:
                return std::string();
        }
    }

    std::string GetDocsPath()
    {
        return std::string();
    }

    std::string GetInstallPath()
    {
        // Paths deliberately carry no "sdmc:" device prefix: std::filesystem only treats paths
        // starting with '/' as absolute, and libctru maps those to the SD card anyway.
        return kSdDataPath;
    }

    std::string GetCurrentExecutablePath()
    {
        return std::string();
    }

    bool ShouldIgnoreCase()
    {
        // The SD card is FAT32: case-insensitive.
        return true;
    }

    bool IsPathSeparator(char c)
    {
        return c == '/';
    }

    uint64_t GetLastModified(std::string_view path)
    {
        struct stat statInfo{};
        if (stat(std::string(path).c_str(), &statInfo) == 0)
        {
            return statInfo.st_mtime;
        }
        return 0;
    }

    uint64_t GetFileSize(std::string_view path)
    {
        struct stat statInfo{};
        if (stat(std::string(path).c_str(), &statInfo) == 0)
        {
            return statInfo.st_size;
        }
        return 0;
    }

    std::string ResolveCasing(std::string_view path, bool fileExists)
    {
        // The SD card (FAT32) is case-insensitive, so if the file exists the requested casing is
        // fine as it is. An empty result means "not present": callers rely on that to try other
        // locations (e.g. the Steam RCT1 Deluxe layout under RCTdeluxe_install/).
        std::string result;
        if (fileExists)
        {
            result = std::string(path);
        }
        return result;
    }

    std::string FormatShortDate(std::time_t timestamp)
    {
        char date[20];
        std::strftime(date, sizeof(date), "%d/%m/%Y", std::localtime(&timestamp));
        return std::string(date);
    }

    std::string FormatTime(std::time_t timestamp)
    {
        char time[20];
        std::strftime(time, sizeof(time), "%H:%M:%S", std::localtime(&timestamp));
        return std::string(time);
    }

    bool IsColourTerminalSupported()
    {
        return false;
    }

    bool IsRunningInWine()
    {
        return false;
    }

    bool FindApp(std::string_view app, std::string* output)
    {
        return false;
    }

    int32_t Execute(const char* args[], std::string* output)
    {
        LOG_WARNING("The 3DS cannot execute external processes.");
        return -1;
    }

    bool ProcessIsElevated()
    {
        return false;
    }

    bool LockSingleInstance()
    {
        return true;
    }

    int32_t GetDrives()
    {
        return 0;
    }

    time_t FileGetModifiedTime(u8string_view path)
    {
        struct stat buf;
        if (stat(u8string(path).c_str(), &buf) == 0)
        {
            return buf.st_mtime;
        }
        return 100;
    }

    datetime64 GetDatetimeNowUTC()
    {
        const datetime64 epochAsTicks = 621355968000000000;
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        uint64_t utcEpochTicks = static_cast<uint64_t>(tv.tv_sec) * 10000000uLL + tv.tv_usec * 10;
        return epochAsTicks + utcEpochTicks;
    }

    bool RequireNewWindow(bool openGL)
    {
        return true;
    }

    std::string GetUsername()
    {
        if (_cfguReady)
        {
            // The system profile name is UTF-16, at most 10 code units.
            u16 name[11] = {};
            if (R_SUCCEEDED(CFGU_GetConfigInfoBlk2(sizeof(u16) * 10, 0x000A0000, name)))
            {
                std::string result;
                for (u16 c : name)
                {
                    if (c == 0)
                        break;
                    if (c < 0x80)
                        result += static_cast<char>(c);
                    else if (c < 0x800)
                    {
                        result += static_cast<char>(0xC0 | (c >> 6));
                        result += static_cast<char>(0x80 | (c & 0x3F));
                    }
                    else
                    {
                        result += static_cast<char>(0xE0 | (c >> 12));
                        result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                        result += static_cast<char>(0x80 | (c & 0x3F));
                    }
                }
                if (!result.empty())
                    return result;
            }
        }
        return "Player";
    }

    static u8 GetSystemRegion()
    {
        u8 region = CFG_REGION_EUR;
        if (_cfguReady)
            CFGU_SecureInfoGetRegion(&region);
        return region;
    }

    uint16_t GetLocaleLanguage()
    {
        u8 language = CFG_LANGUAGE_EN;
        if (_cfguReady)
            CFGU_GetSystemLanguage(&language);
        switch (language)
        {
            case CFG_LANGUAGE_JP:
                return LANGUAGE_JAPANESE;
            case CFG_LANGUAGE_EN:
                return GetSystemRegion() == CFG_REGION_USA ? LANGUAGE_ENGLISH_US : LANGUAGE_ENGLISH_UK;
            case CFG_LANGUAGE_FR:
                return LANGUAGE_FRENCH;
            case CFG_LANGUAGE_DE:
                return LANGUAGE_GERMAN;
            case CFG_LANGUAGE_IT:
                return LANGUAGE_ITALIAN;
            case CFG_LANGUAGE_ES:
                return LANGUAGE_SPANISH;
            case CFG_LANGUAGE_ZH:
                return LANGUAGE_CHINESE_SIMPLIFIED;
            case CFG_LANGUAGE_KO:
                return LANGUAGE_KOREAN;
            case CFG_LANGUAGE_NL:
                return LANGUAGE_DUTCH;
            case CFG_LANGUAGE_PT:
                return LANGUAGE_PORTUGUESE_BR;
            case CFG_LANGUAGE_RU:
                return LANGUAGE_RUSSIAN;
            case CFG_LANGUAGE_TW:
                return LANGUAGE_CHINESE_TRADITIONAL;
            default:
                return LANGUAGE_ENGLISH_UK;
        }
    }

    CurrencyType GetLocaleCurrency()
    {
        switch (GetSystemRegion())
        {
            case CFG_REGION_JPN:
                return GetCurrencyValue("JPY");
            case CFG_REGION_USA:
                return GetCurrencyValue("USD");
            case CFG_REGION_EUR:
                return GetCurrencyValue("EUR");
            case CFG_REGION_AUS:
                return GetCurrencyValue("AUD");
            case CFG_REGION_KOR:
                return GetCurrencyValue("KRW");
            default:
                return GetCurrencyValue(nullptr);
        }
    }

    MeasurementFormat GetLocaleMeasurementFormat()
    {
        return GetSystemRegion() == CFG_REGION_USA ? MeasurementFormat::imperial : MeasurementFormat::metric;
    }

    uint8_t GetLocaleDateFormat()
    {
        switch (GetSystemRegion())
        {
            case CFG_REGION_USA:
                return DATE_FORMAT_MONTH_DAY_YEAR;
            case CFG_REGION_JPN:
            case CFG_REGION_KOR:
            case CFG_REGION_CHN:
            case CFG_REGION_TWN:
                return DATE_FORMAT_YEAR_MONTH_DAY;
            default:
                return DATE_FORMAT_DAY_MONTH_YEAR;
        }
    }

    TemperatureUnit GetLocaleTemperatureFormat()
    {
        return GetSystemRegion() == CFG_REGION_USA ? TemperatureUnit::fahrenheit : TemperatureUnit::celsius;
    }

    SteamPaths GetSteamPaths()
    {
        return {};
    }

    std::vector<std::string> GetSearchablePathsRCT1()
    {
        return { kRct1Path };
    }

    std::vector<std::string> GetSearchablePathsRCT2()
    {
        struct stat st{};
        bool hasG1 = stat("/3ds/openrct2/rct2/Data/g1.dat", &st) == 0;
        std::printf("Looking for RCT2 in %s: Data/g1.dat %s\n", kRct2Path, hasG1 ? "found" : "NOT found");
        return { kRct2Path };
    }

    u8string StrDecompToPrecomp(u8string_view input)
    {
        return u8string(input);
    }

    bool HandleSpecialCommandLineArgument(const char* argument)
    {
        return false;
    }
} // namespace OpenRCT2::Platform

#endif // __3DS__
