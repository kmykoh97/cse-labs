// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }

  pthread_mutex_init(&mutex, NULL);
}

// a demo to show how to use RPC
extent_protocol::status
extent_client::create(uint32_t type, extent_protocol::extentid_t &id, unsigned long long parent)
{
  // extent_protocol::status ret = extent_protocol::OK;
  // // Your lab2 part1 code goes here
  // ret = cl->call(extent_protocol::create, type, id);

  // return ret;

  extent_protocol::status ret = extent_protocol::OK;

  pthread_mutex_lock(&mutex);
  extent_cache * ec = new extent_cache();
  ret = cl->call(extent_protocol::create, type, id);
  ret = cl->call(extent_protocol::get, id, ec->buffer);
  ret = cl->call(extent_protocol::getattr, id, ec->attr);
  ec->buffer_status = true;
  ec->attr_status = true;
  extent_cache_table[id] = ec;
  pthread_mutex_unlock(&mutex);

  printf("create %lld\n", id);

  return ret;
}

// extra client to process readdir
// this is needed because when create a new dir, the client do not flush the original parent inode
// if we solve the problem using flush during create, will clikely cause concurrency issue when another process take the extent_cache of the parent and later it is freed when flushed
// due to my implementation of cache lock policy, we will likely need to hardcode using getHelper during readdir
extent_protocol::status 
extent_client::getHelper(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  ret = cl->call(extent_protocol::get, eid, buf);

  return ret;
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  // extent_protocol::status ret = extent_protocol::OK;
  // // Your lab2 part1 code goes here
  // ret = cl->call(extent_protocol::get, eid, buf);

  // return ret;

  printf("get %lld\n", eid);
  extent_protocol::status ret = extent_protocol::OK;

  pthread_mutex_lock(&mutex);
  if (extent_cache_table.find(eid) == extent_cache_table.end()) {
    extent_cache * ec = new extent_cache();
    ret = cl->call(extent_protocol::get, eid, ec->buffer);
    ret = cl->call(extent_protocol::getattr, eid, ec->attr);
    ec->buffer_status = true;
    ec->attr_status = true;
    extent_cache_table[eid] = ec;
  }

  extent_cache *temp = extent_cache_table[eid];

  if (temp->remove_status) {
    pthread_mutex_unlock(&mutex);
    return extent_protocol::NOENT;
  }

  if (temp->buffer_status) {
    buf = temp->buffer;
  } else {
    ret = cl->call(extent_protocol::get, eid, temp->buffer);
    temp->buffer_status = true;
    buf = temp->buffer;
  }

  pthread_mutex_unlock(&mutex);

  return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
  // extent_protocol::status ret = extent_protocol::OK;
  // ret = cl->call(extent_protocol::getattr, eid, attr);

  // return ret;
  printf("getattr %lld\n", eid);
  extent_protocol::status ret = extent_protocol::OK;
  
  pthread_mutex_lock(&mutex);
  if (extent_cache_table.find(eid) == extent_cache_table.end()) {
    extent_cache * ec = new extent_cache();
    extent_cache_table[eid] = ec;
  }

  extent_cache *temp = extent_cache_table[eid];

  if (temp->remove_status) {
    pthread_mutex_unlock(&mutex);
    return extent_protocol::NOENT;
  }

  if (temp->attr_status) {
    attr = temp->attr;
  } else {
    ret = cl->call(extent_protocol::getattr, eid, temp->attr);
    temp->attr_status = true;
    attr = temp->attr;
  }
  pthread_mutex_unlock(&mutex);

  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  // extent_protocol::status ret = extent_protocol::OK;
  // // Your lab2 part1 code goes here
  // ret = cl->call(extent_protocol::put, eid, buf, ret);

  // return ret;

  printf("put %lld\n", eid);
  extent_protocol::status ret = extent_protocol::OK;
  
  pthread_mutex_lock(&mutex);
  if (extent_cache_table.find(eid) == extent_cache_table.end()) {
    extent_cache * ec = new extent_cache();
    extent_cache_table[eid] = ec;
  }

  extent_cache *temp = extent_cache_table[eid];

  if (!temp->attr_status) {
    ret = cl->call(extent_protocol::getattr, eid, temp->attr);
    temp->attr_status = true;
  }

  temp->buffer = buf;
  temp->attr.atime = temp->attr.ctime = temp->attr.mtime = time(NULL);
  temp->attr.size = buf.size();
  temp->buffer_status = true;
  temp->dirty_status = true;
  temp->remove_status = false;
  pthread_mutex_unlock(&mutex);

  return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  // extent_protocol::status ret = extent_protocol::OK;
  // // Your lab2 part1 code goes here
  // ret = cl->call(extent_protocol::remove, eid, ret);

  // return ret;

  printf("remove %lld\n", eid);
  extent_protocol::status ret = extent_protocol::OK;
  
  pthread_mutex_lock(&mutex);
  if (extent_cache_table.find(eid) == extent_cache_table.end()) {
    extent_cache * ec = new extent_cache();
    ret = cl->call(extent_protocol::getattr, eid, ec->attr);
    ec->attr_status = true;
    extent_cache_table[eid] = ec;
  }

  extent_cache *temp = extent_cache_table[eid];
  temp->remove_status = true; // indicate removal
  pthread_mutex_unlock(&mutex);

  return ret;
}

extent_protocol::status
extent_client::flush(extent_protocol::extentid_t eid)
{
  extent_protocol::status ret = extent_protocol::OK;
  // return ret;
  
  printf("flush %lld\n", eid);
  pthread_mutex_lock(&mutex);

  if (extent_cache_table.find(eid) == extent_cache_table.end()) {
    pthread_mutex_unlock(&mutex);
    return ret; // no cache. Data not modified!
  }

  extent_cache* ec = extent_cache_table[eid];
  if (ec->remove_status) {
    ret = cl->call(extent_protocol::remove, eid, ret);
  } else if (ec->dirty_status) {
    ret = cl->call(extent_protocol::put, eid, ec->buffer, ret);
  }
  
  extent_cache_table.erase(eid);
  pthread_mutex_unlock(&mutex);

  return ret;
}