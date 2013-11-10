// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "rpc.h"

class extent_client {
 private:
  rpcc *cl;
  // cache for extent
  struct extent_cache {
    extent_protocol::extentid_t eid;
    std::string extent;
    bool dirty;
    extent_cache(extent_protocol::extentid_t _eid, std::string _extent, bool _dirty = false) {
      eid = _eid;
      extent = _extent;
      dirty = _dirty;
    }
  };
  // cache for attr
  struct attr_cache {
    extent_protocol::extentid_t eid;
    extent_protocol::attr attr;
    attr_cache(extent_protocol::extentid_t _eid, extent_protocol::attr _attr) {
      eid = _eid;
      attr = _attr;
    }
  };
  // extent cache map
  std::map<extent_protocol::extentid_t, extent_cache *> extent_cache_map;
  // attr cache map
  std::map<extent_protocol::extentid_t, attr_cache *> attr_cache_map;
  // mutex for cache
  pthread_mutex_t cache_mutex;

 public:
  extent_client(std::string dst);

  extent_protocol::status get(extent_protocol::extentid_t eid, 
			      std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
  void flush(extent_protocol::extentid_t);
};

#line 65 "../extent_client.h"
#endif 

