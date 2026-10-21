/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ImageTable.h"

#include "../Context.h"
#include "../Diagnostic.h"
#include "../OpenRCT2.h"
#include "../PlatformEnvironment.h"
#include "../SpriteIds.h"
#include "../core/File.h"
#include "../core/FileScanner.h"
#include "../core/Guard.hpp"
#include "../core/IStream.hpp"
#include "../core/Json.hpp"
#include "../core/Path.hpp"
#include "../platform/Platform.h"
#include "../core/String.hpp"
#include "../drawing/Drawing.Sprite.h"
#include "../drawing/ImageImporter.h"
#include "Object.h"
#include "ObjectFactory.h"

#include <cstdio>
#include <map>
#include <dirent.h>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <stdexcept>

namespace OpenRCT2
{
    static thread_local std::map<u8string, std::unique_ptr<Object>> _objDataCache = {};

    struct ImageTable::RequiredImage
    {
        G1Element g1{};
        std::unique_ptr<RequiredImage> next_zoom;

        bool HasData() const
        {
            return g1.offset != nullptr;
        }

        RequiredImage() = default;
        RequiredImage(const RequiredImage&) = delete;

        RequiredImage(const G1Element& orig)
        {
            auto length = G1CalculateDataSize(&orig);
            g1 = orig;
            g1.offset = new uint8_t[length];
            std::memcpy(g1.offset, orig.offset, length);
            g1.flags.unset(G1Flag::hasZoomSprite);
        }

        RequiredImage(uint32_t idx, std::function<const G1Element*(uint32_t)> getter)
        {
            auto orig = getter(idx);
            if (orig != nullptr)
            {
                auto length = G1CalculateDataSize(orig);
                g1 = *orig;
                g1.offset = new uint8_t[length];
                std::memcpy(g1.offset, orig->offset, length);
                if (g1.flags.has(G1Flag::hasZoomSprite) && g1.zoomedOffset != 0)
                {
                    // Fetch image for next zoom level
                    next_zoom = std::make_unique<RequiredImage>(static_cast<uint32_t>(idx - g1.zoomedOffset), getter);
                    if (!next_zoom->HasData())
                    {
                        next_zoom = nullptr;
                        g1.flags.unset(G1Flag::hasZoomSprite);
                    }
                }
            }
        }

        ~RequiredImage()
        {
            delete[] g1.offset;
        }
    };

    std::vector<std::unique_ptr<ImageTable::RequiredImage>> ImageTable::ParseImages(IReadObjectContext* context, std::string s)
    {
        std::vector<std::unique_ptr<RequiredImage>> result;
        if (s.empty())
        {
            result.push_back(std::make_unique<RequiredImage>());
        }
        else if (String::startsWith(s, "$CSG"))
        {
            auto rangeStart = s.find('[');
            auto rangeEnd = s.find(']');
            if (rangeStart != std::string::npos && rangeEnd != std::string::npos)
            {
                auto rangeString = s.substr(rangeStart, rangeEnd - rangeStart + 1);
                auto range = ParseRange(rangeString);
                if (IsCsgLoaded())
                {
                    for (auto i : range)
                    {
                        result.push_back(
                            std::make_unique<RequiredImage>(
                                static_cast<uint32_t>(SPR_CSG_BEGIN + i),
                                [](uint32_t idx) -> const G1Element* { return GfxGetG1Element(idx); }));
                    }
                }
                else
                {
                    std::string id(context->GetObjectIdentifier());
                    LOG_WARNING("CSG not loaded inserting placeholder images for %s", id.c_str());
                    result.resize(range.size());
                    for (auto& res : result)
                    {
                        res = std::make_unique<RequiredImage>();
                    }
                }
            }
        }
        else if (String::startsWith(s, "$G1"))
        {
            auto rangeStart = s.find('[');
            auto rangeEnd = s.find(']');
            if (rangeStart != std::string::npos && rangeEnd != std::string::npos)
            {
                auto rangeString = s.substr(rangeStart, rangeEnd - rangeStart + 1);
                auto range = ParseRange(rangeString);
                for (auto i : range)
                {
                    result.push_back(
                        std::make_unique<RequiredImage>(
                            static_cast<uint32_t>(i), [](uint32_t idx) -> const G1Element* { return GfxGetG1Element(idx); }));
                }
            }
        }
        else if (String::startsWith(s, "$RCT2:OBJDATA/"))
        {
            auto name = s.substr(14);
            auto rangeStart = name.find('[');
            auto rangeEnd = name.find(']');
            if (rangeStart != std::string::npos && rangeEnd != std::string::npos)
            {
                auto rangeString = name.substr(rangeStart, rangeEnd - rangeStart + 1);
                auto range = ParseRange(rangeString);
                name = name.substr(0, rangeStart);
                result = LoadObjectImages(context, name, range);
            }
        }
        else if (String::startsWith(s, "$LGX:"))
        {
            auto name = s.substr(5);
            auto rangeStart = name.find('[');
            auto rangeEnd = name.find(']');
            if (rangeStart != std::string::npos && rangeEnd != std::string::npos)
            {
                auto rangeString = name.substr(rangeStart, rangeEnd - rangeStart + 1);
                auto range = ParseRange(rangeString);
                name = name.substr(0, rangeStart);
                result = LoadImageArchiveImages(context, name, range);
            }
            else
            {
                result = LoadImageArchiveImages(context, name);
            }
        }
        else
        {
            try
            {
                auto imageData = context->GetData(s);
                auto image = Imaging::ReadFromBuffer(imageData);
                auto meta = Drawing::ImageImportMeta{};

                Drawing::ImageImporter importer;
                auto importResult = importer.Import(image, meta);

                result.push_back(std::make_unique<RequiredImage>(importResult.Element));
            }
            catch (const std::exception& e)
            {
                auto msg = String::stdFormat("Unable to load image '%s': %s", s.c_str(), e.what());
                context->LogWarning(ObjectError::badImageTable, msg.c_str());
                result.push_back(std::make_unique<RequiredImage>());
            }
        }
        return result;
    }

    std::vector<std::unique_ptr<ImageTable::RequiredImage>> ImageTable::ParseImages(
        IReadObjectContext* context, std::vector<std::pair<std::string, Image>>& imageSources, json_t& el)
    {
        Guard::Assert(el.is_object(), "ImageTable::ParseImages expects parameter el to be object");

        auto path = Json::GetString(el["path"]);
        auto meta = Drawing::createImageImportMetaFromJson(el);

        std::vector<std::unique_ptr<RequiredImage>> result;
        try
        {
            auto itSource = std::find_if(
                imageSources.begin(), imageSources.end(),
                [&path](const std::pair<std::string, Image>& item) { return item.first == path; });
            if (itSource == imageSources.end())
            {
                throw std::runtime_error("Unable to find image in image source list.");
            }
            auto& image = itSource->second;

            Drawing::ImageImporter importer;
            auto importResult = importer.Import(image, meta);
            auto g1element = importResult.Element;
            result.push_back(std::make_unique<RequiredImage>(g1element));
        }
        catch (const std::exception& e)
        {
            auto msg = String::stdFormat("Unable to load image '%s': %s", path.c_str(), e.what());
            context->LogWarning(ObjectError::badImageTable, msg.c_str());
            result.push_back(std::make_unique<RequiredImage>());
        }
        return result;
    }

    std::vector<std::unique_ptr<ImageTable::RequiredImage>> ImageTable::LoadImageArchiveImages(
        IReadObjectContext* context, const std::string& path, const std::vector<int32_t>& range)
    {
        std::vector<std::unique_ptr<RequiredImage>> result;
        auto gxRaw = context->GetData(path);
        std::optional<Gx> gxData = GfxLoadGx(gxRaw);
        if (gxData.has_value())
        {
            // Fix entry data offsets
            for (uint32_t i = 0; i < gxData->header.numEntries; i++)
            {
                if (gxData->elements[i].offset == nullptr)
                {
                    gxData->elements[i].offset = gxData->data.get();
                }
                else
                {
                    gxData->elements[i].offset += reinterpret_cast<uintptr_t>(gxData->data.get());
                }
            }

            if (!range.empty())
            {
                size_t placeHoldersAdded = 0;
                for (auto i : range)
                {
                    if (i >= 0 && (i < static_cast<int32_t>(gxData->header.numEntries)))
                    {
                        result.push_back(std::make_unique<RequiredImage>(gxData->elements[i]));
                    }
                    else
                    {
                        result.push_back(std::make_unique<RequiredImage>());
                        placeHoldersAdded++;
                    }
                }

                // Log place holder information
                if (placeHoldersAdded > 0)
                {
                    std::string msg = "Adding " + std::to_string(placeHoldersAdded) + " placeholders";
                    context->LogWarning(ObjectError::invalidProperty, msg.c_str());
                }
            }
            else
            {
                for (int i = 0; i < static_cast<int32_t>(gxData->header.numEntries); i++)
                    result.push_back(std::make_unique<RequiredImage>(gxData->elements[i]));
            }
        }
        else
        {
            auto msg = String::stdFormat("Unable to load Gx '%s'", path.c_str());
            context->LogWarning(ObjectError::badImageTable, msg.c_str());
            for (size_t i = 0; i < range.size(); i++)
            {
                result.push_back(std::make_unique<RequiredImage>());
            }
        }
        return result;
    }

#ifdef __3DS__
    namespace
    {
        struct LegacyImageCacheEntry
        {
            std::vector<G1Element> images;
            std::unique_ptr<uint8_t[]> data;
        };
        thread_local std::map<std::string, LegacyImageCacheEntry> _legacyImageCache;

        struct CachedImageHeader
        {
            int16_t width, height, xOffset, yOffset;
            uint16_t flags;
            int32_t zoomedOffset;
            uint32_t dataOffset;
            uint32_t dataLength;
        };
        constexpr uint32_t kLegacyImageCacheMagic = 0x31494C4F; // "OLI1"

        std::string LegacyImageCachePath(const std::string& name)
        {
            auto base = name;
            auto dot = base.find('.');
            if (dot != std::string::npos)
                base.erase(dot);
            return "/3ds/openrct2/OpenRCT2/cache/legacy/" + String::toUpper(base) + ".img";
        }

        LegacyImageCacheEntry* LegacyImageCacheGet(const std::string& name)
        {
            auto it = _legacyImageCache.find(name);
            if (it != _legacyImageCache.end())
                return &it->second;

            FILE* f = std::fopen(LegacyImageCachePath(name).c_str(), "rb");
            if (f == nullptr)
                return nullptr;
            std::setvbuf(f, nullptr, _IOFBF, 256 * 1024);
            uint32_t magic = 0, count = 0, dataSize = 0;
            bool ok = std::fread(&magic, 4, 1, f) == 1 && std::fread(&count, 4, 1, f) == 1 && std::fread(&dataSize, 4, 1, f) == 1
                && magic == kLegacyImageCacheMagic && count < 100000 && dataSize < 64 * 1024 * 1024;
            LegacyImageCacheEntry entry;
            std::vector<CachedImageHeader> headers;
            if (ok)
            {
                headers.resize(count);
                entry.data = std::make_unique<uint8_t[]>(std::max<uint32_t>(dataSize, 1));
                ok = (count == 0 || std::fread(headers.data(), sizeof(CachedImageHeader), count, f) == count)
                    && (dataSize == 0 || std::fread(entry.data.get(), 1, dataSize, f) == dataSize);
            }
            std::fclose(f);
            if (!ok)
                return nullptr;
            entry.images.reserve(count);
            for (const auto& h : headers)
            {
                if (static_cast<uint64_t>(h.dataOffset) + h.dataLength > dataSize)
                    return nullptr;
                G1Element g1{};
                g1.offset = entry.data.get() + h.dataOffset;
                g1.width = h.width;
                g1.height = h.height;
                g1.xOffset = h.xOffset;
                g1.yOffset = h.yOffset;
                g1.flags.holder = h.flags;
                g1.zoomedOffset = h.zoomedOffset;
                entry.images.push_back(g1);
            }
            auto inserted = _legacyImageCache.emplace(name, std::move(entry));
            return &inserted.first->second;
        }

        void LegacyImageCachePut(const std::string& name, const G1Element* images, int32_t numImages)
        {
            if (images == nullptr)
                return;
            Path::CreateDirectory("/3ds/openrct2/OpenRCT2/cache");
            Path::CreateDirectory("/3ds/openrct2/OpenRCT2/cache/legacy");
            auto path = LegacyImageCachePath(name);
            auto tmpPath = path + ".tmp";
            FILE* f = std::fopen(tmpPath.c_str(), "wb");
            if (f == nullptr)
                return;
            std::setvbuf(f, nullptr, _IOFBF, 256 * 1024);
            std::vector<CachedImageHeader> headers;
            headers.reserve(numImages);
            uint32_t dataSize = 0;
            for (int32_t i = 0; i < numImages; i++)
            {
                const auto& g1 = images[i];
                CachedImageHeader h{};
                h.width = g1.width;
                h.height = g1.height;
                h.xOffset = g1.xOffset;
                h.yOffset = g1.yOffset;
                h.flags = g1.flags.holder;
                h.zoomedOffset = g1.zoomedOffset;
                h.dataOffset = dataSize;
                h.dataLength = g1.offset != nullptr ? static_cast<uint32_t>(G1CalculateDataSize(&g1)) : 0;
                dataSize += h.dataLength;
                headers.push_back(h);
            }
            const uint32_t count = static_cast<uint32_t>(numImages);
            bool ok = std::fwrite(&kLegacyImageCacheMagic, 4, 1, f) == 1 && std::fwrite(&count, 4, 1, f) == 1
                && std::fwrite(&dataSize, 4, 1, f) == 1
                && (count == 0 || std::fwrite(headers.data(), sizeof(CachedImageHeader), count, f) == count);
            for (int32_t i = 0; ok && i < numImages; i++)
            {
                const auto& h = headers[i];
                if (h.dataLength > 0)
                    ok = std::fwrite(images[i].offset, 1, h.dataLength, f) == h.dataLength;
            }
            std::fclose(f);
            if (ok)
                std::rename(tmpPath.c_str(), path.c_str());
            else
                std::remove(tmpPath.c_str());
        }
    } // namespace
#endif

    std::vector<std::unique_ptr<ImageTable::RequiredImage>> ImageTable::LoadObjectImages(
        IReadObjectContext* context, const std::string& name, const std::vector<int32_t>& range)
    {
        std::vector<std::unique_ptr<RequiredImage>> result;
        Object* obj = nullptr;
        const G1Element* images = nullptr;
        int32_t numImages = 0;

#ifdef __3DS__
        // Decoding the legacy .DAT twin of every JSON object is the slow part of loading a park on the
        // console, so the decoded image table is cached on the SD card after the first time.
        if (context->ShouldLoadImages())
        {
            auto* entry = LegacyImageCacheGet(name);
            if (entry != nullptr)
            {
                images = entry->images.data();
                numImages = static_cast<int32_t>(entry->images.size());
            }
        }
        if (images == nullptr)
#endif
        {
            auto cached = _objDataCache.find(name);
            if (cached != _objDataCache.end())
            {
                obj = cached->second.get();
            }
            else
            {
                auto objectPath = FindLegacyObject(name);
                auto tmp = ObjectFactory::CreateObjectFromLegacyFile(objectPath.c_str(), context->ShouldLoadImages());
                auto inserted = _objDataCache.insert({ name, std::move(tmp) });
                obj = inserted.first->second.get();
            }
            if (obj != nullptr)
            {
                auto& imgTable = static_cast<const Object*>(obj)->GetImageTable();
                numImages = static_cast<int32_t>(imgTable.GetCount());
                images = imgTable.GetImages();
#ifdef __3DS__
                if (context->ShouldLoadImages())
                    LegacyImageCachePut(name, images, numImages);
#endif
            }
        }

        if (images != nullptr)
        {
            size_t placeHoldersAdded = 0;
            for (auto i : range)
            {
                if (i >= 0 && i < numImages)
                {
                    result.push_back(
                        std::make_unique<RequiredImage>(
                            static_cast<uint32_t>(i), [images](uint32_t idx) -> const G1Element* { return &images[idx]; }));
                }
                else
                {
                    result.push_back(std::make_unique<RequiredImage>());
                    placeHoldersAdded++;
                }
            }

            // Log place holder information
            if (placeHoldersAdded > 0)
            {
                std::string msg = "Adding " + std::to_string(placeHoldersAdded) + " placeholders";
                context->LogWarning(ObjectError::invalidProperty, msg.c_str());
            }
        }
        else
        {
            std::string msg = "Unable to open '" + name + "'";
            context->LogWarning(ObjectError::invalidProperty, msg.c_str());
            for (size_t i = 0; i < range.size(); i++)
            {
                result.push_back(std::make_unique<RequiredImage>());
            }
        }
        return result;
    }

    std::vector<int32_t> ImageTable::ParseRange(std::string s)
    {
        // Currently only supports [###] or [###..###]
        std::vector<int32_t> result = {};
        if (s.length() >= 3 && s[0] == '[' && s[s.length() - 1] == ']')
        {
            s = s.substr(1, s.length() - 2);
            auto parts = String::split(s, "..");
            if (parts.size() == 1)
            {
                result.push_back(String::parse<int32_t>(parts[0]));
            }
            else
            {
                auto left = String::parse<int32_t>(parts[0]);
                auto right = String::parse<int32_t>(parts[1]);
                if (left <= right)
                {
                    for (auto i = left; i <= right; i++)
                    {
                        result.push_back(i);
                    }
                }
                else
                {
                    for (auto i = right; i >= left; i--)
                    {
                        result.push_back(i);
                    }
                }
            }
        }
        return result;
    }

    std::string ImageTable::FindLegacyObject(const std::string& name)
    {
        const auto& env = GetContext()->GetPlatformEnvironment();
        auto objectsPath = env.GetDirectoryPath(DirBase::rct2, DirId::objects);
#ifdef __3DS__
        // Every filesystem call costs tens of milliseconds on the console, so the ObjData folder is listed
        // once and looked up by (upper-case) file name from then on.
        {
            static std::mutex cacheMutex;
            static std::string cachedRoot;
            static std::unordered_map<std::string, std::string> cache;
            std::lock_guard<std::mutex> lock(cacheMutex);
            if (cachedRoot != objectsPath)
            {
                cache.clear();
                // readdir only: a stat per file (what the generic scanner does) costs a minute here.
                if (DIR* dir = opendir(objectsPath.c_str()))
                {
                    while (dirent* entry = readdir(dir))
                    {
                        std::string fileName = entry->d_name;
                        auto upper = String::toUpper(fileName);
                        if (upper.size() > 4 && (upper.ends_with(".DAT") || upper.ends_with(".POB")))
                            cache.emplace(upper, Path::Combine(objectsPath, fileName));
                    }
                    closedir(dir);
                }
                cachedRoot = objectsPath;
            }
            auto it = cache.find(String::toUpper(name));
            if (it == cache.end())
            {
                auto pobName = name;
                auto ext = pobName.find(".DAT");
                if (ext != std::string::npos)
                    pobName.replace(ext, 4, ".POB");
                it = cache.find(String::toUpper(pobName));
            }
            if (it != cache.end())
                return it->second;
        }
#endif
        auto objectPath = Path::Combine(objectsPath, name);
        if (File::Exists(objectPath))
        {
            return objectPath;
        }

        std::string altName = name;
        auto rangeStart = name.find(".DAT");
        if (rangeStart != std::string::npos)
        {
            altName.replace(rangeStart, 4, ".POB");
        }
        objectPath = Path::Combine(objectsPath, altName);
        if (File::Exists(objectPath))
        {
            return objectPath;
        }

        if (!File::Exists(objectPath))
        {
            // Search recursively for any file with the target name (case insensitive)
            auto filter = Path::Combine(objectsPath, u8"*.dat;*.pob");
            auto scanner = Path::scanDirectory(filter, true);
            while (scanner->next())
            {
                // Only the file name is compared; getPathRelative() would canonicalise every
                // path (several filesystem calls per file), which is very slow on some platforms.
                auto currentName = Path::GetFileName(scanner->getPath());
                if (String::iequals(currentName, name) || String::iequals(currentName, altName))
                {
                    objectPath = scanner->getPath();
                    break;
                }
            }
        }
        return objectPath;
    }

    ImageTable::~ImageTable()
    {
        if (_data == nullptr)
        {
            for (auto& entry : _entries)
            {
                delete[] entry.offset;
            }
        }
    }

    void ImageTable::Read(IReadObjectContext* context, IStream* stream)
    {
        if (gOpenRCT2NoGraphics)
        {
            return;
        }

        try
        {
            uint32_t numImages = stream->ReadValue<uint32_t>();
            uint32_t imageDataSize = stream->ReadValue<uint32_t>();

            uint64_t headerTableSize = numImages * 16;
            uint64_t remainingBytes = stream->GetLength() - stream->GetPosition() - headerTableSize;
            if (remainingBytes > imageDataSize)
            {
                context->LogVerbose(ObjectError::badImageTable, "Image table size longer than expected.");
                imageDataSize = static_cast<uint32_t>(remainingBytes);
            }

            auto dataSize = static_cast<size_t>(imageDataSize);
            auto data = std::make_unique<uint8_t[]>(dataSize);
            if (data == nullptr)
            {
                context->LogError(ObjectError::badImageTable, "Image table too large.");
                throw std::runtime_error("Image table too large.");
            }

            // Read g1 element headers
            uintptr_t imageDataBase = reinterpret_cast<uintptr_t>(data.get());
            std::vector<G1Element> newEntries;
            for (uint32_t i = 0; i < numImages; i++)
            {
                G1Element g1Element{};

                uintptr_t imageDataOffset = static_cast<uintptr_t>(stream->ReadValue<uint32_t>());
                g1Element.offset = reinterpret_cast<uint8_t*>(imageDataBase + imageDataOffset);

                g1Element.width = stream->ReadValue<int16_t>();
                g1Element.height = stream->ReadValue<int16_t>();
                g1Element.xOffset = stream->ReadValue<int16_t>();
                g1Element.yOffset = stream->ReadValue<int16_t>();
                g1Element.flags = stream->ReadValue<G1Flags>();
                g1Element.zoomedOffset = stream->ReadValue<uint16_t>();

                newEntries.push_back(std::move(g1Element));
            }

            // Read g1 element data
            size_t readBytes = static_cast<size_t>(stream->TryRead(data.get(), dataSize));

            // If data is shorter than expected (some custom objects are unfortunately like that)
            size_t unreadBytes = dataSize - readBytes;
            if (unreadBytes > 0)
            {
                std::fill_n(data.get() + readBytes, unreadBytes, 0);
                context->LogWarning(ObjectError::badImageTable, "Image table size shorter than expected.");
            }

            _data = std::move(data);
            _entries.insert(_entries.end(), newEntries.begin(), newEntries.end());
        }
        catch (const std::exception&)
        {
            context->LogError(ObjectError::badImageTable, "Bad image table.");
            throw;
        }
    }

    std::vector<std::pair<std::string, Image>> ImageTable::GetImageSources(IReadObjectContext* context, json_t& jsonImages)
    {
        std::vector<std::pair<std::string, Image>> result;
        for (auto& jsonImage : jsonImages)
        {
            if (jsonImage.is_object() && jsonImage.contains("path"))
            {
                auto path = Json::GetString(jsonImage["path"]);
                auto keepPalette = Json::GetString(jsonImage["palette"]) == "keep";
                auto itSource = std::find_if(result.begin(), result.end(), [&path](const std::pair<std::string, Image>& item) {
                    return item.first == path;
                });
                if (itSource == result.end())
                {
                    auto imageData = context->GetData(path);
                    auto imageFormat = keepPalette ? ImageFormat::png : ImageFormat::png32;
                    auto image = Imaging::ReadFromBuffer(imageData, imageFormat);
                    auto pair = std::make_pair<std::string, Image>(std::move(path), std::move(image));
                    result.push_back(std::move(pair));
                }
            }
        }
        return result;
    }

    bool ImageTable::ReadJson(IReadObjectContext* context, json_t& root)
    {
        Guard::Assert(root.is_object(), "ImageTable::ReadJson expects parameter root to be object");

        bool usesFallbackSprites = false;

        if (context->ShouldLoadImages())
        {
            // First gather all the required images from inspecting the JSON
            std::vector<std::unique_ptr<RequiredImage>> allImages;
            auto jsonImages = root["images"];
            if (!IsCsgLoaded() && root.contains("noCsgImages"))
            {
                jsonImages = root["noCsgImages"];
                usesFallbackSprites = true;
            }

            auto imageSources = GetImageSources(context, jsonImages);

            for (auto& jsonImage : jsonImages)
            {
                if (jsonImage.is_string())
                {
                    auto strImage = jsonImage.get<std::string>();
                    auto images = ParseImages(context, strImage);
                    allImages.insert(
                        allImages.end(), std::make_move_iterator(images.begin()), std::make_move_iterator(images.end()));
                }
                else if (jsonImage.is_object())
                {
                    if (jsonImage.contains("gx"))
                    {
                        auto xOverride = Json::GetNumber<int16_t>(jsonImage["x"], std::numeric_limits<int16_t>::max());
                        auto yOverride = Json::GetNumber<int16_t>(jsonImage["y"], std::numeric_limits<int16_t>::max());
                        const bool hasXOverride = xOverride != std::numeric_limits<int16_t>::max();
                        const bool hasYOverride = yOverride != std::numeric_limits<int16_t>::max();

                        auto strImage = jsonImage["gx"].get<std::string>();
                        auto images = ParseImages(context, strImage);

                        if (hasXOverride || hasYOverride)
                        {
                            for (auto& image : images)
                            {
                                if (hasXOverride)
                                    image->g1.xOffset = xOverride;
                                if (hasYOverride)
                                    image->g1.yOffset = yOverride;
                            }
                        }

                        allImages.insert(
                            allImages.end(), std::make_move_iterator(images.begin()), std::make_move_iterator(images.end()));
                    }
                    else
                    {
                        auto images = ParseImages(context, imageSources, jsonImage);
                        allImages.insert(
                            allImages.end(), std::make_move_iterator(images.begin()), std::make_move_iterator(images.end()));
                    }
                }
            }

            // Now add all the images to the image table
            auto imagesStartIndex = GetCount();
            for (const auto& img : allImages)
            {
                const auto& g1 = img->g1;
                AddImage(&g1);
            }

            // Add all the zoom images at the very end of the image table.
            // This way it should not affect the offsets used within the object logic.
            for (size_t j = 0; j < allImages.size(); j++)
            {
                const auto tableIndex = imagesStartIndex + j;
                const auto* img = allImages[j].get();
                if (img->next_zoom != nullptr)
                {
                    img = img->next_zoom.get();

                    // Set old image zoom offset to zoom image which we are about to add
                    auto g1a = const_cast<G1Element*>(&GetImages()[tableIndex]);
                    g1a->zoomedOffset = static_cast<int32_t>(tableIndex) - static_cast<int32_t>(GetCount());

                    while (img != nullptr)
                    {
                        auto g1b = img->g1;
                        if (img->next_zoom != nullptr)
                        {
                            g1b.zoomedOffset = -1;
                        }
                        AddImage(&g1b);
                        img = img->next_zoom.get();
                    }
                }
            }
        }

        _objDataCache.clear();
#ifdef __3DS__
        _legacyImageCache.clear();
#endif

        return usesFallbackSprites;
    }

    void ImageTable::AddImage(const G1Element* g1)
    {
        G1Element newg1 = *g1;
        auto length = G1CalculateDataSize(g1);
        if (length == 0)
        {
            newg1.offset = nullptr;
        }
        else
        {
            newg1.offset = new uint8_t[length];
            std::copy_n(g1->offset, length, newg1.offset);
        }
        _entries.push_back(std::move(newg1));
    }

    void ImageTable::addPalette(const G1Palette& g1)
    {
        Guard::Assert(g1.flags.has(G1Flag::isPalette));
        const auto base = reinterpret_cast<const G1Element*>(&g1);
        AddImage(base);
    }
} // namespace OpenRCT2
