//Vicente Castillo y Oscar Montecinos
#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <algorithm>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 8000
#define BUFFERSIZE 1024

void crearSocket(int &sock) {
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {        
        std::cerr << "Error Creación de Socket" << std::endl;
        exit(1);
    }
}

void configurarServidor(int socket, struct sockaddr_in &conf) {
    conf.sin_family = AF_INET;
    conf.sin_addr.s_addr = htonl(INADDR_ANY);
    conf.sin_port = htons(PORT);

    if ((bind(socket, (struct sockaddr *)&conf, sizeof(conf))) < 0) {
        std::cerr << "Error de enlace" << std::endl;
        exit(1);
    }
}

void escucharClientes(int sock, int n) {
    if (listen(sock, n) < 0) {
        std::cerr << "Error listening" << std::endl;
        exit(1);
    }
}

void aceptarConexion(int &sockNuevo, int sock, struct sockaddr_in &conf) {
    socklen_t tamannoConf = sizeof(conf);

    if ((sockNuevo = accept(sock, (struct sockaddr *)&conf, &tamannoConf)) < 0) {
        std::cerr << "Error accepting" << std::endl;
        exit(1);
    }
}