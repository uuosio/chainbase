#include <chainbase/chainbase.hpp>
#include <chainbase/undo_index.hpp>

#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/unit_test.hpp>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chainbase/pinnable_mapped_file.hpp>
#include <chainbase/secondary_index.hpp>
#include <chainbase/shared_object_ptr.hpp>

using namespace chainbase;
using namespace boost::multi_index;

namespace bmi = boost::multi_index;
using bmi::composite_key;
using bmi::composite_key_compare;
using bmi::const_mem_fun;
using bmi::indexed_by;
using bmi::member;
using bmi::ordered_non_unique;
using bmi::ordered_unique;
using bmi::tag;

struct by_id;

// gcc-11 adds new dynamic memory warnings that return false positives for many
// of the stack allocated instantiations of chainbase::undo_index<>. For
// example, see test_insert_modify. Evaluation of the actual behavior using ASAN
// with gcc-11 and clang-11 indicates these are false positives. The warning is
// disabled for gcc-11 and above.
#if defined(__GNUC__) && (__GNUC__ >= 11) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif

namespace undo_index_tests {

BOOST_AUTO_TEST_SUITE(secondary_index_tests)

static int exception_counter = 0;
static int throw_at = -1;
struct test_exception_base {};
template <typename E> struct test_exception : E, test_exception_base {
    template <typename... A>
    test_exception(A &&... a) : E{static_cast<E &&>(a)...} {}
};
template <typename E, typename... A> void throw_point(A &&... a) {
    if (throw_at != -1 && exception_counter++ >= throw_at) {
        throw test_exception<E>{static_cast<A &&>(a)...};
    }
}
template <typename F> void test_exceptions(F &&f) {
    for (throw_at = 0;; ++throw_at) {
        exception_counter = 0;
        try {
            f();
            break;
        } catch (test_exception_base &) {
        }
    }
    throw_at = -1;
    exception_counter = 0;
}

struct throwing_copy {
    throwing_copy() { throw_point<std::bad_alloc>(); }
    throwing_copy(const throwing_copy &) { throw_point<std::bad_alloc>(); }
    throwing_copy(throwing_copy &&) noexcept = default;
    throwing_copy &operator=(const throwing_copy &) {
        throw_point<std::bad_alloc>();
        return *this;
    }
    throwing_copy &operator=(throwing_copy &&) noexcept = default;
};

namespace bip = boost::interprocess;
using segment_manager = chainbase::pinnable_mapped_file::segment_manager;

template <typename T> struct test_allocator {
    using value_type = T;
    using pointer = bip::offset_ptr<T>;

    test_allocator() {
        size_t size = 1024 * 1024 * 64;
        _manager = new ((segment_manager *)malloc(size)) segment_manager{size};
    }

    ~test_allocator() {
        _manager->~segment_manager();
        free(_manager);
    }

    template <typename U>
    test_allocator(const test_allocator<U> &other) : _manager{other._manager} {}

    template <typename U> struct rebind { using other = test_allocator<U>; };

    pointer allocate(std::size_t num) {
        throw_point<std::bad_alloc>();
        return pointer{(T *)_manager->allocate(num * sizeof(T))};
    }

    void deallocate(const pointer &p, std::size_t num) {
        _manager->deallocate(&*p);
    }

    segment_manager *get_segment_manager() { return _manager; }

    segment_manager *_manager;
};

struct test_element_t;

using test_node_allocator = chainbase::node_allocator<test_element_t>;

struct test_element_t {
    template <typename C, typename A>
    test_element_t(C &&c, const test_allocator<A> &) {
        c(*this);
    }

    template <typename C, typename A> test_element_t(C &&c, A &&a) { c(*this); }

    chainbase::oid<test_element_t> id;
    int secondary;
    throwing_copy dummy;

    chainbase::oid<test_element_t> get_id() const { return id; }

    int get_secondary() const { return secondary; }
};

struct conflict_element_t {
    template <typename C, typename A>
    conflict_element_t(C &&c, const test_allocator<A> &) {
        c(*this);
    }
    chainbase::oid<conflict_element_t> id;
    int x0;
    int x1;
    int x2;
    throwing_copy dummy;
};

struct basic_element_t {
    template <typename C, typename A>
    basic_element_t(C &&c, const test_allocator<A> &) {
        c(*this);
    }
    chainbase::oid<basic_element_t> id;
    throwing_copy dummy;
};

template <typename F> struct scope_fail {
    scope_fail(F &&f)
        : _f{static_cast<F &&>(f)}, _exception_count{
                                        std::uncaught_exceptions()} {}
    ~scope_fail() {
        if (_exception_count != std::uncaught_exceptions())
            _f();
    }
    F _f;
    int _exception_count;
};

// TODO: Replace with boost::multi_index::key once we bump our minimum Boost
// version to at least 1.69
template <typename T> struct key_impl;
template <typename C, typename T> struct key_impl<T C::*> {
    template <auto F> using fn = boost::multi_index::member<C, T, F>;
};

template <auto Fn> using key = typename key_impl<decltype(Fn)>::template fn<Fn>;

#define EXCEPTION_TEST_CASE(name)                                              \
    void name##impl();                                                         \
    BOOST_AUTO_TEST_CASE(name) { test_exceptions(&name##impl); }               \
    void name##impl()

EXCEPTION_TEST_CASE(test_simple) {
    chainbase::secondary_index<
        basic_element_t, test_allocator<basic_element_t>,
        boost::multi_index::ordered_unique<key<&basic_element_t::id>>>
        i0;
    i0.emplace([](basic_element_t &elem) {});
    const basic_element_t *element = i0.find(0);
    BOOST_TEST((element != nullptr && element->id == 0));
    const basic_element_t *e1 = i0.find(1);
    BOOST_TEST(e1 == nullptr);
    i0.emplace([](basic_element_t &elem) {});
    const basic_element_t *e2 = i0.find(1);
    BOOST_TEST((e2 != nullptr && e2->id == 1));

    i0.modify(*element, [](basic_element_t &elem) {});
    i0.remove(*element);
    element = i0.find(0);
    BOOST_TEST(element == nullptr);
}

// If an exception is thrown while an undo session is active, undo will restore
// the state.
template <typename C> auto capture_state(const C &index) {
    std::vector<std::pair<test_element_t, const test_element_t *>> vec;
    for (const auto &elem : index) {
        vec.emplace_back(elem, &elem);
    }
    return scope_fail{[vec = std::move(vec), &index] {
        BOOST_TEST(index.size() == vec.size());
        for (const auto &[elem, ptr] : vec) {
            auto *actual0 = index.find(elem.id);
            BOOST_TEST(actual0 == ptr); // reference stability is guaranteed
            if (actual0 != nullptr) {
                BOOST_TEST(actual0->id == elem.id);
                BOOST_TEST(actual0->secondary == elem.secondary);
            }
            auto actual1iter = index.template get<1>().find(elem.secondary);
            BOOST_TEST((actual1iter != index.template get<1>().end() &&
                        &*actual1iter == actual0));
        }
    }};
}

struct test_element_ptr {
  public:
    template <typename Constructor, typename Allocator>
    test_element_ptr(Constructor &&c, Allocator &&a) : ptr(a) {
        c(*this);
    }

    chainbase::shared_object_ptr<test_element_t> ptr;

    chainbase::oid<test_element_t> get_id() const { return ptr->id; }

    int get_secondary() const { return ptr->secondary; }

    operator test_element_t &() const { return *ptr; }

    test_element_t &operator*() const { return *ptr; }

    test_element_t &operator->() const { return *ptr; }
};

EXCEPTION_TEST_CASE(test_insert_undo) {
    using test_secondary_index = chainbase::secondary_index<
        test_element_ptr, chainbase::node_allocator<test_element_t>,
        boost::multi_index::ordered_unique<const_mem_fun<
            test_element_ptr, int, &test_element_ptr::get_secondary>>>;

    using undo_index = chainbase::undo_index<
        test_element_t, chainbase::node_allocator<test_element_t>,
        boost::multi_index::ordered_unique<key<&test_element_t::id>>,
        boost::multi_index::ordered_unique<key<&test_element_t::secondary>>>;

    size_t size = 256 * 1024 * 1024;
    auto _manager = new ((segment_manager *)malloc(size)) segment_manager{size};
    chainbase::node_allocator<undo_index> alloc(_manager);
    chainbase::node_allocator<test_element_t> alloc2(_manager);

    auto _i0_ptr = alloc.allocate(sizeof(undo_index));
    auto *_i0 = new (&*_i0_ptr) undo_index(alloc2);
    undo_index &i0 = *_i0;

    auto _sec_ptr = alloc.allocate(sizeof(test_secondary_index));
    auto *_sec = new (&*_sec_ptr)test_secondary_index(alloc2);
    test_secondary_index &sec = *_sec;

    auto clear = scope_exit{
        [_i0, _sec, _i0_ptr, _sec_ptr, _manager, &alloc]() {
            std::cout << "clear," << "throw_at: " << throw_at << std::endl;
            _i0->~undo_index();
            _sec->~test_secondary_index();
            alloc.deallocate(_i0_ptr, 1);
            alloc.deallocate(_sec_ptr, 1);
            _manager->~segment_manager();
            free(_manager);
        }
    };

    auto _database_configure =
        _manager->construct<chainbase::database_configure>(
            chainbase::database_configure_name)();
    _database_configure->unique_segment_manager_id = 1;
    chainbase::allocator_set_segment_manager(
        _database_configure->unique_segment_manager_id, _manager);

    i0.set_on_undo_created([&sec](const test_element_t &elem) {
        std::cout << "on_undo_created: " << elem.id << " " << elem.secondary
                  << std::endl;
        auto key = elem.get_secondary();
        auto *obj = sec.find(key);
        BOOST_TEST(obj != nullptr);
        sec.remove(*obj);
    });

    i0.set_on_undo_modified([&sec](const test_element_t &cur_elem,
                                   std::function<void()> modify_fn) {
        std::cout << "on_undo_modified: " << cur_elem.id << std::endl;
        auto key = cur_elem.get_secondary();
        auto *obj = sec.find(key);
        BOOST_TEST(obj != nullptr);
        modify_fn();
        sec.modify(*obj, [](test_element_ptr &elem) {});
    });

    i0.set_on_undo_removed([&sec, &alloc2](const test_element_t &elem) {
        std::cout << "on_undo_removed: " << elem.id << std::endl;
        auto key = elem.get_secondary();
        auto *obj = sec.find(key);
        BOOST_TEST(obj == nullptr);
        sec.emplace([&alloc2, &elem](test_element_ptr &elem_ptr) {
            elem_ptr.ptr = chainbase::shared_object_ptr<test_element_t>(
                chainbase::allocator_type(alloc2.get_segment_manager()), elem);
        });
    });

    const auto &elem = i0.emplace([](test_element_t &elem) {
        elem.secondary = 42;
    });

    sec.emplace([&sec, &elem](test_element_ptr &elem_ptr) {
        elem_ptr.ptr = chainbase::shared_object_ptr<test_element_t>(
            chainbase::allocator_type(sec.get_allocator().get_segment_manager()),
            elem
        );
    });

    BOOST_TEST(i0.find(0)->secondary == 42);

    BOOST_TEST(sec.find(42) != nullptr);

    {
        auto undo_checker = capture_state(i0);
        auto session = i0.start_undo_session(true);
        const auto &elem = i0.emplace([](test_element_t &elem) {
            elem.secondary = 12;
        });

        sec.emplace([&sec, &elem](test_element_ptr &elem_ptr) {
            elem_ptr.ptr = chainbase::shared_object_ptr<test_element_t>(
                chainbase::allocator_type(
                    sec.get_allocator().get_segment_manager()),
                elem);
        });

        BOOST_TEST(i0.find(1)->secondary == 12);
    }

    {
        auto undo_checker = capture_state(i0);
        auto session = i0.start_undo_session(true);
        auto *elem = i0.find(0);
        auto key = elem->get_secondary();
        auto *obj = sec.find(key);
        i0.modify(*elem, [](test_element_t &elem) {
            elem.secondary = 12;
        });

        sec.modify(*obj, [](test_element_ptr &elem) {
        });

        BOOST_TEST(i0.find(0)->secondary == 12);
        BOOST_TEST(sec.find(12)->ptr->secondary == 12);
    }

    BOOST_TEST(i0.find(0)->secondary == 42);
    BOOST_TEST(i0.find(1) == nullptr);

    BOOST_TEST(sec.find(42)->ptr->secondary == 42);
    BOOST_TEST(sec.find(12) == nullptr);

    {
        auto undo_checker = capture_state(i0);
        auto session = i0.start_undo_session(true);
        auto *elem = i0.find(0);
        auto key = elem->get_secondary();
        BOOST_TEST(key == 42);
        i0.remove(*elem);
        auto *obj = sec.find(key);
        BOOST_TEST(obj != nullptr);
        sec.remove(*obj);

        BOOST_TEST(i0.find(0) == nullptr);
        BOOST_TEST(sec.find(key) == nullptr);
        std::cout << "test_insert_undo: remove end" << std::endl;
    }

    BOOST_TEST(i0.find(0)->secondary == 42);
    BOOST_TEST(i0.find(1) == nullptr);

    BOOST_TEST(sec.find(42)->ptr->secondary == 42);
    BOOST_TEST(sec.find(12) == nullptr);
    std::cout << "test_insert_undo end" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()

}

