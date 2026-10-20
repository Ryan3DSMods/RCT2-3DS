/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef __3DS__

    #include "UiContext.h"

    #include <cstdio>
    #include <openrct2/Diagnostic.h>
    #include <openrct2/ui/UiContext.h>
    #include <string>
    #include <vector>

namespace OpenRCT2::Ui
{
    // The 3DS has no desktop: no native dialogs, no browser, no file picker.
    // OpenRCT2 falls back to its own in-game file browser when HasFilePicker() is false.
    class N3DSContext final : public IPlatformUiContext
    {
    public:
        void SetWindowIcon(SDL_Window* window) override
        {
        }

        bool IsSteamOverlayAttached() override
        {
            return false;
        }

        void ShowMessageBox(SDL_Window* window, const std::string& message) override
        {
            // Shown on the top-screen console.
            std::printf("[MessageBox] %s\n", message.c_str());
            LOG_INFO("MessageBox: %s", message.c_str());
        }

        bool HasMenuSupport() override
        {
            return false;
        }

        int32_t ShowMenuDialog(const std::vector<std::string>& options, const std::string& title, const std::string& text) override
        {
            return -1;
        }

        void OpenFolder(const std::string& path) override
        {
        }

        void OpenURL(const std::string& url) override
        {
            std::printf("[URL] %s\n", url.c_str());
        }

        std::string ShowFileDialog(SDL_Window* window, const FileDialogDesc& desc) override
        {
            return {};
        }

        std::string ShowDirectoryDialog(SDL_Window* window, const std::string& title) override
        {
            return {};
        }

        bool HasFilePicker() const override
        {
            return false;
        }
    };

    std::unique_ptr<IPlatformUiContext> CreatePlatformUiContext()
    {
        return std::make_unique<N3DSContext>();
    }
} // namespace OpenRCT2::Ui

#endif // __3DS__
