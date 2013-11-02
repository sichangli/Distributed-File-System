// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>



static void *
releasethread(void *x)
{
  lock_client_cache *cc = (lock_client_cache *) x;
  cc->releaser();
  return 0;
}

int lock_client_cache::last_port = 0;

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // assert(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  /* register RPC handlers with rlsrpc */
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry);

  pthread_t th;
  int r = pthread_create(&th, NULL, &releasethread, (void *) this);
  assert (r == 0);

  // init mutex for client
  if (pthread_mutex_init(&client_mutex, NULL)) {
    perror("Cannot init client mutext");
    exit(1);
  }
  // init cv for releaser
  if (pthread_cond_init(&releaser_cond, NULL)) {
    perror("Cannot init cv for releaser");
    exit(1);
  }
  rth = th;
}

lock_client_cache::~lock_client_cache()
{
  int r;
  std::map<lock_protocol::lockid_t, lock *>::iterator it;
  // wait the releaser thread to finish
  pthread_join(rth, NULL);

  // free all locks
  for (it = locks.begin(); it != locks.end(); it++) {
    lock_protocol::lockid_t lid = it->first;
    lock *l = it->second;
    // release lock back to server before termination
    if (FREE == l->state) {
      cl->call(lock_protocol::release, lid, id, l->xid, r);
    }
    pthread_cond_destroy(&l->cond);
    pthread_cond_destroy(&l->retry_cond);
    pthread_cond_destroy(&l->order_cond);
    delete l;
  }
  locks.clear();

  pthread_mutex_destroy(&client_mutex);
  pthread_cond_destroy(&releaser_cond);
}

void
lock_client_cache::releaser()
{

  // This method should be a continuous loop, waiting to be notified of
  // freed locks that have been revoked by the server, so that it can
  // send a release RPC.
  while (true) {
    pthread_mutex_lock(&client_mutex);
    pthread_cond_wait(&releaser_cond, &client_mutex);
    while (!revokes.empty()) {
      lock *l = revokes.front();
      lock_protocol::lockid_t lid = l->lid;
      revokes.pop();
      int xid = l->xid;
      pthread_mutex_unlock(&client_mutex);
      int r;
      cl->call(lock_protocol::release, lid, id, xid, r);
      pthread_mutex_lock(&client_mutex);
      l->state = NONE;
      l->xid++;
      // wake up thread who acquire the lock when it's releasing
      pthread_cond_signal(&l->cond);
    }
    pthread_mutex_unlock(&client_mutex);
  }
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  pthread_mutex_lock(&client_mutex);
  // if the lock is new
  lock *l = locks[lid];
  if (!l) {
    l = new lock(lid);
    locks[lid] = l;
  }

  while (FREE != l->state) {
    // the lock is never seen before, send acquire to server
    if (NONE == l->state) {
      int r;
      printf("  acquire on client %s: thread %lu acquiring lock %llu from server\n", id.c_str(), pthread_self(), lid);
      l->state = ACQUIRING;
      int xid = l->xid;
      pthread_mutex_unlock(&client_mutex);
      lock_protocol::status ret = cl->call(lock_protocol::acquire, lid, id, xid, r);
      pthread_mutex_lock(&client_mutex);
      l->xxid = l->xid + 1;
      if (lock_protocol::OK == ret) {
        printf("  acquire on client %s: thread %lu gets OK from server for lock %llu\n", id.c_str(), pthread_self(), lid);
      } else {  // server returns RETRY
        printf("  acquire on client %s: thread %lu gets RETRY from server for lock %llu\n", id.c_str(), pthread_self(), lid);
        // printf("  acquire: xxid %d\n", xxid);
        // wake up out of order retry
        pthread_cond_signal(&l->order_cond);
        // wait retry rpc from server
        pthread_cond_wait(&l->retry_cond, &client_mutex);
        l->xid++;
        xid = l->xid;
        // got retry from server, call acquire again, server guarantee it will get the lock
        pthread_mutex_unlock(&client_mutex);
        cl->call(lock_protocol::acquire, lid, id, xid, r);
        pthread_mutex_lock(&client_mutex);
        l->xxid = l->xid + 1;
        // wake up out of order revoke
        pthread_cond_signal(&l->order_cond);
      }
      break;
    } else { // the lock is LOCKED or ACQUIRING or RELEASING or REVOKED
      // the thread already hold the lock
      if (LOCKED == l->state && pthread_self() == l->th) {
        return lock_protocol::OK;
      }
      printf("  acquire on client %s: thread %lu waits, the lock %llu is not available\n", id.c_str(), pthread_self(), lid);
      pthread_cond_wait(&l->cond, &client_mutex);
    }
  }
  // the lock is free, assign the state and thread
  printf("  acquire on client %s: thread %lu gets the lock %llu\n", id.c_str(), pthread_self(), lid);
  l->state = LOCKED;
  l->th = pthread_self();
  pthread_mutex_unlock(&client_mutex);
  return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
  ScopedLock slock(&client_mutex);
  lock *l = locks[lid];
  // unkown lock
  if (!l) {
    printf("  release on client %s: cannot release unknown lock\n", id.c_str());
    return lock_protocol::NOENT;
  }

  // if the thread does not hold the lock
  if (pthread_self() != l->th) {
    printf("  release on client %s: thread %lu does not hold the lock, cannot release\n", id.c_str(), pthread_self());
    return lock_protocol::NOENT;
  }

  if (LOCKED == l->state) {
    printf("  release on client %s: thread %lu released lock %llu to another thread\n", id.c_str(), pthread_self(), lid);
    // signal other threads
    l->state = FREE;
    pthread_cond_signal(&l->cond);
  } else if (REVOKED == l->state) { // if the lock has been revoked
    printf("  release on client %s: thread %lu released lock %llu, the lock is revoked, wake up the releaser\n", id.c_str(), pthread_self(), lid);
    l->state = RELEASING;
    revokes.push(l);
    // a revoked lock is released, wake up the releaser
    pthread_cond_signal(&releaser_cond);
  } else {
    printf("  release on client %s: invalid lock state\n", id.c_str());
    return lock_protocol::NOENT;
  }
  return lock_protocol::OK;
}

rlock_protocol::status
lock_client_cache::revoke(lock_protocol::lockid_t lid, int xxid, int &r)
{
  ScopedLock slock(&client_mutex);
  lock *l = locks[lid];
  if (!l) {
    printf("  revoke on client %s: cannot revoke unknown lock\n", id.c_str());
    return rlock_protocol::RPCERR;
  }

  // if out of order
  if (xxid != l->xxid) {
    printf("  revoke on client %s: revoke before acquire\n", id.c_str());
    printf("  revoke on client %s: xxid %d\n", id.c_str(), xxid);
    printf("  revoke on client %s: l->xxid %d\n", id.c_str(), l->xxid);
    // wait until acquire finish
    pthread_cond_wait(&l->order_cond, &client_mutex);
  }

  printf("  revoke on client %s: the lock %llu is revoked by server\n", id.c_str(), lid);
  // if the revoked lock is free, wake up the releaser
  if (FREE == l->state) {
    printf("  revoke on client %s: the revoked lock %llu is FREE, wake up releaser\n", id.c_str(), lid);
    l->state = RELEASING;
    // add lock to revokes list
    revokes.push(l);
    // wake up releaser
    pthread_cond_signal(&releaser_cond);
  } else if (LOCKED == l->state) {
    printf("  revoke on client %s: the revoked lock %llu is LOCKED, set its state to REVOKED\n", id.c_str(), lid);
    l->state = REVOKED;
  } else {
    printf("  revoke on client %s: invalid lock state\n", id.c_str());
    return rlock_protocol::RPCERR;
  }
  return rlock_protocol::OK;
}

rlock_protocol::status
lock_client_cache::retry(lock_protocol::lockid_t lid, int xxid, int &r)
{
  ScopedLock slock(&client_mutex);
  lock *l = locks[lid];
  if (!l) {
    printf("  retry on client %s: cannot retry unknown lock\n", id.c_str());
    return rlock_protocol::RPCERR;
  }

  // if out of order
  if (xxid != l->xxid) {
    printf("  retry on client %s: retry before acquire\n", id.c_str());
    printf("  revoke on client %s: xxid %d\n", id.c_str(), xxid);
    printf("  revoke on client %s: l->xxid %d\n", id.c_str(), l->xxid);
    // wait until acquire finish
    pthread_cond_wait(&l->order_cond, &client_mutex);
  }

  printf("  retry on client %s: the lock %llu is retried by server\n", id.c_str(), lid);
  if (ACQUIRING == l->state) {
    printf("  retry on client %s: the lock %llu is ACQUIRING, wake up the thread to retry\n", id.c_str(), lid);
    pthread_cond_signal(&l->retry_cond);
    return rlock_protocol::OK;
  } else {
    printf("  retry on client %s: invalid lock state\n", id.c_str());
    return rlock_protocol::RPCERR;
  }
}

