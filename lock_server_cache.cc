// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"

lock_server_cache::lock_server_cache()
{
  pthread_mutex_init(&mutex, NULL);
}

lock_server_cache::~lock_server_cache()
{
    pthread_mutex_destroy(&mutex);
}

int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
                               int &)
{
  lock_protocol::status ret = lock_protocol::OK;

  // initialise lock
  pthread_mutex_lock(&mutex);

  std::map<lock_protocol::lockid_t, lock_status *>::iterator it = lock_status_table_.find(lid);

  // if no lid in map, generate new item for the map
  if (it == lock_status_table_.end()) {
    lock_status * ls = new lock_status();
    ls->owner = id;
    lock_status_table_[lid] = ls;
  }

  lock_status *ls = lock_status_table_[lid];
  if (ls->lock) {
    ls->id.push_back(id);
    ret = lock_protocol::RETRY;
    pthread_mutex_unlock(&mutex);

    while (rcall(ls->owner, rlock_protocol::revoke, lid) != rlock_protocol::OK) {
      tprintf("send revoke %llu to %s fail.\n", lid, ls->owner.c_str());        
    }
  } else {
    ls->lock = true;
    ls->owner = id;
    pthread_mutex_unlock(&mutex);
  }

  return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  bool toProcess = false;

  // mutex lock
  pthread_mutex_lock(&mutex);

  std::map<lock_protocol::lockid_t, lock_status *>::iterator it = lock_status_table_.find(lid);
  if (it == lock_status_table_.end()) {
    tprintf("release %llu from %s: no entry\n", lid, id.c_str());
    pthread_mutex_unlock(&mutex);

    return lock_protocol::NOENT;
  }

  lock_status * ls = lock_status_table_[lid];
  if (ls->lock) {
    if (ls->owner == id) {
      ls->lock = false;
      ls->owner = ""; // remove owner

      if (!ls->id.empty()) {
        ls->lock = true;
        ls->owner = ls->id[0];
        ls->id.erase(ls->id.begin()); // remove first element from vector
        toProcess = true;
      }
    } else {
        tprintf("release %llu from %s: do not own this lock!\n", lid, id.c_str());
        ret = lock_protocol::NOENT;
    }
  } else {
    tprintf("release %llu from %s: not locked!\n", lid, id.c_str());
    ret = lock_protocol::NOENT;
  }

  // release lock
  pthread_mutex_unlock(&mutex);

  if (toProcess) {
    while (rcall(ls->owner, rlock_protocol::retry, lid) != rlock_protocol::OK) {
      tprintf("send retry %llu to %s fail.\n", lid, ls->owner.c_str());
    }

    if (!ls->id.empty()) {
      while (rcall(ls->owner, rlock_protocol::revoke, lid) != rlock_protocol::OK) {
        tprintf("send revoke %llu to %s fail.\n", lid, ls->owner.c_str());
      }
    }
  }

  return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

rlock_protocol::status
lock_server_cache::rcall(std::string id, int protocol, lock_protocol::lockid_t lid)
{
    int r;
    lock_protocol::status ret = lock_protocol::OK;

    handle h(id);
    rpcc* cl = h.safebind();
    if (cl) {
        ret = cl->call(protocol, lid, r);
    } else {
        ret = lock_protocol::RPCERR;
    }
    
    return ret;
}
