#include <boost/test/unit_test.hpp>
#include <chainbase/chainbase.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>

#include <boost/container/flat_map.hpp>
#include <optional>
#include <iostream>
#include <chrono>
#include <vector>

#include <chainbase/pinnable_mapped_file.hpp>
#include <chainbase/secondary_index.hpp>

using namespace chainbase;
using namespace boost::multi_index;

using segment_manager_map_t = boost::container::flat_map<void*, void *>;
static segment_manager_map_t                  _segment_manager_map;

template<typename T>
static std::optional<allocator<T>> get_allocator(void *object) {
   if (!_segment_manager_map.empty()) {
      auto it = _segment_manager_map.upper_bound(object);
      if(it == _segment_manager_map.begin())
         return {};
      auto [seg_start, seg_end] = *(--it);
      // important: we need to check whether the pointer is really within the segment, as shared objects'
      // can also be created on the stack (in which case the data is actually allocated on the heap using
      // std::allocator). This happens for example when `shared_cow_string`s are inserted into a bip::multimap,
      // and temporary pairs are created on the stack by the bip::multimap code.
      if (object < seg_end)
         return allocator<T>(reinterpret_cast<segment_manager *>(seg_start));
   }
   return {};
}

BOOST_AUTO_TEST_CASE( benchmark_find_segment_manager_test ) {
    const size_t segment_size = 4096;
    const size_t gap_size = 1024;

    for (size_t num_segments = 1; num_segments <= 100000; num_segments*=10) {
        // Use a static vector to avoid stack overflow for large allocations.
        static std::vector<char> memory_pool(num_segments * (segment_size + gap_size));
        std::vector<void*> test_pointers;
        test_pointers.reserve(num_segments * 2 + 3);

        _segment_manager_map.clear(); // Ensure the map is empty before the test

        for (size_t i = 0; i < num_segments; ++i) {
            char* seg_start_char = memory_pool.data() + i * (segment_size + gap_size);
            void* seg_start = seg_start_char;
            void* seg_end = seg_start_char + segment_size;
            _segment_manager_map[seg_start] = seg_end;

            // Create a pointer inside the segment
            test_pointers.push_back(seg_start_char + segment_size / 2);
        }

        // Add some pointers that are not in any segment (in the gaps)
        for (size_t i = 0; i < num_segments; ++i) {
            char* gap_start_char = memory_pool.data() + i * (segment_size + gap_size) + segment_size;
            test_pointers.push_back(gap_start_char + gap_size / 2);
        }

        // A pointer before the first segment
        test_pointers.push_back(memory_pool.data() - 1);
        // A pointer after the last segment
        test_pointers.push_back(memory_pool.data() + memory_pool.size());
        // A null pointer
        test_pointers.push_back(nullptr);


        std::cout << "\nBenchmarking get_allocator with " << _segment_manager_map.size() << " segments..." << std::endl;

        auto start = std::chrono::steady_clock::now();
        uint64_t total_calls = 0;

        // Run for a few seconds to get a stable measurement
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            for (void* p : test_pointers) {
                volatile auto alloc = get_allocator<char>(p);
                (void)alloc;
                total_calls++;
            }
        }

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double seconds = duration.count() / 1e6;
        double calls_per_second = total_calls / seconds;

        std::cout << "Total calls: " << total_calls << std::endl;
        std::cout << "Elapsed time: " << seconds << " s" << std::endl;
        std::cout << "Average calls per second: " << std::fixed << calls_per_second << std::endl;

        _segment_manager_map.clear(); // Clean up to not affect other tests
    }

}
