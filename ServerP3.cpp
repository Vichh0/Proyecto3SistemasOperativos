//Vicente Castillo y Oscar Montecinos
#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <algorithm>
#include <thread>
#include <atomic>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 8000
#define BUFFERSIZE 1024

// Contador atómico de clientes activos
static std::atomic<int> activeClients(0);

// Forward declarations for functions used before their definitions
void piedrapapeltijeras(int sockCliente);
void tivia(int sockCliente);
void manejarCliente(int sockCliente, int clientId);

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
        piedrapapeltijeras(sockCliente);
    } else if (seleccion == "2\n") {
        tivia(sockCliente);
    } else {
        // Si no es una selección de juego válida, asumimos protocolo de ChatP3.
        // El cliente envió su nombre (sin newline). Responderemos con un saludo
        // y luego entramos en un bucle de eco hasta recibir "BYE".
        std::string nombre = seleccion;
        // Eliminar posibles '\r' y '\n' al final
        while (!nombre.empty() && (nombre.back() == '\n' || nombre.back() == '\r'))
            nombre.pop_back();

        std::string bienvenida = "Bienvenido " + nombre + "\n";
        send(sockCliente, bienvenida.c_str(), bienvenida.size(), 0);

        // Bucle de chat: leer mensajes y responder hasta BYE
        while (true) {
            char buf[BUFFERSIZE] = {0};
            int n = read(sockCliente, buf, BUFFERSIZE - 1);
            if (n < 0) {
                std::cerr << "Error leyendo mensaje del cliente" << std::endl;
                break;
            } else if (n == 0) {
                std::cerr << "Cliente cerró la conexión" << std::endl;
                break;
            }
            buf[n] = '\0';
            std::string msg(buf);

            // Normalizar el mensaje para comparación
            std::string msg_trim = msg;
            while (!msg_trim.empty() && (msg_trim.back() == '\n' || msg_trim.back() == '\r'))
                msg_trim.pop_back();

            if (msg_trim == "BYE") {
                std::string despedida = "Adios " + nombre + "\n";
                send(sockCliente, despedida.c_str(), despedida.size(), 0);
                break;
            } else {
                // Responder con eco (o cualquier lógica de servidor)
                std::string respuesta = "Servidor: " + msg;
                send(sockCliente, respuesta.c_str(), respuesta.size(), 0);
            }
        }
    }
}

void piedrapapeltijeras(int sockCliente) {
    // Implementar la lógica del juego aquí. Por ahora solo enviamos un mensaje
    std::string msg = "PiedraPapelTijeras: función no implementada\n";
    send(sockCliente, msg.c_str(), msg.size(), 0);
}

void tivia(int sockCliente)   {
    // Implementar la lógica del juego aquí. Por ahora solo enviamos un mensaje
    std::string msg = "Trivia: función no implementada\n";
    send(sockCliente, msg.c_str(), msg.size(), 0);
}

// Thread function to handle a connected client
void manejarCliente(int sockCliente, int clientId) {
    std::cout << "Manejando cliente " << clientId << std::endl;

    // Read game selection and dispatch
    seleccionjuego(sockCliente);

    // Close client socket when done
    close(sockCliente);

    // Decrement active client count and log
    activeClients--;
    std::cout << "Cliente " << clientId << " desconectado" << std::endl;
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Uso: " << argv[0] << " <nClientes>  (ej: " << argv[0] << " 1)" << std::endl;
        return 1;
    }

    int nClientes = 0;
    try {
        nClientes = std::stoi(argv[1]);
    } catch (const std::invalid_argument &e) {
        std::cerr << "Argumento inválido para nClientes: debe ser un número entero positivo.\n";
        std::cerr << "Uso: " << argv[0] << " <nClientes>  (ej: " << argv[0] << " 1)" << std::endl;
        return 1;
    } catch (const std::out_of_range &e) {
        std::cerr << "Argumento fuera de rango para nClientes." << std::endl;
        return 1;
    }

    if (nClientes < 1) {
        std::cerr << "nClientes debe ser >= 1" << std::endl;
        return 1;
    }

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

    // 4. Aceptar conexiones (cada conexión en su propio hilo)
    std::cout << "Esperando conexiones..." << std::endl;
    int clienteIdCounter = 0;

    while (true) {
        int sockCliente;
        struct sockaddr_in confCliente;

        aceptarConexion(sockCliente, sockServidor, confCliente);

        // Si ya alcanzamos el máximo de clientes concurrentes, rechazamos
        if (activeClients.load() >= nClientes) {
            std::string msg = "Servidor lleno, intente más tarde\n";
            send(sockCliente, msg.c_str(), msg.size(), 0);
            close(sockCliente);
            std::cout << "Rechazada conexión: servidor lleno" << std::endl;
            continue;
        }

        // Aceptada
        clienteIdCounter++;
        activeClients++;
        std::cout << "Cliente " << clienteIdCounter << " conectado (activos: " << activeClients.load() << ")" << std::endl;

        // Crear hilo detachable para manejar el cliente
        std::thread t(manejarCliente, sockCliente, clienteIdCounter);
        t.detach();
    }

    close(sockServidor);
    std::cout << "Servidor cerrado" << std::endl;
    return 0;
}