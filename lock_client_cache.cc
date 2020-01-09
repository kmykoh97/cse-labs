// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"
#include <unistd.h>


int lock_client_cache::last_port = 0;

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // VERIFY(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

  pthread_mutex_init(&mutex, NULL);
}

lock_client_cache::~lock_client_cache()
{

  pthread_mutex_destroy(&mutex);
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;
  bool toProcess = false;
  pthread_mutex_lock(&mutex);

  if (lock_status_table_.find(lid) == lock_status_table_.end()) {
    tprintf("release %llu: no entry!\n", lid);
    pthread_mutex_unlock(&mutex);

    return lock_protocol::NOENT;
  }

  lock_status * ls = lock_status_table_[lid];
  if (ls->status != LOCKED) {
    ret = lock_protocol::NOENT;
  } else if (ls->revoke_status == false) {
    lu->dorelease(lid);
    usleep(100000);
    ls->status = FREE;
    pthread_cond_signal(&ls->condi);
  } else {
    lu->dorelease(lid);
    usleep(100000);
    toProcess = true;
    ls->status = RELEASING;
    ls->revoke_status = false;
  }

  pthread_mutex_unlock(&mutex);

  if (toProcess) {
   
    int r;
    do {
      ret = cl->call(lock_protocol::release, lid, id, r);
    } while (ret != lock_protocol::OK);

    set_status_mutex(ls, NONE);
    pthread_cond_signal(&ls->condi);
  }

  return ret;
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
  int ret = rlock_protocol::OK;
  bool toProcess = false;
  pthread_mutex_lock(&mutex);
  
  if (lock_status_table_.find(lid) == lock_status_table_.end()) {
    tprintf("revoke %llu: no entry!\n", lid);
    pthread_mutex_unlock(&mutex);

    return ret;
  }

  lock_status * ls = lock_status_table_[lid];
  switch (ls->status) {
    case NONE:
      break;
    case FREE:
      toProcess = true;
      ls->status = RELEASING;
      ls->revoke_status = false;
      break;
    case LOCKED: case ACQUIRING: case RELEASING:
      ls->revoke_status = true;
      break;
    default:
      break;
  }

  pthread_mutex_unlock(&mutex);

  if (toProcess) {
    lu->dorelease(lid);
    usleep(100000);
    int r;
    do {
      ret = cl->call(lock_protocol::release, lid, id, r);
    } while (ret != lock_protocol::OK);

    set_status_mutex(ls, NONE);
    pthread_cond_signal(&ls->condi);
  }

  return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
  int ret = rlock_protocol::OK;
  pthread_mutex_lock(&mutex);

  if (lock_status_table_.find(lid) == lock_status_table_.end()) {
    tprintf("retry %llu: no entry!\n", lid);
  } else {
    lock_status* ls = lock_status_table_[lid];

    if (ls->status == ACQUIRING) {
      ls->status = LOCKED;
    }
  }

  pthread_mutex_unlock(&mutex);
  return ret;
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;
  bool toProcess = false;
    
  pthread_mutex_lock(&mutex);
  if (lock_status_table_.find(lid) == lock_status_table_.end()) {
    lock_status * ls = new lock_status();
    lock_status_table_[lid] = ls;
  }

  lock_status* ls = lock_status_table_[lid];
  if (ls->status == NONE) {
    ls->status = ACQUIRING;
    toProcess = true;
  } else if (ls->status == FREE) {
    ls->status = LOCKED;
  } else {
    do {
      pthread_cond_wait(&ls->condi, &mutex);
    } while ((ls->status != NONE) && (ls->status != FREE));

    if (ls->status == NONE) {
      ls->status = ACQUIRING;
      toProcess = true;
    } else {
      ls->status = LOCKED;
    }
  }

  pthread_mutex_unlock(&mutex);
    
  if (toProcess) {
    int r;
    ret = cl->call(lock_protocol::acquire, lid, id, r);

    while (ret != lock_protocol::OK) {
      if (is_status_mutex(ls, LOCKED)) {
        break;
      }

      if (ret != lock_protocol::RETRY) {
        ret = cl->call(lock_protocol::acquire, lid, id, r);
      }
    }

    set_status_mutex(ls, LOCKED);
  }

  return ret;
}

void
lock_client_cache::set_status_mutex(lock_status* ls, int st)
{
  pthread_mutex_lock(&mutex);
  ls->status = st;
  pthread_mutex_unlock(&mutex);
}

bool
lock_client_cache::is_status_mutex(lock_status* ls, int st)
{
  pthread_mutex_lock(&mutex);
  bool ret = (ls->status == st);
  pthread_mutex_unlock(&mutex);
  return ret;
}
