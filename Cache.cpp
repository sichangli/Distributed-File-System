#include "Cache.h"
#include <iostream>
#include <algorithm>

#define CACHE_MAX 1024 * 1024 * 64

// functor for LRU policy
class LRU {
public:
    bool operator()(const std::pair<std::string , CachedFile*>& lhs, const std::pair<std::string , CachedFile*>& rhs) {
        return lhs.second->getLastUsed() < rhs.second->getLastUsed();
    }
};

Cache::Cache() : size(0) {}

void Cache::deleteCachedFile(CachedFile* cached) {
    char* file_buffer = cached->getBuffer();
    delete [] file_buffer;
    delete cached;
    cached = NULL;
}

void Cache::runPolicy(const std::string& file_name, CachedFile* cached) {
    // do not cache a file larger than 64MB
    if (cached->getSize() > CACHE_MAX) {
        deleteCachedFile(cached);
        return;
    }
    
    // delete all lru files until the cache have enough space for current file
    while (size + cached->getSize() > CACHE_MAX)
        deleteLRUFile();
    
    // add current file into cache and update cache size
    cache[file_name] = cached;
    size += cached->getSize();
    
    std::cout << "File " << file_name << " is added to cache" << std::endl;
}

void Cache::deleteLRUFile() {
    // find the least recent used file in cache, delete and update
    std::map<std::string, CachedFile*>::iterator lru_iter = min_element(cache.begin(), cache.end(), LRU());
    
    std::string lru_file_name = lru_iter->first;
    CachedFile* lru_file = lru_iter->second;
    
    // update the cache size and delete
    size -= lru_file->getSize();
    deleteCachedFile(lru_file);
    cache.erase(lru_iter);
    
    std::cout << "File " << lru_file_name << " is removed from cache" << std::endl;
}

CachedFile* Cache::getFile(const std::string& file_name) {
    return cache.find(file_name) == cache.end() ? NULL : cache[file_name];
}

size_t Cache::getSize() const {
    return size;
}
