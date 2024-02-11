#include <chainbase/undo_index.hpp>

#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <boost/test/unit_test.hpp>
#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/test_case.hpp>
// gcc-11 adds new dynamic memory warnings that return false positives for many of the stack allocated instantiations of
// chainbase::undo_index<>. For example, see test_insert_modify. Evaluation of the actual behavior using ASAN with gcc-11 and
// clang-11 indicates these are false positives. The warning is disabled for gcc-11 and above.
#if defined(__GNUC__) && (__GNUC__ >= 11) && !defined(__clang__)
#  pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif

namespace test {
BOOST_AUTO_TEST_SUITE(undo_index_tests2)

static int exception_counter = 0;
static int throw_at = -1;
struct test_exception_base {};
template<typename E>
struct test_exception : E, test_exception_base {
  template<typename... A>
  test_exception(A&&... a) : E{static_cast<E&&>(a)...} {}
};

template<typename E, typename... A>
void throw_point(A&&... a) {
   if(throw_at != -1 && exception_counter++ >= throw_at) {
     throw test_exception<E>{static_cast<A&&>(a)...};
   }
}

template<typename F>
void test_exceptions(const char *name, F&& f) {
   for(throw_at = 0; ; ++throw_at) {
      exception_counter = 0;
      try {
         f();
         break;
      } catch(test_exception_base&) {
      }
   }
   throw_at = -1;
   exception_counter = 0;
}

struct throwing_copy {
   throwing_copy() { throw_point<std::bad_alloc>(); }
   // throwing_copy() { }
   throwing_copy(const throwing_copy&) { throw_point<std::bad_alloc>(); }
   throwing_copy(throwing_copy&&) noexcept = default;
   throwing_copy& operator=(const throwing_copy&) { throw_point<std::bad_alloc>(); return *this; }
   throwing_copy& operator=(throwing_copy&&) noexcept = default;
};

struct test_element_t;
struct conflict_element_t;


namespace bip = boost::interprocess;
using segment_manager = chainbase::pinnable_mapped_file::segment_manager;

template<typename T>
struct test_allocator {
   using value_type = T;
   using pointer = bip::offset_ptr<T>;

   test_allocator() {
      size_t size = 1024*1024*64;
      _manager = new ((segment_manager *)malloc(size))segment_manager{size};
   }

   ~test_allocator() {
      _manager->~segment_manager();
      free(_manager);
   }

   template<typename U>
   test_allocator(const test_allocator<U>& other) : _manager{other._manager} {

   }

   template<typename U>
   struct rebind { using other = test_allocator<U>; };

   pointer allocate(std::size_t num) {
      auto name = std::string_view(typeid(T).name());
      // std::cout<<"allocate name:"<<boost::core::demangle(typeid(T).name())<<std::endl;
      if (name.find("created_node") != std::string_view::npos) {
         // DO NOT THROW in undo_index::on_create
      } else {
         throw_point<std::bad_alloc>();
      }
      return pointer{(T*)_manager->allocate(num*sizeof(T))};
   }

   void deallocate(const pointer& p, std::size_t num) {
      _manager->deallocate(&*p);
   }

   segment_manager* get_segment_manager() {
      return _manager;
   }

   segment_manager* _manager;
};

struct test_element_t {
   template<typename C, typename A>
   test_element_t(C&& c, const test_allocator<A>&) {
      c(*this);
   }
   chainbase::oid<test_element_t> id;
   int secondary;
   throwing_copy dummy;
};


struct conflict_element_t {
   template<typename C, typename A>
   conflict_element_t(C&& c, const test_allocator<A>&) { c(*this); }
   chainbase::oid<conflict_element_t> id;
   int x0;
   int x1;
   int x2;
   throwing_copy dummy;
};

struct basic_element_t {
   template<typename C, typename A>
   basic_element_t(C&& c, const test_allocator<A>&) { c(*this); }
   chainbase::oid<basic_element_t> id;
   throwing_copy dummy;
};


template<typename F>
struct scope_fail {
   scope_fail(F&& f) : _f{static_cast<F&&>(f)}, _exception_count{std::uncaught_exceptions()} {}
   ~scope_fail() {
      if(_exception_count != std::uncaught_exceptions()) _f();
   }
   F _f;
   int _exception_count;
};

// TODO: Replace with boost::multi_index::key once we bump our minimum Boost version to at least 1.69
template<typename T>
struct key_impl;
template<typename C, typename T>
struct key_impl<T C::*> { template<auto F> using fn = boost::multi_index::member<C, T, F>; };

template<auto Fn>
using key = typename key_impl<decltype(Fn)>::template fn<Fn>;


#define EXCEPTION_TEST_CASE(name)                               \
   void name##impl();                                         \
   BOOST_AUTO_TEST_CASE(name) {                                \
      test_exceptions(#name, &name##impl);                            \
   } \
   void name##impl ()

EXCEPTION_TEST_CASE(test_simple2) {
   chainbase::undo_index<basic_element_t, test_allocator<basic_element_t>, boost::multi_index::ordered_unique<key<&basic_element_t::id>>> i0;

   i0.emplace([](basic_element_t& elem) {});
   const basic_element_t* element = i0.find(0);
   BOOST_TEST((element != nullptr && element->id == 0));
   const basic_element_t* e1 = i0.find(1);
   BOOST_TEST(e1 == nullptr);
   i0.emplace([](basic_element_t& elem) {});
   const basic_element_t* e2 = i0.find(1);
   BOOST_TEST((e2 != nullptr && e2->id == 1));

   i0.modify(*element, [](basic_element_t& elem) {});
   i0.remove(*element);
   element = i0.find(0);
   BOOST_TEST(element == nullptr);
}

// If an exception is thrown while an undo session is active, undo will restore the state.
template<typename C>
auto capture_state(const C& index) {
   std::vector<std::pair<test_element_t, const test_element_t*>> vec;
   for(const auto& elem : index) {
     vec.emplace_back(elem, &elem);
   }
   return scope_fail{[vec = std::move(vec), &index]{
      BOOST_TEST(index.size() == vec.size());
      for(const auto& [elem, ptr] : vec) {
         const test_element_t *actual0 = nullptr;
         for (auto& e : index) {
            if (e.id == elem.id) {
               actual0 = &e;
            }
         }

         BOOST_TEST(actual0 == ptr); // reference stability is guaranteed
         if (actual0 != nullptr) {
            BOOST_TEST(actual0->id == elem.id);
            BOOST_TEST(actual0->secondary == elem.secondary);
         }
         auto actual1iter = index.template get<0>().find(elem.secondary);
         BOOST_TEST((actual1iter != index.template get<0>().end() && &*actual1iter == actual0));
      }
   }};
}

EXCEPTION_TEST_CASE(test_insert_undo2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                        //  boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.find(12)->secondary == 12);
   }

   BOOST_TEST(i0.find(42)->secondary == 42);
   BOOST_TEST(i0.find(12) == nullptr);
}

EXCEPTION_TEST_CASE(test_insert_squash2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session0 = i0.start_undo_session(true);
      auto session1 = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.find(12)->secondary == 12);
      session1.squash();
      BOOST_TEST(i0.find(12)->secondary == 12);
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
   BOOST_TEST(i0.find(12) == nullptr);
}

EXCEPTION_TEST_CASE(test_insert_push2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                        //  boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.get_created_value_count() == 1);
      BOOST_TEST(i0.find(12)->secondary == 12);
      session.push();
      i0.commit(i0.revision());
   }
   BOOST_TEST(!i0.has_undo_session());
   BOOST_TEST(i0.get_created_value_count() == 0);
   BOOST_TEST(i0.find(42)->secondary == 42);
   BOOST_TEST(i0.find(12)->secondary == 12);
}

EXCEPTION_TEST_CASE(test_modify_undo2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session = i0.start_undo_session(true);
      i0.modify(*i0.find(42), [](test_element_t& elem) { elem.secondary = 18; });
      BOOST_TEST(i0.find(18)->secondary == 18);
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
}

EXCEPTION_TEST_CASE(test_modify_squash2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session0 = i0.start_undo_session(true);
      auto session1 = i0.start_undo_session(true);
      i0.modify(*i0.find(42), [](test_element_t& elem) { elem.secondary = 18; });
      BOOST_TEST(i0.find(18)->secondary == 18);
      session1.squash();
      BOOST_TEST(i0.find(18)->secondary == 18);
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
}

EXCEPTION_TEST_CASE(test_modify_push2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session = i0.start_undo_session(true);
      i0.modify(*i0.find(42), [](test_element_t& elem) { elem.secondary = 18; });
      BOOST_TEST(i0.find(18)->secondary == 18);
      session.push();
      i0.commit(i0.revision());
   }
   BOOST_TEST(!i0.has_undo_session());
   BOOST_TEST(i0.get_created_value_count() == 0);
   BOOST_TEST(i0.find(18)->secondary == 18);
}

EXCEPTION_TEST_CASE(test_remove_undo2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session = i0.start_undo_session(true);
      i0.remove(*i0.find(42));
      BOOST_TEST(i0.find(42) == nullptr);
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
}

EXCEPTION_TEST_CASE(test_remove_squash2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session0 = i0.start_undo_session(true);
      auto session1 = i0.start_undo_session(true);
      i0.remove(*i0.find(42));
      BOOST_TEST(i0.find(42) == nullptr);
      session1.squash();
      BOOST_TEST(i0.find(42) == nullptr);
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
}

EXCEPTION_TEST_CASE(test_remove_push2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session = i0.start_undo_session(true);
      i0.remove(*i0.find(42));
      BOOST_TEST(i0.find(42) == nullptr);
      session.push();
      i0.commit(i0.revision());
   }
   BOOST_TEST(!i0.has_undo_session());
   BOOST_TEST(i0.find(42) == nullptr);
}

EXCEPTION_TEST_CASE(test_insert_modify2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
   BOOST_TEST(i0.find(12)->secondary == 12);
   i0.modify(*i0.find(12), [](test_element_t& elem) { elem.secondary = 24; });
   BOOST_TEST(i0.find(24)->secondary == 24);
}

EXCEPTION_TEST_CASE(test_insert_modify_undo2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
   auto undo_checker = capture_state(i0);
   auto session = i0.start_undo_session(true);
   i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
   BOOST_TEST(i0.find(12)->secondary == 12);
   i0.modify(*i0.find(12), [](test_element_t& elem) { elem.secondary = 24; });
   BOOST_TEST(i0.find(24)->secondary == 24);
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
   BOOST_TEST(i0.find(24) == nullptr);
}


EXCEPTION_TEST_CASE(test_insert_modify_squash2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session1 = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.find(12)->secondary == 12);
      auto session2 = i0.start_undo_session(true);
      i0.modify(*i0.find(12), [](test_element_t& elem) { elem.secondary = 24; });
      BOOST_TEST(i0.find(24)->secondary == 24);
      session2.squash();
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
   BOOST_TEST(i0.find(24) == nullptr);
}

EXCEPTION_TEST_CASE(test_insert_remove_undo2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.find(12)->secondary == 12);
      i0.remove(*i0.find(12));
      BOOST_TEST(i0.find(12) == nullptr);
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
   BOOST_TEST(i0.find(12) == nullptr);
}

EXCEPTION_TEST_CASE(test_insert_remove_squash2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session1 = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.find(12)->secondary == 12);
      auto session2 = i0.start_undo_session(true);
      i0.remove(*i0.find(12));
      BOOST_TEST(i0.find(12) == nullptr);
      session2.squash();
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
   BOOST_TEST(i0.find(12) == nullptr);
}

EXCEPTION_TEST_CASE(test_modify_modify_undo2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session = i0.start_undo_session(true);
      i0.modify(*i0.find(42), [](test_element_t& elem) { elem.secondary = 18; });
      BOOST_TEST(i0.find(18)->secondary == 18);
      i0.modify(*i0.find(18), [](test_element_t& elem) { elem.secondary = 24; });
      BOOST_TEST(i0.find(24)->secondary == 24);
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
}

EXCEPTION_TEST_CASE(test_modify_modify_squash2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session1 = i0.start_undo_session(true);
      i0.modify(*i0.find(42), [](test_element_t& elem) { elem.secondary = 18; });
      BOOST_TEST(i0.find(18)->secondary == 18);
      auto session2 = i0.start_undo_session(true);
      i0.modify(*i0.find(18), [](test_element_t& elem) { elem.secondary = 24; });
      BOOST_TEST(i0.find(24)->secondary == 24);
      session2.squash();
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
}

EXCEPTION_TEST_CASE(test_modify_remove_undo2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
   auto undo_checker = capture_state(i0);
   auto session = i0.start_undo_session(true);
   i0.modify(*i0.find(42), [](test_element_t& elem) { elem.secondary = 18; });
   BOOST_TEST(i0.find(18)->secondary == 18);
   i0.remove(*i0.find(18));
   BOOST_TEST(i0.find(18) == nullptr);
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
}

EXCEPTION_TEST_CASE(test_modify_remove_squash2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session1 = i0.start_undo_session(true);
      i0.modify(*i0.find(42), [](test_element_t& elem) { elem.secondary = 18; });
      BOOST_TEST(i0.find(18)->secondary == 18);
      auto session2 = i0.start_undo_session(true);
      i0.remove(*i0.find(18));
      BOOST_TEST(i0.find(18) == nullptr);
      session2.squash();
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
}

EXCEPTION_TEST_CASE(test_squash_one2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      i0.modify(*i0.find(42), [](test_element_t& elem) { elem.secondary = 18; });
      BOOST_TEST(i0.find(18)->secondary == 18);
      auto session2 = i0.start_undo_session(true);
      i0.remove(*i0.find(18));
      BOOST_TEST(i0.find(18) == nullptr);
      session2.squash();
   }
}

EXCEPTION_TEST_CASE(test_insert_non_unique2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   BOOST_CHECK_THROW(i0.emplace([](test_element_t& elem) { elem.secondary = 42; }),  std::exception);
   BOOST_TEST(i0.find(42)->secondary == 42);
}

EXCEPTION_TEST_CASE(test_modify_conflict2) {
   chainbase::undo_index<conflict_element_t, test_allocator<conflict_element_t>,
                         boost::multi_index::ordered_unique<key<&conflict_element_t::x0>>,
                         boost::multi_index::ordered_unique<key<&conflict_element_t::x1>>,
                         boost::multi_index::ordered_unique<key<&conflict_element_t::x2>>> i0;

   // insert 3 elements
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 0; elem.x1 = 10; elem.x2 = 10; });
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 1; elem.x2 = 11; });
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 12; elem.x1 = 12; elem.x2 = 2; });
   {
      auto session = i0.start_undo_session(true);
      // set them to a different value
      i0.modify(*i0.find(0), [](conflict_element_t& elem) { elem.x0 = 10; elem.x1 = 10; elem.x2 = 10; });
      i0.modify(*i0.find(11), [](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 11; elem.x2 = 11; });
      i0.modify(*i0.find(12), [](conflict_element_t& elem) { elem.x0 = 12; elem.x1 = 12; elem.x2 = 12; });
      // create a circular conflict with the original values
      i0.modify(*i0.find(10), [](conflict_element_t& elem) { elem.x0 = 10; elem.x1 = 1; elem.x2 = 10; });
      i0.modify(*i0.find(11), [](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 11; elem.x2 = 2; });
      i0.modify(*i0.find(12), [](conflict_element_t& elem) { elem.x0 = 0; elem.x1 = 12; elem.x2 = 12; });
   }

   BOOST_TEST(i0.find(0)->x0 == 0);
   BOOST_TEST(i0.find(11)->x1 == 1);
   BOOST_TEST(i0.find(12)->x2 == 2);

   // Check lookup in the other indices
   BOOST_TEST(i0.get<0>().find(0)->x0 == 0);
   BOOST_TEST(i0.get<0>().find(11)->x0 == 11);
   BOOST_TEST(i0.get<0>().find(12)->x0 == 12);
   BOOST_TEST(i0.get<1>().find(10)->x1 == 10);
   BOOST_TEST(i0.get<1>().find(1)->x1 == 1);
   BOOST_TEST(i0.get<1>().find(12)->x1 == 12);
   BOOST_TEST(i0.get<2>().find(10)->x2 == 10);
   BOOST_TEST(i0.get<2>().find(11)->x2 == 11);
   BOOST_TEST(i0.get<2>().find(2)->x2 == 2);
}

BOOST_DATA_TEST_CASE(test_insert_fail2, boost::unit_test::data::make({true, false}), use_undo) {

   chainbase::undo_index<conflict_element_t, test_allocator<conflict_element_t>,
                        //  boost::multi_index::ordered_unique<key<&conflict_element_t::id>>,
                         boost::multi_index::ordered_unique<key<&conflict_element_t::x0>>,
                         boost::multi_index::ordered_unique<key<&conflict_element_t::x1>>,
                         boost::multi_index::ordered_unique<key<&conflict_element_t::x2>>> i0;

   // insert 3 elements
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 10; elem.x1 = 10; elem.x2 = 10; });
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 11; elem.x2 = 11; });
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 12; elem.x1 = 12; elem.x2 = 12; });
   {
      auto session = i0.start_undo_session(true);
      // Insert a value with a duplicate
      BOOST_CHECK_THROW(i0.emplace([](conflict_element_t& elem) { elem.x0 = 81; elem.x1 = 11; elem.x2 = 91; }), std::logic_error);
   }
   BOOST_TEST(i0.find(10)->x0 == 10);
   BOOST_TEST(i0.find(11)->x1 == 11);
   BOOST_TEST(i0.find(12)->x2 == 12);
   // Check lookup in the other indices
   BOOST_TEST(i0.get<0>().find(10)->x0 == 10);
   BOOST_TEST(i0.get<0>().find(11)->x0 == 11);
   BOOST_TEST(i0.get<0>().find(12)->x0 == 12);
   BOOST_TEST(i0.get<1>().find(10)->x1 == 10);
   BOOST_TEST(i0.get<1>().find(11)->x1 == 11);
   BOOST_TEST(i0.get<1>().find(12)->x1 == 12);
   BOOST_TEST(i0.get<2>().find(10)->x2 == 10);
   BOOST_TEST(i0.get<2>().find(11)->x2 == 11);
   BOOST_TEST(i0.get<2>().find(12)->x2 == 12);
}

EXCEPTION_TEST_CASE(test_modify_fail2) {
   chainbase::undo_index<conflict_element_t, test_allocator<conflict_element_t>,
                         boost::multi_index::ordered_unique<key<&conflict_element_t::x0>>,
                         boost::multi_index::ordered_unique<key<&conflict_element_t::x1>>,
                         boost::multi_index::ordered_unique<key<&conflict_element_t::x2>>> i0;

   // insert 3 elements
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 10; elem.x1 = 10; elem.x2 = 10; });
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 11; elem.x2 = 11; });
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 12; elem.x1 = 12; elem.x2 = 12; });
   {
      auto session = i0.start_undo_session(true);
      // Insert a value with a duplicate
      i0.emplace([](conflict_element_t& elem) { elem.x0 = 71; elem.x1 = 81; elem.x2 = 91; });
      // BOOST_CHECK_THROW(i0.modify(i0.get(3), [](conflict_element_t& elem) { elem.x0 = 71; elem.x1 = 10; elem.x2 = 91; }), std::logic_error);
   }

   BOOST_TEST(i0.get<0>().size() == 3);
   BOOST_TEST(i0.get<1>().size() == 3);
   BOOST_TEST(i0.get<2>().size() == 3);
   // BOOST_TEST(i0.get<3>().size() == 3);
   BOOST_TEST(i0.find(10)->x0 == 10);
   BOOST_TEST(i0.find(11)->x1 == 11);
   BOOST_TEST(i0.find(12)->x2 == 12);
   // Check lookup in the other indices
   BOOST_TEST(i0.get<0>().find(10)->x0 == 10);
   BOOST_TEST(i0.get<0>().find(11)->x0 == 11);
   BOOST_TEST(i0.get<0>().find(12)->x0 == 12);
   BOOST_TEST(i0.get<1>().find(10)->x1 == 10);
   BOOST_TEST(i0.get<1>().find(11)->x1 == 11);
   BOOST_TEST(i0.get<1>().find(12)->x1 == 12);
   BOOST_TEST(i0.get<2>().find(10)->x2 == 10);
   BOOST_TEST(i0.get<2>().find(11)->x2 == 11);
   BOOST_TEST(i0.get<2>().find(12)->x2 == 12);
}

struct by_secondary {};

BOOST_AUTO_TEST_CASE(test_project2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                        //  boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::tag<by_secondary>, key<&test_element_t::secondary>>> i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.project<by_secondary>(i0.begin()) == i0.get<by_secondary>().begin());
   BOOST_TEST(i0.project<by_secondary>(i0.end()) == i0.get<by_secondary>().end());
   BOOST_TEST(i0.project<0>(i0.begin()) == i0.get<by_secondary>().begin());
   BOOST_TEST(i0.project<0>(i0.end()) == i0.get<by_secondary>().end());
}


EXCEPTION_TEST_CASE(test_remove_tracking_session2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 20; });
   auto session = i0.start_undo_session(true);
   auto tracker = i0.track_removed();
   i0.emplace([](test_element_t& elem) { elem.secondary = 21; });
   const test_element_t& elem0 = *i0.find(20);
   const test_element_t& elem1 = *i0.find(21);
   BOOST_CHECK(!tracker.is_removed(elem0));
   BOOST_CHECK(!tracker.is_removed(elem1));
   tracker.remove(elem0);
   tracker.remove(elem1);
   BOOST_CHECK(tracker.is_removed(elem0));
   BOOST_CHECK(tracker.is_removed(elem1));
}


EXCEPTION_TEST_CASE(test_remove_tracking_no_session2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 20; });
   auto tracker = i0.track_removed();
   i0.emplace([](test_element_t& elem) { elem.secondary = 21; });
   const test_element_t& elem0 = *i0.find(20);
   const test_element_t& elem1 = *i0.find(21);
   BOOST_CHECK(!tracker.is_removed(elem0));
   BOOST_CHECK(!tracker.is_removed(elem1));
   tracker.remove(elem0);
   tracker.remove(elem1);
   BOOST_CHECK(tracker.is_removed(elem0));
   BOOST_CHECK(tracker.is_removed(elem1));
}

size_t get_used_memory(segment_manager* manager) {
   return manager->get_size() - manager->get_free_memory();
}

EXCEPTION_TEST_CASE(test_memory_usage2) {
   using test_element_index = chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                        //  boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > >;
   test_element_index i0;
   auto *manager = i0.get_allocator().get_segment_manager();

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto session = i0.start_undo_session(true);
   }
   size_t used_memory = get_used_memory(manager);
   {
      auto session = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.get_created_value_count() == 1);
      BOOST_TEST(i0.find(12)->secondary == 12);
   }
   BOOST_TEST(used_memory == get_used_memory(manager));
   used_memory = get_used_memory(manager);
   {
      auto session = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.get_created_value_count() == 1);
      BOOST_TEST(i0.find(12)->secondary == 12);
      session.push();
      i0.commit(i0.revision());
   }
   BOOST_TEST(sizeof(test_element_index::node) + 8 == get_used_memory(manager) - used_memory);
   {
      auto session = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 13; });
      BOOST_TEST(i0.get_created_value_count() == 1);
      BOOST_TEST(i0.find(13)->secondary == 13);
      session.push();
      i0.commit(i0.revision());
   }

   BOOST_TEST(!i0.has_undo_session());
   BOOST_TEST(i0.get_created_value_count() == 0);
   BOOST_TEST(i0.find(42)->secondary == 42);
   // BOOST_TEST(i0.find(12) == nullptr);
}

EXCEPTION_TEST_CASE(test_memory_usage3) {
   using test_element_index = chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > >;
   test_element_index i0;

   auto *manager = i0.get_allocator().get_segment_manager();

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
      auto session = i0.start_undo_session(true);
   }
   size_t used_memory = get_used_memory(manager);
   {
      auto session = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.get_created_value_count() == 0);
      BOOST_TEST(i0.find(1)->secondary == 12);
      auto delta = i0.last_undo_session();
      BOOST_TEST(delta.new_values.size() == 1);
      BOOST_TEST(delta.new_values[0]->secondary == 12);
   }
   BOOST_TEST(used_memory == get_used_memory(manager));
   used_memory = get_used_memory(manager);
   {
      auto session = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.get_created_value_count() == 0);
      BOOST_TEST(i0.find(1)->secondary == 12);
      session.push();
      i0.commit(i0.revision());
   }
   BOOST_TEST(sizeof(test_element_index::node) + 8 == get_used_memory(manager) - used_memory);
   {
      auto session = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 13; });
      BOOST_TEST(i0.get_created_value_count() == 0);
      BOOST_TEST(i0.find(2)->secondary == 13);
      session.push();
      i0.commit(i0.revision());
   }

   BOOST_TEST(!i0.has_undo_session());
   BOOST_TEST(i0.get_created_value_count() == 0);
   BOOST_TEST(i0.find(0)->secondary == 42);
   // BOOST_TEST(i0.find(12) == nullptr);
}

EXCEPTION_TEST_CASE(test_commit2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                        //  boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   {
      auto session = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.get_created_value_count() == 1);
      BOOST_TEST(i0.find(12)->secondary == 12);
      session.push();
      auto delta = i0.last_undo_session();
      BOOST_TEST(delta.new_values.size() == 1);
      BOOST_TEST(delta.new_values[0]->secondary == 12);

      auto revision = i0.revision();

      auto session2 = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 13; });
      BOOST_TEST(i0.get_created_value_count() == 2);
      BOOST_TEST(i0.find(13)->secondary == 13);
      session2.push();
      // auto revision2 = i0.revision();
      delta = i0.last_undo_session();
      BOOST_TEST(delta.new_values.size() == 1);
      BOOST_TEST(delta.new_values[0]->secondary == 13);

      auto session3 = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 14; });
      BOOST_TEST(i0.get_created_value_count() == 3);
      BOOST_TEST(i0.find(14)->secondary == 14);
      session3.push();
      delta = i0.last_undo_session();
      BOOST_TEST(delta.new_values.size() == 1);
      BOOST_TEST(delta.new_values[0]->secondary == 14);

      auto revision3 = i0.revision();

      i0.commit(revision);
      BOOST_TEST(i0.get_created_value_count() == 2);
      // i0.commit(revision2);
      // BOOST_TEST(i0.get_created_value_count() == 1);
      i0.commit(revision3);
      BOOST_TEST(i0.get_created_value_count() == 0);
   }
   BOOST_TEST(!i0.has_undo_session());
   BOOST_TEST(i0.get_created_value_count() == 0);
   BOOST_TEST(i0.find(42)->secondary == 42);
   BOOST_TEST(i0.find(12)->secondary == 12);
   BOOST_TEST(i0.find(13)->secondary == 13);
   BOOST_TEST(i0.find(14)->secondary == 14);
}

EXCEPTION_TEST_CASE(test_commit_undo2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                        //  boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0{};

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   {
      auto session = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.get_created_value_count() == 1);
      BOOST_TEST(i0.find(12)->secondary == 12);
      session.push();
      auto revision = i0.revision();
      
      auto session2 = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 13; });
      BOOST_TEST(i0.get_created_value_count() == 2);
      BOOST_TEST(i0.find(13)->secondary == 13);
      session2.push();

      auto session3 = i0.start_undo_session(true);
      i0.emplace([](test_element_t& elem) { elem.secondary = 14; });
      BOOST_TEST(i0.get_created_value_count() == 3);
      BOOST_TEST(i0.find(14)->secondary == 14);
      session3.push();

      i0.commit(revision);
      BOOST_TEST(i0.get_created_value_count() == 2);

      i0.undo();
      BOOST_TEST(i0.find(14) == nullptr);
      BOOST_TEST(i0.find(13)->secondary == 13);
      BOOST_TEST(i0.find(12)->secondary == 12);
      BOOST_TEST(i0.get_created_value_count() == 1);

      i0.undo();
      BOOST_TEST(i0.find(14) == nullptr);
      BOOST_TEST(i0.find(13) == nullptr);
      BOOST_TEST(i0.find(12)->secondary == 12);
   }

   BOOST_TEST(!i0.has_undo_session());
}

EXCEPTION_TEST_CASE(test_last_undo_session2) {
   {
      chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                           boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                           boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;
      {
         auto session = i0.start_undo_session(true);
         i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
         i0.emplace([](test_element_t& elem) { elem.secondary = 13; });
         BOOST_TEST(i0.get_created_value_count() == 0);
         BOOST_TEST(i0.find(0)->secondary == 12);
         BOOST_TEST(i0.find(1)->secondary == 13);
         auto delta = i0.last_undo_session();
         BOOST_TEST(delta.new_values.size() == 2);
         BOOST_TEST(delta.new_values[0]->secondary == 12);
         BOOST_TEST(delta.new_values[1]->secondary == 13);
      }
   }

   {
      chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                           //  boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                           boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

      {
         auto session = i0.start_undo_session(true);
         i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
         i0.emplace([](test_element_t& elem) { elem.secondary = 13; });
         BOOST_TEST(i0.get_created_value_count() == 2);
         BOOST_TEST(i0.find(12)->secondary == 12);
         BOOST_TEST(i0.find(13)->secondary == 13);
         auto delta = i0.last_undo_session();
         BOOST_TEST(delta.new_values.size() == 2);
         BOOST_TEST(delta.new_values[0]->secondary == 12);
         BOOST_TEST(delta.new_values[1]->secondary == 13);
      }
   }

}

EXCEPTION_TEST_CASE(test_new_session_insert_new_session_remove_undo2) {
   chainbase::undo_index<test_element_t, test_allocator<test_element_t>,
                         boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0;

   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(42)->secondary == 42);
   {
      auto undo_checker = capture_state(i0);
      auto session = i0.start_undo_session(true);
      auto& obj = i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0.find(12)->secondary == 12);

      auto session2 = i0.start_undo_session(true);
      i0.remove(obj);
      BOOST_TEST(i0.find(12) == nullptr);

      session2.undo();
      BOOST_TEST(i0.find(12)->secondary == 12);

      session.undo();
      BOOST_TEST(i0.find(12) == nullptr);
      i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
   }
   BOOST_TEST(i0.find(42)->secondary == 42);
   BOOST_TEST(i0.find(12)->secondary == 12);
}

BOOST_AUTO_TEST_SUITE_END()

}