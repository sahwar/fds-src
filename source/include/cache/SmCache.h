/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_INCLUDE_CACHE_SMCACHE_H_
#define SOURCE_INCLUDE_CACHE_SMCACHE_H_

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "concurrency/RwLock.h"
#include "fds_error.h"
#include "fds_module.h"
#include "util/Log.h"

namespace fds {

// TODO (bszmyd):  Fri 12 Sep 2014 03:15:47 PM MDT
//   Eviction policy should be a templatization on cache,
//   not an inheritence like KvCache does it. So it should read
//   something like:
//
//   SmCache<int, int, fds::eviction_policy::lru> cache;
//
//   Right now all SmCache's are LRU eviction

/**
 * An abstract interface for a generic key-value cache. The cache
 * takes a basic size, either in number of element, and caches elements
 * using a specified eviction algorithm.
 *
 * This class IS thread safe
 */
template<class K, class V, class _Hash = std::hash<K>>
class SmCache : public Module, boost::noncopyable {
    public:
     typedef K key_type;
     typedef V mapped_type;
     typedef _Hash hash_type;
     typedef std::size_t size_type;
     typedef std::shared_ptr<mapped_type> value_type;

    // Eviction list
    private:
     typedef std::pair<key_type, value_type> entry_type;
     typedef std::list<entry_type> cache_type;

     typedef typename cache_type::iterator iterator;
     typedef typename cache_type::const_iterator const_iterator;

     // Backing data structure that implements key to value associated lookup.
     typedef typename std::unordered_map<key_type, iterator, hash_type> index_type;

    public:
     /**
      * Constructs the cache object but does not init
      * @param[in] modName     Name of this module
      * @param[in] _max_entries Maximum number of cache entries
      *
      * @return none
      */
     SmCache(const std::string& module_name, size_type const _max_entries) :
         Module(module_name.c_str()),
         cache_map(),
         eviction_list(),
         max_entries(_max_entries),
         cache_lock() { }

     ~SmCache() {}

     /**
      * Adds a key-value pair to the cache.
      * The cache will take ownership of value pointers added to
      * the cache. That is, the pointer cannot be deleted by
      * whoever passes it in. The cache will become responsible for that.
      * If the key already exists, it will be overwritten.
      * If the cache is full, the evicted entry will be returned.
      * Entries that are evicted are returned as unique_ptr so the
      * caller can use it before it gets freed.
      *
      * @param[in] key   Key to use for indexing
      * @param[in] value Associated value
      *
      * @return A shared_ptr<T> to any evicted entry
      */
     value_type add(const key_type& key, value_type value) {
         SCOPEDWRITE(cache_lock);
         GLOGTRACE << "Adding key " << key;
         // Remove any exiting entry from cache
         remove(key);

         // Add the entry to the front of the eviction list and into the map.
         eviction_list.emplace_front(key, value);
         cache_map[key] = eviction_list.begin();

         // Check if anything needs to be evicted
         if (eviction_list.size() > this->max_entries) {
             entry_type evicted = eviction_list.back();
             eviction_list.pop_back();

             GLOGTRACE << "Evicting key " << evicted.first;
             value_type entryToEvict = evicted.second;
             fds_verify(entryToEvict);

             // Remove the cache iterator entry from the map
             cache_map.erase(evicted.first);

             // Make sure we're not over the limit
             fds_verify(eviction_list.size() == this->max_entries);
             return entryToEvict;
         }

         // Return a NULL pointer if nothing was evicted
         return value_type(nullptr);
     }

     /**
      * Convenience add-by-value method. Copies incoming value
      * prior to inserting it into the cache line.
      *
      * @param[in] key   Key to use for indexing
      * @param[in] value Associated value
      *
      * @return A shared_ptr<T> to any evicted entry
      */
     value_type add(const key_type &key, mapped_type const& value) {
         return add(key, std::make_shared<mapped_type>(value));
     }

     /**
      * Removes all keys and values from the cache line
      *
      * @return none
      */
     void clear() {
         SCOPEDWRITE(cache_lock);
         // Clear all of the containers
         eviction_list.clear();
         cache_map.clear();
     }

     /**
      * Returns the value for the assoicated key. When a value
      * is returned, the cache RETAINS ownership of pointer and
      * it cannot be freed by the caller.
      * When a value is return, the entry's access is updated.
      * 
      * @param[in]  key      Key to use for indexing
      * @param[out] value_out Pointer to value
      * @para[in]   do_touch  Whether to track this access
      *
      * @return ERR_OK if a value is returned, ERR_NOT_FOUND otherwise.
      */
     Error get(const key_type &key,
                       value_type& value_out,
                       fds_bool_t const do_touch = true) {
         SCOPEDWRITE(cache_lock);
         typename cache_type::iterator elemIt;

         // Touch the entry in the list if we want
         // to track this access
         if (do_touch == true) {
             elemIt = touch(key);
         } else {
             auto mapIt = cache_map.find(key);
             if (mapIt == cache_map.end())
                 elemIt = eviction_list.end();
             else
                 elemIt = mapIt->second;
         }

         if (elemIt == eviction_list.end())
             return ERR_NOT_FOUND;

         // Set the return value
         value_out = elemIt->second;
         return ERR_OK;
     }

     /**
      * Checks if a key exists in the cache
      * 
      * @param[in]  key  Key to use for indexing
      *
      * @return true if key exists, false otherwise
      */
     fds_bool_t exists(const K &key) const {
         SCOPEDREAD(cache_lock);
         return (cache_map.find(key) != cache_map.end());
     }

     /**
      * Returns the current number of entries in the cache
      *
      * @return number of entries
      */
     size_type getNumEntries() const {
         SCOPEDREAD(cache_lock);
         return eviction_list.size();
     }

     /// Init module
     int  mod_init(SysParams const *const param) {
         return 0;
     }
     /// Start module
     void mod_startup() {
     }
     /// Shutdown module
     void mod_shutdown() {
     }

    private:
     // Maximum number of entries in the cache
     size_type max_entries;

     // Cache line itself
     cache_type eviction_list;

     // K->V association lookup
     index_type cache_map;

     // Synchronization members
     fds_rwlock cache_lock;

     /**
      * Removes a key and value from the cache
      *
      * @param[in] key   Key to use for indexing
      *
      * @return none
      */
     void remove(const key_type &key) {
         // Locate the key in the map
         auto mapIt = cache_map.find(key);
         if (mapIt != cache_map.end()) {
             iterator cacheEntry = mapIt->second;
             // Remove from the cache_map
             cache_map.erase(mapIt);
             // Remove from the eviction_list
             eviction_list.erase(cacheEntry);
         }
     }


     /**
      * Touches a key for the purpose of representing an access.
      *
      * @param[in]  key  Key to use for indexing
      *
      * @return ERR_OK if a value is returned, ERR_NOT_FOUND otherwise.
      */
     typename cache_type::iterator touch(const key_type &key) {
         auto mapIt = cache_map.find(key);
         if (mapIt == cache_map.end()) return eviction_list.end();

         iterator existingEntry = mapIt->second;
         value_type value  = (*existingEntry).second;

         // Move the entry's position to the front
         // in the eviction list
         eviction_list.erase(existingEntry);
         entry_type updatedEntry(key, value);
         eviction_list.push_front(updatedEntry);

         // Remove the existing map entry since the
         // iterator into the eviction list is changing
         cache_map.erase(mapIt);
         cache_map[key] = eviction_list.begin();

         return eviction_list.begin();
     }

};
}  // namespace fds

#endif  // SOURCE_INCLUDE_CACHE_SMCACHE_H_
