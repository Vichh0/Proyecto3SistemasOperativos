//Vicente Castillo y Oscar Montecinos
#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <algorithm>
#include <thread>
#include <atomic>
#include <vector>
#include <map>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <random>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cerrno>

#define PORT 8000
#define BUFFERSIZE 1024

// Contador atómico de clientes activos
static std::atomic<int> activeClients(0);

// Estructuras para gestionar clientes conectados
struct ClientInfo {
    int sock;
    std::string name;
    int id;
    bool inMenu = true;
};

static std::vector<ClientInfo> clients;
static std::mutex clients_mutex;

// Funciones auxiliares
void broadcastMessage(const std::string &msg, int exceptSock = -1) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto &c : clients) {
        if (c.sock == exceptSock) continue;
        send(c.sock, msg.c_str(), msg.size(), 0);
    }
}

// Trim helper: remove leading and trailing whitespace
static std::string trim(const std::string &s) {
    size_t start = 0;
    while (start < s.size() && std::isspace((unsigned char)s[start])) start++;
    size_t end = s.size();
    while (end > start && std::isspace((unsigned char)s[end-1])) end--;
    return s.substr(start, end - start);
}

std::string getClientNameById(int id) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto &c : clients) if (c.id == id) return c.name;
    return std::string("Desconocido");
}

int getClientIdBySock(int sock) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto &c : clients) if (c.sock == sock) return c.id;
    return -1;
}

void setClientMenuState(int clientId, bool state) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto &c : clients) if (c.id == clientId) { c.inMenu = state; break; }
}

void setAllClientsMenuState(bool state) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto &c : clients) c.inMenu = state;
}

void sendMenuToClient(int sock) {
    std::string menu = "Menu principal - comandos disponibles:\n";
    menu += "/juego_trivia -> iniciar trivia (global)\n";
    menu += "/piedra_papel_tijera -> jugar RPS (vs maquina o vs jugador)\n";
    menu += "Para chatear aquí, debe haber exactamente 2 usuarios conectados; de lo contrario use un comando.\n";
    send(sock, menu.c_str(), menu.size(), 0);
}

// Trivia game state
static std::atomic<bool> triviaActive(false);
static std::atomic<bool> questionActive(false);
static std::string currentAnswer;
static std::atomic<bool> answered(false);
static std::mutex trivia_mutex;
static std::map<int,int> triviaScores; // clientId -> score
static int triviaLastResponder = -1;

// Trivia: preguntas simples (pregunta, respuesta)
static const std::vector<std::pair<std::string,std::string>> triviaQuestions = {
    {"¿Nombre del juego de Kratos?", "God of War"},
    {"¿Primer Call of Duty con Zombies?", "World at War"},
    {"¿Personaje con bigote de nintendo?", "Mario"},
    {"¿Color del traje de link tradicional?", "verde"}
};

void triviaThread() {
    {
        std::lock_guard<std::mutex> lock(trivia_mutex);
        if (triviaActive.load()) return; // ya activo
        triviaActive = true;
        triviaScores.clear();
        // marcar a todos los clientes como fuera del menu (en juego)
        setAllClientsMenuState(false);
        // inicializar puntajes actuales
        std::lock_guard<std::mutex> lock2(clients_mutex);
        for (auto &c : clients) triviaScores[c.id] = 0;
    }

    // Enviar reglas básicas de la trivia
    {
        std::ostringstream rules;
        rules << "Inicia Trivia! Responde lo más rápido posible.\n";
        rules << "Reglas: " << triviaQuestions.size() << " preguntas. El primer jugador en enviar la respuesta correcta obtiene 1 punto por pregunta.\n";
        rules << "Tiempo por pregunta: 10 segundos.\n";
        broadcastMessage(rules.str());
    }

    for (auto &q : triviaQuestions) {
        std::string originalAnswer = q.second;
        // normalize answer to lower-case trimmed for comparison
        std::string norm = q.second;
        while (!norm.empty() && (norm.back() == '\n' || norm.back() == '\r' || norm.back()==' ')) norm.pop_back();
        while (!norm.empty() && (norm.front()==' ')) norm.erase(norm.begin());
        std::transform(norm.begin(), norm.end(), norm.begin(), ::tolower);

        {
            std::lock_guard<std::mutex> lock(trivia_mutex);
            currentAnswer = norm;
            answered = false;
            triviaLastResponder = -1;
            questionActive = true;
        }

        std::string pregunta = std::string("Pregunta: ") + q.first + "\n";
        broadcastMessage(pregunta);

        // Habilitar la escritura para respuestas y esperar hasta 10 segundos o hasta que alguien responda
        {
            std::lock_guard<std::mutex> lock(trivia_mutex);
            questionActive = true;
            answered = false;
            triviaLastResponder = -1;
        }

        // Informar a los clientes que tienen 10 segundos para responder
        broadcastMessage("Escribe tu respuesta ahora (10s)\n");

        const int timeoutMs = 10000;
        const int stepMs = 100;
        int waited = 0;
        while (waited < timeoutMs) {
            if (answered.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
            waited += stepMs;
        }

        {
            std::lock_guard<std::mutex> lock(trivia_mutex);
            questionActive = false;
        }

        // anunciar resultado (si alguien respondió, lo hizo antes y triviaLastResponder está seteado)
        if (answered.load() && triviaLastResponder != -1) {
            std::string quien = getClientNameById(triviaLastResponder);
            std::string msg = "Respuesta correcta de: " + quien + " (" + originalAnswer + ")\n";
            broadcastMessage(msg);
        } else {
            std::string correcta = std::string("Respuesta correcta: ") + originalAnswer + "\n";
            broadcastMessage(correcta);
            broadcastMessage("Nadie respondió correctamente en tiempo.\n");
        }
        // breve pausa antes de la siguiente pregunta
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Resultado final
    std::string resultsStr;
    {
        std::ostringstream oss;
        oss << "Resultados de la Trivia:\n";
        // Lock trivia mutex first then clients to avoid deadlocks (consistent order)
        std::lock_guard<std::mutex> lockt(trivia_mutex);
        std::lock_guard<std::mutex> lockc(clients_mutex);
        for (auto &c : clients) {
            int s = 0;
            if (triviaScores.count(c.id)) s = triviaScores[c.id];
            oss << c.name << ": " << s << "\n";
        }
        resultsStr = oss.str();
    }
    // Broadcast fuera de locks
    broadcastMessage(resultsStr);

    // Avisar que la partida terminó y devolver al menu principal
    broadcastMessage("partida terminada, volviendo al menu principal\n");

    // Preparar lista de sockets y luego enviar menús fuera del lock
    std::vector<int> clientSocks;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto &c : clients) clientSocks.push_back(c.sock);
    }

    // Marcar trivia como inactiva antes de enviar menús
    triviaActive = false;
    setAllClientsMenuState(true);

    for (int s : clientSocks) sendMenuToClient(s);
}

// Forward declarations for functions used before their definitions
void piedrapapeltijeras(int sockCliente);
void tivia(int sockCliente);
void playRPSvsMachine(int sockCliente, const std::string &nombre);
void playRPSvsPlayer(int sockCliente, const std::string &nombre, int clientId);
void manejarCliente(int sockCliente, int clientId);

void crearSocket(int &sock) {
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {        
        std::cerr << "Error Creación de Socket" << std::endl;
        exit(1);
    }
}

void configurarServidor(int socket, struct sockaddr_in &conf) {
    // Asegurar que la estructura esté inicializada a cero
    std::memset(&conf, 0, sizeof(conf));
    // Permitir reusar la dirección rápidamente (evita EADDRINUSE en reinicios rápidos)
    int opt = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Warning: setsockopt(SO_REUSEADDR) falló: " << std::strerror(errno) << std::endl;
    }
    conf.sin_family = AF_INET;
    conf.sin_addr.s_addr = htonl(INADDR_ANY);
    conf.sin_port = htons(PORT);

    if ((bind(socket, (struct sockaddr *)&conf, sizeof(conf))) < 0) {
        std::cerr << "Error de enlace: " << std::strerror(errno) << std::endl;
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
// Piedra-Papel-Tijera structures
struct PvPGame {
    int player1Id = -1;
    int player2Id = -1;
    int player1Sock = -1;
    int player2Sock = -1;
    std::string player1Name;
    std::string player2Name;
    std::string move1;
    std::string move2;
    bool ready1 = false;
    bool ready2 = false;
    bool resultSent = false;
    bool replay1 = false;
    bool replay2 = false;
    bool hasReplay1 = false;
    bool hasReplay2 = false;
    std::mutex mtx;
    std::condition_variable cv;
};

static std::shared_ptr<PvPGame> waitingGame = nullptr;
static std::mutex waiting_mutex;

// Normaliza el movimiento (minúsculas) y acepta varias formas
std::string normalizeMove(const std::string &m) {
    std::string s = trim(m);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "piedra" || s == "p") return "piedra";
    if (s == "papel" || s == "pa") return "papel";
    if (s == "tijera" || s == "tijeras" || s == "t") return "tijera";
    return s;
}

// Decide ganador: 0 empate, 1 player1 gana, 2 player2 gana
int decideRPS(const std::string &a, const std::string &b) {
    if (a == b) return 0;
    if (a == "piedra" && b == "tijera") return 1;
    if (a == "tijera" && b == "papel") return 1;
    if (a == "papel" && b == "piedra") return 1;
    return 2;
}

// Juego vs máquina
void playRPSvsMachine(int sockCliente, const std::string &nombre) {
    // marcar cliente como en juego (fuera del menu)
    int cid = getClientIdBySock(sockCliente);
    if (cid != -1) setClientMenuState(cid, false);
    const int maxAttempts = 5;
    std::string move;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> dist(0,2);

    while (true) {
        std::string prompt = "Elegiste jugar contra la máquina. Envía 'piedra', 'papel' o 'tijera' (o escribe CANCEL para salir)\n";
        int attempts = 0;
        move.clear();

        while (attempts < maxAttempts) {
            send(sockCliente, prompt.c_str(), prompt.size(), 0);
            char buf[BUFFERSIZE] = {0};
            int n = read(sockCliente, buf, BUFFERSIZE - 1);
            if (n <= 0) {
                // cliente desconectó
                if (cid != -1) setClientMenuState(cid, true);
                return;
            }
            buf[n] = '\0';
            std::string raw(buf);
            raw = trim(raw);
            std::string rawLower = raw;
            std::transform(rawLower.begin(), rawLower.end(), rawLower.begin(), ::tolower);
            if (rawLower == "cancel") {
                send(sockCliente, "Partida cancelada por el usuario.\n", 33, 0);
                if (cid != -1) {
                    setClientMenuState(cid, true);
                    sendMenuToClient(sockCliente);
                }
                return;
            }
            move = normalizeMove(raw);
            if (move == "piedra" || move == "papel" || move == "tijera") break;
            std::string inval = "Movimiento inválido. Intenta de nuevo o escribe CANCEL para salir.\n";
            send(sockCliente, inval.c_str(), inval.size(), 0);
            attempts++;
        }

        if (!(move == "piedra" || move == "papel" || move == "tijera")) {
            send(sockCliente, "No se recibió un movimiento válido. Se cancela la partida.\n", 50, 0);
            if (cid != -1) {
                setClientMenuState(cid, true);
                sendMenuToClient(sockCliente);
            }
            return;
        }

        // Generar movimiento de la máquina
        int r = dist(gen);
        std::string machine = (r==0?"piedra":(r==1?"papel":"tijera"));

        int res = decideRPS(move, machine);
        std::string resultado;
        if (res == 0) resultado = "Empate! Ambos eligieron " + move + "\n";
        else if (res == 1) resultado = "Ganaste! Tu " + move + " vence a " + machine + "\n";
        else resultado = "Perdiste. Tu " + move + " pierde contra " + machine + "\n";

        send(sockCliente, resultado.c_str(), resultado.size(), 0);
        // Anunciar resultado a la sala (RPS vs máquina)
        {
            std::string summary = "RPS - ";
            summary += nombre + " (" + move + ") vs Máquina (" + machine + "): ";
            if (res == 0) summary += "Empate\n";
            else if (res == 1) summary += nombre + " gana\n";
            else summary += "Máquina gana\n";
            broadcastMessage(summary);
        }

        if (res == 0) {
            // Empate: ofrecer volver a jugar
            std::string ask = "Empate! ¿Jugar otra ronda? (si/no)\n";
            send(sockCliente, ask.c_str(), ask.size(), 0);
            char rb[BUFFERSIZE] = {0};
            int rn = read(sockCliente, rb, BUFFERSIZE - 1);
            if (rn <= 0) {
                if (cid != -1) setClientMenuState(cid, true);
                return;
            }
            rb[rn] = '\0';
            std::string ans(rb);
            while (!ans.empty() && (ans.back()=='\n' || ans.back()=='\r')) ans.pop_back();
            std::transform(ans.begin(), ans.end(), ans.begin(), ::tolower);
            if (ans == "si" || ans == "s" || ans == "yes" || ans == "y") {
                // continuar lazo y jugar otra ronda
                continue;
            } else {
                std::string endmsg = "partida terminada, volviendo al menu principal\n";
                send(sockCliente, endmsg.c_str(), endmsg.size(), 0);
                if (cid != -1) {
                    setClientMenuState(cid, true);
                    sendMenuToClient(sockCliente);
                }
                return;
            }
        } else {
            // Win or lose: finalizar y volver al menu
            std::string endmsg = "partida terminada, volviendo al menu principal\n";
            send(sockCliente, endmsg.c_str(), endmsg.size(), 0);
            if (cid != -1) {
                setClientMenuState(cid, true);
                sendMenuToClient(sockCliente);
            }
            return;
        }
    }
}

void playRPSvsPlayer(int sockCliente, const std::string &nombre, int clientId) {
    std::shared_ptr<PvPGame> mygame;
    {
        std::lock_guard<std::mutex> lock(waiting_mutex);
        if (!waitingGame) {
            // No one is waiting, create a new game
            waitingGame = std::make_shared<PvPGame>();
            waitingGame->player1Id = clientId;
            waitingGame->player1Sock = sockCliente;
            waitingGame->player1Name = nombre;
            mygame = waitingGame;
        } else {
            // Someone is waiting, join their game
            mygame = waitingGame;
            waitingGame = nullptr; // Clear the waiting game
            mygame->player2Id = clientId;
            mygame->player2Sock = sockCliente;
            mygame->player2Name = nombre;
        }
    }

    if (mygame->player2Id == -1) {
        // I am the first player, wait for the second
        send(sockCliente, "Esperando rival...\n", 20, 0);
        std::unique_lock<std::mutex> lk(mygame->mtx);
        if (!mygame->cv.wait_for(lk, std::chrono::seconds(30), [&]{ return mygame->player2Id != -1; })) {
            // Timeout
            send(sockCliente, "Nadie se unió. Volviendo al menú.\n", 35, 0);
            setClientMenuState(clientId, true);
            sendMenuToClient(sockCliente);
            waitingGame = nullptr; // Clear the waiting game
            return;
        }
    } else {
        // I am the second player, notify the first
        mygame->cv.notify_all();
    }

    // Both players are matched
    setClientMenuState(mygame->player1Id, false);
    setClientMenuState(mygame->player2Id, false);

    // Game logic here
    // Each player sends their move, we determine the winner, etc.
    // For now, let's just simulate one round:

    while (true) {
        std::string prompt1 = "Jugador 1 (" + mygame->player1Name + "), elige: piedra, papel o tijera (o escribe CANCEL para salir)\n";
        send(mygame->player1Sock, prompt1.c_str(), prompt1.size(), 0);

        std::string prompt2 = "Jugador 2 (" + mygame->player2Name + "), elige: piedra, papel o tijera (o escribe CANCEL para salir)\n";
        send(mygame->player2Sock, prompt2.c_str(), prompt2.size(), 0);

        // Leer movimientos de ambos jugadores
        for (int i = 0; i < 2; ++i) {
            char buf[BUFFERSIZE] = {0};
            int n = read((i == 0 ? mygame->player1Sock : mygame->player2Sock), buf, BUFFERSIZE - 1);
            if (n <= 0) {
                // cliente desconectó
                if (i == 0) {
                    // Player 1 disconnected
                    send(mygame->player2Sock, "El jugador 1 se ha desconectado. Fin del juego.\n", 45, 0);
                } else {
                    // Player 2 disconnected
                    send(mygame->player1Sock, "El jugador 2 se ha desconectado. Fin del juego.\n", 45, 0);
                }
                // Cleanup and exit
                setClientMenuState(mygame->player1Id, true);
                setClientMenuState(mygame->player2Id, true);
                sendMenuToClient(mygame->player1Sock);
                sendMenuToClient(mygame->player2Sock);
                return;
            }
            buf[n] = '\0';
            std::string raw(buf);
            raw = trim(raw);
            std::string rawLower = raw;
            std::transform(rawLower.begin(), rawLower.end(), rawLower.begin(), ::tolower);
            if (rawLower == "cancel") {
                send(mygame->player1Sock, "Partida cancelada por el usuario.\n", 33, 0);
                send(mygame->player2Sock, "El otro jugador canceló la partida.\n", 36, 0);
                // Cleanup and exit
                setClientMenuState(mygame->player1Id, true);
                setClientMenuState(mygame->player2Id, true);
                sendMenuToClient(mygame->player1Sock);
                sendMenuToClient(mygame->player2Sock);
                return;
            }
            if (i == 0) {
                mygame->move1 = normalizeMove(raw);
            } else {
                mygame->move2 = normalizeMove(raw);
            }
        }

        // Ambos jugadores han hecho su movimiento, determinar ganador
        int res = decideRPS(mygame->move1, mygame->move2);
        std::string resultado;
        if (res == 0) resultado = "Empate! Ambos eligieron " + mygame->move1 + "\n";
        else if (res == 1) resultado = "Jugador 1 gana! " + mygame->move1 + " vence a " + mygame->move2 + "\n";
        else resultado = "Jugador 2 gana! " + mygame->move2 + " vence a " + mygame->move1 + "\n";

        send(mygame->player1Sock, resultado.c_str(), resultado.size(), 0);
        send(mygame->player2Sock, resultado.c_str(), resultado.size(), 0);

        // Preguntar si quieren volver a jugar
        for (int i = 0; i < 2; ++i) {
            std::string ask = "¿Jugar otra ronda, " + (i == 0 ? mygame->player1Name : mygame->player2Name) + "? (si/no)\n";
            send((i == 0 ? mygame->player1Sock : mygame->player2Sock), ask.c_str(), ask.size(), 0);
            char rb[BUFFERSIZE] = {0};
            int rn = read((i == 0 ? mygame->player1Sock : mygame->player2Sock), rb, BUFFERSIZE - 1);
            if (rn <= 0) {
                // cliente desconectó
                if (i == 0) {
                    // Player 1 disconnected
                    send(mygame->player2Sock, "El jugador 1 se ha desconectado. Fin del juego.\n", 45, 0);
                } else {
                    // Player 2 disconnected
                    send(mygame->player1Sock, "El jugador 2 se ha desconectado. Fin del juego.\n", 45, 0);
                }
                // Cleanup and exit
                setClientMenuState(mygame->player1Id, true);
                setClientMenuState(mygame->player2Id, true);
                sendMenuToClient(mygame->player1Sock);
                sendMenuToClient(mygame->player2Sock);
                return;
            }
            rb[rn] = '\0';
            std::string ans(rb);
            while (!ans.empty() && (ans.back()=='\n' || ans.back()=='\r')) ans.pop_back();
            std::transform(ans.begin(), ans.end(), ans.begin(), ::tolower);
            if (ans != "si" && ans != "s" && ans != "yes" && ans != "y") {
                std::string endmsg = "partida terminada, volviendo al menu principal\n";
                send(mygame->player1Sock, endmsg.c_str(), endmsg.size(), 0);
                send(mygame->player2Sock, endmsg.c_str(), endmsg.size(), 0);
                setClientMenuState(mygame->player1Id, true);
                setClientMenuState(mygame->player2Id, true);
                sendMenuToClient(mygame->player1Sock);
                sendMenuToClient(mygame->player2Sock);
                return;
            }
        }
        // Si ambos quieren seguir, continuar el bucle y jugar otra ronda
    }
}

void manejarCliente(int sockCliente, int clientId) {
    std::cout << "Manejando cliente " << clientId << std::endl;

    // Primer read: obtener nombre del cliente
    char buffer[BUFFERSIZE] = {0};
    int valread = read(sockCliente, buffer, BUFFERSIZE - 1);
    if (valread <= 0) {
        close(sockCliente);
        activeClients--;
        return;
    }
    buffer[valread] = '\0';
    std::string nombre(buffer);
    while (!nombre.empty() && (nombre.back() == '\n' || nombre.back() == '\r')) nombre.pop_back();

    // Registrar cliente (en menu por defecto)
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        ClientInfo ci;
        ci.sock = sockCliente;
        ci.name = nombre;
        ci.id = clientId;
        ci.inMenu = true;
        clients.push_back(ci);
    }

    // Enviar bienvenida local y notificar a la sala
    std::string bienvenida = "Bienvenido " + nombre + "\n";
    send(sockCliente, bienvenida.c_str(), bienvenida.size(), 0);
    broadcastMessage("Usuario " + nombre + " se ha conectado\n", sockCliente);
    // Enviar menú inicial al cliente
    sendMenuToClient(sockCliente);

    // Bucle principal: recibir mensajes del cliente
    while (true) {
        char buf[BUFFERSIZE] = {0};
        int n = read(sockCliente, buf, BUFFERSIZE - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        std::string msg(buf);
        // Trim leading/trailing whitespace
        msg = trim(msg);

        if (msg.empty()) continue;

        // Comandos que inician juegos
        if (msg == "/juego_trivia") {
            if (!triviaActive.load()) {
                // marcar a todos como en juego y lanzar trivia
                setAllClientsMenuState(false);
                std::thread t(triviaThread);
                t.detach();
            } else {
                send(sockCliente, "Ya hay una trivia en curso\n", 22, 0);
            }
            continue;
        }

        if (msg == "/piedra_papel_tijera") {
            setClientMenuState(clientId, false); // Mark as out of menu to process game input
            
            std::string prompt = "Elige modo: 1) vs Maquina 2) vs Jugador\n";
            send(sockCliente, prompt.c_str(), prompt.size(), 0);

            char choiceBuf[BUFFERSIZE] = {0};
            int cn = read(sockCliente, choiceBuf, BUFFERSIZE - 1);

            if (cn > 0) {
                choiceBuf[cn] = '\0';
                std::string choice = trim(std::string(choiceBuf));

                if (choice == "1") {
                    playRPSvsMachine(sockCliente, nombre);
                } else if (choice == "2") {
                    playRPSvsPlayer(sockCliente, nombre, clientId);
                } else {
                    send(sockCliente, "Opción inválida. Volviendo al menú.\n", 38, 0);
                    setClientMenuState(clientId, true); // Restore menu state
                    sendMenuToClient(sockCliente);
                }
            } else {
                // Error or disconnect, restore menu state and the loop will exit
                setClientMenuState(clientId, true);
            }
            continue; // After handling game logic, continue to the next loop iteration
        }

        // Si hay una pregunta activa para la trivia, chequear respuestas
        if (triviaActive.load() && questionActive.load()) {
            std::string lower = trim(msg);
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    std::lock_guard<std::mutex> lock(trivia_mutex);
                    if (questionActive.load() && !answered.load() && lower == currentAnswer) {
                        // Otorgar punto y registrar al respondedor
                        triviaScores[clientId]++;
                        answered = true;
                        triviaLastResponder = clientId;
                    }
            // Si la trivia está activa, también no hacemos broadcast normal
            continue;
        }

        // Comando para desconectarse
        if (msg == "BYE") {
            std::string despedida = "Adios " + nombre + "\n";
            send(sockCliente, despedida.c_str(), despedida.size(), 0);
            break;
        }

        // Si el cliente está en el menu principal, permitimos chat directo solo si hay 2 usuarios.
        bool isInMenu = false;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (auto &c : clients) if (c.id == clientId) { isInMenu = c.inMenu; break; }
        }
        if (isInMenu) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            if (clients.size() == 2) {
                // Enviar solo al otro usuario
                for (auto &c : clients) {
                    if (c.id != clientId) {
                        std::string privado = nombre + " (privado): " + msg + "\n";
                        send(c.sock, privado.c_str(), privado.size(), 0);
                        break;
                    }
                }
            } else {
                std::string info = "En el menu principal. Comandos disponibles:\n";
                info += "/juego_trivia\n";
                info += "/piedra_papel_tijera\n";
                info += "Escribe comando para jugar.\n";
                send(sockCliente, info.c_str(), info.size(), 0);
            }
            continue;
        }

        // Mensaje normal: reenviar a todos
        std::string paraSala = nombre + ": " + msg + "\n";
        broadcastMessage(paraSala, sockCliente);
    }

    // Limpieza al desconectar
    close(sockCliente);
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(std::remove_if(clients.begin(), clients.end(), [clientId](const ClientInfo &c){ return c.id == clientId; }), clients.end());
    }
    
    activeClients--;
    std::cout << "Cliente " << clientId << " desconectado" << std::endl;
} // <-- This closes manejarCliente

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
