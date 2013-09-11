#include <iostream>
#include <string>
#include <cstdlib>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <fstream>
#include <cstdlib>
#include <cstring>

#include "Cache.h"
#include "CachedFile.h"

#define CACHE_MAX 1024 * 1024 * 64

void validatePort(char* port_str) {
    int port = atoi(port_str);
    if (port < 1024 || port > 65535) {
        std::cerr << "Invalid port number. Port number should be between 1024 and 65535." << std::endl;
        exit(EXIT_FAILURE);
    }
}

void validateDirPath(char* dir_path) {
    struct stat sb;
    if (stat(dir_path, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
        std::cerr << dir_path << " doesn't exist. Please enter a valid direcotry path." << std::endl;
        exit(EXIT_FAILURE);
    }
}

void validateArgs(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Invalid arguments. Usage: tcp_server port_to_listen_on file_directory" << std::endl;
        exit(EXIT_FAILURE);
    }
    validatePort(argv[1]);
    validateDirPath(argv[2]);
}

void printError(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int createServerSocket(int port) {
    int ssock = socket(AF_INET, SOCK_STREAM, 0);
    if (ssock < 0)
        printError("Cannot open socket on server");
    
    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(port);
    
    if (bind(ssock, (struct sockaddr*) &saddr, sizeof(saddr)) < 0)
        printError("Cannot bind socket on server");
    
    listen(ssock, 5);
    
    return ssock;
}

int acceptConnection(int ssock, struct sockaddr_in* caddr) {
    socklen_t clilen = sizeof(*caddr);
    int sock = accept(ssock, (struct sockaddr*) caddr, &clilen);
    if (sock < 0)
        printError("Cannot accept connection request from client");
    return sock;
}

void sendData(int sock, const char* data, size_t len, int flags, const char* error_msg) {
    size_t sent = 0;
    size_t left = len;
    while (left != 0) {
        ssize_t n = send(sock, data + sent, left, flags);
        if (n < 0)
            printError(error_msg);
        else {
            sent += n;
            left -= n;
        }
    }
}

void recvData(int sock, char* data, size_t len, int flags, const char* error_msg) {
    size_t received = 0;
    size_t left = len;
    while (left != 0) {
        ssize_t n = recv(sock, data + received, left, flags);
        if (n < 0)
            printError(error_msg);
        else if (n == 0) {
            std::cerr << "The socket is closed." << std::endl;
            exit(EXIT_FAILURE);
        } else {
            received += n;
            left -=n;
        }
    }
}

void recvFileName(int sock, std::string& file_name) {
    // get the file name length
    size_t file_name_len;
    recvData(sock, (char*) &file_name_len, sizeof(size_t), 0, "Cannot recv the file name length from client");
    
    // get the file name
    char* file_name_buf = new char[file_name_len + 1];
    recvData(sock, file_name_buf, file_name_len, 0, "Cannot get the file name from client");
    file_name_buf[file_name_len] = '\0';
    file_name = file_name_buf;
    delete [] file_name_buf;
}

void getClientIP(std::string& client_ip, const struct sockaddr_in& caddr) {
    char client_ip_buf[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &caddr.sin_addr.s_addr, client_ip_buf, INET_ADDRSTRLEN) == NULL)
        printError("Cannot convert client ip to string");
    client_ip = client_ip_buf;
}

// check whether file exists on the server
bool fileExists(const std::string& file_path) {
    struct stat sb;
    return stat(file_path.c_str(), &sb) == 0 && S_ISREG(sb.st_mode);
}

void sendStatus(char status, int sock) {
    sendData(sock, &status, sizeof(char), 0, "Cannot send the status to client");
}

CachedFile* readFileToBuffer(const std::string& file_path) {
    std::ifstream ifs(file_path.c_str(), std::ifstream::binary);
    if (!ifs) {
        std::cerr << "Cannot open file on server." << std::endl;
        exit(EXIT_FAILURE);
    }
    
    // get the size of the file
    ifs.seekg(0, ifs.end);
    size_t file_size = ifs.tellg();
    ifs.seekg(0, ifs.beg);
    
    // create buffer and read file into it
    char* file_buffer = new char[file_size];
    ifs.read(file_buffer, file_size);
    if (!ifs) {
        std::cerr << "Cannot read file on server." << std::endl;
        exit(EXIT_FAILURE);
    }
    ifs.close();
    
    CachedFile* cached = new CachedFile(file_buffer, file_size, time(0));
    return cached;
}

void sendFile(int sock, const CachedFile* cached) {
    size_t file_size = cached->getSize();
    char* file_buffer = cached->getBuffer();
    
    // send the file size
    sendData(sock, (char*) &file_size, sizeof(size_t), 0, "Cannot send the file size to client");
    
    // send the data
    sendData(sock, file_buffer, file_size, 0, "Cannot send the file to client");
}

int main(int argc, char* argv[]) {
    // check whether all arguments are valid
    validateArgs(argc, argv);
    
    // get valid port number and file directory from arguments
    int port = atoi(argv[1]);
    std::string dir_path = argv[2];
    
    // create server socket and listen to it
    int ssock = createServerSocket(port);
    
    // create cache
    Cache cache;
    
    while (true) {
        struct sockaddr_in caddr;
        // accept connection from client
        int sock = acceptConnection(ssock, &caddr);
        
        // get the file name from client
        std::string file_name;
        recvFileName(sock, file_name);
        
        // get the client ip address
        std::string client_ip;
        getClientIP(client_ip, caddr);
        
        std::cout << "Client " << client_ip << " is requesting file " << file_name << std::endl;
        
        std::string file_path = dir_path + "/" + file_name;
//        printf("File Path: %s\n", file_path.c_str());
        
        // find the file from the cache
        CachedFile* cached = cache.getFile(file_name);

        // if in the cache
        if (cached) {
            sendStatus('y', sock);
            
            sendFile(sock, cached);
            
            // update the time stamp
            cached->setLastUsed(time(0));
            
            std::cout << "Cache hit. File " << file_name << " sent to the client" << std::endl;
            
//            std::cout << "Time stamp for file " << file_name << " is " << cached->getLastUsed() << std::endl;
            
        } else if (fileExists(file_path)) {
            sendStatus('y', sock);
            
            // send file to client
            CachedFile* cached = readFileToBuffer(file_path);
            sendFile(sock, cached);
    
            std::cout << "Cache miss. File " << file_name << " sent to the client" << std::endl;
            
            cache.runPolicy(file_name, cached);
            
//            std::cout << "Time stamp for file " << file_name << " is " << cached->getLastUsed() << std::endl;
            
        } else {
            sendStatus('n', sock);
            std::cout << "File " << file_name << " does not exist" << std::endl;
        }
        
        close(sock);
        
        std::cout << "Cache size: " << cache.getSize() << std::endl;
        std::cout << "======================================================" << std::endl;
    }
    
    close(ssock);
    
    return 0;
}
