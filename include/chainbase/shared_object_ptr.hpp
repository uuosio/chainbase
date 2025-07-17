#pragma once

#include <boost/container/container_fwd.hpp>
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

#include <cstddef>
#include <cstring>
#include <algorithm>
#include <string>

#include <chainbase/pinnable_mapped_file.hpp>
#include <chainbase/shared_object_allocator.hpp>
#include <chainbase/chainbase_node_allocator.hpp>

namespace chainbase {
    namespace bip = boost::interprocess;

    template<typename T>
    class shared_object_ptr {
    public:

        shared_object_ptr(const allocator_type& alloc) : _data_ptr_offset(0) {
            uint64_t id = database_get_unique_segment_manager_id(alloc.get_segment_manager());
            if (id > max_segment_manager_id || id == 0) {
                std::stringstream ss;
                ss << "2: shared_object: invalid segment_manager_id: " << id;
                BOOST_THROW_EXCEPTION(std::runtime_error("2: shared_object: invalid segment_manager_id"));
            }
            _segment_manager_id = id;
        }

        explicit shared_object_ptr(const shared_object_allocator& alloc) : shared_object_ptr(*alloc.get_first_allocator()) {
        }

        shared_object_ptr(const allocator_type& alloc, const T& obj) : _data_ptr_offset(0) {
            uint64_t id = database_get_unique_segment_manager_id(alloc.get_segment_manager());
            if (id > max_segment_manager_id || id == 0) {
                std::stringstream ss;
                ss << "2: shared_object: invalid segment_manager_id: " << id;
                BOOST_THROW_EXCEPTION(std::runtime_error("2: shared_object: invalid segment_manager_id"));
            }
            _segment_manager_id = id;
            set_offset(&obj);
        }

        shared_object_ptr(const shared_object_allocator& alloc, const T& obj) : shared_object_ptr(*alloc.get_first_allocator(), obj) {
        }

        shared_object_ptr(const shared_object_ptr& other): _data_ptr_offset(other._data_ptr_offset), _segment_manager_id(other._segment_manager_id) {
        }

        shared_object_ptr(shared_object_ptr&& other) : _data_ptr_offset(other._data_ptr_offset), _segment_manager_id(other._segment_manager_id) {
            other._data_ptr_offset = 0;
            other._segment_manager_id = 0;
        }

        shared_object_ptr& operator=(const shared_object_ptr& other) {
            if (this != &other) {
                _data_ptr_offset = other._data_ptr_offset;
                _segment_manager_id = other._segment_manager_id;
            }
            return *this;
        }

        shared_object_ptr& operator=(shared_object_ptr&& other) {
            if (this != &other) {
                _data_ptr_offset = other._data_ptr_offset;
                _segment_manager_id = other._segment_manager_id;

                other._data_ptr_offset = 0;
                other._segment_manager_id = 0;
            }
            return *this;
        }

        ~shared_object_ptr() {
        }

        bool operator==(const shared_object_ptr& rhs) const {
            if (_data_ptr_offset == 0 && rhs._data_ptr_offset == 0) {
                return true;
            }

            if (_data_ptr_offset == 0 || rhs._data_ptr_offset == 0) {
                return false;
            }

            return get() == rhs.get();
        }

        bool operator!=(const shared_object_ptr& rhs) const { return !(*this == rhs); }

        // allocator_type get_allocator() const {
        //     return allocator_type(allocator_get_segment_manager_by_id(_segment_manager_id));
        // }

        chainbase::pinnable_mapped_file::segment_manager* get_segment_manager() const {
            return allocator_get_segment_manager_by_id(_segment_manager_id);
        }

        T& get() const {
            if (get_offset() == 0) {
                BOOST_THROW_EXCEPTION(std::runtime_error("shared_object: invalid offset"));
            }
            return *reinterpret_cast<T*>(uint64_t(get_segment_manager()) + get_offset());
        }

        T& operator*() {
            return get();
        }
        
        T& operator*() const {
            return get();
        }

        T *operator->() {
            return &get();
        }

        T *operator->() const {
            return &get();
        }

        uint64_t get_offset() const {
            return _data_ptr_offset;
        }

        void set_offset(const T *ptr) const {
            auto *manager = get_segment_manager();
            if (uint64_t(ptr) > uint64_t(manager) && uint64_t(ptr) < uint64_t(manager) + manager->get_size()) {
                // valid pointer
            } else {
                printf("shared_object_ptr: invalid pointer, ptr: %p, manager: %p, size: %zu\n", ptr, manager, manager->get_size());
                BOOST_THROW_EXCEPTION( std::runtime_error("shared_object_ptr: invalid pointer") );
            }
            _data_ptr_offset = uint64_t(ptr) - uint64_t(manager);
        }

        uint64_t get_segment_manager_id() const {
            return _segment_manager_id;
        }

    private:
        mutable uint64_t _data_ptr_offset:48;
        mutable uint64_t _segment_manager_id:16;
    };

} // namespace chainbase
