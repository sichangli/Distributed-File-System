#ifndef CACHEDFILE_H
#define CACHEDFILE_H

#include <ctime>

class CachedFile {
public:
    CachedFile(char*, size_t, time_t);
    void setLastUsed(time_t);
    char* getBuffer() const;
    size_t getSize() const;
    time_t getLastUsed() const;
    
private:
    char* file_buffer;
    size_t file_size;
    time_t last_used;
};

#endif
