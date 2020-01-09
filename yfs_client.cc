// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  lock_release_user_derived *flusher = new lock_release_user_derived(ec);
  lc = new lock_client_cache(lock_dst, flusher);
//   if (ec->put(0, "") != extent_protocol::OK)
//       printf("error init root dir\n"); // XYB: init root dir
}


yfs_client::inum
yfs_client::n2i(std::string n)
{
    std::istringstream ist(n);
    unsigned long long finum;
    ist >> finum;
    return finum;
}

std::string
yfs_client::filename(inum inum)
{
    std::ostringstream ost;
    ost << inum;
    return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
    extent_protocol::attr a;
    // lc->acquire(inum);

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        // lc->release(inum);
        return false;
    }

    // lc->release(inum);

    if (a.type == extent_protocol::T_FILE) {
        printf("isfile: %lld is a file\n", inum);
        return true;
    } 
    printf("isfile: %lld is a dir\n", inum);
    return false;
}
/** Your code here for Lab...
 * You may need to add routines such as
 * readlink, issymlink here to implement symbolic link.
 * 
 * */

bool
yfs_client::isdir(inum inum)
{
    extent_protocol::attr a;
    // lc->acquire(inum);

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        // lc->release(inum);
        return false;
    }

    // lc->release(inum);

    if (a.type == extent_protocol::T_DIR) {
        printf("isdir: %lld is a dir\n", inum);
        return true;
    } 
    printf("isdir: %lld is a file\n", inum);
    return false;
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
    int r = OK;

    printf("getfile %016llx\n", inum);
    extent_protocol::attr a;
    lc->acquire(inum);
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    fin.atime = a.atime;
    fin.mtime = a.mtime;
    fin.ctime = a.ctime;
    fin.size = a.size;
    printf("getfile %016llx -> sz %llu\n", inum, fin.size);

release:
    lc->release(inum);
    return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
    int r = OK;

    printf("getdir %016llx\n", inum);
    extent_protocol::attr a;
    lc->acquire(inum);
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;

release:
    lc->release(inum);
    return r;
}


#define EXT_RPC(xx) do { \
    if ((xx) != extent_protocol::OK) { \
        printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
        r = IOERR; \
        goto release; \
    } \
} while (0)

// Only support set size of attr
int
yfs_client::setattr(inum ino, size_t size)
{
    int r = OK;

    /*
     * your code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */
    std::string buffer;
    lc->acquire(ino);
    r = ec->get(ino, buffer);
    if (r != OK) { // fail get
        lc->release(ino);
        return r;
    }
    
    if (size == buffer.size()) {
        lc->release(ino);
        return r;
    }
    
    buffer.resize(size, '\0');
    r = ec->put(ino, buffer);

    lc->release(ino);
    return r;
}

int
yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup is what you need to check if file exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */
    lc->acquire(parent);
    if (!isdir(parent)) {
        lc->release(parent);
        return IOERR;
    }

    // checks duplicate
    bool flag;
    inum temp = 0;
    lookup(parent, name, flag, temp);

    if (flag) {
        lc->release(parent);
        return EXIST;
    }

    // no exception, proceed with creating file
    if (ec->create(extent_protocol::T_FILE, ino_out, parent) != OK) { // warning
        lc->release(parent);
        return IOERR;
    }

    // get parent information
    std::string stringBuf;
    if (ec->get(parent, stringBuf) != OK) {
        lc->release(parent);
        return IOERR;
    }
    stringBuf.append(name);
    stringBuf.append("/");
    stringBuf.append(filename(ino_out));
    stringBuf.append("/");

    if (ec->put(parent, stringBuf) != OK) {
        lc->release(parent);
        return IOERR;
    }

    lc->release(parent);
    return r;
}

int
yfs_client::mkdir(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;
    /*
     * your code goes here.
     * note: lookup is what you need to check if directory exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */
    lc->acquire(parent);
    if (!isdir(parent)) {
        lc->release(parent);
        return IOERR;
    }

    // checks duplicate
    bool flag;
    inum temp = 0;
    lookup(parent, name, flag, temp);

    if (flag) {
        lc->release(parent);
        return EXIST;
    }

    // no exception, proceed with creating directory
    if (ec->create(extent_protocol::T_DIR, ino_out, parent) != OK) {
        lc->release(parent);
        return IOERR;
    }

    // get parent information
    std::string stringBuf;
    if (ec->get(parent, stringBuf) != OK) {
        lc->release(parent);
        return IOERR;
    }
    stringBuf.append(name);
    stringBuf.append("/");
    stringBuf.append(filename(ino_out));
    stringBuf.append("/");

    if (ec->put(parent, stringBuf) != OK) {
        lc->release(parent);
        return IOERR;
    }

    lc->release(parent);
    return r;
}

int
yfs_client::lookup(inum parent, const char *name, bool &found, inum &inode)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup file from parent dir according to name;
     * you should design the format of directory content.
     */
    // if it is a directory, return error
    // lc->acquire(parent);
    found = false;

    if (!isdir(parent)) {
        // lc->release(parent);
        return IOERR;
    }

    // list out directories
    std::list<dirent> directoryList;
    std::list<dirent>::iterator it;
    r = readdir(parent, directoryList);
    if (r != OK) {
        // lc->release(parent);
        return r;
    }

    // loop through and find similar filename
    for (it = directoryList.begin(); it != directoryList.end(); it++) {
        dirent d = *it;
        
        // checks similarity
        if (name == d.name) {
            inode = d.inum;
            found = true;

            // lc->release(parent);
            return OK;
        }
    }

    // lc->release(parent);
    return r;
}

int
yfs_client::readdir(inum dir, std::list<dirent> &list)
{
    int r = OK;

    /*
     * your code goes here.
     * note: you should parse the dirctory content using your defined format,
     * and push the dirents to the list.
     */
    // check directory type
    // lc->acquire(dir);
    if (!isdir(dir)) {
        // lc->release(dir);
        return IOERR;
    }

    // read directory into buffer and check empty string
    std::string buffer;
    if (ec->getHelper(dir, buffer) != OK) {
        // lc->release(dir);
        return IOERR;
    }
    if (buffer == "") {
        // lc->release(dir);
        return OK;
    }

    // complete the list based on buffer
    std::string delimiter = "/";
    size_t pos = 0;
    std::string token;
    // start to process buffer in this stage
    std::vector<std::string> v;
    while ((pos = buffer.find(delimiter)) != std::string::npos) {
        token = buffer.substr(0, pos);
        v.push_back(token);
        buffer.erase(0, pos + delimiter.length());
    }
    // start to push to list in this stage
    int s = v.size();
    for (int i = 0; i < s; i += 2) { // need to skip inum
        dirent temp;
        temp.inum = n2i(v[i+1]);
        temp.name = v[i];
        list.push_back(temp);
    }

    // lc->release(dir);
    return r;
}

int
yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
    int r = OK;

    /*
     * your code goes here.
     * note: read using ec->get().
     */
    lc->acquire(ino);
    r = ec->get(ino, data);

    if (r != OK) {
        lc->release(ino);
        return r;
    }
    data.erase(0, off); // because want size starting from offset, we will delete the initial offset bytes

    // when less than size bytes are available, you should return to fuse only the available number of bytes
    if (size < data.size()) {
        data.resize(size);
    }

    lc->release(ino);
    return r;
}

int
yfs_client::write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
    int r = OK;

    /*
     * your code goes here.
     * note: write using ec->put().
     * when off > length of original file, fill the holes with '\0'.
     */
    lc->acquire(ino);
    std::string buffer;
    r = ec->get(ino, buffer);

    if (r != OK) {
        lc->release(ino);
        return r;
    }

    if (int(off + size) > buffer.size()) {
        buffer.resize(off + size, '\0');
    }

    buffer.replace(off, size, data, size);
    r = ec->put(ino, buffer);

    if (r != extent_protocol::OK) {
        lc->release(ino);
        return r;
    }

    bytes_written = buffer.size() < off ? (size + off - buffer.size()) : size;

    lc->release(ino);
    return r;
}

int yfs_client::unlink(inum parent,const char *name)
{
    int r = OK;

    /*
     * your code goes here.
     * note: you should remove the file using ec->remove,
     * and update the parent directory content.
     */
    lc->acquire(parent);
    // check if parent is directory
    if (!isdir(parent)) {
        lc->release(parent);
        return IOERR;
    }

    // check file existence
    bool flag = false;
    yfs_client::inum inodeNo = 0;
    lookup(parent, name, flag, inodeNo);
    if (!flag) {
        lc->release(parent);
        return NOENT;
    }

    // list directories, find file, unlink, set inode to the new directories
    std::list<dirent> directoryList;
    std::list<dirent>::iterator it;
    r = readdir(parent, directoryList);
    if(r != OK){
        lc->release(parent);
        return r;
    }

    flag = false;
    std::string newDirectory = "";
    std::string fileName = std::string(name);
    for(it = directoryList.begin(); it != directoryList.end(); it++){
        if (it->name == fileName) {
            flag = true;
            r = ec->remove(it->inum);
            if (r != OK) {
                lc->release(parent);
                return r;
            }
        } else {
            newDirectory.append(it->name);
            newDirectory.append("/");
            newDirectory.append(filename(it->inum));
            newDirectory.append("/");
        }
    }
    
    if (flag) {
        r = ec->put(parent, newDirectory);

        if (r != OK) {
            lc->release(parent);
            return r;
        }
    } else {
        r = NOENT;
    }

    lc->release(parent);
    return r;
}

bool yfs_client::issymlink(inum ino) {
    // get attribute from ino
    // lc->acquire(ino);
    extent_protocol::attr a;
    if (ec->getattr(ino, a) != extent_protocol::OK) {
        // lc->release(ino);
        return false;
    }

    // check attribute if it is of type symbolic link
    if (a.type == extent_protocol::T_SYMLINK) {
        // lc->release(ino);
        return true;
    }

    // lc->release(ino);
    return false;
}

int yfs_client::readlink(inum ino, std::string& link) {
    lc->acquire(ino);
    // check if is symbolic link
    if (!issymlink(ino)) {
        lc->release(ino);
        return IOERR;
    }

    // get symbolic link to link from ino
    if (ec->get(ino, link) != OK) {
        lc->release(ino);
        return IOERR;
    }

    lc->release(ino);
    return OK;
}

int yfs_client::symlink(inum parent, const char* name, const char* link, inum &tempInum) {
    lc->acquire(parent);
    // check if parent is directory
    if (!isdir(parent)) {
        lc->release(parent);
        return IOERR;
    }

    // check file existence
    bool flag = false;
    inum inodeNo = 0;
    lookup(parent, name, flag, inodeNo);
    if (flag) {
        lc->release(parent);
        return EXIST;
    }

    // create symbolic link
    if (ec->create(extent_protocol::T_SYMLINK, tempInum, parent) != extent_protocol::OK) {
        lc->release(parent);
        return IOERR;
    }

    // write symbolic link
    if (ec->put(tempInum, std::string(link)) != extent_protocol::OK) {
        lc->release(parent);
        return IOERR;
    }

    // ammend parent information
    std::string buffer;
    if (ec->get(parent, buffer) != extent_protocol::OK) {
        lc->release(parent);
        return IOERR;
    } 

    buffer.append(name);
    buffer.append("/");
    buffer.append(filename(tempInum));
    buffer.append("/");

    if (ec->put(parent, buffer) != extent_protocol::OK) {
        lc->release(parent);
        return IOERR;
    }

    lc->release(parent);
    return OK;
}