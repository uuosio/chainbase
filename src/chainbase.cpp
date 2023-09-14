#include <chainbase/chainbase.hpp>
#include <boost/array.hpp>

#include <iostream>

#ifndef _WIN32
#include <sys/mman.h>
#endif

namespace chainbase {

   database::database(const std::filesystem::path& dir, open_flags flags, uint64_t shared_file_size, bool allow_dirty,
                      pinnable_mapped_file::map_mode db_map_mode) :
      _db_file(dir, flags & database::read_write, shared_file_size, allow_dirty, db_map_mode),
      _read_only(flags == database::read_only)
   {
      _read_only_mode = _read_only;
   }

   database::~database()
   {
      _index_list.clear();
      _index_map.clear();
   }

   void database::undo()
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to undo in read-only mode" ) );
      for( auto& item : _index_list )
      {
         item->undo();
      }
   }

   void database::squash()
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to squash in read-only mode" ) );
      for( auto& item : _index_list )
      {
         item->squash();
      }
   }

   void database::commit( int64_t revision )
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to commit in read-only mode" ) );
      for( auto& item : _index_list )
      {
         item->commit( revision );
      }
   }

   void database::undo_all()
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to undo_all in read-only mode" ) );
      for( auto& item : _index_list )
      {
         item->undo_all();
      }
   }

   database::session database::start_undo_session( bool enabled )
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to start_undo_session in read-only mode" ) );
      if( enabled ) {
         vector< std::unique_ptr<abstract_session> > _sub_sessions;
         _sub_sessions.reserve( _index_list.size() );
         for( auto& item : _index_list ) {
            _sub_sessions.push_back( item->start_undo_session( enabled ) );
         }
         return session( std::move( _sub_sessions ) );
      } else {
         return session();
      }
   }

   int64_t database::revision()const {
      if( _index_list.size() == 0 ) return -1;
      return _index_list[0]->revision();
   }

   void database::set_revision( uint64_t revision )
   {
      if ( _read_only_mode ) {
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to set revision in read-only mode" ) );
      }
      for( auto i : _index_list ) i->set_revision( revision );
   }

   pinnable_mapped_file::segment_manager* database::get_segment_manager() {
      return _db_file.get_segment_manager();
   }

   const pinnable_mapped_file::segment_manager* database::get_segment_manager() const {
      return _db_file.get_segment_manager();
   }

   size_t database::get_free_memory()const
   {
      return _db_file.get_segment_manager()->get_free_memory();
   }

   void database::set_read_only_mode() {
      _read_only_mode = true;
   }

   void database::unset_read_only_mode() {
         if ( _read_only )
            BOOST_THROW_EXCEPTION( std::logic_error( "attempting to unset read_only_mode while database was opened as read only" ) );
      _read_only_mode = false;
   }

   bool database::has_undo_session() const
   {
      assert( _index_list.size() > 0 );
      return _index_list[0]->has_undo_session();
   }

   database::database_index_row_count_multiset database::row_count_per_index()const {
      database_index_row_count_multiset ret;
      for(const auto& ai_ptr : _index_map) {
         if(!ai_ptr)
            continue;
         ret.emplace(make_pair(ai_ptr->row_count(), ai_ptr->type_name()));
      }
      return ret;
   }

   static undo_index_events *s_undo_index_events = nullptr;

   undo_index_events *get_undo_index_events() {
      return s_undo_index_events;
   }

   void set_undo_index_events(undo_index_events *event) {
      s_undo_index_events = event;
   }

}  // namespace chainbase
