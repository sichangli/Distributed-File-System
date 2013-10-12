// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include "lock_client.h"
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
  lc = new lock_client(lock_dst);
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
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;

  // acquire the lock from lock server
  lc->acquire(inum);

  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
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
  // release the lock from lock server
  lc->release(inum);
  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;

  // acquire the lock from lock server
  lc->acquire(inum);

  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  // release the lock from lock server
  lc->release(inum);
  return r;
}

// read content of dir into a map
int
yfs_client::readDir(inum pinum, std::map<std::string, inum> &dir) {
  std::string buf;
  std::string line;
  if (ec->get(pinum, buf) != extent_protocol::OK)
    return IOERR;
  std::istringstream iss(buf);
  while (std::getline(iss, line)) {
    std::istringstream iss2(line);
    std::string name;
    inum inum;
    iss2 >> name >> inum;
    dir[name] = inum;
  }
  return OK;
}

// write a map back to dir file
int
yfs_client::writeDir(inum pinum, std::map<std::string, inum> &dir) {
  std::map<std::string, inum>::iterator iter;
  std::ostringstream oss;
  for (iter = dir.begin(); iter != dir.end(); iter++) 
    oss << iter->first << ' ' << iter->second << std::endl;
  if (ec->put(pinum, oss.str()) != extent_protocol::OK)
    return IOERR;
  return OK;
}

void
yfs_client::generateInum(inum &inum)
{
  int r;
  srand(time(NULL) + getpid());
  inum = rand();
  inum = inum | 0x80000000;

  // check whether the inum already exists, if yes, keep generating
  // until the server reports no
  ec->check(inum, r);
  while (r) {
    inum = rand();
    inum = inum | 0x80000000;
    ec->check(inum, r);
  }
}

int
yfs_client::create(inum pinum, const char *name, inum &ino)
{
  int r = OK;
  std::map<std::string, inum> dir;
  std::string name_str(name);

  // acquire the lock from lock server
  lc->acquire(pinum);

  printf("create in parent %016llx\n", pinum);

  r = readDir(pinum, dir);
  if (r != OK)
    goto release;
  // if file name already exists
  if (dir.find(name_str) != dir.end()) {
    r = EXIST;
    goto release;
  }

  // generate the ino and update the parent dir
  generateInum(ino);
  dir[name_str] = ino;
  printf("%s -> %016llx\n", name_str.c_str(), dir[name_str]);
  r = writeDir(pinum, dir);
  if (r != OK)
    goto release;

  // put a empty file
  if (ec->put(ino, std::string()) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  printf("created new inum %016llx\n", ino);

 release:
  // release the lock from lock server
  lc->release(pinum);
  return r;
}

int
yfs_client::lookup(inum pinum, const char *name, inum &ino, bool &found)
{
  int r = OK;
  std::map<std::string, inum> dir;
  std::string name_str(name);

  // acquire the lock from lock server
  lc->acquire(pinum);

  printf("lookup in parent %016llx for file name %s\n", pinum, name);

  r = readDir(pinum, dir);
  if (r != OK)
    goto release;

  // set found to true if file name exists
  if (dir.find(name_str) != dir.end()) {
    found = true;
    ino = dir[name_str];
  }

 release:
  // release the lock from lock server
  lc->release(pinum);
  return r;
}

int
yfs_client::readdir(inum ino, std::map<std::string, inum> &dir)
{
  int r = OK;

  // acquire the lock from lock server
  lc->acquire(ino);

  printf("readdir %016llx\n", ino);

  r = readDir(ino, dir);

  // release the lock from lock server
  lc->release(ino);
  return r;
}

int
yfs_client::setFileSize(inum inum, unsigned long long size)
{
  int r = OK;
  std::string buf;

  // acquire the lock from lock server
  lc->acquire(inum);

  printf("setFileSize %016llx\n", inum);
  if (ec->get(inum, buf) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  // if set to a larger size, need to pad 0
  if (buf.size() < size)
    buf.append(size - buf.size(), '\0');
  // if set to smaller size, truncate
  else
    buf = buf.substr(0, size);

  // write back
  if (ec->put(inum, buf) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  printf("setFileSize %016llx -> sz %llu\n", inum, size);

 release:
  // release the lock from lock server
  lc->release(inum);
  return r;
}

int
yfs_client::read(inum inum, size_t size, size_t off, std::string &buf)
{
  int r = OK;
  std::string data;

  // acquire the lock from lock server
  lc->acquire(inum);

  if (ec->get(inum, data) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  // invalid offset
  if (off >= data.size()) {
    r = IOERR;
    goto release;
  }

  buf = data.substr(off, size);

 release:
  // release the lock from lock server
  lc->release(inum);
  return r;
}

int
yfs_client::write(inum inum, const std::string &buf, size_t size, size_t off)
{
  int r = OK;
  std::string data;

  // acquire the lock from lock server
  lc->acquire(inum);

  if (ec->get(inum, data) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  // offset past the end of file
  if (off >= data.size()) {
    // append '\0' for the hole
    data.append(off - data.size(), '\0');
    // append the real data
    data.append(buf);
  } else if (off + size <= data.size()) {
    data.replace(off, size, buf);
  } else {
    data = data.substr(0, off) + buf;
  }

  // write new data to file
  if (ec->put(inum, data) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

 release:
  // release the lock from lock server
  lc->release(inum);
  return r;
}

int
yfs_client::remove(inum pinum, const std::string &name)
{
  int r = OK;
  std::map<std::string, inum> dir;

  // acquire the lock from lock server
  lc->acquire(pinum);

  printf("remove in parent %016llx for file name %s\n", pinum, name.c_str());

  r = readDir(pinum, dir);
  if (r != OK)
    goto release;

  // if cannot find in parent dir
  if (dir.find(name) == dir.end()) {
    r = IOERR;
    goto release;
  }

  // cannot remove dir
  if (isdir(dir[name])) {
    r = IOERR;
    goto release;
  }

  // if found, remove from server
  if (ec->remove(dir[name]) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  // update parent dir and write back
  dir.erase(name);
  r = writeDir(pinum, dir);
  if (r != OK)
    goto release;

  printf("removed in parent %016llx for file name %s\n", pinum, name.c_str());

 release:
  // release the lock from lock server
  lc->release(pinum);
  return r;
}

void
yfs_client::generateDirInum(inum &inum)
{
  int r;
  srand(time(NULL) + getpid());
  inum = rand();
  inum &= 0x7FFFFFFF;

  // check whether the inum already exists, if yes, keep generating
  // until the server reports no
  ec->check(inum, r);
  while (r) {
    inum = rand();
    inum &= 0x7FFFFFFF;
    ec->check(inum, r);
  }
}

int
yfs_client::mkdir(inum pinum, const std::string &name, inum &ino)
{
  int r = OK;
  std::map<std::string, inum> dir;

  // acquire the lock from lock server
  lc->acquire(pinum);

  printf("mkdir in parent %016llx for dir name %s\n", pinum, name.c_str());

  r = readDir(pinum, dir);
  if (r != OK)
    goto release;

  // if dir name already exists
  if (dir.find(name) != dir.end()) {
    r = EXIST;
    goto release;
  }

  // genearate the ino and update parent dir file
  generateDirInum(ino);
  dir[name] = ino;

  r = writeDir(pinum, dir);
  if (r != OK)
    goto release;

  // put a empty dir
  if (ec->put(ino, std::string()) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  printf("mkdir new inum %016llx\n", ino);

 release:
  // release the lock from lock server
  lc->release(pinum);
  return r;
}