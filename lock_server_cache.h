#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>


#include <map>
#include <vector>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"

class lock_server_cache {
 private:
  int nacquire;
 public:
  lock_server_cache();
  ~lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);

 private:
 struct lock_status
  {
    bool lock; // lock status
    std::string owner;
    std::vector<std::string> id; // array of waiting clients

    lock_status() {
      lock = false; // initialise lock as free
      owner = "";
      id.clear();
    }
  };
  std::map<lock_protocol::lockid_t, lock_status *> lock_status_table_;
  pthread_mutex_t mutex;
  rlock_protocol::status rcall(std::string, int, lock_protocol::lockid_t);
};

#endif