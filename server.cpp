/*
*   Autores:
*   Descripción:
*           Programa que funge como servidor para el manejo de conexiones por medio de sockets
*           de forma que se puedan manejar mensajes entre usuarios y otras funcionalidades.
*/


#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "chat.pb.h"  // Asegúrate de incluir tu archivo generado por protobuf

std::map<std::string, int> client_fds;
std::mutex clients_mutex;

void handle_client(int client_fd) {
    char buffer[1024];
    while (true) {
        int read_size = recv(client_fd, buffer, 1024, 0);
        if (read_size == 0) {
            // Handle client disconnection
            break;
        } else if (read_size < 0) {
            std::cerr << "Error reading from socket" << std::endl;
            break;
        }

        chat::Request request;
        request.ParseFromArray(buffer, read_size);

        switch (request.operation()) {
            case chat::REGISTER_USER:
                // Handle user registration
                break;
            case chat::SEND_MESSAGE:
                // Handle sending messages
                break;
            case chat::UPDATE_STATUS:
                // Handle status updates
                break;
            case chat::GET_USERS:
                // Handle fetching users
                break;
            default:
                std::cerr << "Unsupported operation received" << std::endl;
                break;
        }
    }

    close(client_fd);
    // Remove client from map
    clients_mutex.lock();
    // Assume username is known here
    client_fds.erase("username");
    clients_mutex.unlock();
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <port>" << std::endl;
        return 1;
    }

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Cannot open socket" << std::endl;
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    server_addr.sin_port = htons(atoi(argv[2]));

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        return 1;
    }

    listen(server_fd, 5);

    while (true) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            std::cerr << "Accept failed" << std::endl;
            continue;
        }

        std::thread client_thread(handle_client, client_fd);
        client_thread.detach();
    }

    return 0;
}
