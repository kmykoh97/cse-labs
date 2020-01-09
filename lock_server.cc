// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server():
  nacquire (0)
{
  pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&cond, NULL);
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
	// Your lab2 part2 code goes here
  r = lid;

  // mutex lock
  pthread_mutex_lock(&mutex);

  std::map<lock_protocol::lockid_t, int>::iterator it = lock_state_table.find(lid);
  if (it != lock_state_table.end()) { // lock is avalable from previous creation
    if (lock_state_table[lid] == F) {
      lock_state_table[lid] = L;
    } else {
      while (lock_state_table[lid] == L) {
        pthread_cond_wait(&cond, &mutex);
      }

      lock_state_table[lid] = L;
    }
  } else { // lock is never initiated. create new map
    lock_state_table[lid] = L;
  }

  // mutex unlock
  pthread_mutex_unlock(&mutex);
  return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
	// Your lab2 part2 code goes here
  r = lid;

  // mutex lock
  pthread_mutex_lock(&mutex);

  std::map<lock_protocol::lockid_t, int>::iterator it = lock_state_table.find(lid);
  if (it != lock_state_table.end()) {
    lock_state_table[lid] = F; // set free
  } else {
    ret = lock_protocol::NOENT;
  }

  // mutex unlock
  pthread_mutex_unlock(&mutex);
  pthread_cond_signal(&cond);
  return ret;
}
