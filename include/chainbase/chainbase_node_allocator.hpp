#pragma once

#include <cstddef>
#include <boost/interprocess/offset_ptr.hpp>

#include <chainbase/pinnable_mapped_file.hpp>

namespace chainbase {

   namespace bip = boost::interprocess;

   template<typename T, typename S>
   class chainbase_node_allocator {
    public:
      using value_type = T;
      using pointer = bip::offset_ptr<T>;
      using segment_manager = pinnable_mapped_file::segment_manager;

      chainbase_node_allocator(segment_manager* manager) : _manager{manager}, _alloc1(manager), _alloc2(manager), _alloc3(manager) {}
      chainbase_node_allocator(const chainbase_node_allocator& other) : _manager(other._manager), _alloc1(other._alloc1), _alloc2(other._alloc2), _alloc3(other._alloc3) {}
      template<typename U>
      chainbase_node_allocator(const chainbase_node_allocator<U, S>& other) : _manager(other._manager), _alloc1(other._alloc1), _alloc2(other._alloc2), _alloc3(other._alloc3) {}
      pointer allocate(std::size_t num) {
         if (num == 1) {
            if (_freelist == nullptr) {
               get_some();
            }
            list_item* result = &*_freelist;
            _freelist = _freelist->_next;
            result->~list_item();
            return pointer{(T*)result};
         } else {
            return pointer{(T*)_manager->allocate(num*sizeof(T))};
         }
      }
      void deallocate(const pointer& p, std::size_t num) {
         if (num == 1) {
            _freelist = new (&*p) list_item{_freelist};
         } else {
            _manager->deallocate(&*p);
         }
      }
      bool operator==(const chainbase_node_allocator& other) const { return this == &other; }
      bool operator!=(const chainbase_node_allocator& other) const { return this != &other; }
      segment_manager* get_segment_manager() const { return _manager.get(); }

      void set_second_segment_manager(segment_manager* manager) {
         _manager2 = manager;
         auto tmp = allocator_type(manager);
         swap(_alloc2, tmp);
      }

      void set_third_segment_manager(segment_manager* manager) {
         _manager3 = manager;
         auto tmp = allocator_type(manager);
         swap(_alloc3, tmp);
      }

      segment_manager* get_second_segment_manager() const {
         return _manager2;
      }

      segment_manager* get_third_segment_manager() const {
         return _manager3;
      }

      bip::offset_ptr<allocator_type> get_first_allocator() {
         return bip::offset_ptr<allocator_type>::pointer_to(_alloc1);
      }

      bip::offset_ptr<allocator_type> get_second_allocator() {
         return bip::offset_ptr<allocator_type>::pointer_to(_alloc2);
      }

      bip::offset_ptr<allocator_type> get_third_allocator() {
         return bip::offset_ptr<allocator_type>::pointer_to(_alloc3);
      }

    private:
      template<typename T2, typename S2>
      friend class chainbase_node_allocator;
      void get_some() {
         static_assert(sizeof(T) >= sizeof(list_item), "Too small for free list");
         static_assert(sizeof(T) % alignof(list_item) == 0, "Bad alignment for free list");
         char* result = (char*)_manager->allocate(sizeof(T) * 64);
         _freelist = bip::offset_ptr<list_item>{(list_item*)result};
         for(int i = 0; i < 63; ++i) {
            char* next = result + sizeof(T);
            new(result) list_item{bip::offset_ptr<list_item>{(list_item*)next}};
            result = next;
         }
         new(result) list_item{nullptr};
      }
      struct list_item { bip::offset_ptr<list_item> _next; };
      bip::offset_ptr<segment_manager> _manager;
      bip::offset_ptr<list_item> _freelist{};

      segment_manager* _manager2 = nullptr;
      segment_manager* _manager3 = nullptr;
      allocator_type _alloc1;
      allocator_type _alloc2;
      allocator_type _alloc3;
   };

}  // namepsace chainbase
