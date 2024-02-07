#pragma once

#include <stdint.h>

namespace chainbase {
    class undo_index_events {
        public:
            undo_index_events() {}
            virtual uint64_t get_instance_id() const = 0;
            virtual bool is_read_only() const = 0;
            virtual void on_find_begin(uint64_t instance_id, uint64_t database_id, const std::type_info& key_type_info, const std::type_info& value_type_info, const void *key) = 0;
            virtual void on_find_end(uint64_t instance_id, uint64_t database_id, const std::type_info& key_type_info, const std::type_info& value_type_info, const void *key, const void *obj) = 0;
            virtual void on_lower_bound_begin(uint64_t instance_id, uint64_t database_id, const std::type_info& key_type_info, const std::type_info& value_type_info, const void *key) = 0;
            virtual void on_lower_bound_end(uint64_t instance_id, uint64_t database_id, const std::type_info& key_type_info, const std::type_info& value_type_info, const void *key, const void *obj) = 0;
            virtual void on_upper_bound_begin(uint64_t instance_id, uint64_t database_id, const std::type_info& key_type_info, const std::type_info& value_type_info, const void *key) = 0;
            virtual void on_upper_bound_end(uint64_t instance_id, uint64_t database_id, const std::type_info& key_type_info, const std::type_info& value_type_info, const void *key, const void *obj) = 0;
            virtual void on_equal_range_begin(uint64_t instance_id, uint64_t database_id, const std::type_info& key_type_info, const std::type_info& value_type_info, const void *key) = 0;
            virtual void on_equal_range_end(uint64_t instance_id, uint64_t database_id, const std::type_info& key_type_info, const std::type_info& value_type_info, const void *key) = 0;
            virtual void on_create_begin(uint64_t instance_id, uint64_t database_id, const std::type_info& value_type_info, const void *id) = 0;
            virtual void on_create_end(uint64_t instance_id, uint64_t database_id, const std::type_info& value_type_info, const void *id, const void *obj) = 0;

            virtual void on_add_value_in_undo(uint64_t instance_id, uint64_t database_id, const std::type_info& value_type_info, const void *obj) = 0;
            virtual void on_remove_value_in_undo(uint64_t instance_id, uint64_t database_id, const std::type_info& value_type_info, const void *obj) = 0;

            virtual void on_modify_begin(uint64_t instance_id, uint64_t database_id, const std::type_info& value_type_info, const void *obj) = 0;
            virtual void on_modify_end(uint64_t instance_id, uint64_t database_id, const std::type_info& value_type_info, const void *obj, bool success) = 0;
            virtual void on_remove_begin(uint64_t instance_id, uint64_t database_id, const std::type_info& value_type_info, const void *obj) = 0;
            virtual void on_remove_end(uint64_t instance_id, uint64_t database_id, const std::type_info& value_type_info) = 0;
    };

    undo_index_events *get_undo_index_events(uint64_t instance_id);
    void add_undo_index_events(undo_index_events *event);
    void clear_undo_index_events(uint64_t instance_id);

    bool undo_index_is_read_only(uint64_t instance_id);

    template<typename K, typename V>
    inline const void undo_index_on_find_begin(uint64_t instance_id, uint64_t database_id, const K& key) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_find_begin(instance_id, database_id, typeid(K), typeid(V), &key);
    }

    template<typename K, typename V>
    inline void undo_index_on_find_end(uint64_t instance_id, uint64_t database_id, const K& key, const V *obj) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_find_end(instance_id, database_id, typeid(K), typeid(V), &key, obj);
    }

    template<typename K, typename V>
    inline void undo_index_on_lower_bound_begin(uint64_t instance_id, uint64_t database_id, const K& key) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_lower_bound_begin(instance_id, database_id, typeid(K), typeid(V), &key);
    }

    template<typename K, typename V>
    inline void undo_index_on_lower_bound_end(uint64_t instance_id, uint64_t database_id, const K& key, const V *obj) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_lower_bound_end(instance_id, database_id, typeid(K), typeid(V), &key, obj);
    }


    template<typename K, typename V>
    inline void undo_index_on_upper_bound_begin(uint64_t instance_id, uint64_t database_id, const K& key) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_upper_bound_begin(instance_id, database_id, typeid(K), typeid(V), &key);
    }

    template<typename K, typename V>
    inline void undo_index_on_upper_bound_end(uint64_t instance_id, uint64_t database_id, const K& key, const V *obj) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_upper_bound_end(instance_id, database_id, typeid(K), typeid(V), &key, obj);
    }

    template<typename K, typename V>
    inline void undo_index_on_equal_range_begin(uint64_t instance_id, uint64_t database_id, const K& key) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_equal_range_begin(instance_id, database_id, typeid(K), typeid(V), &key);
    }

    template<typename K, typename V>
    inline void undo_index_on_equal_range_end(uint64_t instance_id, uint64_t database_id, const K& key) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_equal_range_end(instance_id, database_id, typeid(K), typeid(V), &key);
    }


    template<typename id_type, typename V>
    inline void undo_index_on_create_begin(uint64_t instance_id, uint64_t database_id, const id_type& id) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_create_begin(instance_id, database_id, typeid(V), &id);
    }

    template<typename id_type, typename V>
    inline void undo_index_on_create_end(uint64_t instance_id, uint64_t database_id, const id_type& id, const V *obj) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_create_end(instance_id, database_id, typeid(V), &id, obj);
    }

    template<typename V>
    inline void undo_index_on_add_value_in_undo(uint64_t instance_id, uint64_t database_id, const V *obj) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_add_value_in_undo(instance_id, database_id, typeid(V), obj);
    }

    template<typename V>
    inline void undo_index_on_remove_value_in_undo(uint64_t instance_id, uint64_t database_id, const V *obj) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_remove_value_in_undo(instance_id, database_id, typeid(V), obj);
    }

    template<typename V>
    inline void undo_index_on_modify_begin(uint64_t instance_id, uint64_t database_id, const V *obj) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_modify_begin(instance_id, database_id, typeid(V), obj);
    }

    template<typename V>
    inline void undo_index_on_modify_end(uint64_t instance_id, uint64_t database_id, const V *obj, bool success) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_modify_end(instance_id, database_id, typeid(V), obj, success);
    }

    template<typename V>
    inline void undo_index_on_remove_begin(uint64_t instance_id, uint64_t database_id, const V *obj) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_remove_begin(instance_id, database_id, typeid(V), obj);
    }

    template<typename V>
    inline void undo_index_on_remove_end(uint64_t instance_id, uint64_t database_id) {
        auto event = get_undo_index_events(instance_id);
        if (!event) return;
        event->on_remove_end(instance_id, database_id, typeid(V));
    }
}
