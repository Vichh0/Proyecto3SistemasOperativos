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

void seleccionjuego(int sockCliente) {
    char buffer[BUFFERSIZE] = {0};
    int valread = read(sockCliente, buffer, BUFFERSIZE - 1);
    if (valread < 0) {
        std::cerr << "Error al recibir selección de juego" << std::endl;
        return;
    } else if (valread == 0) {
        std::cerr << "Cliente cerró la conexión" << std::endl;
        return;
    }
    buffer[valread] = '\0';
    std::string seleccion(buffer);

    if (seleccion == "1\n") {
        piedrapapeltijeras();
    } else if (seleccion == "2\n") {
        tivia();
    } else {
        std::cerr << "Selección de juego inválida" << std::endl;
    }
}

void piedrapapeltijeras() {


}

void tivia()   {


}


int main(int argc, char *argv[]) {
    if (argc < 2)
        return 0;
    
    int nClientes = std::stoi(argv[1]);
    
    if (nClientes < 1)
        return 0;

    // 1. Configuración del Socket
    int sockServidor;
    crearSocket(sockServidor);

    // 2. Vinculación
    struct sockaddr_in confServidor;
    configurarServidor(sockServidor, confServidor);

    // 3. Escuchando conexiones entrantes
    escucharClientes(sockServidor, nClientes);

    std::cout << "Servidor escuchando en puerto " << PORT << std::endl;
    std::cout << "Máximo de clientes simultáneos: " << nClientes << std::endl;

    // 4. Aceptar conexiones (con hilos)
    std::thread threads[nClientes];
    int clienteActual = 0;

    for (int i = 0; i < nClientes; i++) {
        int sockCliente;
        struct sockaddr_in confCliente;
        
        aceptarConexion(sockCliente, sockServidor, confCliente);
        
        std::cout << "Cliente " << (i + 1) << " conectado" << std::endl;
        
        // Crear un hilo para manejar este cliente
        threads[i] = std::thread(manejarCliente, sockCliente, i + 1);
    }

    // Esperar a que todos los hilos terminen
    for (int i = 0; i < nClientes; i++) {
        if (threads[i].joinable()) {
            threads[i].join();
        }
    }

    close(sockServidor);
    std::cout << "Servidor cerrado" << std::endl;
    return 0;
}