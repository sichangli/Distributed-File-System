// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_client.h"

#line 15 "../lock_client_cache.h"

#line 17 "../lock_client_cache.h"
// Classes that inherit lock_release_user can override dorelease so that 
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 6.
class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};
#line 26 "../lock_client_cache.h"

#line 28 "../lock_client_cache.h"

// SUGGESTED LOCK CACHING IMPLEMENTATION PLAN:
//
// to work correctly for lab 7,  all the requests on the server run till 
// completion and threads wait on condition variables on the client to
// wait for a lock.  this allows the server to be replicated using the
// replicated state machine approach.
//
// On the client a lock can be in several states:
//  - free: client owns the lock and no thread has it
//  - locked: client owns the lock and a thread has it
//  - acquiring: the client is acquiring ownership
//  - releasing: the client is releasing ownership
//
// in the state acquiring and locked there may be several threads
// waiting for the lock, but the first thread in the list interacts
// with the server and wakes up the threads when its done (released
// the lock).  a thread in the list is identified by its thread id
// (tid).
//
// a thread is in charge of getting a lock: if the server cannot grant
// it the lock, the thread will receive a retry reply.  at some point
// later, the server sends the thread a retry RPC, encouraging the client
// thread to ask for the lock again.
//
// once a thread has acquired a lock, its client obtains ownership of
// the lock. the client can grant the lock to other threads on the client 
// without interacting with the server. 
//
// the server must send the client a revoke request to get the lock back. this
// request tells the client to send the lock back to the
// server when the lock is released or right now if no thread on the
// client is holding the lock.  when receiving a revoke request, the
// client adds it to a list and wakes up a releaser thread, which returns
// the lock the server as soon it is free.
//
// the releasing is done in a separate a thread to avoid
// deadlocks and to ensure that revoke and retry RPCs from the server
// run to completion (i.e., the revoke RPC cannot do the release when
// the lock is free.
//
// a challenge in the implementation is that retry and revoke requests
// can be out of order with the acquire and release requests.  that
// is, a client may receive a revoke request before it has received
// the positive acknowledgement on its acquire request.  similarly, a
// client may receive a retry before it has received a response on its
// initial acquire request.  a flag field is used to record if a retry
// has been received.
//

#line 79 "../lock_client_cache.h"

#line 81 "../lock_client_cache.h"

class lock_client_cache;

// Client runs lock_revoke_server to receive revoke RPCs from lock_server
class lock_reverse_server {
 private:
  lock_client_cache *lc;
 public:
  lock_reverse_server(lock_client_cache *lc);
  int revoke(lock_protocol::lockid_t, lock_protocol::xid_t, int &r);
  int retry(lock_protocol::lockid_t, lock_protocol::xid_t, int &r);
};

// Clients that caches locks.  The server can revoke locks using 
// lock_revoke_server.
#line 98 "../lock_client_cache.h"
class lock_client_cache : public lock_client {
 private:
#line 103 "../lock_client_cache.h"
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;

#line 109 "../lock_client_cache.h"
  lock_protocol::xid_t xid;
  typedef unsigned long tid_t;
  enum cstate { UNUSED, FREE, LOCKED, ACQUIRING, RELEASING};
  struct lock_entry {
    lock_entry(lock_protocol::lockid_t _lid) {
      assert(pthread_cond_init(&cond, NULL) == 0);  
      lid = _lid;
      state = UNUSED;
      retry = false;
      xid = 0;
      once = false;
    }
    lock_protocol::lockid_t lid;
    cstate state;
    pthread_cond_t cond;
    bool retry;
    bool once;
    lock_protocol::xid_t xid;
    tid_t tid;
    std::list<tid_t> threads;
  };
  std::map<lock_protocol::lockid_t,lock_entry*> lock_cache;
  std::map<lock_protocol::lockid_t,lock_protocol::xid_t> nextacquire;
  std::list<lock_protocol::lockid_t> releaselist;
  pthread_cond_t release_cond;
  pthread_mutex_t lock_cache_mutex;
  lock_reverse_server *rls;
  void flush(lock_entry *);
  void addrelease(lock_protocol::lockid_t name);
  bool isrevoked(lock_entry *l);
  void dorelease(lock_protocol::lockid_t, std::string id, 
		 lock_protocol::xid_t xid);
#line 143 "../lock_client_cache.h"
 public:
  static int last_port;
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache() {};
  lock_protocol::status acquire(lock_protocol::lockid_t);
  virtual lock_protocol::status release(lock_protocol::lockid_t);
  void releaser();
#line 152 "../lock_client_cache.h"
  lock_protocol::status dorevoke(lock_protocol::lockid_t, 
				 lock_protocol::xid_t);
  lock_protocol::status doretry(lock_protocol::lockid_t, 
				lock_protocol::xid_t);
  lock_protocol::status subscribe(std::string);
#line 158 "../lock_client_cache.h"
};
#line 160 "../lock_client_cache.h"
#endif


