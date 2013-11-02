#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"
#include <queue>


class lock_server_cache {
 private:
  enum lstate {
    FREE,
    LOCKED,
    REVOKED,
    RETRIED
 	};
  struct lock {
    lock(lock_protocol::lockid_t _lid) {
      lid = _lid;
      state = FREE;
    }
    lock_protocol::lockid_t lid;
    std::string id;
    lstate state;
    // clients waiting for the lock
    std::queue<std::string> waitings;
    // xid for each client
    std::map<std::string, int> xids;
    // xid of the client holding the lock
    int xid;
  };
 	std::map<lock_protocol::lockid_t, lock *> locks;
  pthread_mutex_t server_mutex;
  pthread_cond_t revoker_cond;
  pthread_cond_t retryer_cond;
  std::queue<lock *> revokes;
  std::queue<lock *> retries;
  std::map<std::string, rpcc *> rpccs;

  // get the rpcc from client id
  rpcc *getRpcc(const std::string &);
 public:
  lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  void revoker();
  void retryer();
  lock_protocol::status acquire(lock_protocol::lockid_t, std::string, int, int &);
  lock_protocol::status release(lock_protocol::lockid_t, std::string, int, int &);
};

#endif
