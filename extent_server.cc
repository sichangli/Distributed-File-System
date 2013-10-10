// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fstream>

extent_server::extent_server() {}

// return file path representing the id
std::string getIdStr(extent_protocol::extentid_t id) {
  char id_buf[17];
  sprintf(id_buf, "%016llx", id);
  return std::string(id_buf);
}

// get the path of the file
std::string getFilePath(const std::string &id_str) {
  return "ID/" + id_str;
}

// get the path of attr file
std::string getAttrPath(const std::string &id_str) {
  return "ID/" + id_str + "_attr";
}

// check whether file already exists
bool fileExists(const std::string &path) {
  struct stat sb;
  return stat(path.c_str(), &sb) == 0 && S_ISREG(sb.st_mode);
}

// read attr from attr file
int readAttrFile(const std::string& attr_path, extent_protocol::attr &a) {
  std::ifstream ifs(attr_path.c_str());
  if (!ifs) {
    std::cerr << "Cannot read attr file " << attr_path << std::endl;
    return extent_protocol::IOERR;
  }
  ifs >> a.atime >> a.mtime >> a.ctime >> a.size;
  ifs.close();
  return extent_protocol::OK;
}

// write attr to attr file
int writeAttrFile(const std::string& attr_path, const extent_protocol::attr &a) {
  std::ofstream ofs(attr_path.c_str());
  if (!ofs) {
    std::cerr << "Cannot write attr file " << attr_path << std::endl;
    return extent_protocol::IOERR;
  }
  ofs << a.atime << " " << a.mtime << " " << a.ctime << " " << a.size << std::endl;
  ofs.close();
  return extent_protocol::OK;
}

// read file data into buf
int readFile(const std::string& file_path, std::string &buf) {
  std::ifstream ifs(file_path.c_str());
  if (!ifs) {
    std::cerr << "Cannot read data file " << file_path << std::endl;
    return extent_protocol::IOERR;
  }
  // read file into buf
  ifs.seekg(0, std::ios::end);
  buf.resize(ifs.tellg());
  ifs.seekg(0, std::ios::beg);
  ifs.read(&buf[0], buf.length());
  ifs.close();
  return extent_protocol::OK;
}

// write data to file
int writeFile(const std::string& file_path, const std::string &buf) {
  std::ofstream ofs(file_path.c_str());
  if (!ofs) {
    std::cerr << "Cannot write data file " << file_path << std::endl;
    return extent_protocol::IOERR;
  }
  // std::cout << buf;
  ofs << buf;
  ofs.close();
  return extent_protocol::OK;
}

int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
  extent_protocol::status ret = extent_protocol::OK;
  std::string id_str = getIdStr(id);
  std::string file_path = getFilePath(id_str);
  std::string attr_path = getAttrPath(id_str);
  extent_protocol::attr a;

  a.atime = a.mtime = a.ctime = time(NULL);
  a.size = buf.length();

  // write attr back to attr file
  ret = writeAttrFile(attr_path, a);
  if (ret != extent_protocol::OK)
      return ret;
  // write data back to data file
  ret = writeFile(file_path, buf);
  if (ret != extent_protocol::OK)
      return ret;

  return ret;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  std::string id_str = getIdStr(id);
  std::string file_path = getFilePath(id_str);
  std::string attr_path = getAttrPath(id_str);
  extent_protocol::attr a;

  if (!fileExists(attr_path) || !fileExists(file_path)) {
    std::cerr << "File " << file_path << " doesn't exist. Cannot get." << std::endl;
    return extent_protocol::NOENT;
  }

  // read attr file and update
  ret = readAttrFile(attr_path, a);
  if (ret != extent_protocol::OK)
      return ret;
  a.atime = time(NULL);

  // write attr back to attr file
  ret = writeAttrFile(attr_path, a);
  if (ret != extent_protocol::OK)
      return ret;
  // read file into buf
  ret = readFile(file_path, buf);
  if (ret != extent_protocol::OK)
      return ret;

  return ret;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  // You replace this with a real implementation. We send a phony response
  // for now because it's difficult to get FUSE to do anything (including
  // unmount) if getattr fails.
  extent_protocol::status ret = extent_protocol::OK;
  std::string id_str = getIdStr(id);
  std::string attr_path = getAttrPath(id_str);
  std::string file_path = getFilePath(id_str);

  if (!fileExists(attr_path) || !fileExists(file_path)) {
    std::cerr << "Attr file " << attr_path << " doesn't exist. Cannot get attr." << std::endl;
    return extent_protocol::NOENT;
  }

  // read attr file and update
  ret = readAttrFile(attr_path, a);
  if (ret != extent_protocol::OK)
      return ret;
  a.atime = time(NULL);

  // write attr back to attr file
  ret = writeAttrFile(attr_path, a);
  if (ret != extent_protocol::OK)
      return ret;

  return ret;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  std::string id_str = getIdStr(id);
  std::string file_path = getFilePath(id_str);
  std::string attr_path = getAttrPath(id_str);

  printf("remove %016llx\n", id);

  if (!fileExists(attr_path) || !fileExists(file_path)) {
    std::cerr << "File " << file_path << " doesn't exist. Cannot remove." << std::endl;
    return extent_protocol::NOENT;
  }
  // remove data file and attr file
  if (::remove(attr_path.c_str()) != 0 || ::remove(file_path.c_str()) != 0)
    return extent_protocol::IOERR;

  printf("removed %016llx\n", id);

  return extent_protocol::OK;
}

int extent_server::check(extent_protocol::extentid_t id, int &r)
{
  std::string id_str = getIdStr(id);
  std::string file_path = getFilePath(id_str);
  std::string attr_path = getAttrPath(id_str);

  printf("check %016llx\n", id);

  if (fileExists(attr_path) && fileExists(file_path)) {
    printf("checked %016llx exists\n", id);
    r = 1;
  } else {
    printf("checked %016llx does not exist\n", id);
    r = 0;
  }

  return extent_protocol::OK;
}
