#include "./utils/chat.pb.h" // Include the generated protobuf header
#include "./utils/message.h"
#include "./utils/constants.h"
#include <iostream>
#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <errno.h> // For errno, EPIPE
#include <cstring> // For strerror

std::mutex clients_mutex;
std::map<int, std::string> client_sessions;  // Maps client socket to username
std::map<std::string, User> user_details;    // Maps username to user struct
std::atomic<bool> running(true);
int server_fd;

void handle_registration(const chat::Request& request, int client_sock) {
    auto& reg_request = request.register_user();
    std::string username = reg_request.username();
    std::lock_guard<std::mutex> lock(clients_mutex);

    // Obtener IP del cliente
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(client_sock, (struct sockaddr*)&addr, &addr_len);
    std::string client_ip = inet_ntoa(addr.sin_addr);

    if (user_details.find(username) != user_details.end()) {
        send_error_response(client_sock, "Username is already taken", chat::StatusCode::BAD_REQUEST);
        return;
    }

    User user{username, client_ip, chat::UserStatus::ONLINE, std::chrono::system_clock::now()};
    user_details[username] = user;
    client_sessions[client_sock] = username;

    send_success_response(client_sock, "User registered successfully", chat::Operation::REGISTER_USER);
}

void handle_send_message(const chat::Request& request, int client_sock) {
    const auto& msg_request = request.send_message();
    std::string recipient = msg_request.recipient();
    std::string message_content = msg_request.content();

    std::lock_guard<std::mutex> lock(clients_mutex);
    if (recipient.empty()) {  // Broadcast message
        for (const auto& session : client_sessions) {
            if (session.first != client_sock) {
                send_message_to_client(session.first, message_content);
            }
        }
    } else {  // Direct message
        int recipient_sock = -1;
        for (const auto& session : client_sessions) {
            if (user_details[session.second].username == recipient) {
                recipient_sock = session.first;
                break;
            }
        }
        if (recipient_sock != -1) {
            send_message_to_client(recipient_sock, message_content);
        } else {
            send_error_response(client_sock, "Recipient not found", chat::StatusCode::BAD_REQUEST);
        }
    }
}

void update_status(const chat::Request& request, int client_sock) {
    const auto& status_request = request.update_status();
    std::string username = client_sessions[client_sock];
    chat::UserStatus new_status = status_request.new_status();

    std::lock_guard<std::mutex> lock(clients_mutex);
    if (user_details.find(username) != user_details.end()) {
        user_details[username].status = new_status;
        send_success_response(client_sock, "Status updated successfully", chat::Operation::UPDATE_STATUS);
    } else {
        send_error_response(client_sock, "User not found", chat::StatusCode::BAD_REQUEST);
    }
}

void handle_get_users(const chat::Request& request, int client_sock) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    chat::UserListResponse user_list;

    for (const auto& user_pair : user_details) {
        chat::User* user_proto = user_list.add_users();
        user_proto->set_username(user_pair.second.username);
        user_proto->set_status(user_pair.second.status);
    }

    send_user_list_response(client_sock, user_list, "User list fetched successfully");
}

void send_success_response(int client_sock, const std::string& message, chat::Operation operation) {
    chat::Response response;
    response.set_message(message);
    response.set_status_code(chat::StatusCode::OK);
    response.set_operation(operation);
    std::string serialized_response = response.SerializeAsString();
    send(client_sock, serialized_response.data(), serialized_response.size(), 0);
}

void send_error_response(int client_sock, const std::string& error_message, chat::StatusCode status) {
    chat::Response response;
    response.set_message(error_message);
    response.set_status_code(status);
    std::string serialized_response = response.SerializeAsString();
    send(client_sock, serialized_response.data(), serialized_response.size(), 0);
}

void send_user_list_response(int client_sock, const chat::UserListResponse& user_list, const std::string& message) {
    chat::Response response;
    response.set_message(message);
    response.set_status_code(chat::StatusCode::OK);
    response.mutable_user_list()->CopyFrom(user_list);
    std::string serialized_response = response.SerializeAsString();
    send(client_sock, serialized_response.data(), serialized_response.size(), 0);
}

void send_message_to_client(int client_sock, const std::string& message) {
    chat::Response response;
    response.set_message(message);
    response.set_status_code(chat::StatusCode::OK);
    std::string serialized_response = response.SerializeAsString();
    send(client_sock, serialized_response.data(), serialized_response.size(), 0);
}


struct User {
    std::string username;
    std::string ip;
    chat::UserStatus status;
    std::chrono::system_clock::time_point last_active;
};

void handle_client(int client_sock) {
    while (running) {
        chat::Request request;
        if (RPM(client_sock, request) == false) {
            std::cerr << "Failed to read message from client. Closing connection." << std::endl;
            // Handle closing
            break;
        }

        std::lock_guard<std::mutex> lock(clients_mutex);
        auto &user = user_details[client_sessions[client_sock]];

        // Update user activity time
        user.last_active = std::chrono::system_clock::now();

        switch (request.operation()) {
        case chat::Operation::REGISTER_USER:
            handle_registration(request, client_sock);
            break;
        case chat::Operation::SEND_MESSAGE:
            handle_send_message(request, client_sock);
            break;
        case chat::Operation::UPDATE_STATUS:
            update_status(request, client_sock);
            break;
        case chat::Operation::GET_USERS:
            handle_get_users(request, client_sock);
            break;
        case chat::Operation::UNREGISTER_USER:
            unregister_user(client_sock);
            break;
        default:
            std::cerr << "Unknown request type." << std::endl;
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    setup_server(argc, argv); // Setup listening socket, bind, etc.
    std::cout << "Server listening on port " << std::stoi(argv[1]) << std::endl;

    while (running) {
        int client_sock = accept(server_fd, NULL, NULL);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }
        std::thread client_thread(handle_client, client_sock);
        client_thread.detach();
    }
    // Cleanup
    close(server_fd);
    return 0;
}


