#pragma once

#include <system_error>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/container/flat_map.hpp>

#include <filesystem>
namespace chainbase {

namespace bip = boost::interprocess;

using segment_manager = bip::managed_mapped_file::segment_manager;

template<typename T>
using allocator = bip::allocator<T, segment_manager>;

enum db_error_code {
   ok = 0,
   dirty,
   incompatible,
   incorrect_db_version,
   not_found,
   bad_size,
   unsupported_win32_mode,
   bad_header,
   no_access,
   aborted,
   no_mlock
};

const std::error_category& chainbase_error_category();

inline std::error_code make_error_code(db_error_code e) noexcept {
   return std::error_code(static_cast<int>(e), chainbase_error_category());
}

class chainbase_error_category : public std::error_category {
public:
   const char* name() const noexcept override;
   std::string message(int ev) const override;
};

class pinnable_mapped_file {
   public:
      typedef typename bip::managed_mapped_file::segment_manager segment_manager;

      enum map_mode {
         mapped,
         heap,
         locked
      };

      pinnable_mapped_file(const std::filesystem::path& dir, bool writable, uint64_t shared_file_size, bool allow_dirty, map_mode mode);
      pinnable_mapped_file(pinnable_mapped_file&& o);
      pinnable_mapped_file& operator=(pinnable_mapped_file&&);
      pinnable_mapped_file(const pinnable_mapped_file&) = delete;
      pinnable_mapped_file& operator=(const pinnable_mapped_file&) = delete;
      ~pinnable_mapped_file();

      segment_manager* get_segment_manager() const { return _segment_manager;}
      size_t           check_memory_and_flush_if_needed();

      bip::mapped_region& get_mapped_region() { return _file_mapped_region; }

      // @brief Finds the allocator associated with a given pointer by looking up the segment it belongs to.
      // @note The performance of this function depends on the number of segments in `_segment_manager_map`.
      //       With a large number of segments, the lookup time can become significant.
      //
      // Benchmark results on Intel(R) Xeon(R) CPU E5-2686 v4 @ 2.30GHz:
      // - ~130.8 million calls/sec with 1 segments.
      // - ~115.5 million calls/sec with 10 segments.
      // - ~84.6 million calls/sec with 100 segments.
      // - ~22.8 million calls/sec with 1000 segments.
      // - ~20.8 million calls/sec with 10000 segments.
      // - ~19.8 million calls/sec with 100000 segments.
      //
      template<typename T>
      static std::optional<allocator<T>> get_allocator(void *object) {
         if (!_segment_manager_map.empty()) {
            auto it = _segment_manager_map.upper_bound(object);
            if(it == _segment_manager_map.begin())
               return {};
            auto [seg_start, seg_end] = *(--it);
            // important: we need to check whether the pointer is really within the segment, as shared objects'
            // can also be created on the stack (in which case the data is actually allocated on the heap using
            // std::allocator). This happens for example when `shared_cow_string`s are inserted into a bip::multimap,
            // and temporary pairs are created on the stack by the bip::multimap code.
            if (object < seg_end)
               return allocator<T>(reinterpret_cast<segment_manager *>(seg_start));
         }
         return {};
      }

   private:
      void                                          set_mapped_file_db_dirty(bool);
      void                                          load_database_file(boost::asio::io_service& sig_ios);
      void                                          save_database_file();
      bool                                          all_zeros(char* data, size_t sz);
      void                                          setup_non_file_mapping();

      bip::file_lock                                _mapped_file_lock;
      std::filesystem::path                         _data_file_path;
      std::string                                   _database_name;
      bool                                          _writable;
      map_mode                                      _map_mode;

      bip::file_mapping                             _file_mapping;
      bip::mapped_region                            _file_mapped_region;
      void*                                         _non_file_mapped_mapping = nullptr;
      size_t                                        _non_file_mapped_mapping_size = 0;

#ifdef _WIN32
      bip::permissions                              _db_permissions;
#else
      bip::permissions                              _db_permissions{S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH};
#endif

      segment_manager*                              _segment_manager = nullptr;

      constexpr static unsigned                     _db_size_multiple_requirement = 1024*1024; //1MB

      using segment_manager_map_t = boost::container::flat_map<void*, void *>;
      static segment_manager_map_t                  _segment_manager_map;
};

std::istream& operator>>(std::istream& in, pinnable_mapped_file::map_mode& runtime);
std::ostream& operator<<(std::ostream& osm, pinnable_mapped_file::map_mode m);

}
