stackalloc
=========

A quick prototype for vector-like containers based on address space
reservation.

This has the following advantages:

1) Allows interior pointers, since memory never moves (!).
2) Super cheap buffer allocation (pointer bump).
3) Super cheap `push_back` (pointer bump, no bounds check needed).

The quick benchmark in `test.cpp` shows the following speedup
over `std::vector` (on a 8700K CPU on Windows):

* Clang 10 release:

    ```
    [5 elems] stackalloc: 0.0032, stl naive: 0.1376, stl opt: 0.0302, ratio: 42.47x / 9.32x faster!
    [50 elems] stackalloc: 0.0328, stl naive: 0.3030, stl opt: 0.0782, ratio: 9.25x / 2.39x faster!
    [500 elems] stackalloc: 0.2966, stl naive: 1.0705, stl opt: 0.6607, ratio: 3.61x / 2.23x faster!
    ```

* VS2019 release:

    ```
    [5 elems] stackalloc: 0.0057, stl naive: 0.1555, stl opt: 0.0275, ratio: 27.46x / 4.85x faster!
    [50 elems] stackalloc: 0.0657, stl naive: 0.3761, stl opt: 0.0883, ratio: 5.72x / 1.34x faster!
    [500 elems] stackalloc: 0.6485, stl naive: 1.2304, stl opt: 0.7924, ratio: 1.90x / 1.22x faster!
    ```

* VS2019 debug:

    ```
    [5 elems] stackalloc: 0.0802, stl naive: 1.2429, stl opt: 0.5559, ratio: 15.49x / 6.93x faster!
    [50 elems] stackalloc: 0.6124, stl naive: 4.4576, stl opt: 2.6846, ratio: 7.28x / 4.38x faster!
    [500 elems] stackalloc: 5.5520, stl naive: 26.9562, stl opt: 23.5752, ratio: 4.86x / 4.25x faster!

    ```

Since on Windows the minimum address space reservation is 64KB, the code in this repo is
such that these "stacks" that back vector memory are shared between vectors where
possible (thru RAII), such that even small vectors are cheap.

Disadvantages:

* You need to manage all your memory in vectors owned by local variables, rather than using dynamic memory directly. This is actually not much of a limitation anymore, since these vectors allow interior pointers, so they can function as general allocators with no limitations.
* Since it relies on address space reservation, it does not work on platforms that do not
  support `mmap` (or `VirtualAlloc`), like some IOT / embedded platforms, and currently also
  WebAssembly (https://github.com/WebAssembly/memory64/issues/4)