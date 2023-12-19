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

namespace chainbase {

   namespace bip = boost::interprocess;

   class shared_string_ex {
      struct impl {
         uint32_t reference_count;
         uint32_t size;
         char data[0];
      };
    public:
      using iterator = const char*;
      using const_iterator = const char*;

      explicit shared_string_ex(shared_object_allocator& alloc) : _data_ptr_offset(0) {
         uint64_t id = database_get_unique_id(alloc.get_second_allocator()->get_segment_manager());
         if (id > 0xffff || id == 0) {
            std::stringstream ss;
            ss << "1: shared_string_ex: invalid segment_manager_id: " << id;
            BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
         }
         _segment_manager_id = id;
      }

      shared_string_ex(const allocator_type& alloc) : _data_ptr_offset(0) {
         uint64_t id = database_get_unique_id(alloc.get_segment_manager());
         if (id > 0xffff || id == 0) {
            std::stringstream ss;
            ss << "2: shared_string_ex: invalid segment_manager_id: " << id;
            BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
         }
         _segment_manager_id = id;
      }

      shared_string_ex(const allocator_type& alloc, uint64_t data_ptr_offset)
      : _data_ptr_offset(data_ptr_offset) {
         uint64_t id = database_get_unique_id(alloc.get_segment_manager());
         if (id > 0xffff || id == 0) {
            std::stringstream ss;
            ss << "3: shared_string_ex: invalid segment_manager_id: " << id;
            BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
         }
         _segment_manager_id = id;
      }

      template<typename Iter>
      explicit shared_string_ex(Iter begin, Iter end, const allocator_type alloc) : shared_string_ex(alloc) {
         std::size_t size = std::distance(begin, end);
         impl* new_data = (impl*)&*get_allocator().allocate(sizeof(impl) + size + 1);
         new_data->reference_count = 1;
         new_data->size = size;
         std::copy(begin, end, new_data->data);
         new_data->data[size] = '\0';
         set_offset(new_data);
      }

      explicit shared_string_ex(const char* ptr, std::size_t size, const allocator_type alloc) : shared_string_ex(alloc) {
         impl* new_data = (impl*)&*get_allocator().allocate(sizeof(impl) + size + 1);
         new_data->reference_count = 1;
         new_data->size = size;
         std::memcpy(new_data->data, ptr, size);
         new_data->data[size] = '\0';
         set_offset(new_data);
      }

      explicit shared_string_ex(std::size_t size, boost::container::default_init_t, const allocator_type alloc) : shared_string_ex(alloc) {
         impl* new_data = (impl*)&*get_allocator().allocate(sizeof(impl) + size + 1);
         new_data->reference_count = 1;
         new_data->size = size;
         new_data->data[size] = '\0';
         set_offset(new_data);
      }

      shared_string_ex(const shared_string_ex& other) : _data_ptr_offset(other._data_ptr_offset), _segment_manager_id(other._segment_manager_id) {
         if(_impl() != nullptr) {
            ++_impl()->reference_count;
         }
      }

      shared_string_ex(shared_string_ex&& other) : _data_ptr_offset(other._data_ptr_offset), _segment_manager_id(other._segment_manager_id) {
         other._data_ptr_offset = 0;
         other._segment_manager_id = 0;
      }

      shared_string_ex& operator=(const shared_string_ex& other) {
         if (this != &other) {
            dec_refcount();
            _data_ptr_offset = other._data_ptr_offset;
            _segment_manager_id = other._segment_manager_id;
            if (_data_ptr_offset != 0) {
               ++_impl()->reference_count;
            }
         }
         return *this;
      }

      shared_string_ex& operator=(shared_string_ex&& other) {
         if (this != &other) {
            dec_refcount();
            _data_ptr_offset = other._data_ptr_offset;
            _segment_manager_id = other._segment_manager_id;
            other._segment_manager_id = 0;
            other._data_ptr_offset = 0;
         }
         return *this;
      }

      ~shared_string_ex() {
         dec_refcount();
      }

      void resize(std::size_t new_size, boost::container::default_init_t) {
         dec_refcount();
         _data_ptr_offset = 0;
         if (new_size == 0) {
            return;
         }
         impl* new_data = (impl*)&*get_allocator().allocate(sizeof(impl) + new_size + 1);
         new_data->reference_count = 1;
         new_data->size = new_size;
         new_data->data[new_size] = '\0';
         set_offset(new_data);
      }

      template<typename F>
      void resize_and_fill(std::size_t new_size, F&& f) {
         resize(new_size, boost::container::default_init);
         if (new_size > 0) {
            static_cast<F&&>(f)(_impl()->data, new_size);
         }
      }

      void assign(const char* ptr, std::size_t _size) {
         if (size() > 0 && _size == size() && _impl()->reference_count == 1) {
            std::memcpy((char *)data(), ptr, _size);
            return;
         }

         dec_refcount();
         _data_ptr_offset = 0;
         if (_size == 0) {
            return;
         }
         impl* new_data = (impl*)&*get_allocator().allocate(sizeof(impl) + _size + 1);
         new_data->reference_count = 1;
         new_data->size = _size;
         if(_size) {
            std::memcpy(new_data->data, ptr, _size);
         }
         new_data->data[_size] = '\0';
         set_offset(new_data);
      }

      void assign(const unsigned char* ptr, std::size_t size) {
         assign((char*)ptr, size);
      }

      const char * data() const {
         if (get_offset() == 0) {
            return nullptr;
         }
         return _impl()->data;
      }

      impl *_impl() const {
         if (get_offset() == 0) {
            return nullptr;
         }
         return reinterpret_cast<impl*>(uint64_t(get_segment_manager()) + get_offset());
      }

      std::size_t size() const {
         if (_impl()) {
            return _impl()->size;
         } else {
            return 0;
         }
      }

      const_iterator begin() const { return data(); }

      const_iterator end() const {
         if (_impl()) {
            return _impl()->data + _impl()->size;
         } else {
            return nullptr;
         }
      }

      int compare(std::size_t start, std::size_t count, const char* other, std::size_t other_size) const {
         std::size_t sz = size();
         if(start > sz) {
            BOOST_THROW_EXCEPTION(std::out_of_range{"shared_string_ex::compare"});
         }

         count = std::min(count, sz - start);
         std::size_t cmp_len = std::min(count, other_size);
         const char* start_ptr = data() + start;
         int result = std::char_traits<char>::compare(start_ptr, other, cmp_len);

         if (result != 0) {
            return result;
         } else if (count < other_size) {
            return -1;
         } else if(count > other_size) {
            return 1;
         } else {
            return 0;
         }
      }

      bool operator==(const shared_string_ex& rhs) const {
         return size() == rhs.size() && std::memcmp(data(), rhs.data(), size()) == 0;
      }

      bool operator!=(const shared_string_ex& rhs) const {
         return !(*this == rhs);
      }

      allocator_type get_allocator() const {
         return allocator_type(get_segment_manager());
      }

      chainbase::pinnable_mapped_file::segment_manager* get_segment_manager() const {
         return allocator_get_segment_manager_by_id(_segment_manager_id);
      }

      uint64_t get_offset() const {
         return _data_ptr_offset;
      }

      void set_offset(impl *ptr) {
         auto *manager = get_segment_manager();
         if (uint64_t(ptr) < uint64_t(manager)) {
            BOOST_THROW_EXCEPTION( std::runtime_error("shared_string_ex: invalid pointer") );
         }
         _data_ptr_offset = uint64_t(ptr) - uint64_t(manager);
      }

      void set_offset(uint64_t data_ptr_offset) {
         _data_ptr_offset = data_ptr_offset;
      }

    private:
      void dec_refcount() {
         if(_impl() && --_impl()->reference_count == 0) {
            get_allocator().deallocate((char*)_impl(), sizeof(impl) + _impl()->size + 1);
            _data_ptr_offset = 0;
         }
      }
      uint64_t _data_ptr_offset:48;
      uint64_t _segment_manager_id:16;
   };

}  // namepsace chainbase
