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

#include "stackalloc.h"

#include <vector>

#ifdef _WIN32  // FIXME
	#include <windows.h>
#endif

// Quick benchmarking helper.
template<typename F> double time_function(size_t max, F f) {
    #ifdef _WIN32
		LARGE_INTEGER time_frequency, time_start, time_end;
		QueryPerformanceFrequency(&time_frequency);
		QueryPerformanceCounter(&time_start);
		for (size_t i = 0; i < max; i++) f();
		QueryPerformanceCounter(&time_end);
		return double(time_end.QuadPart - time_start.QuadPart) / double(time_frequency.QuadPart);
    #else
		for (size_t i = 0; i < max; i++) f();
		return 0;
    #endif
}

int main() {

	const size_t num_iters = 1;// 100000;
	int sum = 0;

	for (int num_elems : { 5, 50, 500 }) {

		auto push_access_pop = [&](auto &v) {
			for (int i = 0; i < num_elems; i++) v.push_back(i);
			for (int i = 0; i < num_elems; i++) sum += v[i];
			for (int i = 0; i < num_elems; i++) v.pop_back();
			sum += (int)v.size();
		};

		// Bench this vector:
		auto time1 = time_function(num_iters, [&]() {
			// A vector with reserved storage, uses first stack, but does
			// not "lock" it.
			sa::vector_max<int> vm(num_elems);
			push_access_pop(vm);
			// Uses the first stack also, shares storage with `vm`, but
			// locks this stack for its lifetime because its of unknown size.
			sa::vector<int> v1;
			push_access_pop(v1);
			{
				// Uses a second stack automatically since the first one is locked.
				sa::vector<int> v2;
				push_access_pop(v2);
			}
			{
				// Re-uses the second stack, since that one is unlocked by now.
				sa::vector<int> v3;
				push_access_pop(v3);
			}
		});

		// VS the STL naively (no reserve):
		auto time2 = time_function(num_iters, [&]() {
			std::vector<int> vm;
			push_access_pop(vm);
			std::vector<int> v1;
			push_access_pop(v1);
			{
				std::vector<int> v2;
				push_access_pop(v2);
			}
			{
				std::vector<int> v3;
				push_access_pop(v3);
			}
		});

		// VS the STL optimally (with reserve):
		auto time3 = time_function(num_iters, [&]() {
			std::vector<int> vm;
			vm.reserve(num_elems);
			push_access_pop(vm);
			std::vector<int> v1;
			v1.reserve(num_elems);
			push_access_pop(v1);
			{
				std::vector<int> v2;
				v2.reserve(num_elems);
				push_access_pop(v2);
			}
			{
				std::vector<int> v3;
				v3.reserve(num_elems);
				push_access_pop(v3);
			}
		});

		printf("[%d elems] stackalloc: %.4f, stl naive: %.4f, stl opt: %.4f, ratio: %.2fx / %.2fx faster!\n",
			   num_elems, time1, time2, time3, time2 / time1, time3 / time1);

	}

    /*
	RESULTS:
	8700K CPU

	Clang 10 release:

	[5 elems] stackalloc: 0.0032, stl naive: 0.1376, stl opt: 0.0302, ratio: 42.47x / 9.32x faster!
    [50 elems] stackalloc: 0.0328, stl naive: 0.3030, stl opt: 0.0782, ratio: 9.25x / 2.39x faster!
    [500 elems] stackalloc: 0.2966, stl naive: 1.0705, stl opt: 0.6607, ratio: 3.61x / 2.23x faster!

	VS2019 release:

	[5 elems] stackalloc: 0.0057, stl naive: 0.1555, stl opt: 0.0275, ratio: 27.46x / 4.85x faster!
	[50 elems] stackalloc: 0.0657, stl naive: 0.3761, stl opt: 0.0883, ratio: 5.72x / 1.34x faster!
	[500 elems] stackalloc: 0.6485, stl naive: 1.2304, stl opt: 0.7924, ratio: 1.90x / 1.22x faster!

	VS2019 debug:

	[5 elems] stackalloc: 0.0802, stl naive: 1.2429, stl opt: 0.5559, ratio: 15.49x / 6.93x faster!
	[50 elems] stackalloc: 0.6124, stl naive: 4.4576, stl opt: 2.6846, ratio: 7.28x / 4.38x faster!
	[500 elems] stackalloc: 5.5520, stl naive: 26.9562, stl opt: 23.5752, ratio: 4.86x / 4.25x faster!

	*/

	// More examples.

	struct MyObject { int a; };
	// Is it a vector, an allocator.. who knows?
	sa::vector_pool<MyObject> pool;
	auto &o1 = pool.alloc({ 1 });
	auto &o2 = pool.alloc({ 2 });
	auto &o3 = pool.alloc({ 3 });
	// Let's free one in the middle:
	pool.reuseable(o2);
	// Still legal to access, we've only signed it up for overwriting.
	assert(o2.a == 2);
	auto &o4 = pool.alloc({ 4 });
	// Overwritten now.
	assert(o2.a == 4);
	assert(o4.a == 4);
	assert(&o2 == &o4);

 	return 0;
}

