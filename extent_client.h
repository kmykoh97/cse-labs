// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "extent_server.h"
#include <map>

class extent_client {
 private:
  rpcc *cl;

  struct extent_cache
  {
    std::string buffer; // data cache
    extent_protocol::attr attr; // attribute cache
    bool dirty_status;
    bool remove_status;
    bool attr_status; // indicate whether attributes has been fetched from server
    bool buffer_status; // indicate whether data has been fetched from server

    extent_cache() {
      buffer = "";
      dirty_status = false;
      remove_status = false;
      attr_status = false;
      buffer_status = false;
    }
  };

  pthread_mutex_t mutex;
  std::map<extent_protocol::extentid_t, extent_cache *>extent_cache_table;

 public:
  extent_client(std::string dst);

  extent_protocol::status create(uint32_t type, extent_protocol::extentid_t &eid, unsigned long long parent);
  extent_protocol::status get(extent_protocol::extentid_t eid, std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
  extent_protocol::status flush(extent_protocol::extentid_t eid); // newly added to flush all dirty data into disk
  extent_protocol::status getHelper(extent_protocol::extentid_t eid, std::string &buf); // specially created method for readdir get without buffer
};

#endif 

