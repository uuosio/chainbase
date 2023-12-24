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
    class shared_object {
    public:
        explicit shared_object(shared_object_allocator& alloc) : _data_ptr_offset(0) {
            uint64_t id = database_get_unique_segment_manager_id(alloc.get_second_allocator()->get_segment_manager());
            if (id > max_segment_manager_id || id == 0) {
                std::stringstream ss;
                ss << "1: shared_object: invalid segment_manager_id: " << id;
                BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
            }
            _segment_manager_id = id;
        }

        explicit shared_object(const allocator_type& alloc) : _data_ptr_offset(0) {
            uint64_t id = database_get_unique_segment_manager_id(alloc.get_segment_manager());
            if (id > max_segment_manager_id || id == 0) {
                std::stringstream ss;
                ss << "2: shared_object: invalid segment_manager_id: " << id;
                BOOST_THROW_EXCEPTION(std::runtime_error("2: shared_object: invalid segment_manager_id"));
            }
            _segment_manager_id = id;
        }

        shared_object(const shared_object& other): _data_ptr_offset(0), _segment_manager_id(0) {
            _new(other);
        }

        shared_object(shared_object&& other) : _data_ptr_offset(other._data_ptr_offset), _segment_manager_id(other._segment_manager_id) {
            other._data_ptr_offset = 0;
            other._segment_manager_id = 0;
        }

        shared_object& operator=(const shared_object& other) {
            if (this != &other) {
                _free();
                _new(other);
            }
            return *this;
        }

        shared_object& operator=(shared_object&& other) {
            if (this != &other) {
                _free();
                _data_ptr_offset = other._data_ptr_offset;
                _segment_manager_id = other._segment_manager_id;

                other._data_ptr_offset = 0;
                other._segment_manager_id = 0;
            }
            return *this;
        }

        void _new(const shared_object& other) {
            _segment_manager_id = other._segment_manager_id;
            if (other._data_ptr_offset) {
                auto alloc = get_allocator();
                auto *data = new ((T *)&*alloc.allocate(sizeof(T))) T{alloc};
                *data = other.get();
                set_offset(data);
            } else {
                _data_ptr_offset = 0;
            }
        }

        void _free() {
            if (_data_ptr_offset) {
                auto& obj = get();
                obj.~T();
                get_allocator().deallocate((char*)&obj, sizeof(T));
                _data_ptr_offset = 0;
                _segment_manager_id = 0;
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

        allocator_type get_allocator() const {
            return allocator_type(allocator_get_segment_manager_by_id(_segment_manager_id));
        }

        chainbase::pinnable_mapped_file::segment_manager* get_segment_manager() const {
            return allocator_get_segment_manager_by_id(_segment_manager_id);
        }

        T& get_writable() {
            auto *manager = get_segment_manager();
            auto writable_id = database_get_writable_segment_manager_id(manager);
            if (writable_id > max_segment_manager_id || writable_id == 0) {
                std::stringstream ss;
                ss << "shared_object::set_writable: invalid segment_manager_id: " << writable_id;
                BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
            }

            if (writable_id != _segment_manager_id) {
                auto *manager = allocator_get_segment_manager_by_id(writable_id);
                allocator_type alloc(manager);
                auto *data = new ((T *)&*alloc.allocate(sizeof(T))) T{alloc};
                if (get_offset() != 0) {
                    *data = get();
                }
                _free();
                _segment_manager_id = writable_id;
                set_offset(data);
                return *data;
            }

            return *reinterpret_cast<T*>(uint64_t(get_segment_manager()) + get_offset());
        }

        T& get() const {
            if (get_offset() == 0) {
                auto alloc = get_allocator();
                T *data = new ((T *)&*alloc.allocate(sizeof(T))) T{alloc};
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

        uint64_t get_offset() const {
            return _data_ptr_offset;
        }

        void set_offset(T *ptr) const {
            auto *manager = get_segment_manager();
            if (uint64_t(ptr) > uint64_t(manager) && uint64_t(ptr) < uint64_t(manager) + manager->get_size()) {
                // valid pointer
            } else {
                BOOST_THROW_EXCEPTION( std::runtime_error("shared_object: invalid pointer") );
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
