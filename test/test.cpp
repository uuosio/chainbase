#define BOOST_TEST_MODULE chainbase test
#include <boost/test/unit_test.hpp>
#include <chainbase/chainbase.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>

#include <iostream>
#include "temp_directory.hpp"

using namespace chainbase;
using namespace boost::multi_index;

//BOOST_TEST_SUITE( serialization_tests, clean_database_fixture )

struct book : public chainbase::object<0, book> {

   template<typename Constructor, typename Allocator>
    book(  Constructor&& c, Allocator&& a ) {
       c(*this);
    }

    id_type id;
    int a;
    int b;
    int c;
};

typedef multi_index_container<
  book,
  indexed_by<
     ordered_unique< member<book,book::id_type,&book::id> >,
     ordered_unique< BOOST_MULTI_INDEX_MEMBER(book,int,a) >,
     ordered_unique< BOOST_MULTI_INDEX_MEMBER(book,int,b) >
  >,
  chainbase::node_allocator<book>
> book_index;

CHAINBASE_SET_INDEX_TYPE( book, book_index )


BOOST_AUTO_TEST_CASE( open_and_create ) {
   temp_directory temp_dir;
   const auto& temp = temp_dir.path();
   std::cerr << temp << " \n";

   chainbase::database db(temp, database::read_write, 1024*1024*8);
   chainbase::database db2(temp, database::read_only, 0, true); /// open an already created db
   BOOST_CHECK_THROW( db2.add_index< book_index >(), std::runtime_error ); /// index does not exist in read only database

   db.add_index< book_index >();
   BOOST_CHECK_THROW( db.add_index<book_index>(), std::logic_error ); /// cannot add same index twice


   db2.add_index< book_index >(); /// index should exist now


   BOOST_TEST_MESSAGE( "Creating book" );
   const auto& new_book = db.create<book>( []( book& b ) {
      b.a = 3;
      b.b = 4;
   } );
   const auto& copy_new_book = db2.get( book::id_type(0) );
   BOOST_REQUIRE( &new_book != &copy_new_book ); ///< these are mapped to different address ranges

   BOOST_REQUIRE_EQUAL( new_book.a, copy_new_book.a );
   BOOST_REQUIRE_EQUAL( new_book.b, copy_new_book.b );

   db.modify( new_book, [&]( book& b ) {
      b.a = 5;
      b.b = 6;
   });
   BOOST_REQUIRE_EQUAL( new_book.a, 5 );
   BOOST_REQUIRE_EQUAL( new_book.b, 6 );

   BOOST_REQUIRE_EQUAL( new_book.a, copy_new_book.a );
   BOOST_REQUIRE_EQUAL( new_book.b, copy_new_book.b );

   {
      auto session = db.start_undo_session(true);
      db.modify( new_book, [&]( book& b ) {
         b.a = 7;
         b.b = 8;
      });

      BOOST_REQUIRE_EQUAL( new_book.a, 7 );
      BOOST_REQUIRE_EQUAL( new_book.b, 8 );
   }
   BOOST_REQUIRE_EQUAL( new_book.a, 5 );
   BOOST_REQUIRE_EQUAL( new_book.b, 6 );

   {
      auto session = db.start_undo_session(true);
      const auto& book2 = db.create<book>( [&]( book& b ) {
         b.a = 9;
         b.b = 10;
      });

      BOOST_REQUIRE_EQUAL( new_book.a, 5 );
      BOOST_REQUIRE_EQUAL( new_book.b, 6 );
      BOOST_REQUIRE_EQUAL( book2.a, 9 );
      BOOST_REQUIRE_EQUAL( book2.b, 10 );
   }
   BOOST_CHECK_THROW( db2.get( book::id_type(1) ), std::out_of_range );
   BOOST_REQUIRE_EQUAL( new_book.a, 5 );
   BOOST_REQUIRE_EQUAL( new_book.b, 6 );


   {
      auto session = db.start_undo_session(true);
      db.modify( new_book, [&]( book& b ) {
         b.a = 7;
         b.b = 8;
      });

      BOOST_REQUIRE_EQUAL( new_book.a, 7 );
      BOOST_REQUIRE_EQUAL( new_book.b, 8 );
      session.push();
   }
   BOOST_REQUIRE_EQUAL( new_book.a, 7 );
   BOOST_REQUIRE_EQUAL( new_book.b, 8 );
   db.undo();
   BOOST_REQUIRE_EQUAL( new_book.a, 5 );
   BOOST_REQUIRE_EQUAL( new_book.b, 6 );

   BOOST_REQUIRE_EQUAL( new_book.a, copy_new_book.a );
   BOOST_REQUIRE_EQUAL( new_book.b, copy_new_book.b );
}

BOOST_AUTO_TEST_CASE( test_shared_string ) {
   temp_directory temp_dir;
   const auto& temp = temp_dir.path();
   std::cerr << temp << " \n";

   chainbase::database db(temp, database::read_write, 1024*1024*8);
   db.add_index< book_index >();

   auto& idx = db.get_mutable_index< book_index >();
   auto alloc = idx.get_allocator().get_first_allocator();

   size_t free_memory = db.get_free_memory();
   {
      auto s1 = shared_cow_string(*alloc);
      auto s2 = shared_cow_string(*alloc);
      shared_cow_string *s1_ptr = &s1;
      // test assign method
      s1.assign("", 0);
      BOOST_TEST(nullptr == s1.data());

      s1.assign("hello", 5);

      // test resize
      s1.resize(0, boost::container::default_init);
      BOOST_TEST(db.get_free_memory() == free_memory);

      s1.assign("hello", 5);

      const char *data_old = s1.data();
      BOOST_TEST(nullptr != data_old);

      // test self assignment
      *s1_ptr = s1;
      BOOST_TEST(data_old == s1.data());

      // test self move assignment
      *s1_ptr = std::move(s1);
      BOOST_TEST(data_old == s1.data());

      // test normal assigment
      s2 = s1;
      BOOST_TEST(data_old == s2.data());

      //test move assignment
      s2 = std::move(s1);
      BOOST_TEST(data_old == s2.data());
      BOOST_TEST(s1.data() == nullptr);

      // test move constructor
      auto s3 = shared_cow_string(std::move(s2));
      BOOST_TEST(data_old == s3.data());
      BOOST_TEST(s2.data() == nullptr);
   }
   BOOST_TEST(free_memory == db.get_free_memory());

   {
      auto s1 = shared_string_ex(alloc);
      auto s2 = shared_string_ex(alloc);
      shared_string_ex *s1_ptr = &s1;

      // test assign method
      s1.assign("", 0);
      BOOST_TEST(nullptr == s1.data());

      s1.assign("hello", 5);
      const char *data_old = s1.data();
      BOOST_TEST(nullptr != data_old);

      // test self assignment
      *s1_ptr = s1; 
      BOOST_TEST(data_old == s1.data());

      // test self move assignment
      *s1_ptr = std::move(s1);
      BOOST_TEST(data_old == s1.data());

      // test normal assigment
      s2 = s1;
      BOOST_TEST(data_old == s2.data());

      //test move assignment
      s2 = std::move(s1);
      BOOST_TEST(data_old == s2.data());
      BOOST_TEST(s1.data() == nullptr);

      // test move constructor
      auto s3 = shared_string_ex(std::move(s2));
      BOOST_TEST(data_old == s3.data());
      BOOST_TEST(s2.data() == nullptr);
   }
   BOOST_TEST(free_memory == db.get_free_memory());

   {
      auto s1 = shared_object<shared_cow_string>(alloc);
      auto s2 = shared_object<shared_cow_string>(alloc);
      shared_object<shared_cow_string> *s1_ptr = &s1;

      // test assign method
      s1->assign("", 0);
      BOOST_TEST(nullptr == s1->data());

      s1->assign("hello", 5);
      const char *data_old = s1->data();
      BOOST_TEST(nullptr != data_old);

      // test self assignment
      *s1_ptr = s1; 
      BOOST_TEST(data_old == s1->data());

      // test self move assignment
      *s1_ptr = std::move(s1);
      BOOST_TEST(data_old == s1->data());

      // test normal assigment
      s2 = s1;
      BOOST_TEST(data_old == s2->data());

      //test move assignment
      s2 = std::move(s1);
      BOOST_TEST(data_old == s2->data());
      BOOST_TEST(s1.get_offset() == 0);

      // test move constructor
      auto s3 = shared_object<shared_cow_string>(std::move(s2));
      BOOST_TEST(data_old == s3->data());
      BOOST_TEST(s2.get_offset() == 0);
   }
   BOOST_TEST(free_memory == db.get_free_memory());
   return;
}

BOOST_AUTO_TEST_CASE( test_create_ex ) {
   temp_directory temp_dir;
   const auto& temp = temp_dir.path();
   std::cerr << temp << " \n";

   chainbase::database db(temp, database::read_write, 1024*1024*8);
   db.add_index< book_index >();
   
   auto& idx = db.get_mutable_index< book_index >();
   idx.init_next_id(0);

   auto session = db.start_undo_session(true);
   const auto& new_book1 = db.create<book>( []( book& b ) {
      b.a = 1;
      b.b = 2;
   } );
   BOOST_TEST(!idx.is_mature_object(new_book1));

   const auto& new_book2 = db.create_without_undo<book>( []( book& b ) {
      b.a = 3;
      b.b = 4;
      b.c = 5;
   } );
   BOOST_TEST(idx.is_mature_object(new_book2));
   BOOST_TEST(new_book2.id == book::id_type(0));

   const auto& copy_new_book2 = db.get( book::id_type(0) );
   BOOST_TEST(copy_new_book2.id == new_book2.id);

   db.modify( copy_new_book2, [&]( book& b ) {
      // b.a = 5;
      // b.b = 6;
   });

   BOOST_TEST(!idx.is_mature_object(copy_new_book2));

   session.squash();
   BOOST_TEST(idx.is_mature_object(copy_new_book2));

{
   auto session = db.start_undo_session(true);
   db.remove(copy_new_book2);
   session.undo();
   const auto *new_book_ptr = db.find( book::id_type(0) );
   BOOST_TEST(new_book_ptr != nullptr);
}

{
   auto session = db.start_undo_session(true);
   db.modify(copy_new_book2, [](book& b){
      b.c = 123;
   });
   session.undo();
   const auto *new_book_ptr = db.find( book::id_type(0) );
   BOOST_TEST(new_book_ptr != nullptr);
   BOOST_TEST(new_book_ptr->c == 5);
}

{
   auto session = db.start_undo_session(true);
   db.remove_without_undo(copy_new_book2);
   session.undo();
   const auto *new_book_ptr = db.find( book::id_type(0) );
   BOOST_TEST(new_book_ptr == nullptr);
}

}


BOOST_AUTO_TEST_CASE( test_exists ) {
   temp_directory temp_dir;
   const auto& temp = temp_dir.path();
   std::cerr << temp << " \n";

   chainbase::database db(temp, database::read_write, 1024*1024*8);
   db.add_index< book_index >();
   
   auto& idx = db.get_mutable_index< book_index >();
   idx.init_next_id(0);

   auto session = db.start_undo_session(true);
   const auto& new_book1 = db.create<book>( []( book& b ) {
      b.a = 1;
      b.b = 2;
   } );

   BOOST_TEST(idx.exists(new_book1));

   chainbase::allocator<char> alloc = chainbase::allocator<char>(idx.get_allocator().get_segment_manager());
   auto new_book2 = book([](auto& obj){
      obj.a = 0;
      obj.b = 2;
   }, alloc);

   BOOST_TEST(idx.exists(new_book2));

   new_book2 = book([](auto& obj){
      obj.a = 1;
      obj.b = 0;
   }, alloc);

   BOOST_TEST(idx.exists(new_book2));

   auto new_book3 = book([](auto& obj){
      obj.a = 3;
      obj.b = 4;
   }, alloc);

   BOOST_TEST(!idx.exists(new_book3));
}

// BOOST_AUTO_TEST_SUITE_END()
