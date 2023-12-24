#include <chainbase/shared_object_allocator.hpp>
#include <map>
#include <unordered_map>
#include <vector>
#include <sstream>

namespace chainbase {

    static std::vector<segment_manager*> s_segment_manager_vector = {};
    static std::map<segment_manager*, uint64_t> s_segment_manager_to_id_map = {};

    void allocator_set_segment_manager(uint64_t segment_manager_id, segment_manager *manager) {
        if (segment_manager_id > max_segment_manager_id || segment_manager_id == 0) {
            std::stringstream ss;
            ss << "allocator_set_segment_manager: invalid segment_manager_id: " << segment_manager_id;
            BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
            return;
        }

        if (s_segment_manager_vector.size() == 0) {
            s_segment_manager_vector.resize(default_segment_manager_cache_size);
        }

        if (s_segment_manager_vector.size() <= segment_manager_id) {
            s_segment_manager_vector.resize(segment_manager_id + 1);
        }
        auto *old_manager = s_segment_manager_vector[segment_manager_id];
        if (old_manager != nullptr) {
            s_segment_manager_to_id_map.erase(old_manager);
        }
        s_segment_manager_vector[segment_manager_id] = manager;
        s_segment_manager_to_id_map[manager] = segment_manager_id;
    }

    size_t allocator_get_segment_manager_id(segment_manager *manager) {
        auto it = s_segment_manager_to_id_map.find(manager);
        if (it == s_segment_manager_to_id_map.end()) {
            BOOST_THROW_EXCEPTION(std::runtime_error("allocator_get_segment_manager_id: segment_manager not found"));
            return 0;
        }
        return it->second;
    }

    segment_manager *allocator_get_segment_manager_by_id(uint64_t manager_manager_id) {
        if (manager_manager_id >= s_segment_manager_vector.size() || manager_manager_id == 0) {
            std::stringstream ss;
            ss << "allocator_get_segment_manager_by_id 1: invalid segment_manager_id: " << manager_manager_id;
            BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
            return nullptr;
        }
        auto *ret = s_segment_manager_vector[manager_manager_id];
        if (ret == nullptr) {
            std::stringstream ss;
            ss << "allocator_get_segment_manager_by_id 2: invalid segment_manager_id: " << manager_manager_id;
            BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
            return nullptr;
        }
        return ret;
    }
}