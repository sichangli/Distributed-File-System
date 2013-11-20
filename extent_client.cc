// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// The calls assume that the caller holds a lock on the extent

extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
  // init cach_mutex
  if (pthread_mutex_init(&cache_mutex, NULL)) {
    printf("cannot init mutex for cache\n");
    exit(1);
  }
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  ScopedLock slock(&cache_mutex);
  extent_protocol::status ret = extent_protocol::OK;

  // if it's already removed
  if (removed.find(eid) != removed.end()) {
    printf("  get %016llx: extent is already removed, return NOENT\n", eid);
    return extent_protocol::NOENT;
  }
  // find cache from cache map
  extent_cache *ec = extent_cache_map[eid];
  // if the extent is not cached
  if (!ec) {
    printf("  get %016llx: extent is not cached, sending get to server\n", eid);
    ret = cl->call(extent_protocol::get, eid, buf);
    if (extent_protocol::OK == ret) {
      printf("  get %016llx: server returned OK, adding extent to cache\n", eid);
      extent_cache_map[eid] = new extent_cache(eid, buf); 
    }
  } else {  // extent is cached
    printf("  get %016llx: extent is cached, return the cache\n", eid);
    buf = ec->extent;
  }
  return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
  ScopedLock slock(&cache_mutex);
  extent_protocol::status ret = extent_protocol::OK;

  // if it's already removed
  if (removed.find(eid) != removed.end()) {
    printf("  getattr %016llx: extent is already removed, return NOENT\n", eid);
    return extent_protocol::NOENT;
  }

  attr_cache *ac = attr_cache_map[eid];
  // if the attr is not cached
  if (!ac) {
    printf("  getattr %016llx: attr is not cached, sending getattr to server\n", eid);
    ret = cl->call(extent_protocol::getattr, eid, attr);
    if (extent_protocol::OK == ret) {
      printf("  getattr %016llx: server returned OK, adding attr to cache\n", eid);
      attr_cache_map[eid] = new attr_cache(eid, attr);
    }
  } else {
    printf("  getattr %016llx: attr is cached, return the cache\n", eid);
    attr = ac->attr;
  }
  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  ScopedLock slock(&cache_mutex);
  extent_protocol::status ret = extent_protocol::OK;
  // find cache from cache map
  extent_cache *ec = extent_cache_map[eid];
  // if it has been removed
  if (removed.find(eid) != removed.end()) {
    printf("  put %016llx: extent is already removed, remove it from removed list\n", eid);
    removed.erase(eid);
  }

  // if extent not cached
  if (!ec) {
    printf("  put %016llx: extent is not cached, create the cache\n", eid);
    extent_cache_map[eid] = new extent_cache(eid, buf, true);
  } else { // if cached
    printf("  put %016llx: extent is cached, update the cache\n", eid);
    ec->extent = buf;
    ec->dirty = true;
  }

  attr_cache *ac = attr_cache_map[eid];
  extent_protocol::attr a;
  a.atime = a.mtime = a.ctime = time(NULL);
  a.size = buf.size();
  if (!ac) {
    printf("  put %016llx: attr is not cached, create the cache\n", eid);
    attr_cache_map[eid] = new attr_cache(eid, a);
  } else {
    printf("  put %016llx: attr is cached, update the cache\n", eid);
    ac->attr = a;
  }
  return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  ScopedLock slock(&cache_mutex);
  extent_protocol::status ret = extent_protocol::OK;

  // if it's already removed
  if (removed.find(eid) != removed.end()) {
    printf("  remove %016llx: extent is already removed, return NOENT\n", eid);
    return extent_protocol::NOENT;
  }
  // find cache from cache map
  extent_cache *ec = extent_cache_map[eid];
  if (!ec) {
    printf("  remove %016llx: extent is not cached, add to remove set\n", eid);
    removed.insert(eid);
  } else {
    printf("  remove %016llx: extent is cached, remove the cache and add to remove set\n", eid);
    delete ec;
    removed.insert(eid);
    extent_cache_map.erase(eid);
  }

  attr_cache *ac = attr_cache_map[eid];
  if (ac) {
    printf("  remove %016llx: attr is cached, remove the attr cache\n", eid);
    delete ac;
    attr_cache_map.erase(eid);
  }
  return ret;
}

void
extent_client::flush(extent_protocol::extentid_t eid)
{
  ScopedLock slock(&cache_mutex);
  int r;

  // if it's in the removed list
  if (removed.find(eid) != removed.end()) {
    printf("  flush %016llx: extent is already removed, call remove on server\n", eid);
    cl->call(extent_protocol::remove, eid, r);
    return;
  }

  // delete extent cache
  extent_cache *ec = extent_cache_map[eid];
  if (ec) {
    if (ec->dirty) {
      printf("  flush %016llx: extent is dirty, call put on server\n", eid);
      cl->call(extent_protocol::put, eid, ec->extent, r);
    }
    delete ec;
    extent_cache_map.erase(eid);
  }
  // delete attr cache
  attr_cache *ac = attr_cache_map[eid];
  if (ac) {
    delete ac;
    attr_cache_map.erase(eid);
  }
}


