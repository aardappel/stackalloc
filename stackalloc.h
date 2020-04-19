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

#include <cstdint>
#include <cstring>
#include <cassert>

/*

A library that implements functionality similar to what you'd normally
get from using std::vector, except with:
1) Allows interior pointers, since memory never moves (!).
2) Super cheap buffer allocation (pointer bump).
3) Super cheap push_back (pointer bump, no bounds check needed).

*/


namespace sa {

// Low level functions implementing platform specific ways to obtain
// large chunks of growable address space.

uint8_t *alloc_stack_address_space(size_t size);
void dealloc_stack_address_space(uint8_t *mem, size_t size);

struct stack {
    uint8_t *sp = nullptr;
    uint8_t *memory = nullptr;
    size_t size = 0;

    stack() {}

    bool alloc(size_t _size) {
        sp = memory = alloc_stack_address_space(size = _size);
        return memory != nullptr;
    }

    ~stack() {
        if (memory) {
            dealloc_stack_address_space(memory, size);
        }
    }

    stack(const stack &) = delete;
    stack(stack &&) = delete;
    stack &operator=(const stack &) = delete;
};

// This "basic" version needs to be explictly supplied a stack to allocate
// on, which may be useful when more control/speed is required.
template<typename T>
struct basic_vector {
    uint8_t *end;
    uint8_t *begin;

    // No allocation!
    basic_vector(uint8_t *sp) : end(sp), begin(sp) {}

    // No de-allocation!
    ~basic_vector() {
    }

    // No (re) allocation, no capacity check.
    void push_back(const T &t) {
        // FIXME: avoid copy / construct in place?
        memcpy(end, &t, sizeof(T));
        end += sizeof(T);
    }

    void pop_back() {
        assert(end > begin);
        end -= sizeof(T);
    }

    T &operator[](size_t i) {
        assert(i * sizeof(T) < static_cast<size_t>(end - begin));
        return *(reinterpret_cast<T *>(begin) + i);
    }

    T &back() {
        assert(end > begin);
        return *(reinterpret_cast<T *>(end) - 1);
    }

    T &pop() {
        assert(end > begin);
        end -= sizeof(T);
        return *(reinterpret_cast<T *>(end));
    }

    size_t size() { return (begin - end) / sizeof(T); }

    void push_multiple(const T *elems, size_t size) {
        memcpy(end, elems, size * sizeof(T));
        end += size * sizeof(T);
    }
};


stack *acquire_stack();
void release_stack();

// This one automatically acquires a stack and holds on to it for its lifetime.
// This is for cases where the max size is not known, or very variable.
// Most users want to be using this one by default.
template<typename T>
struct vector : basic_vector<T> {
    vector() : basic_vector<T>(acquire_stack()->sp) {}
    ~vector() { release_stack(); }
};


// This one has a fixed capacity, so does NOT hold on to a stack,
// and thus can share stack storage with others.
template<typename T>
struct vector_max : basic_vector<T> {
    stack *st;
    uint8_t *capacity;

    vector_max(size_t max)
        : basic_vector<T>(nullptr), st(acquire_stack()), capacity(st->sp + max * sizeof(T)) {
        this->begin = this->end = st->sp;  // FIXME: do not repeat this?
        st->sp += max * sizeof(T);
        release_stack();
    }

    ~vector_max() {
        st->sp = this->begin;
    }

    void push_back(const T &t) {
        assert(this->end < capacity);
        memcpy(this->end, &t, sizeof(T));
        this->end += sizeof(T);
    }
};


// This one is fixed at creation time. Useful for strings and such.
// Also doesn't hold on to a stack.
// FIXME: probably shouldn't inherit because we don't want to allow push/pop.
template<typename T>
struct vector_fixed : basic_vector<T> {
    stack *st;

    vector_fixed(stack *st, T *t, size_t len) : basic_vector<T>(st), st(st) {
        memcpy(begin, t, len * sizeof(T));
        st->sp = this->end = begin + len * sizeof(T);
    }

    ~vector_fixed() {
        st->sp = this->begin();
    }
};


// Since these vectors can safely have interior pointers, we can do more things
// with them, like this one can have arbitrary elements reused.
template<typename T>
struct vector_pool : vector<T> {
    // Woah, we're using one of our own vectors as freelist!
    // This will cause vector_pool to lock 2 stacks during its lifetime.
    // Also fun: we track elements as pointers, because why not.
    vector<T *> free_list;

    // Prefer to use these methods to create new elements instead of
    // push_back, though push_back still works if you know you don't need
    // to reuse (such as at the start).
    T &alloc(const T &t) {
        if (free_list.size()) {
            auto tn = free_list.pop();
            // FIXME: avoid copy / construct in place?
            return *tn = t;
        } else {
            this->push_back(t);
            return this->back();
        }
    }

    // Different name from "free" to indicate what it really does.
    // Note that this does not produce dangling pointers: the elements
    // we free stays a legit initialized object, until its overwritten.
    void reuseable(T &t) {
        assert(reinterpret_cast<uint8_t *>(&t) >= this->begin &&
               reinterpret_cast<uint8_t *>(&t) < this->end);
        free_list.push_back(&t);
    }
};

template<typename T, typename S>
struct vector_nested {
    uint8_t *start;

    size_t size() { return static_cast<size_t>(*reinterpret_cast<S *>(start)); }
    T *begin() { return reinterpret_cast<T *>(start + sizeof(S)); }
};

// How about a vector of vectors, all inline?
// This is now more practical than with std::vector, since we can now
// have pointers to the interior vectors and pass them on.
// This essentially is a vector<vector_fixed>, though the C++
// typesystem doesn't understand variable sized types.
// The template parameter S is for the size type of interior vectors,
// for example this could be uint8_t if you wanted to store lots of
// small strings compactly.
// TODO: make it work with any variable length thing, not just vector_nested.
template<typename T, typename S>
struct vector_of_vectors {
    vector<uint8_t> buf;

    vector_nested<T,S> push_back(const T *elems, size_t size) {
        auto vn = vector_nested<T,S> { buf->begin };
        auto st = static_cast<S>(size);
        buf.push_multiple(reinterpret_cast<const uint8_t *>(&st), sizeof(S));
        buf.push_multiple(reinterpret_cast<const uint8_t *>(elems), size sizeof(T));
        return vn;
    }
};

}  // namespace sa
