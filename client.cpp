#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "./messageUtil/chat.pb.h"

void receiveMessages(int sock) {
    char buffer[1024];
    while (true) {
        int bytes_received = recv(sock, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            std::cerr << "Disconnected or error receiving data." << std::endl;
            break;
        }
        // Deserializar y procesar el mensaje
        chat::Response response;
        response.ParseFromArray(buffer, bytes_received);
        std::cout << "Received: " << response.message() << std::endl;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <username> <IP server> <port>\n";
        return 1;
    }
    
    std::string username = argv[1];
    std::string server_ip = argv[2];
    int port = std::stoi(argv[3]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        perror("Connection failed");
        return 1;
    }

    // Enviar solicitud de registro
    chat::Request request;
    request.set_operation(chat::REGISTER_USER);
    chat::NewUserRequest* newUser = new chat::NewUserRequest();
    newUser->set_username(username);
    request.set_allocated_register_user(newUser);

    std::string request_str;
    request.SerializeToString(&request_str);
    send(sock, request_str.c_str(), request_str.size(), 0);

    std::thread receiverThread(receiveMessages, sock);
    receiverThread.detach();

    // Interfaz de usuario
    std::string input;
    while (true) {
        std::getline(std::cin, input);
        if (input == "exit") {
            break;
        }
        // Envío de mensajes, cambio de estado, etc.
    }

    close(sock);
    return 0;
}
