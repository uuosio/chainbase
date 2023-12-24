#pragma once

#include <boost/container/container_fwd.hpp>
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

#include <cstddef>
#include <cstring>
#include <algorithm>
#include <string>

#include <chainbase/pinnable_mapped_file.hpp>

namespace chainbase {
    const static uint64_t max_segment_manager_id = std::numeric_limits<uint16_t>::max();
    const static uint64_t default_segment_manager_cache_size = 1000;

    namespace bip = boost::interprocess;
    using segment_manager = bip::managed_mapped_file::segment_manager;
    using allocator_type = bip::allocator<char, segment_manager>;
    using allocator_pointer = bip::offset_ptr<allocator_type>;

    class shared_object_allocator: public allocator_type {
    public:
        shared_object_allocator(allocator_pointer a1, allocator_pointer a2) 
        : allocator_type(a1->get_segment_manager()), _alloc1(a1), _alloc2(a2)
        {
        }

        shared_object_allocator(shared_object_allocator&&) = delete;
        shared_object_allocator(const shared_object_allocator&) = delete;
        shared_object_allocator& operator=(shared_object_allocator&&) = delete;
        shared_object_allocator& operator=(const shared_object_allocator&) = delete;

        allocator_pointer get_first_allocator() { return _alloc1; }
        allocator_pointer get_second_allocator() { return _alloc2; }

    private:
        allocator_pointer _alloc1;
        allocator_pointer _alloc2;
    };

    void allocator_set_segment_manager(uint64_t segment_manager_id, segment_manager *manager);
    size_t allocator_get_segment_manager_id(segment_manager *manager);
    segment_manager *allocator_get_segment_manager_by_id(uint64_t manager_manager_id);

    uint64_t database_get_unique_segment_manager_id(segment_manager *manager); //implemented in database.cpp
    uint64_t database_get_writable_segment_manager_id(segment_manager *manager);
} // namespace chainbase
