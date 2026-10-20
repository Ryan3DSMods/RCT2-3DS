/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

// Nintendo 3DS memory routing.
//
// The kernel limits the ordinary (malloc) heap to a 96 MiB address range, while the rest of
// the application's memory is only reachable through the "linear" heap. OpenRCT2 keeps several
// very large blocks alive for the whole session (the entity table, the tile element array,
// g1.dat, object images), so large C++ allocations are served from the linear heap and only
// small ones from malloc. Frees are routed by address range.

#ifdef __3DS__

    #include "../core/Memory.hpp"

    #include <3ds.h>
    #include <algorithm>
    #include <cstdint>
    #include <cstdlib>
    #include <cstring>
    #include <malloc.h>
    #include <new>

extern "C" {
extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;
}

namespace OpenRCT2::N3DSMemory
{
    static constexpr size_t kLinearThreshold = 256 * 1024;

    static LightLock _lock;
    static bool _lockInitialised = false;

    static void Lock()
    {
        if (!_lockInitialised)
        {
            LightLock_Init(&_lock);
            _lockInitialised = true;
        }
        LightLock_Lock(&_lock);
    }

    static void Unlock()
    {
        LightLock_Unlock(&_lock);
    }

    static bool IsLinear(const void* ptr)
    {
        auto address = reinterpret_cast<uintptr_t>(ptr);
        return address >= __ctru_linear_heap && address < __ctru_linear_heap + __ctru_linear_heap_size;
    }

    static void* LinearAlloc(size_t size)
    {
        Lock();
        void* result = linearAlloc(size);
        Unlock();
        return result;
    }

    static void LinearFree(void* ptr)
    {
        Lock();
        linearFree(ptr);
        Unlock();
    }

    void* Alloc(size_t size)
    {
        if (size >= kLinearThreshold)
        {
            if (void* result = LinearAlloc(size))
                return result;
        }
        if (void* result = std::malloc(size))
            return result;
        // malloc heap exhausted: the linear heap is the last resort for small blocks too.
        return LinearAlloc(size);
    }

    void Free(void* ptr)
    {
        if (ptr == nullptr)
            return;
        if (IsLinear(ptr))
            LinearFree(ptr);
        else
            std::free(ptr);
    }

    void* Realloc(void* ptr, size_t size)
    {
        if (ptr == nullptr)
            return Alloc(size);
        if (!IsLinear(ptr) && size < kLinearThreshold)
            return std::realloc(ptr, size);

        size_t oldSize = IsLinear(ptr) ? linearGetSize(ptr) : malloc_usable_size(ptr);
        void* result = Alloc(size);
        if (result == nullptr)
            return nullptr;
        std::memcpy(result, ptr, std::min(oldSize, size));
        Free(ptr);
        return result;
    }

    size_t LinearFree()
    {
        return linearSpaceFree();
    }
} // namespace OpenRCT2::N3DSMemory

using namespace OpenRCT2;

void* operator new(std::size_t size)
{
    void* result = N3DSMemory::Alloc(size != 0 ? size : 1);
    if (result == nullptr)
        throw std::bad_alloc();
    return result;
}

void* operator new[](std::size_t size)
{
    void* result = N3DSMemory::Alloc(size != 0 ? size : 1);
    if (result == nullptr)
        throw std::bad_alloc();
    return result;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return N3DSMemory::Alloc(size != 0 ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return N3DSMemory::Alloc(size != 0 ? size : 1);
}

void operator delete(void* ptr) noexcept
{
    N3DSMemory::Free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    N3DSMemory::Free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
    N3DSMemory::Free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
    N3DSMemory::Free(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    N3DSMemory::Free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    N3DSMemory::Free(ptr);
}

#endif // __3DS__
