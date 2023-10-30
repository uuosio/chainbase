#pragma once

#include <boost/container/container_fwd.hpp>
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

#include <cstddef>
#include <cstring>
#include <algorithm>
#include <string>

#include <chainbase/pinnable_mapped_file.hpp>
#include <chainbase/chainbase_node_allocator.hpp>

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

    template<typename T>
    class shared_object {
    public:        
        // !!alloc must be a persistent value in a database
        explicit shared_object(shared_object_allocator& alloc)
        : _data_ptr_offset(0), _alloc(alloc.get_second_allocator())  {
        }

        shared_object(const shared_object& other) : _alloc(other._alloc) {
            if (other._data_ptr_offset) {
                _alloc = other._alloc;
                auto *data = new ((T *)&*_alloc->allocate(sizeof(T))) T{*_alloc};
                *data = other.get();
                set_offset(data);
            }
        }

        shared_object(shared_object&& other) : _data_ptr_offset(other._data_ptr_offset), _alloc(other._alloc) {
            other._data_ptr_offset = 0;
        }

        shared_object& operator=(const shared_object& other) {
            *this = shared_object{other};
            return *this;
        }

        shared_object& operator=(shared_object&& other) {
            if (this != &other) {
                _free();
                _data_ptr_offset = other._data_ptr_offset;
                _alloc = other._alloc;

                other._data_ptr_offset = 0;
                other._alloc = nullptr;
            }
            return *this;
        }

        void _free() {
            if (_data_ptr_offset) {
                auto& obj = get();
                get().~T();
                _alloc->deallocate((char*)&obj, sizeof(T));
                _data_ptr_offset = 0;
                _alloc = nullptr;
            }
        }

        ~shared_object() {
            _free();
        }

        bool operator==(const shared_object& rhs) const {
            if (_data_ptr_offset == 0 && rhs._data_ptr_offset == 0) {
                return true;
            }

            if (_data_ptr_offset == 0 || rhs._data_ptr_offset == 0) {
                return false;
            }

            return get() == rhs.get();
        }

        bool operator!=(const shared_object& rhs) const { return !(*this == rhs); }

        const allocator_type& get_allocator() const { return *_alloc; }
        chainbase::pinnable_mapped_file::segment_manager* get_segment_manager() const {
            auto *_manager = _alloc->get_segment_manager();
            return _manager;
        }

        T& get() const {
            if (get_offset() == 0) {
                T *data = new ((T *)&*_alloc->allocate(sizeof(T))) T{*_alloc};
                set_offset(&*data);
                return *data;
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

    private:
        uint64_t get_offset() const {
            return _data_ptr_offset;
        }

        void set_offset(T *ptr) const {
            auto *manager = get_segment_manager();
            if (uint64_t(ptr) < uint64_t(manager)) {
                BOOST_THROW_EXCEPTION( std::runtime_error("shared_object: invalid pointer") );
            }
            _data_ptr_offset = uint64_t(ptr) - uint64_t(manager);
        }

    private:
        mutable uint64_t _data_ptr_offset = 0;
        mutable allocator_pointer _alloc = nullptr;
    };

} // namespace chainbase
