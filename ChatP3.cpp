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

int main(int argc, char const *argv[]) {
    if (argc < 2)
        return 0;
    
    std::string nombreCliente = argv[1];

    // 1. Crear Socket
    int sockCliente;
    crearSocket(sockCliente);

    // 2. Conectarse al Servidor
    struct sockaddr_in confServidor;
    configurarCliente(sockCliente, confServidor);
   
    bool primerMensaje = true;
    
    // 3. Comunicarse
    while (true) {
        char buffer[BUFFERSIZE] = {0};
        char buffer2[BUFFERSIZE] = {0};
        
        if (primerMensaje) {
            // Enviar nombre del cliente
            int sent = send(sockCliente, nombreCliente.c_str(), nombreCliente.length(), 0);
            if (sent < 0) {
                std::cerr << "Error al enviar nombre" << std::endl;
                break;
            }
            
            // Recibir respuesta inicial
            int valread = read(sockCliente, buffer, BUFFERSIZE - 1);
            if (valread < 0) {
                std::cerr << "Error al recibir respuesta inicial" << std::endl;
                break;
            } else if (valread == 0) {
                std::cerr << "Servidor cerró la conexión" << std::endl;
                break;
            }
            buffer[valread] = '\0';
            std::cout << buffer;
            primerMensaje = false;
            
        } else {
            // Pedir mensaje al usuario
            std::cout << "Mensaje: ";
            std::string mensajeUsuario;
            std::getline(std::cin, mensajeUsuario);
            mensajeUsuario += "\n";  // Añadir newline para consistencia
            
            if (mensajeUsuario == "BYE\n") {
                // Enviar BYE
                int sent = send(sockCliente, mensajeUsuario.c_str(), mensajeUsuario.length(), 0);
                if (sent < 0) {
                    std::cerr << "Error al enviar BYE" << std::endl;
                    break;
                }
                
                // Recibir respuesta final
                int valread = read(sockCliente, buffer2, BUFFERSIZE - 1);
                if (valread < 0) {
                    std::cerr << "Error al recibir respuesta final" << std::endl;
                    break;
                } else if (valread == 0) {
                    std::cerr << "Servidor cerró la conexión" << std::endl;
                    break;
                }
                buffer2[valread] = '\0';
                std::cout << buffer2 << std::endl;
                break;
                
            } else {
                // Enviar mensaje normal
                int sent = send(sockCliente, mensajeUsuario.c_str(), mensajeUsuario.length(), 0);
                if (sent < 0) {
                    std::cerr << "Error al enviar mensaje" << std::endl;
                    break;
                }
                
                // Recibir respuesta
                int valread = read(sockCliente, buffer, BUFFERSIZE - 1);
                if (valread < 0) {
                    std::cerr << "Error al recibir respuesta" << std::endl;
                    break;
                } else if (valread == 0) {
                    std::cerr << "Servidor cerró la conexión" << std::endl;
                    break;
                }
                buffer[valread] = '\0';
                std::cout << buffer;
            }
        }
    }

    close(sockCliente);
    return 0;
}
