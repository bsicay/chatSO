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
#include "./messageUtil/constants.h"


// Declaración de variables globales
volatile sig_atomic_t running = 1; // Variable para mantener el servidor en ejecución
int server_fd; // Descriptor del socket del servidor

// Estructuras de datos para manejar usuarios y sesiones
std::map<int, std::string> client_sessions; // Mapa de descriptores de socket a nombres de usuario
std::map<std::string, std::string> user_details; // Mapa de nombres de usuario a direcciones IP
std::map<std::string, chat::UserStatus> user_status;                      // Maps username to status
std::mutex clients_mutex;  // Mutex para controlar el acceso a las estructuras de datos compartidas
std::map<std::string, std::chrono::steady_clock::time_point> last_active;
std::mutex activity_mutex;


void update_user_status_and_time(int client_sock, const chat::UpdateStatusRequest &status_request)
{
  std::lock_guard<std::mutex> lock(clients_mutex);
  user_status[client_sessions[client_sock]] = status_request.new_status();
  // last_active[client_sessions[client_sock]] = std::chrono::system_clock::now(); TODO: Move this to any action retrieved on the general handling
}

/**
 * UPDATE_STATUS main function
 */
void update_status(const chat::Request &request, int client_sock, chat::Operation operation)
{
  auto status_request = request.update_status();
  update_user_status_and_time(client_sock, status_request);

  chat::Response response;
  response.set_operation(operation);
  response.set_message("Status updated successfully."); // Consider replacing this with a constant or a configuration value
  response.set_status_code(chat::StatusCode::OK);
  send_response(client_sock, response);
}


void signalHandler(int signum)
{
  std::cout << "\nInterrupt signal (" << signum << ") received.\n";

  // Close the server socket
  close(server_fd);
  running = false;

  std::cout << "Server terminated due to signal." << std::endl;
  exit(signum);
}
void send_message_to_client(int client_sock, const chat::IncomingMessageResponse& message_response, chat::MessageType type) {
    chat::Response response;
    response.set_operation(chat::INCOMING_MESSAGE);
    response.set_status_code(chat::StatusCode::OK);
    auto* incoming_message = response.mutable_incoming_message();
    incoming_message->CopyFrom(message_response);
    incoming_message->set_type(type);
    std::string response_str;
    response.SerializeToString(&response_str);
    send(client_sock, response_str.c_str(), response_str.size(), 0);
}


// Función para actualizar la inactividad de los usuarios
void update_inactivity() {
    std::lock_guard<std::mutex> lock(activity_mutex);
    for (auto& user : last_active) {
        if (std::chrono::duration_cast<std::chrono::minutes>(std::chrono::steady_clock::now() - user.second).count() > AUTO_OFFLINE_SECONDS) {
            // Aquí se podría establecer el estado a INACTIVO
        }
    }
}

void send_broadcast_message(const chat::IncomingMessageResponse &message_response, int client_sock) {
    std::lock_guard<std::mutex> lock(clients_mutex);

    std::cout << "Broadcasting message from client socket " << client_sock << std::endl;

    for (const auto &session : client_sessions) {
        if (session.first != client_sock) { 
            chat::Response response_to_recipient;
            response_to_recipient.set_operation(chat::Operation::INCOMING_MESSAGE);
            response_to_recipient.set_message("Broadcast message incoming.");
            response_to_recipient.set_status_code(chat::StatusCode::OK);
            response_to_recipient.mutable_incoming_message()->CopyFrom(message_response);
            send_response(session.first, response_to_recipient);
        }
    }

    chat::Response response_to_sender;
    response_to_sender.set_message("Broadcast message sent successfully.");
    response_to_sender.set_status_code(chat::StatusCode::OK);
    send_response(client_sock, response_to_sender);
}


void send_direct_message(chat::Response &response_to_sender, chat::Response &response_to_recipient, chat::IncomingMessageResponse &message_response, int client_sock, int recipient_sock)
{
  message_response.set_type(chat::MessageType::DIRECT);
  response_to_recipient.set_message("Message incoming.");
  response_to_recipient.set_status_code(chat::StatusCode::OK);
  response_to_recipient.mutable_incoming_message()->CopyFrom(message_response);
  send_response(recipient_sock, response_to_recipient);

  response_to_sender.set_message("Message sent successfully.");
  response_to_sender.set_status_code(chat::StatusCode::OK);
  send_response(client_sock, response_to_sender);
}



void handle_client(int client_sock); // Predeclaración de handle_client

int find_recipient_socket(const std::string &recipient)
{
  int recipient_sock = -1;
  for (auto &session : client_sessions)
  {
    if (session.second == recipient)
    {
      recipient_sock = session.first;
      break;
    }
  }
  return recipient_sock;
}


chat::IncomingMessageResponse prepare_message_response(const chat::Request &request, int client_sock)
{
  auto message = request.send_message();
  chat::IncomingMessageResponse message_response;
  std::lock_guard<std::mutex> lock(clients_mutex); // Lock the clients mutex, for thread safety
  message_response.set_sender(client_sessions[client_sock]);
  message_response.set_content(message.content());
  return message_response;
}

void handle_send_message(const chat::Request &request, int client_sock, chat::Operation operation) {
    std::cout << "Handling send message from client socket " << client_sock << std::endl;

    chat::Response response_to_sender;
    response_to_sender.set_operation(operation);

    chat::Response response_to_recipient;
    response_to_recipient.set_operation(chat::Operation::INCOMING_MESSAGE);
    chat::IncomingMessageResponse message_response = prepare_message_response(request, client_sock);

    if (request.send_message().recipient().empty()) {
        std::cout << "Sending broadcast message from client socket " << client_sock << std::endl;
        send_broadcast_message(message_response, client_sock);
    } else {
        std::cout << "Sending direct message to " << request.send_message().recipient() << " from client socket " << client_sock << std::endl;
        int recipient_sock = find_recipient_socket(request.send_message().recipient());
        if (recipient_sock != -1) {
            send_direct_message(response_to_sender, response_to_recipient, message_response, client_sock, recipient_sock);
        } else {
            std::cerr << "Recipient not found for direct message from socket " << client_sock << std::endl;
            response_to_sender.set_message("Recipient not found.");
            response_to_sender.set_status_code(chat::StatusCode::BAD_REQUEST);
            send_response(client_sock, response_to_sender);
        }
    }
}


/**
 * Función para manejar el registro de un usuario
 */
bool handle_registration(const chat::Request &request, int client_sock) {
    auto user_request = request.register_user();
    const auto &username = user_request.username();

    std::lock_guard<std::mutex> lock(clients_mutex);

    std::cout << "Attempting to register username: " << username << std::endl;

    // Log current user details
    std::cout << "Current registered users:\n";
    for (const auto& user : user_details) {
        std::cout << "Username: " << user.first << ", IP: " << user.second << std::endl;
    }
    chat::Response response;
    response.set_operation(chat::Operation::REGISTER_USER);

    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    int res = getpeername(client_sock, (struct sockaddr *)&addr, &addr_size);
    std::string ip_str;
    if (res != -1) {
        ip_str = inet_ntoa(addr.sin_addr);
        std::cout << "IP Address Retrieved: " << ip_str << std::endl;
    } else {
        response.set_message("Unable to retrieve IP address.");
        response.set_status_code(chat::StatusCode::BAD_REQUEST);
        send_response(client_sock, response);
        return false;
    }

    if (user_details.find(username) != user_details.end()) {
        std::cout << "Username already taken." << std::endl;
        response.set_message("Username is already taken.");
        response.set_status_code(chat::StatusCode::BAD_REQUEST);
        send_response(client_sock, response);
        return false;
    }

    user_details[username] = ip_str;
    client_sessions[client_sock] = username;

    std::cout << "User registered successfully: " << username << std::endl;

    response.set_message("User registered successfully.");
    response.set_status_code(chat::StatusCode::OK);
    send_response(client_sock, response);
    return true;
}

void add_user_to_response(const std::pair<std::string, std::string> &user, chat::UserListResponse &response)
{
  chat::User *user_proto = response.add_users();
  // Username concatenated string: <username> (<ip>)
  user_proto->set_username(user.first + " (" + user.second + ")");
  user_proto->set_status(user_status[user.first]);
}

/**
 * GET_USERS 
 */
void handle_get_users(const chat::Request &request, int client_sock, chat::Operation operation)
{
  std::lock_guard<std::mutex> lock(clients_mutex);

  chat::Response response;
  response.set_operation(operation);

  chat::UserListResponse user_list_response;

  if (request.get_users().username().empty())
  {
    // Return all connected users
    user_list_response.set_type(chat::UserListType::ALL);
    for (const auto &user : user_details)
    {
      add_user_to_response(user, user_list_response);
    }
    std::cout << "All users returned successfully." << std::endl;
    response.set_message("All users returned successfully.");
    response.set_status_code(chat::StatusCode::OK);
  }
  else
  {
    user_list_response.set_type(chat::UserListType::SINGLE);
    // Return only the specified user
    auto it = user_details.find(request.get_users().username());
    if (it != user_details.end())
    {
      add_user_to_response(*it, user_list_response);
      std::cout << "User returned successfully: " << it->first << std::endl;
      response.set_message("User returned successfully.");
      response.set_status_code(chat::StatusCode::OK);
    }
    else
    {
      std::cout << "User not found: " << request.get_users().username() << std::endl;
      response.set_message("User not found.");
      response.set_status_code(chat::StatusCode::BAD_REQUEST);
    }
  }

  // Copy the user list to the response
  response.mutable_user_list()->CopyFrom(user_list_response);
  // Send the complete response
  send_response(client_sock, response);
}

void unregister_user(int client_sock, bool forced = false)
{
  std::lock_guard<std::mutex> lock(clients_mutex);
  chat::Response response;

  if (client_sessions.find(client_sock) != client_sessions.end())
  {
    std::string username = client_sessions[client_sock];

    // Erase user data from maps
    client_sessions.erase(client_sock);

    user_details.erase(username);

    user_status.erase(username);

    last_active.erase(username);

    // Prepare a response message
    response.set_operation(chat::Operation::UNREGISTER_USER);
    response.set_message("User unregistered successfully.");
    response.set_status_code(chat::StatusCode::OK);
  }
  else
  {
    // User not found or already unregistered, send error response
    response.set_message("User not found or already unregistered.");
    response.set_status_code(chat::StatusCode::BAD_REQUEST);
  }

  if (!forced)
  {
    send_response(client_sock, response);
  }
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
            case chat::Operation::SEND_MESSAGE:
                 handle_send_message(request, client_sock, chat::Operation::SEND_MESSAGE);
                break;
            case chat::Operation::UPDATE_STATUS:
                 update_status(request, client_sock, chat::Operation::UPDATE_STATUS);
                break;
            case chat::Operation::GET_USERS:
                 handle_get_users(request, client_sock, chat::Operation::GET_USERS);
                 break;
            case chat::Operation::UNREGISTER_USER:
                unregister_user(client_sock);
                 break;
            default:
                chat::Response response;
                response.set_message("request type DESCONOCIDO.");
                response.set_status_code(chat::StatusCode::BAD_REQUEST);
                send_response(client_sock, response);
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
