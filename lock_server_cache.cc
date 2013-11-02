// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

static void *
revokethread(void *x)
{
  lock_server_cache *sc = (lock_server_cache *) x;
  sc->revoker();
  return 0;
}

static void *
retrythread(void *x)
{
  lock_server_cache *sc = (lock_server_cache *) x;
  sc->retryer();
  return 0;
}

lock_server_cache::lock_server_cache()
{
  pthread_t th;
  int r = pthread_create(&th, NULL, &revokethread, (void *) this);
  assert (r == 0);
  r = pthread_create(&th, NULL, &retrythread, (void *) this);
  assert (r == 0);
  // init mutex for client
  if (pthread_mutex_init(&server_mutex, NULL)) {
    perror("Cannot init server mutext");
    exit(1);
  }
  // init cv for revoker
  if (pthread_cond_init(&revoker_cond, NULL)) {
    perror("Cannot init cv for revoker");
    exit(1);
  }
  // init cv for retryer
  if (pthread_cond_init(&retryer_cond, NULL)) {
    perror("Cannot init cv for retryer");
    exit(1);
  }
}

rpcc *
lock_server_cache::getRpcc(const std::string &dst) {
  rpcc *cl = rpccs[dst];
  if (!cl) {
    sockaddr_in dstsock;
    make_sockaddr(dst.c_str(), &dstsock);
    cl = new rpcc(dstsock);
    if (cl->bind() < 0) {
      printf("lock_client: call bind\n");
    }
    rpccs[dst] = cl;
  }
  return cl;
}

void
lock_server_cache::revoker()
{
  // This method should be a continuous loop, that sends revoke
  // messages to lock holders whenever another client wants the
  // same lock
  while (true) {
    pthread_mutex_lock(&server_mutex);
    pthread_cond_wait(&revoker_cond, &server_mutex);
    while (!revokes.empty()) {
      lock *l = revokes.front();
      lock_protocol::lockid_t lid = l->lid;
      int xxid = l->xid + 1;
      revokes.pop();
      rpcc *cl = getRpcc(l->id);
      pthread_mutex_unlock(&server_mutex);
      int r;
      cl->call(rlock_protocol::revoke, lid, xxid, r);
      pthread_mutex_lock(&server_mutex);
    }
    pthread_mutex_unlock(&server_mutex);
  }
}


void
lock_server_cache::retryer()
{
  // This method should be a continuous loop, waiting for locks
  // to be released and then sending retry messages to those who
  // are waiting for it.
  while (true) {
    pthread_mutex_lock(&server_mutex);
    pthread_cond_wait(&retryer_cond, &server_mutex);
    while (!retries.empty()) {
      lock *l = retries.front();
      lock_protocol::lockid_t lid = l->lid;
      retries.pop();
      int xxid = l->xids[l->id] + 1;
      l->xids.erase(l->id);
      rpcc *cl = getRpcc(l->id);
      pthread_mutex_unlock(&server_mutex);
      int r;
      cl->call(rlock_protocol::retry, lid, xxid, r);
      pthread_mutex_lock(&server_mutex);
    }
    pthread_mutex_unlock(&server_mutex);
  }
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t, int &r)
{
  r = 0;
  return lock_protocol::OK;
}

lock_protocol::status
lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, int xid, int &r)
{
  ScopedLock slock(&server_mutex);

  lock *l = locks[lid];
  if (!l) {
    l = new lock(lid);
    locks[lid] = l;
  }

  printf("  acquire: from %s for lock %llu\n", id.c_str(), lid);

  if (FREE == l->state) {   // lock is free
    printf("  acquire: the lock is free, grant the lock to %s\n", id.c_str());
    l->id = id;
    l->state = LOCKED;
    l->xid = xid;
    return lock_protocol::OK;
  } else if (LOCKED == l->state) {  // lock is locked
    printf("  acquire: the lock is locked, revoke the lock\n");
    l->waitings.push(id);
    l->state = REVOKED;
    l->xids[id] = xid;
    revokes.push(l);
    pthread_cond_signal(&revoker_cond);
    return lock_protocol::RETRY;
  } else if (REVOKED == l->state) {  // server is sending revoke, no need to send again
    printf("  acquire: the lock is REVOKED\n");
    l->waitings.push(id);
    l->xids[id] = xid;
    return lock_protocol::RETRY;
  } else {  // lock is waiting retry from a client
    printf("  acquire: the lock is RETRIED\n");
    if (id == l->id) { // it's the client we're waiting for
      l->id = id;
      l->state = LOCKED;
      l->xid = xid;
      // if other clients are also waiting, send revoke immediately after granting
      if (!l->waitings.empty()) {
        l->state = REVOKED;
        revokes.push(l);
        pthread_cond_signal(&revoker_cond);
      }
      return lock_protocol::OK;
    } else {
      l->waitings.push(id);
      l->xids[id] = xid;
      return lock_protocol::RETRY;
    }
  }
}

lock_protocol::status
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, int xid, int &r)
{
  ScopedLock slock(&server_mutex);

  lock *l = locks[lid];
  if (!l) {
    printf("  release: cannot release unknown lock\n");
    return lock_protocol::NOENT;
  }

  // the client is not holding the lock
  if (id != l->id) {
    printf("  release: the client is not holding the lock\n");
    return lock_protocol::NOENT;
  }

  // the xid is not the same as acquire
  if (xid != l->xid) {
    printf("  release: different xid from acquire\n");
    return lock_protocol::NOENT;
  }

  printf("  release: client %s release lock %llu\n", id.c_str(), lid);
  // if no other client is waiting on this lock  
  if (l->waitings.empty()) {
    l->id = "";
    l->state = FREE;
  } else {  // if other clients are still waiting, send retry to client at the front of the waiting queue
    l->id = l->waitings.front();
    l->state = RETRIED;
    l->waitings.pop();
    retries.push(l);
    pthread_cond_signal(&retryer_cond);
    printf("  release: send retry to client %s\n", l->id.c_str());
  }
  return lock_protocol::OK;
}