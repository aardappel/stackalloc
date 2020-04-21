// Copyright 2020 Wouter van Oortmerssen. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Normally a fan of header only libraries, but this .cpp is worth it
// to not polute users of these functions with all the system
// specific headers below:

#include "stackalloc.h"

#include <vector>
#include <assert.h>

#ifdef _WIN32
    #define VC_EXTRALEAN
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <memoryapi.h>
#else
    #include <sys/mman.h>
#endif

namespace sa {

#ifdef _WIN32

size_t page_size = 0;
enum {
    // 1MB at a time, just in case these exceptions are expensive, wouldn't
    // want to do it for every single page.
    COMMIT_PAGES_AT_ONCE = 256,
    // Guard pages is the traditional way to do this, but they only support
    // linear initial access. Without a guard page we can support random
    // access which seems more flexible?
    USE_GUARD_PAGES = 0
};

bool commit_and_guard(uint8_t *vp) {
    auto page_increment = page_size * COMMIT_PAGES_AT_ONCE;
    return VirtualAlloc(vp, page_increment, MEM_COMMIT, PAGE_READWRITE) &&
           (!USE_GUARD_PAGES ||
            VirtualAlloc(vp + page_increment, page_size, MEM_COMMIT,
                         PAGE_READWRITE | PAGE_GUARD));
}

LONG WINAPI guard_page_exception_filter(_EXCEPTION_POINTERS *ep) {
    if (ep->ExceptionRecord->ExceptionCode !=
        (USE_GUARD_PAGES ? STATUS_GUARD_PAGE_VIOLATION
                         : STATUS_ACCESS_VIOLATION)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // We hit a guard or reserved page. Find start of page.
    auto hit_address = ep->ExceptionRecord->ExceptionInformation[1];
    auto page_start = (uint8_t *)((size_t)hit_address & ~(page_size - 1));
    if (USE_GUARD_PAGES) page_start += page_size;
    // Commit new pages and/or reinstall our guard page.
    // If this is an access violation but it was outside our reserved space,
    // then VirtualAlloc should fail and turn it into a regular exception.
    // FIXME: If this is a page guard hit, we should really be checking
    // if it is our page guard, not someone elses (e.g. the execution stack?)
    return commit_and_guard(page_start) ? EXCEPTION_CONTINUE_EXECUTION
                                        : EXCEPTION_CONTINUE_SEARCH;
}

uint8_t *alloc_stack_address_space(size_t size) {
    if (!page_size) {
        // SetUnhandledExceptionFilter doesn't actually stop on
        // STATUS_GUARD_PAGE_VIOLATION, but this one does:
        if (!AddVectoredExceptionHandler(1, &guard_page_exception_filter)) {
            return nullptr;
        }
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        page_size = sys_info.dwPageSize;
    }
    auto vp = static_cast<uint8_t *>(VirtualAlloc(0, size, MEM_RESERVE, PAGE_READWRITE));
    return vp && commit_and_guard(vp) ? vp : nullptr;
}

void dealloc_stack_address_space(uint8_t *mem, size_t) {
    VirtualFree(mem, 0, MEM_RELEASE);
}

#else

uint8_t *alloc_stack_address_space(size_t size) {
    auto vp = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
    return vp == MAP_FAILED ? nullptr : vp;
}

void dealloc_stack_address_space(uint8_t *mem, size_t size) {
    munmap(mem, size);
}

#endif


// Implementation of automatic stack management.

static size_t locked = 0;
static size_t allocated = 0;
static const size_t DEFAULT_LARGE_STACK = 1ULL << 36;  // 64GB? Why not?
static const size_t DEFAULT_MAX_STACKS = 1ULL << 10;
static stack stacks[DEFAULT_MAX_STACKS];

stack *acquire_stack() {
    if (allocated == locked) {
        if (allocated == DEFAULT_MAX_STACKS) {
            // We should really never get here unless we're being called
            // in a non-stack way.
            assert(false);
            abort();
        }
        if (!stacks[allocated++].alloc(DEFAULT_LARGE_STACK)) {
            // System doesn't like us allocating this much address space?
            assert(false);
            abort();
        }
    }
    return &stacks[locked++];
}

void release_stack() {
    locked--;
}

}  // namespace sa
