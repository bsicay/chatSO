#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <mutex>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "chat.pb.h"  // Asegúrate que la ruta del include es correcta

void receiveMessages(int sock) {
    char buffer[1024];
    while (true) {
        int bytes_received = recv(sock, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            std::cerr << "Disconnected or error receiving data." << std::endl;
            break;
        }
        chat::Response response;
        response.ParseFromArray(buffer, bytes_received);
        std::cout << "Received: " << response.message() << std::endl;
    }
}

void send_protobuf_message(int sock, const chat::Request& request) {
    std::string output;
    request.SerializeToString(&output);
    send(sock, output.data(), output.size(), 0);
}

void handle_commands(int sock) {
    std::string input;
    while (true) {
        std::getline(std::cin, input);
        if (input == "exit") break;

        std::istringstream iss(input);
        std::vector<std::string> tokens{std::istream_iterator<std::string>{iss},
                                        std::istream_iterator<std::string>{}};

        if (tokens.empty()) continue;

        if (tokens[0] == "send" && tokens.size() > 1) {
            chat::Request request;
            request.set_operation(chat::SEND_MESSAGE);
            chat::SendMessageRequest* message = request.mutable_send_message();
            message->set_content(input.substr(5));  // Skip "send "
            send_protobuf_message(sock, request);
        } else if (tokens[0] == "sendto" && tokens.size() > 2) {
            std::string recipient = tokens[1];
            std::string message = input.substr(7 + recipient.size());  // Skip "sendto {recipient} "
            chat::Request request;
            request.set_operation(chat::SEND_MESSAGE);
            chat::SendMessageRequest* msg = request.mutable_send_message();
            msg->set_recipient(recipient);
            msg->set_content(message);
            send_protobuf_message(sock, request);
        } else if (tokens[0] == "status" && tokens.size() == 2) {
            chat::Request request;
            request.set_operation(chat::UPDATE_STATUS);
            chat::UpdateStatusRequest* status_req = request.mutable_update_status();
            if (tokens[1] == "ONLINE") {
                status_req->set_new_status(chat::UserStatus::ONLINE);
            } else if (tokens[1] == "BUSY") {
                status_req->set_new_status(chat::UserStatus::BUSY);
            } else if (tokens[1] == "OFFLINE") {
                status_req->set_new_status(chat::UserStatus::OFFLINE);
            }
            send_protobuf_message(sock, request);
        } else {
            std::cout << "Unknown command or incorrect usage\n";
        }
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

    // Ejecuta el manejo de comandos en el thread principal
    handle_commands(sock);

    close(sock);
    return 0;
}