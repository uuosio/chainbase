#pragma once

#include <stdint.h>

namespace chainbase {
    class undo_index_events {
        public:
            undo_index_events() {}
            virtual void *on_find_begin(const std::type_info& key_type_info, const std::type_info& valeu_type_info, const void *key) = 0;
            virtual void on_find_end(const std::type_info& key_type_info, const std::type_info& valeu_type_info, const void *key, const void *obj) = 0;
            virtual void on_lower_bound_begin(const std::type_info& key_type_info, const std::type_info& valeu_type_info, const void *key) = 0;
            virtual void on_lower_bound_end(const std::type_info& key_type_info, const std::type_info& valeu_type_info, const void *key, const void *obj) = 0;
            virtual void on_upper_bound_begin(const std::type_info& key_type_info, const std::type_info& valeu_type_info, const void *key) = 0;
            virtual void on_upper_bound_end(const std::type_info& key_type_info, const std::type_info& valeu_type_info, const void *key, const void *obj) = 0;
            virtual void on_equal_range_begin(const std::type_info& key_type_info, const std::type_info& valeu_type_info, const void *key) = 0;
            virtual void on_equal_range_end(const std::type_info& key_type_info, const std::type_info& valeu_type_info, const void *key) = 0;
            virtual void on_create_begin(const std::type_info& valeu_type_info, const void *id) = 0;
            virtual void on_create_end(const std::type_info& valeu_type_info, const void *id, const void *obj) = 0;
            virtual void on_modify_begin(const std::type_info& valeu_type_info, const void *obj) = 0;
            virtual void on_modify_end(const std::type_info& valeu_type_info, const void *obj, bool success) = 0;
            virtual void on_remove_begin(const std::type_info& valeu_type_info, const void *obj) = 0;
            virtual void on_remove_end(const std::type_info& valeu_type_info, const void *obj) = 0;
    };

    undo_index_events *get_undo_index_events();
    void set_undo_index_events(undo_index_events *event);

    template<typename K, typename V>
    inline void *undo_index_on_find_begin(const K& key) {
        auto event = get_undo_index_events();
        if (!event) return nullptr;
        return get_undo_index_events()->on_find_begin(typeid(K), typeid(V), &key);
    }

    template<typename K, typename V>
    inline void undo_index_on_find_end(const K& key, const V *obj) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_find_end(typeid(K), typeid(V), &key, obj);
    }

    template<typename K, typename V>
    inline void undo_index_on_lower_bound_begin(const K& key) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_lower_bound_begin(typeid(K), typeid(V), &key);
    }

    template<typename K, typename V>
    inline void undo_index_on_lower_bound_end(const K& key, const V *obj) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_lower_bound_end(typeid(K), typeid(V), &key, obj);
    }


    template<typename K, typename V>
    inline void undo_index_on_upper_bound_begin(const K& key) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_upper_bound_begin(typeid(K), typeid(V), &key);
    }

    template<typename K, typename V>
    inline void undo_index_on_upper_bound_end(const K& key, const V *obj) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_upper_bound_end(typeid(K), typeid(V), &key, obj);
    }

    template<typename K, typename V>
    inline void undo_index_on_equal_range_begin(const K& key) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_equal_range_begin(typeid(K), typeid(V), &key);
    }

    template<typename K, typename V>
    inline void undo_index_on_equal_range_end(const K& key) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_equal_range_end(typeid(K), typeid(V), &key);
    }


    template<typename id_type, typename V>
    inline void undo_index_on_create_begin(const id_type& id) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_create_begin(typeid(V), &id);
    }

    template<typename id_type, typename V>
    inline void undo_index_on_create_end(const id_type& id, const V *obj) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_create_end(typeid(V), &id, obj);
    }

    template<typename V>
    inline void undo_index_on_modify_begin(const V *obj) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_modify_begin(typeid(V), obj);
    }

    template<typename V>
    inline void undo_index_on_modify_end(const V *obj, bool success) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_modify_end(typeid(V), obj, success);
    }

    template<typename V>
    inline void undo_index_on_remove_begin(const V *obj) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_remove_begin(typeid(V), obj);
    }

    template<typename V>
    inline void undo_index_on_remove_end(const V *obj) {
        auto event = get_undo_index_events();
        if (!event) return;
        get_undo_index_events()->on_remove_end(typeid(V), obj);
    }
}
