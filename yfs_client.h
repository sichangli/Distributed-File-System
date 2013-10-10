#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>

#include "lock_protocol.h"
#include "lock_client.h"

class yfs_client {
  extent_client *ec;
  lock_client *lc;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, FBIG, EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    unsigned long long inum;
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);
  int readDir(inum, std::map<std::string, inum> &);
  int writeDir(inum, std::map<std::string, inum> &);
  // generate unique inum
  void generateInum(inum &);
 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);
  // create file
  int create(inum, const char *, inum &);
  // lookup by name
  int lookup(inum, const char *, inum &, bool &);
  // read content of dir
  int readdir(inum, std::map<std::string, inum> &);
  // set size of a file
  int setFileSize(inum, unsigned long long);
  // read file content
  int read(inum, size_t, size_t, std::string &);
  // write file content
  int write(inum, const std::string &, size_t, size_t);
};

#endif 
