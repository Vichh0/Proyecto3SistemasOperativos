//Vicente Castillo y Oscar Montecinos
#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PORT 8000
#define BUFFERSIZE 1024

void crearSocket(int &sock) {
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {        
        std::cerr << "Error Creación de Socket" << std::endl;
        exit(1);
    }
}

void configurarCliente(int sock, struct sockaddr_in &conf) {
    conf.sin_family = AF_INET;
    conf.sin_port = htons(PORT);
    conf.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&conf, sizeof(conf)) < 0) {
        std::cerr << "Connection Failed" << std::endl;
        exit(1);
    }
}