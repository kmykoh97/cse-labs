// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_client.h"
#include "lang/verify.h"
#include <pthread.h>
#include <map>
#include "extent_client.h"


// Classes that inherit lock_release_user can override dorelease so that 
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 6.
class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};

class lock_release_user_derived : public lock_release_user {
 public:
  extent_client *ec;
  lock_release_user_derived(extent_client *ec_) {
      ec = ec_;
  }
  virtual void dorelease(lock_protocol::lockid_t lockid) {
      ec->flush(lockid);
  }
  virtual ~lock_release_user_derived() {};
};

class lock_client_cache : public lock_client {
 private:
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;
  
 public:
  static int last_port;
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  ~lock_client_cache();
  lock_protocol::status acquire(lock_protocol::lockid_t);
  lock_protocol::status release(lock_protocol::lockid_t);
  rlock_protocol::status revoke_handler(lock_protocol::lockid_t, 
                                        int &);
  rlock_protocol::status retry_handler(lock_protocol::lockid_t, 
                                       int &);

 private:
  /* set of states
  * none: client knows nothing about this lock
  * free: client owns the lock and no thread has it
  * locked: client owns the lock and a thread has it
  * acquiring: the client is acquiring ownership
  * releasing: the client is releasing ownership
  */
  enum states { NONE, FREE, LOCKED, ACQUIRING, RELEASING };
  struct lock_status
  {
    int status;
    bool revoke_status;
    pthread_cond_t condi;

    lock_status() {
      status = NONE;
      revoke_status = false; // default to not revoked!
      pthread_cond_init(&condi, NULL);
    }

    ~lock_status() {
      pthread_cond_destroy(&condi);
    }
  };
  pthread_mutex_t mutex;
  std::map<lock_protocol::lockid_t, lock_status *> lock_status_table_;
  void set_status_mutex(lock_status*, int);
  bool is_status_mutex(lock_status*, int);
};


#endif