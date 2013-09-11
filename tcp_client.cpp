#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <cstring>
#include <cstdlib>

void validatePort(const char* port_str) {
    int port = atoi(port_str);
    if (port < 1024 || port > 65535) {
        std::cerr << "Invalid port number. Port number should be between 1024 and 65535." << std::endl;
        exit(EXIT_FAILURE);
    }
}

void validateDirPath(const char* dir_path) {
    struct stat sb;
    if (stat(dir_path, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
        std::cerr << dir_path << " doesn't exist. Please enter a valid direcotry path." << std::endl;
        exit(EXIT_FAILURE);
    }
}

void validateArgs(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Invalid arguments. Usage: tcp_client server_host server_port file_name directory" << std::endl;
        exit(EXIT_FAILURE);
    }
    validatePort(argv[2]);
    validateDirPath(argv[4]);
}

void printError(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int createSocket(const char* ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        printError("Cannot open socket on client");
    
    struct in_addr addr;
    inet_pton(AF_INET, ip, &addr);
    struct hostent* host = gethostbyaddr(&addr, sizeof(addr), AF_INET);
    if (host == NULL) {
        fprintf(stderr, "%s doesn't exist. Please enter a valid host ip.\n", ip);
        exit(EXIT_FAILURE);
    }
    
    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    memcpy(&saddr.sin_addr.s_addr, host->h_addr, host->h_length);
    saddr.sin_port = htons(port);
    if (connect(sock, (struct sockaddr*) &saddr, sizeof(saddr)) < 0)
        printError("Cannot connet to the server");
    
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

void sendFileName(const std::string& file_name, int sock) {
    // send file name length first
    size_t file_name_len = file_name.size();
    sendData(sock, (char*) &file_name_len, sizeof(size_t), 0, "Cannot send the file name length to server");
    
    // send the file name
    sendData(sock, file_name.c_str(), file_name_len, 0, "Cannot send the file name to server");
}

// check whether file exists on server
bool fileExists(int sock) {
    char status;
    recvData(sock, &status, sizeof(char), 0, "Cannot receive status from the server");
    return status == 'y' ? true : false;
}

void recvFile(int sock, const std::string& file_path) {
    // recv the size of the file
    size_t file_size;
    recvData(sock, (char*) &file_size, sizeof(size_t), 0, "Cannot recv file size from server");
    
    // recv the data of the file
    char* file_buf = new char[file_size];
    recvData(sock, file_buf, file_size, 0, "Cannot recv file from the server");
    
    std::ofstream ofs(file_path.c_str(), std::ofstream::binary);
    if (!ofs) {
        std::cerr << "Cannot open file to write on client." << std::endl;
        exit(EXIT_FAILURE);
    }
    ofs.write(file_buf, file_size);
    if (!ofs) {
        std::cerr << "Cannot write data to file on client." << std::endl;
        exit(EXIT_FAILURE);
    }
    ofs.close();
    delete [] file_buf;
}

int main(int argc, char* argv[]) {
    // check whether all arguments are valid
    validateArgs(argc, argv);
    
    // get all valid arguments
    char* ip = argv[1];
    int port = atoi(argv[2]);
    std::string file_name = argv[3];
    std::string dir_path = argv[4];
    
    // create socket and connet to server
    int sock = createSocket(ip, port);
    
    // send the file name to the server
    sendFileName(file_name, sock);
    
    if (fileExists(sock)) {
        std::string file_path = dir_path + "/" + file_name;
        recvFile(sock, file_path);
        std::cout << "File " << file_name << " saved" << std::endl;
    } else {
        std::cout << "File " << file_name << " does not exist in the server" << std::endl;
    }
    
    close(sock);
    
    return 0;
}


