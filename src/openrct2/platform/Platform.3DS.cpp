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
    #include <cstdio>
    #include <cstdlib>
    #include <cstring>
    #include <ctime>
    #include <sys/stat.h>
    #include <sys/time.h>

// libctru lets the application size its memory regions through weak symbols.
extern "C" {
// Main thread stack: OpenRCT2 keeps sizeable buffers on the stack (paint, formatting, bidi).
u32 __stacksize__ = 2 * 1024 * 1024;
// Linear (GPU-visible) heap: only the screen framebuffers and audio buffers live here,
// so shrink it from libctru's 32 MiB default and leave the rest to the main heap.
u32 __ctru_linear_heap_size = 8 * 1024 * 1024;
}

namespace OpenRCT2::Platform
{
    static constexpr const utf8* kUserRoot = "sdmc:/3ds/openrct2";
    static constexpr const utf8* kSdDataPath = "sdmc:/3ds/openrct2/data";
    static constexpr const utf8* kRomfsDataPath = "romfs:/data";
    static constexpr const utf8* kRct2Path = "sdmc:/3ds/openrct2/rct2";
    static constexpr const utf8* kRct1Path = "sdmc:/3ds/openrct2/rct1";

    static bool _cfguReady = false;
    static bool _romfsReady = false;

    // Called from main() before anything else on the console.
    void N3DSPlatformInit()
    {
        // New 3DS: 804 MHz + L2 cache. No-op on an original 3DS.
        osSetSpeedupEnable(true);
        _romfsReady = R_SUCCEEDED(romfsInit());
        _cfguReady = R_SUCCEEDED(cfguInit());
        mkdir("sdmc:/3ds", 0777);
        mkdir(kUserRoot, 0777);
    }

    void N3DSConsoleInit()
    {
        consoleInit(GFX_TOP, nullptr);
        std::printf("OpenRCT2 for Nintendo 3DS\n");
    }

    void N3DSPlatformExit()
    {
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
        if (_romfsReady && Path::DirectoryExists(kRomfsDataPath))
        {
            return kRomfsDataPath;
        }
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
        // Case-insensitive file system: whatever casing was asked for will do.
        return std::string(path);
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
