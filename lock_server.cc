// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <slock.h>

// initialize the mutex and condition variable
lock_server::lock_server()
{
  assert(pthread_mutex_init(&lock_mutex, NULL) == 0);
  assert(pthread_cond_init(&lock_cv, NULL) == 0);
}

// destroy the mutext and condition variable
lock_server::~lock_server()
{
  std::map<lock_protocol::lockid_t, lock_info *>::iterator iter;

  assert(pthread_mutex_destroy(&lock_mutex) == 0);
  assert(pthread_cond_destroy(&lock_cv) == 0);

  // free the lock map
  for (iter = lock_map.begin(); iter != lock_map.end(); iter++)
    delete iter->second;
  lock_map.clear();
}

// initialize the lock info
lock_server::lock_info::lock_info(unsigned int clt, lock_state state)
: clt(clt), state(state) {}

lock_protocol::status
lock_server::stat(unsigned int clt, lock_protocol::lockid_t lid, int &r)
{
  ScopedLock lock(&lock_mutex);
  lock_protocol::status ret = lock_protocol::OK;

  printf("========================================================\n");
  printf("stat request from clt %u for lock %llu\n", clt, lid);

  if (nacquire_map.find(lid) == nacquire_map.end())
    r = 0;
  else
    r = nacquire_map[lid];
  return ret;
}

lock_protocol::status
lock_server::acquire(unsigned int clt, lock_protocol::lockid_t lid, int &r)
{
  ScopedLock lock(&lock_mutex);
  lock_protocol::status ret = lock_protocol::OK;

  printf("========================================================\n");
  printf("acquire request from clt %u for lock %llu\n", clt, lid);

  // if a lock doesn't exist before
  if (lock_map.find(lid) == lock_map.end()) {
    printf("lock %llu doesn't exist. Creating a new lock for clt %u\n", lid, clt);
    lock_map[lid] = new lock_info(clt, LOCKED);
    nacquire_map[lid] = 1;
    printf("Granted lock %llu to clt %u\n", lid, clt);

    return ret;
  }

  // if clt already owned the lock
  // if (lock_map[lid]->state == LOCKED && lock_map[lid]->clt == clt) {
  //   printf("clt %u already owned the lock %llu\n", clt, lid);
  //   return lock_protocol::NOENT;
  // }

  // if the the lock is locked, wait until it's free
  while (lock_map[lid]->state == LOCKED) {
    printf("lock %llu is owned by clt %u. Wait until the lock is free\n", lid, lock_map[lid]->clt);
    pthread_cond_wait(&lock_cv, &lock_mutex);
  }
  // grant the lock
  lock_map[lid]->clt = clt;
  lock_map[lid]->state = LOCKED;
  nacquire_map[lid]++;
  printf("Granted lock %llu to clt %u\n", lid, clt);

  return ret;
}

lock_protocol::status
lock_server::release(unsigned int clt, lock_protocol::lockid_t lid, int &r)
{
  ScopedLock lock(&lock_mutex);
  lock_protocol::status ret = lock_protocol::OK;

  printf("========================================================\n");
  printf("release request from clt %u for lock %llu\n", clt, lid);

  // if clt doesn't own the lock
  if (lock_map.find(lid) == lock_map.end() || lock_map[lid]->clt != clt) {
    printf("clt %u doesn't own the lock %llu. cannot release\n", clt, lid);
    return lock_protocol::NOENT;
  }

  // free the lock and signal other threads
  lock_map[lid]->state = FREE;
  pthread_cond_signal(&lock_cv);

  printf("clt %u released lock %llu successfully\n", clt, lid);

  return ret;
}