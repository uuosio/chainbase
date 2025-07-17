#pragma once

#include <boost/multi_index_container_fwd.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/avltree.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/container/deque.hpp>
#include <boost/throw_exception.hpp>
#include <boost/mpl/fold.hpp>
#include <boost/mp11/list.hpp>
#include <boost/mp11/algorithm.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/core/demangle.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <cassert>
#include <memory>
#include <type_traits>
#include <sstream>
#include <iostream>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>

#include "shared_object.hpp"
#include "undo_index_events.hpp"
#include "undo_index.hpp"

namespace chainbase {

   template<typename T, typename Allocator, typename... Indices>
   class secondary_index {
    public:
      using value_type = T;
      using allocator_type = Allocator;

      static_assert((... && is_valid_index<Indices>), "Only ordered_unique indices are supported");

      secondary_index() = default;
      explicit secondary_index(const Allocator& a) : _allocator{a} {

      }

      ~secondary_index() {
         clear_impl<1>();
         std::get<0>(_indices).clear_and_dispose([&](pointer p){ dispose_node(*p); });
      }

      void validate()const {
         if( sizeof(node) != _size_of_value_type || sizeof(*this) != _size_of_this )
            BOOST_THROW_EXCEPTION( std::runtime_error("content of memory does not match data expected by executable") );
      }
    
      struct node : hook<Indices, Allocator>..., value_holder<T> {
         using value_type = T;
         using allocator_type = Allocator;
         template<typename... A>
         explicit node(A&&... a) : value_holder<T>{static_cast<A&&>(a)...} {}
         const T& item() const { return *this; }
         uint64_t _mtime = 0; // _monotonic_revision when the node was last modified or created.
      };
      static constexpr int erased_flag = -2; // 0,1,and -1 are used by the tree

      using indices_type = std::tuple<set_impl<node, Indices>...>;

      using index0_set_type = std::tuple_element_t<0, indices_type>;
      
      template<typename Tag>
      using index_type_by_tag = std::tuple_element_t<find_tag<Tag, Indices...>::value, indices_type>;

      using alloc_traits = typename std::allocator_traits<Allocator>::template rebind_traits<node>;

      using index0_type = boost::mp11::mp_first<boost::mp11::mp_list<Indices...>>;

      using pointer = value_type*;
      using const_iterator = typename index0_set_type::const_iterator;

      // Exception safety: strong
      template<typename Constructor>
      const value_type& emplace( Constructor&& c ) {
         auto p = alloc_traits::allocate(_allocator, 1);
         auto guard0 = scope_exit{[&]{ alloc_traits::deallocate(_allocator, p, 1); }};
         auto constructor = [&]( value_type& v ) {
            c( v );
         };
         alloc_traits::construct(_allocator, &*p, constructor, propagate_allocator(_allocator));
         auto guard1 = scope_exit{[&]{ alloc_traits::destroy(_allocator, &*p); }};

         if(!insert_impl<0>(p->_item)) {
            std::stringstream ss;
            ss << "emplace 2: could not insert object: " << boost::core::demangle( typeid( value_type ).name() );
            ss << ", database_id: " << _database_id;
            ss << ", most likely a uniqueness constraint was violated";
            BOOST_THROW_EXCEPTION( std::logic_error{ ss.str() } );
         }
         
         on_create(p->_item);
         guard1.cancel();
         guard0.cancel();

         return p->_item;
      }

      size_t indices_count() const {
         return sizeof...(Indices);
      }

      template<int N = 0>
      bool _walk_indices( std::function<void(size_t index_type, size_t object_pos, const value_type&)> f ) const {
         if constexpr (N < sizeof...(Indices)) {
            auto& idx = std::get<N>(_indices);
            size_t index = 0;
            for (const auto& obj : idx) {
               f(N, index, obj);
               index += 1;
            }
            return _walk_indices<N+1>(f);
         }
         return true;
      }

      bool walk_indices( std::function<void(size_t index_type, size_t object_pos, const value_type&)> f ) const {
         return _walk_indices<0>(f);
      }

      // Exception safety: basic.
      // If the modifier leaves the object in a state that conflicts
      // with another object, it will either be reverted or erased.
      template<typename Modifier>
      void modify( const value_type& obj, Modifier&& m) {
         value_type& node_ref = const_cast<value_type&>(obj);
         bool success = false;
         {
            auto guard0 = scope_exit{[&]{
               if(!post_modify<true, 0>(node_ref)) { // first index value type is not id_type
                  remove(obj);
               } else {
                  success = true;
               }
            }};
            m(node_ref);
         }
         if(!success)
            BOOST_THROW_EXCEPTION( std::logic_error{ "could not modify object, most likely a uniqueness constraint was violated" } );
      }

      void remove( const value_type& obj ) noexcept {
         auto& node_ref = const_cast<value_type&>(obj);
         erase_impl(node_ref);
         dispose_node(node_ref);
      }

    public:

      template<typename CompatibleKey>
      const value_type* find( CompatibleKey&& key) const {
         const auto& index = std::get<0>(_indices);
         auto iter = index.find(static_cast<CompatibleKey&&>(key));
         if (iter != index.end()) {
            return &*iter;
         } else {
            return nullptr;
         }
      }

      template<typename CompatibleKey>
      const value_type& get( CompatibleKey&& key )const {
         auto ptr = find( static_cast<CompatibleKey&&>(key) );
         if( !ptr ) {
            std::stringstream ss;
            ss << "key not found (" << boost::core::demangle( typeid( key ).name() ) << "): " << key;
            BOOST_THROW_EXCEPTION( std::out_of_range( ss.str().c_str() ) );
         }
         return *ptr;
      }

      uint64_t revision() const { return _revision; }

      void set_revision( uint64_t revision ) {
         if( revision < _revision )
            BOOST_THROW_EXCEPTION( std::logic_error("revision cannot decrease") );

         _revision = revision;
      }

      void set_database_id( uint64_t id ) {
         _database_id = id;
         _set_database_id(id);
      }

      template<int N = 0>
      void _set_database_id( uint64_t database_id ) {
         if constexpr (N < sizeof...(Indices)) {
            auto& idx = std::get<N>(_indices);
            idx.set_database_id(database_id);

            _set_database_id<N+1>(database_id);
         }
      }

      uint64_t get_database_id() const {
         return _database_id;
      }

      uint64_t get_instance_id() const {
         return _instance_id;
      }

      void set_instance_id( uint64_t instance_id ) {
         _instance_id = instance_id;
         _set_instance_id(instance_id);
      }

      template<int N = 0>
      void _set_instance_id( uint64_t instance_id ) {
         if constexpr (N < sizeof...(Indices)) {
            auto& idx = std::get<N>(_indices);
            idx.set_instance_id(instance_id);

            _set_instance_id<N+1>(instance_id);
         }
      }

      template<typename Tag>
      const auto& get() const { return std::get<find_tag<Tag, Indices...>::value>(_indices); }

      template<int N>
      const auto& get() const { return std::get<N>(_indices); }

      template<typename Tag>
      size_t get_index_position() const { return find_tag<Tag, Indices...>::value; }

      std::size_t size() const {
         return std::get<0>(_indices).size();
      }

      bool empty() const {
         return std::get<0>(_indices).empty();
      }

      template<typename Tag, typename Iter>
      auto project(Iter iter) const {
         return project<find_tag<Tag, Indices...>::value>(iter);
      }

      template<int N, typename Iter>
      auto project(Iter iter) const {
         if(iter == get<boost::mp11::mp_find<boost::mp11::mp_list<typename set_impl<node, Indices>::const_iterator...>, Iter>::value>().end())
            return get<N>().end();
         return get<N>().iterator_to(*iter);
      }

      auto begin() const { return get<0>().begin(); }
      auto end() const { return get<0>().end(); }

      auto& get_allocator() { return _allocator; }

      template<int N = 0>
      bool _exists(const value_type& p) const {
         if constexpr (N < sizeof...(Indices)) {
            auto& idx = std::get<N>(_indices);
            
            if (idx.empty()) {
                return _exists<N+1>(p);
            }

            using base_type = typename std::decay_t<decltype(idx)>::base_type;
            typename base_type::key_compare cmp;
            typename base_type::key_of_value key_extractor;

            const auto& min_val = *idx.begin();
            auto end_it = idx.end();
            --end_it;
            const auto& max_val = *end_it;

            auto p_key = key_extractor(p);
            auto min_key = key_extractor(min_val);
            auto max_key = key_extractor(max_val);

            if (!cmp(p_key, min_key) && !cmp(max_key, p_key)) {
                if (idx.find(p) != idx.end()) {
                   return true;
                }
            }
            return _exists<N+1>(p);
         }
         return false;
      }

      bool exists(const value_type& p) const {
         return _exists<0>(p);
      }

    private:

      static node& to_node(value_type& obj) {
         return static_cast<node&>(*boost::intrusive::get_parent_from_member(&obj, &value_holder<value_type>::_item));
      }
      static node& to_node(const value_type& obj) {
         return to_node(const_cast<value_type&>(obj));
      }

      template<int N = 0>
      bool insert_impl(value_type& p) {
         if constexpr (N < sizeof...(Indices)) {
            auto [iter, inserted] = std::get<N>(_indices).insert_unique(p);
            if(!inserted) return false;
            auto guard = scope_exit{[this,iter=iter]{ std::get<N>(_indices).erase(iter); }};
            if(insert_impl<N+1>(p)) {
               guard.cancel();
               return true;
            }
            return false;
         }
         return true;
      }

      // Moves a modified node into the correct location
      template<bool unique, int N = 0>
      bool post_modify(value_type& p) {
         if constexpr (N < sizeof...(Indices)) {
            auto& idx = std::get<N>(_indices);
            auto iter = idx.iterator_to(p);
            bool fixup = false;
            if (iter != idx.begin()) {
               auto copy = iter;
               --copy;
               if (!idx.value_comp()(*copy, p)) fixup = true;
            }
            ++iter;
            if (iter != idx.end()) {
               if(!idx.value_comp()(p, *iter)) fixup = true;
            }
            if(fixup) {
               auto iter2 = idx.iterator_to(p);
               idx.erase(iter2);
               if constexpr (unique) {
                  auto [new_pos, inserted] = idx.insert_unique(p);
                  if (!inserted) {
                     idx.insert_before(new_pos, p);
                     return false;
                  }
               } else {
                  idx.insert_equal(p);
               }
            }
            return post_modify<unique, N+1>(p);
         }
         return true;
      }

      template<int N = 0>
      void erase_impl(value_type& p) {
         if constexpr (N < sizeof...(Indices)) {
            auto& setN = std::get<N>(_indices);
            setN.erase(setN.iterator_to(p));
            erase_impl<N+1>(p);
         }
      }

      void on_create(const value_type& value) {
      }

      value_type* on_modify( const value_type& obj) {
         return nullptr;
      }

      template<int N = 0>
      void clear_impl() noexcept {
         if constexpr(N < sizeof...(Indices)) {
            std::get<N>(_indices).clear();
            clear_impl<N+1>();
         }
      }

      void dispose_node(node& node_ref) noexcept {
         node* p{&node_ref};
         alloc_traits::destroy(_allocator, p);
         alloc_traits::deallocate(_allocator, p, 1);
      }

      void dispose_node(value_type& node_ref) noexcept {
         dispose_node(static_cast<node&>(*boost::intrusive::get_parent_from_member(&node_ref, &value_holder<value_type>::_item)));
      }

      rebind_alloc_t<Allocator, node> _allocator;

      indices_type _indices;

      uint64_t _revision = 0;
      uint64_t _monotonic_revision = 0;
      uint64_t _database_id = 0;
      uint64_t _instance_id = 0;
      uint32_t                        _size_of_value_type = sizeof(node);
      uint32_t                        _size_of_this = sizeof(secondary_index);
   };

   template<typename MultiIndexContainer>
   struct multi_index_to_secondary_index_impl;

   template<typename T, typename I, typename A>
   struct mi_to_ui_ii2;
   template<typename T, typename... I, typename A>
   struct mi_to_ui_ii2<T, boost::mp11::mp_list<I...>, A> {
      using type = secondary_index<T, A, I...>;
   };

   // struct to_mp11 {
   //    template<typename State, typename T>
   //    using apply = boost::mpl::identity<boost::mp11::mp_push_back<State, T>>;
   // };

   template<typename T, typename I, typename A>
   struct multi_index_to_secondary_index_impl<boost::multi_index_container<T, I, A>> {
      using as_mp11 = typename boost::mpl::fold<I, boost::mp11::mp_list<>, to_mp11>::type;
      using type = typename mi_to_ui_ii2<T, as_mp11, A>::type;
   };

   // Converts a multi_index_container to a corresponding undo_index.
   template<typename MultiIndexContainer>
   using multi_index_to_secondary_index = typename multi_index_to_secondary_index_impl<MultiIndexContainer>::type;
}
