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
    namespace bip = boost::interprocess;

    using allocator_type = bip::allocator<char, chainbase::pinnable_mapped_file::segment_manager>;
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
} // namespace chainbase
