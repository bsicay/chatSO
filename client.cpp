#include "./utils/chat.pb.h" // Include the generated protobuf header
#include "./utils/message.h"
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <sstream>

#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN "\x1b[36m"
#define RESET "\x1b[0m"

std::atomic<bool> running{true};
std::mutex cout_mutex;
std::mutex send_mutex;

void sendMessage(int sock, const chat::Request& request) {
    std::lock_guard<std::mutex> lock(send_mutex);
    std::string serialized_request = request.SerializeAsString();
    send(sock, serialized_request.data(), serialized_request.size(), 0);
}

void handleServerResponse(int sock) {
    while (running) {
        chat::Response response;
        if (RPM(sock, response)) { // Assuming RPM is a function to read protobuf messages
            std::lock_guard<std::mutex> lock(cout_mutex);
            processResponse(response);
        } else {
            std::cout << RED "Failed to receive message or server closed the connection" RESET << std::endl;
            running = false;
            break;
        }
    }
}

void processResponse(const chat::Response& response) {
    std::string message;
    if (response.status_code() != chat::StatusCode::OK) {
        message = RED "Server error: " + response.message() + RESET;
    } else {
        message = GREEN "Success: " + response.message() + RESET;
    }
    std::cout << message << std::endl;
}

int setupConnection(const std::string& server_ip, int server_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation error \n";
        return -1;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported \n";
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection Failed \n";
        return -1;
    }
    return sock;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <server IP> <server port> <username>\n";
        return 1;
    }

    std::string server_ip = argv[1];
    int server_port = std::stoi(argv[2]);
    std::string username = argv[3];

    int sock = setupConnection(server_ip, server_port);
    if (sock < 0) {
        return -1; // Connection failed
    }

    // Register user
    chat::Request registration_request;
    registration_request.set_operation(chat::Operation::REGISTER_USER);
    registration_request.mutable_register_user()->set_username(username);
    sendMessage(sock, registration_request);

    std::thread responseHandler(handleServerResponse, sock);
    responseHandler.detach();

    std::string input;
    while (getline(std::cin, input) && running) {
        chat::Request request;
        // parse input to fill request
        // sendMessage(sock, request);
    }

    close(sock);
    std::cout << "Exiting..." << std::endl;
    return 0;
}

