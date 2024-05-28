/*
*   Autores:
*   Descripción:
*           Programa que funge como servidor para el manejo de conexiones por medio de sockets
*           de forma que se puedan manejar mensajes entre usuarios y otras funcionalidades.
*/


#include <iostream>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "./messageUtil/chat.pb.h" // Include the generated protobuf header
#include "./messageUtil/message.h"
#include <iostream> // For std::cerr
#include <vector>   // For std::vector
#include <cstring>  // For memcpy
#include <unistd.h> // For ssize_t
#include <cerrno>   // For errno

// Declaración de variables globales
volatile sig_atomic_t running = 1; // Variable para mantener el servidor en ejecución
int server_fd; // Descriptor del socket del servidor

// Estructuras de datos para manejar usuarios y sesiones
std::map<int, std::string> client_sessions; // Mapa de descriptores de socket a nombres de usuario
std::map<std::string, std::string> user_details; // Mapa de nombres de usuario a direcciones IP

std::mutex clients_mutex; // Mutex para controlar el acceso a las estructuras de datos compartidas


void signalHandler(int signum) {
    running = 0; // Establecer running a 0 cerrará el bucle principal
    close(server_fd); // Cierra el socket del servidor
    std::cout << "Server shutting down..." << std::endl;
}

void handle_client(int client_sock); // Predeclaración de handle_client


/**
 * Función para manejar el registro de un usuario
 */
bool handle_registration(const chat::Request &request, int client_sock) {
    auto user_request = request.register_user();
    const auto &username = user_request.username();

    std::lock_guard<std::mutex> lock(clients_mutex);

    chat::Response response;
    response.set_operation(chat::Operation::REGISTER_USER);
    bool registered = false;

    // Obtener la dirección IP del cliente
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    int res = getpeername(client_sock, (struct sockaddr *)&addr, &addr_size);
    std::string ip_str;
    if (res != -1) {
        ip_str = inet_ntoa(addr.sin_addr);
    } else {
        response.set_message("Unable to retrieve IP address.");
        response.set_status_code(chat::StatusCode::BAD_REQUEST);
        send_response(client_sock, response); // Esta función debe ser implementada para enviar la respuesta
        return false;
    }

    // Verificar si el nombre de usuario ya está registrado con cualquier dirección IP
    std::cout << " user " << user_details << std::endl;
    if (user_details.find(username) != user_details.end()) {
        response.set_message("Username is already taken.");
        response.set_status_code(chat::StatusCode::BAD_REQUEST);
        send_response(client_sock, response);
        return false;
    }

    // Registrar al usuario
    user_details[username] = ip_str;
    client_sessions[client_sock] = username;

    response.set_message("User registered successfully.");
    response.set_status_code(chat::StatusCode::OK);
    send_response(client_sock, response);
    return true;
}


/**
 * Función para manejar la conexión de un cliente
 */
void handle_client(int client_sock) {
    bool running = true;
    while (running) {
        chat::Request request;
        if (!receive_request(client_sock, request)) { // Función para recibir una solicitud
            std::cerr << "Failed to read message from client. Closing connection." << std::endl;
            break;
        }

        switch (request.operation()) {
            case chat::Operation::REGISTER_USER:
                if (!handle_registration(request, client_sock)) {
                    std::cerr << "Registration failed for client." << std::endl;
                }
                break;
            // Agregar otros casos para diferentes operaciones
            default:
                break;
        }
    }

    // Cerrar la conexión y limpiar los datos de sesión
    close(client_sock);
    std::lock_guard<std::mutex> lock(clients_mutex);
    if (client_sessions.count(client_sock)) {
        std::string username = client_sessions[client_sock];
        user_details.erase(username);
        client_sessions.erase(client_sock);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <port> <server_name>\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    std::string server_name = argv[2];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket creation failed");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        return 1;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        return 1;
    }

    std::cout << server_name << " listening on port " << port << std::endl;
    std::cout << "Write 'exit' to terminate the server." << std::endl;

    // Configuración del manejador de señales
    signal(SIGINT, signalHandler);

    while (running) {
        int client_sock = accept(server_fd, NULL, NULL);
        if (client_sock < 0) {
            if (!running) break; // Salir si el servidor está cerrando
            perror("Accept failed");
            continue;
        }

        std::thread client_thread(handle_client, client_sock);
        client_thread.detach();
    }

    // Limpiar
    close(server_fd);
    std::cout << "Server closed successfully." << std::endl;
    return 0;
}
