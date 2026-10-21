/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "Guard.hpp"

#include <cstdlib>
#include <cstring>
#include <typeinfo>

#ifdef __3DS__
// See platform/Memory.3DS.cpp: large blocks live in the console's linear heap.
namespace OpenRCT2::N3DSMemory
{
    void* Alloc(size_t size);
    void* Realloc(void* ptr, size_t size);
    void Free(void* ptr);
    size_t LinearFree();
    void DumpEarlyAllocs();
} // namespace OpenRCT2::N3DSMemory
    #define OPENRCT2_MALLOC(size) ::OpenRCT2::N3DSMemory::Alloc(size)
    #define OPENRCT2_REALLOC(ptr, size) ::OpenRCT2::N3DSMemory::Realloc(ptr, size)
    #define OPENRCT2_FREE(ptr) ::OpenRCT2::N3DSMemory::Free(ptr)
#else
    #define OPENRCT2_MALLOC(size) malloc(size)
    #define OPENRCT2_REALLOC(ptr, size) realloc(ptr, size)
    #define OPENRCT2_FREE(ptr) free(ptr)
#endif

/**
 * Utility methods for memory management. Typically helpers and wrappers around the C standard library.
 */
namespace OpenRCT2::Memory
{
    template<typename T>
    static T* Allocate()
    {
        T* result = static_cast<T*>(OPENRCT2_MALLOC(sizeof(T)));
        Guard::ArgumentNotNull(result, "Failed to allocate %zu bytes for %s", sizeof(T), typeid(T).name());
        return result;
    }

    template<typename T>
    static T* Allocate(size_t size)
    {
        T* result = static_cast<T*>(OPENRCT2_MALLOC(size));
        Guard::ArgumentNotNull(result, "Failed to allocate %zu bytes for %s", size, typeid(T).name());
        return result;
    }

    template<typename T>
    static T* Reallocate(T* ptr, size_t size)
    {
        T* result;
        if (ptr == nullptr)
        {
            result = static_cast<T*>(OPENRCT2_MALLOC(size));
        }
        else
        {
            result = static_cast<T*>(OPENRCT2_REALLOC(reinterpret_cast<void*>(ptr), size));
        }
        Guard::ArgumentNotNull(result, "Failed to reallocate %x (%s) to have %zu bytes", ptr, typeid(T).name(), size);
        return result;
    }

    template<typename T>
    static void Free(T* ptr)
    {
        OPENRCT2_FREE(const_cast<void*>(reinterpret_cast<const void*>(ptr)));
    }

} // namespace OpenRCT2::Memory
