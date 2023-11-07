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

      explicit shared_string_ex(shared_object_allocator& alloc)
      : _data_ptr_offset(0),
      _alloc(alloc.get_second_allocator()) {

      }

      shared_string_ex(const allocator_pointer& alloc) : _data_ptr_offset(), _alloc(alloc) {}
      template<typename Iter>
      explicit shared_string_ex(Iter begin, Iter end, const allocator_pointer& alloc) : shared_string_ex(alloc) {
         std::size_t size = std::distance(begin, end);
         impl* new_data = (impl*)&*_alloc->allocate(sizeof(impl) + size + 1);
         new_data->reference_count = 1;
         new_data->size = size;
         std::copy(begin, end, new_data->data);
         new_data->data[size] = '\0';
         set_offset(new_data);
      }

      explicit shared_string_ex(const char* ptr, std::size_t size, const allocator_pointer& alloc) : shared_string_ex(alloc) {
         impl* new_data = (impl*)&*_alloc->allocate(sizeof(impl) + size + 1);
         new_data->reference_count = 1;
         new_data->size = size;
         std::memcpy(new_data->data, ptr, size);
         new_data->data[size] = '\0';
         set_offset(new_data);
      }

      explicit shared_string_ex(std::size_t size, boost::container::default_init_t, const allocator_pointer& alloc) : shared_string_ex(alloc) {
         impl* new_data = (impl*)&*_alloc->allocate(sizeof(impl) + size + 1);
         new_data->reference_count = 1;
         new_data->size = size;
         new_data->data[size] = '\0';
         set_offset(new_data);
      }

      shared_string_ex(const shared_string_ex& other) : _data_ptr_offset(other._data_ptr_offset), _alloc(other._alloc) {
         if(_impl() != nullptr) {
            ++_impl()->reference_count;
         }
      }

      shared_string_ex(shared_string_ex&& other) : _data_ptr_offset(other._data_ptr_offset), _alloc(other._alloc) {
         other._data_ptr_offset = 0;
         other._alloc = nullptr;
      }

      shared_string_ex& operator=(const shared_string_ex& other) {
         *this = shared_string_ex{other};
         return *this;
      }

      shared_string_ex& operator=(shared_string_ex&& other) {
         if (this != &other) {
            dec_refcount();
            _data_ptr_offset = other._data_ptr_offset;
            _alloc = other._alloc;
            other._alloc = nullptr;
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
         impl* new_data = (impl*)&*_alloc->allocate(sizeof(impl) + new_size + 1);
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

      void assign(const char* ptr, std::size_t size) {
         dec_refcount();
         _data_ptr_offset = 0;
         if (size == 0) {
            return;
         }
         impl* new_data = (impl*)&*_alloc->allocate(sizeof(impl) + size + 1);
         new_data->reference_count = 1;
         new_data->size = size;
         if(size) {
            std::memcpy(new_data->data, ptr, size);
         }
         new_data->data[size] = '\0';
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
            //BOOST_THROW_EXCEPTION( std::runtime_error("shared_string: invalid data pointer") );
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

      const allocator_type& get_allocator() const {
         return *_alloc;
      }

      const allocator_pointer get_allocator_ptr() const {
         return _alloc;
      }

      chainbase::pinnable_mapped_file::segment_manager* get_segment_manager() const {
         auto *_manager = _alloc->get_segment_manager();
         return _manager;
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

    private:
      void dec_refcount() {
         if(_impl() && --_impl()->reference_count == 0) {
            _alloc->deallocate((char*)_impl(), sizeof(impl) + _impl()->size + 1);
            _data_ptr_offset = 0;
         }
      }
      uint64_t _data_ptr_offset = 0;
      allocator_pointer _alloc;
   };

}  // namepsace chainbase