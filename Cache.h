#ifndef CACHE_H
#define CACHE_H

#include "CachedFile.h"
#include <map>
#include <string>

class Cache {
public:
    Cache();
    void runPolicy(const std::string&, CachedFile*);
    CachedFile* getFile(const std::string&);
    size_t getSize() const;
    
private:
    void deleteCachedFile(CachedFile*);
    void deleteLRUFile();
    
    std::map<std::string, CachedFile*> cache;
    size_t size;
};

#endif
