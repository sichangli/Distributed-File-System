// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#include <map>
#include <pthread.h>
#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"

class lock_server {

 protected:
  // keep track of number of acquisitions for each lock
  std::map<lock_protocol::lockid_t, int> nacquire_map;

  // state for the lock
  enum lock_state { FREE, LOCKED };

  // keep track of lock state and the clt owns it
  struct lock_info
  {
    lock_info(unsigned int, lock_state);

    unsigned int clt;
    lock_state state;
  };

  // use map to store lock and its state
  std::map<lock_protocol::lockid_t, lock_info *> lock_map;
  // mutex and condition variable for shared data
  pthread_mutex_t lock_mutex;
  pthread_cond_t lock_cv;

 public:
  lock_server();
  ~lock_server();
  lock_protocol::status stat(unsigned int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status acquire(unsigned int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status release(unsigned int clt, lock_protocol::lockid_t lid, int &);
};

#endif 







