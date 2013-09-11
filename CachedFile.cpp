#include "CachedFile.h"

CachedFile::CachedFile(char* file_buffer, size_t file_size, time_t last_used)
: file_buffer(file_buffer), file_size(file_size), last_used(last_used) {}

void CachedFile::setLastUsed(time_t new_time) {
    last_used = new_time;
}

char* CachedFile::getBuffer() const {
    return file_buffer;
}

size_t CachedFile::getSize() const {
    return file_size;
}

time_t CachedFile::getLastUsed() const {
    return last_used;
}

