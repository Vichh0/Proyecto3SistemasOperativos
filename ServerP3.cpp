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

std::string getClientNameById(int id) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto &c : clients) if (c.id == id) return c.name;
    return std::string("Desconocido");
}

// Trivia game state
static std::atomic<bool> triviaActive(false);
static std::atomic<bool> questionActive(false);
static std::string currentAnswer;
static std::atomic<bool> answered(false);
static std::mutex trivia_mutex;
static std::map<int,int> triviaScores; // clientId -> score

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
        // inicializar puntajes actuales
        std::lock_guard<std::mutex> lock2(clients_mutex);
        for (auto &c : clients) triviaScores[c.id] = 0;
    }

    broadcastMessage("Inicia Trivia! Responde lo más rápido posible.\n");

    for (auto &q : triviaQuestions) {
        {
            std::lock_guard<std::mutex> lock(trivia_mutex);
            currentAnswer = q.second;
            answered = false;
            questionActive = true;
        }
        std::string pregunta = std::string("Pregunta: ") + q.first + "\n";
        broadcastMessage(pregunta);

        // esperar un tiempo para respuestas (8 segundos)
        std::this_thread::sleep_for(std::chrono::seconds(8));

        {
            std::lock_guard<std::mutex> lock(trivia_mutex);
            questionActive = false;
        }

        // anunciar respuesta correcta
        std::string correcta = std::string("Respuesta correcta: ") + q.second + "\n";
        broadcastMessage(correcta);
        // breve pausa antes de la siguiente pregunta
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Resultado final
    {
        std::ostringstream oss;
        oss << "Resultados de la Trivia:\n";
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto &c : clients) {
            int s = 0;
            {
                std::lock_guard<std::mutex> lock2(trivia_mutex);
                if (triviaScores.count(c.id)) s = triviaScores[c.id];
            }
            oss << c.name << ": " << s << "\n";
        }
        broadcastMessage(oss.str());
    }

    triviaActive = false;
}

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
    std::string s = m;
    // trim
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back()==' ')) s.pop_back();
    while (!s.empty() && (s.front()==' ')) s.erase(s.begin());
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
    std::string prompt = "Elegiste jugar contra la máquina. Envía 'piedra', 'papel' o 'tijera' (o escribe CANCEL para salir)\n";
    const int maxAttempts = 5;
    int attempts = 0;
    std::string move;
    while (attempts < maxAttempts) {
        send(sockCliente, prompt.c_str(), prompt.size(), 0);
        char buf[BUFFERSIZE] = {0};
        int n = read(sockCliente, buf, BUFFERSIZE - 1);
        if (n <= 0) return; // cliente desconectó
        buf[n] = '\0';
        std::string raw(buf);
        // Trim
        while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r')) raw.pop_back();
        if (raw == "CANCEL" || raw == "cancel" || raw == "Cancel") {
            send(sockCliente, "Partida cancelada por el usuario.\n", 33, 0);
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
        return;
    }

    // Generar movimiento de la máquina
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> dist(0,2);
    int r = dist(gen);
    std::string machine = (r==0?"piedra":(r==1?"papel":"tijera"));

    int res = decideRPS(move, machine);
    std::string resultado;
    if (res == 0) resultado = "Empate! Ambos eligieron " + move + "\n";
    else if (res == 1) resultado = "Ganaste! Tu " + move + " vence a " + machine + "\n";
    else resultado = "Perdiste. Tu " + move + " pierde contra " + machine + "\n";

    send(sockCliente, resultado.c_str(), resultado.size(), 0);
    // Mensaje de cierre de partida para volver al flujo de chat
    std::string endmsg = "Fin de la partida. Escribe /piedra_papel_tijera para volver a jugar o cualquier mensaje para el chat.\n";
    send(sockCliente, endmsg.c_str(), endmsg.size(), 0);
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

    // Registrar cliente
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.push_back({sockCliente, nombre, clientId});
    }

    // Enviar bienvenida local y notificar a la sala
    std::string bienvenida = "Bienvenido " + nombre + "\n";
    send(sockCliente, bienvenida.c_str(), bienvenida.size(), 0);
    broadcastMessage("Usuario " + nombre + " se ha conectado\n", sockCliente);

    // Bucle principal: recibir mensajes del cliente
    while (true) {
        char buf[BUFFERSIZE] = {0};
        int n = read(sockCliente, buf, BUFFERSIZE - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        std::string msg(buf);
        // Trim newline(s)
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();

        if (msg.empty()) continue;

        // Comandos que inician juegos
        if (msg == "/juego_trivia") {
            if (!triviaActive.load()) {
                std::thread t(triviaThread);
                t.detach();
            } else {
                send(sockCliente, "Ya hay una trivia en curso\n", 22, 0);
            }
            continue;
        }

        if (msg == "/piedra_papel_tijera") {
            // Iniciar flujo de Piedra-Papel-Tijera
            std::string prompt = "Elige modo: 1) vs Maquina 2) vs Jugador\n";
            send(sockCliente, prompt.c_str(), prompt.size(), 0);

            // Leer la elección del cliente
            char choiceBuf[BUFFERSIZE] = {0};
            int cn = read(sockCliente, choiceBuf, BUFFERSIZE - 1);
            if (cn <= 0) break;
            choiceBuf[cn] = '\0';
            std::string choice(choiceBuf);
            while (!choice.empty() && (choice.back() == '\n' || choice.back() == '\r')) choice.pop_back();

            if (choice == "1") {
                // vs máquina
                playRPSvsMachine(sockCliente, nombre);
                continue;
            } else if (choice == "2") {
                // vs jugador: emparejar
                std::shared_ptr<PvPGame> mygame;
                {
                    std::lock_guard<std::mutex> lock(waiting_mutex);
                    if (!waitingGame) {
                        // No hay nadie esperando: ser el primero
                        waitingGame = std::make_shared<PvPGame>();
                        waitingGame->player1Id = clientId;
                        waitingGame->player1Sock = sockCliente;
                        waitingGame->player1Name = nombre;
                        mygame = waitingGame;
                    } else {
                        // Hay alguien esperando: emparejar
                        mygame = waitingGame;
                        waitingGame = nullptr; // consumir la espera
                        mygame->player2Id = clientId;
                        mygame->player2Sock = sockCliente;
                        mygame->player2Name = nombre;
                    }
                }

                if (mygame->player2Id == -1) {
                    // Soy el primero: esperar a que llegue otro
                    send(sockCliente, "Esperando rival...\n", 20, 0);
                    std::unique_lock<std::mutex> lk(mygame->mtx);
                    mygame->cv.wait(lk, [&]{ return mygame->player2Id != -1; });
                } else {
                    // Soy el segundo: notificar al primero
                    {
                        std::lock_guard<std::mutex> lk(mygame->mtx);
                        mygame->cv.notify_all();
                    }
                }

                // En este punto ambos players deben estar en mygame
                // Ejecutar rondas hasta que uno no quiera seguir o haya desconexión
                bool gameOver = false;
                while (!gameOver) {
                    // Pida movimiento al correspondiente jugador
                    if (clientId == mygame->player1Id) {
                        std::string info = "Emparejado con " + mygame->player2Name + ". Envía 'piedra','papel' o 'tijera'\n";
                        send(sockCliente, info.c_str(), info.size(), 0);
                        // leer movimiento con hasta 3 intentos
                        const int maxMoveAttempts = 3;
                        int moveAttempts = 0;
                        std::string mv;
                        while (moveAttempts < maxMoveAttempts) {
                            char mb[BUFFERSIZE] = {0};
                            int rn = read(sockCliente, mb, BUFFERSIZE - 1);
                            if (rn <= 0) {
                                // desconexión: notificar adversario y terminar
                                send(mygame->player2Sock, "Rival desconectó. Partida cancelada\n", 34, 0);
                                gameOver = true;
                                break;
                            }
                            mb[rn] = '\0';
                            mv = normalizeMove(std::string(mb));
                            if (mv == "piedra" || mv == "papel" || mv == "tijera") break;
                            send(sockCliente, "Movimiento inválido. Intenta de nuevo.\n", 34, 0);
                            moveAttempts++;
                        }
                        if (gameOver) break;
                        if (!(mv == "piedra" || mv == "papel" || mv == "tijera")) {
                            send(sockCliente, "No se recibió un movimiento válido. Partida cancelada\n", 48, 0);
                            send(mygame->player2Sock, "Rival no envió movimiento válido. Partida cancelada\n", 48, 0);
                            break;
                        }
                        {
                            std::lock_guard<std::mutex> lk(mygame->mtx);
                            mygame->move1 = mv;
                            mygame->ready1 = true;
                        }
                        mygame->cv.notify_all();

                        // Esperar a que el otro envie su movimiento
                        std::unique_lock<std::mutex> lk2(mygame->mtx);
                        mygame->cv.wait(lk2, [&]{ return mygame->ready2 || gameOver; });
                        if (gameOver) break;

                        // Calcular resultado (jugador1 hace el cálculo)
                        if (!mygame->resultSent) {
                            int res = decideRPS(mygame->move1, mygame->move2);
                            std::string r1, r2;
                            if (res == 0) {
                                r1 = r2 = "Empate! Ambos eligieron " + mygame->move1 + "\n";
                            } else if (res == 1) {
                                r1 = "Ganaste! Tu " + mygame->move1 + " vence a " + mygame->move2 + "\n";
                                r2 = "Perdiste. Tu " + mygame->move2 + " pierde contra " + mygame->move1 + "\n";
                            } else {
                                r2 = "Ganaste! Tu " + mygame->move2 + " vence a " + mygame->move1 + "\n";
                                r1 = "Perdiste. Tu " + mygame->move1 + " pierde contra " + mygame->move2 + "\n";
                            }
                            send(mygame->player1Sock, r1.c_str(), r1.size(), 0);
                            send(mygame->player2Sock, r2.c_str(), r2.size(), 0);
                            mygame->resultSent = true;
                        }

                        // Preguntar a ambos si desean jugar otra ronda
                        send(mygame->player1Sock, "¿Jugar otra ronda? (si/no)\n", 27, 0);
                        send(mygame->player2Sock, "¿Jugar otra ronda? (si/no)\n", 27, 0);

                        // leer respuesta del jugador1
                        char rb1[BUFFERSIZE] = {0};
                        int r1n = read(mygame->player1Sock, rb1, BUFFERSIZE - 1);
                        if (r1n <= 0) {
                            send(mygame->player2Sock, "Rival desconectó. Partida finalizada\n", 34, 0);
                            gameOver = true;
                        } else {
                            rb1[r1n] = '\0';
                            std::string ans1(rb1);
                            while (!ans1.empty() && (ans1.back()=='\n'||ans1.back()=='\r')) ans1.pop_back();
                            std::transform(ans1.begin(), ans1.end(), ans1.begin(), ::tolower);
                            mygame->replay1 = (ans1 == "si" || ans1 == "s" || ans1 == "yes" || ans1 == "y");
                            mygame->hasReplay1 = true;
                        }

                        // esperar respuesta del jugador2 (su hilo hará la lectura, pero si jugador2 ya leyó, check)
                        std::unique_lock<std::mutex> lk3(mygame->mtx);
                        mygame->cv.notify_all();
                        // esperar hasta que both have hasReplay or gameOver
                        mygame->cv.wait_for(lk3, std::chrono::seconds(15), [&]{ return mygame->hasReplay2 || gameOver; });

                        if (gameOver) break;

                        // Si jugador2 no respondió a tiempo, considerar no
                        if (!mygame->hasReplay2) mygame->replay2 = false;

                        if (mygame->replay1 && mygame->replay2) {
                            // reiniciar flags para nueva ronda
                            mygame->move1.clear(); mygame->move2.clear();
                            mygame->ready1 = mygame->ready2 = false;
                            mygame->resultSent = false;
                            mygame->hasReplay1 = mygame->hasReplay2 = false;
                            mygame->replay1 = mygame->replay2 = false;
                            // continuar a siguiente ronda
                            continue;
                        } else {
                            // notificar fin de la partida
                            send(mygame->player1Sock, "Partida finalizada. Volviendo al chat.\n", 37, 0);
                            send(mygame->player2Sock, "Partida finalizada. Volviendo al chat.\n", 37, 0);
                            gameOver = true;
                            break;
                        }
                    } else if (clientId == mygame->player2Id) {
                        // jugador 2 (similar flujo)
                        std::string info = "Emparejado con " + mygame->player1Name + ". Envía 'piedra','papel' o 'tijera'\n";
                        send(sockCliente, info.c_str(), info.size(), 0);
                        // notificar posible waiter
                        {
                            std::lock_guard<std::mutex> lk(mygame->mtx);
                            mygame->cv.notify_all();
                        }
                        const int maxMoveAttempts = 3;
                        int moveAttempts = 0;
                        std::string mv;
                        while (moveAttempts < maxMoveAttempts) {
                            char mb[BUFFERSIZE] = {0};
                            int rn = read(sockCliente, mb, BUFFERSIZE - 1);
                            if (rn <= 0) {
                                send(mygame->player1Sock, "Rival desconectó. Partida cancelada\n", 34, 0);
                                gameOver = true;
                                break;
                            }
                            mb[rn] = '\0';
                            mv = normalizeMove(std::string(mb));
                            if (mv == "piedra" || mv == "papel" || mv == "tijera") break;
                            send(sockCliente, "Movimiento inválido. Intenta de nuevo.\n", 34, 0);
                            moveAttempts++;
                        }
                        if (gameOver) break;
                        if (!(mv == "piedra" || mv == "papel" || mv == "tijera")) {
                            send(sockCliente, "No se recibió un movimiento válido. Partida cancelada\n", 48, 0);
                            send(mygame->player1Sock, "Rival no envió movimiento válido. Partida cancelada\n", 48, 0);
                            break;
                        }
                        {
                            std::lock_guard<std::mutex> lk(mygame->mtx);
                            mygame->move2 = mv;
                            mygame->ready2 = true;
                        }
                        mygame->cv.notify_all();

                        // Esperar a que el otro envie su movimiento
                        std::unique_lock<std::mutex> lk2(mygame->mtx);
                        mygame->cv.wait(lk2, [&]{ return mygame->ready1 || gameOver; });
                        if (gameOver) break;

                        // Si jugador2 no fue quien calculó resultado, puede observar y luego responder
                        if (!mygame->resultSent) {
                            int res = decideRPS(mygame->move1, mygame->move2);
                            std::string r1, r2;
                            if (res == 0) {
                                r1 = r2 = "Empate! Ambos eligieron " + mygame->move1 + "\n";
                            } else if (res == 1) {
                                r1 = "Ganaste! Tu " + mygame->move1 + " vence a " + mygame->move2 + "\n";
                                r2 = "Perdiste. Tu " + mygame->move2 + " pierde contra " + mygame->move1 + "\n";
                            } else {
                                r2 = "Ganaste! Tu " + mygame->move2 + " vence a " + mygame->move1 + "\n";
                                r1 = "Perdiste. Tu " + mygame->move1 + " pierde contra " + mygame->move2 + "\n";
                            }
                            send(mygame->player1Sock, r1.c_str(), r1.size(), 0);
                            send(mygame->player2Sock, r2.c_str(), r2.size(), 0);
                            mygame->resultSent = true;
                        }

                        // Preguntar a ambos si desean jugar otra ronda (cada hilo lee su propia respuesta)
                        // leer propia respuesta
                        send(mygame->player1Sock, "¿Jugar otra ronda? (si/no)\n", 27, 0);
                        send(mygame->player2Sock, "¿Jugar otra ronda? (si/no)\n", 27, 0);

                        char rb2[BUFFERSIZE] = {0};
                        int r2n = read(mygame->player2Sock, rb2, BUFFERSIZE - 1);
                        if (r2n <= 0) {
                            send(mygame->player1Sock, "Rival desconectó. Partida finalizada\n", 34, 0);
                            gameOver = true;
                        } else {
                            rb2[r2n] = '\0';
                            std::string ans2(rb2);
                            while (!ans2.empty() && (ans2.back()=='\n'||ans2.back()=='\r')) ans2.pop_back();
                            std::transform(ans2.begin(), ans2.end(), ans2.begin(), ::tolower);
                            mygame->replay2 = (ans2 == "si" || ans2 == "s" || ans2 == "yes" || ans2 == "y");
                            mygame->hasReplay2 = true;
                        }

                        // notificar al otro hilo que ya respondió
                        std::unique_lock<std::mutex> lk3(mygame->mtx);
                        mygame->cv.notify_all();
                        mygame->cv.wait_for(lk3, std::chrono::seconds(15), [&]{ return mygame->hasReplay1 || gameOver; });

                        if (gameOver) break;
                        if (!mygame->hasReplay1) mygame->replay1 = false;

                        if (mygame->replay1 && mygame->replay2) {
                            // reiniciar
                            mygame->move1.clear(); mygame->move2.clear();
                            mygame->ready1 = mygame->ready2 = false;
                            mygame->resultSent = false;
                            mygame->hasReplay1 = mygame->hasReplay2 = false;
                            mygame->replay1 = mygame->replay2 = false;
                            continue;
                        } else {
                            send(mygame->player1Sock, "Partida finalizada. Volviendo al chat.\n", 37, 0);
                            send(mygame->player2Sock, "Partida finalizada. Volviendo al chat.\n", 37, 0);
                            gameOver = true;
                            break;
                        }
                    } else {
                        // no corresponde
                        send(sockCliente, "Error interno de emparejamiento\n", 30, 0);
                        gameOver = true;
                        break;
                    }
                }
                // marcar fin de manejo PvP y continuar el bucle de chat
                continue;
            } else {
                send(sockCliente, "Opción inválida. Cancelando piedra_papel_tijera\n", 45, 0);
                continue;
            }
        }

        // Si hay una pregunta activa para la trivia, chequear respuestas
        if (triviaActive.load() && questionActive.load()) {
            std::string lower = msg;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            std::lock_guard<std::mutex> lock(trivia_mutex);
            if (questionActive.load() && !answered.load() && lower == currentAnswer) {
                // Otorgar punto
                triviaScores[clientId]++;
                answered = true;
                std::string quien = getClientNameById(clientId);
                broadcastMessage("Respuesta correcta de: " + quien + "\n");
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
// end of file