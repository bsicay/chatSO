/*
*   Autores:
*   Descripción:
*           Programa que funge como servidor para el manejo de conexiones por medio de sockets
*           de forma que se puedan manejar mensajes entre usuarios y otras funcionalidades.
*/

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
std::map<int, std::string> client_sessions;                               // Maps client socket to username
std::map<std::string, std::string> user_details;                          // Maps username to IP address
std::map<std::string, chat::UserStatus> user_status;                      // Maps username to status
std::map<std::string, std::chrono::system_clock::time_point> last_active; // User activity tracking TODO: Auto Status Modification

std::atomic<bool> running(true);
int server_fd;
/**
 * REGISTER_USER main function
 */
bool handle_registration(const chat::Request &request, int client_sock, chat::Operation operation)
{
  auto user_request = request.register_user();
  const auto &username = user_request.username();

  std::lock_guard<std::mutex> lock(clients_mutex);

  chat::Response response;
  response.set_operation(operation);
  bool registered = false;

  // Get client IP
  struct sockaddr_in addr;
  socklen_t addr_size = sizeof(struct sockaddr_in);
  int res = getpeername(client_sock, (struct sockaddr *)&addr, &addr_size);
  std::string ip_str;
  if (res != -1)
  {
    ip_str = inet_ntoa(addr.sin_addr);
  }
  else
  {
    ip_str = "Unknown IP"; // TODO: this case is needed to be handled -> not allow to register
    response.set_message("Unable to retrieve IP address.");
    response.set_status_code(chat::StatusCode::BAD_REQUEST);
    SPM(client_sock, response);
    return false;
  }

  // Check for unique IP if HANDLE_UNIQUE_IP is true
  if (HANDLE_UNIQUE_IP)
  {
    for (const auto &entry : user_details)
    {
      if (entry.second == ip_str)
      {
        response.set_message("IP address is already in use.");
        response.set_status_code(chat::StatusCode::BAD_REQUEST);
        SPM(client_sock, response);
        return false;
      }
    }
  }

  // Check if the username is already taken
  if (user_details.find(username) != user_details.end())
  {
    response.set_message("Username is already taken.");
    response.set_status_code(chat::StatusCode::BAD_REQUEST);
    SPM(client_sock, response);
    return false;
  }

  std::cout << "Registering user: " << username << " with IP: " << ip_str << std::endl;

  // Register user
  user_details.emplace(username, ip_str);
  user_status.emplace(username, chat::UserStatus::ONLINE); // Set status to online
  client_sessions.emplace(client_sock, username);          // Link socket to username

  response.set_message("User registered successfully.");
  response.set_status_code(chat::StatusCode::OK);

  SPM(client_sock, response);
  return true;
}

/**
 * GET_USERS auxiliary function
 */
void add_user_to_response(const std::pair<std::string, std::string> &user, chat::UserListResponse &response)
{
  chat::User *user_proto = response.add_users();
  // Username concatenated string: <username> (<ip>)
  user_proto->set_username(user.first + " (" + user.second + ")");
  user_proto->set_status(user_status[user.first]);
}

/**
 * GET_USERS main function
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
    std::cout << "All users fetched successfully." << std::endl;
    response.set_message("All users fetched successfully.");
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
      std::cout << "User fetched successfully: " << it->first << std::endl;
      response.set_message("User fetched successfully.");
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
  SPM(client_sock, response);
}
/**
 * SEND_MESSAGE auxiliary function
 */
chat::IncomingMessageResponse prepare_message_response(const chat::Request &request, int client_sock)
{
  auto message = request.send_message();
  chat::IncomingMessageResponse message_response;
  std::lock_guard<std::mutex> lock(clients_mutex); // Lock the clients mutex, for thread safety
  message_response.set_sender(client_sessions[client_sock]);
  message_response.set_content(message.content());
  return message_response;
}

/**
 * SEND_MESSAGE auxiliary function
 */
void send_broadcast_message(const chat::IncomingMessageResponse &message_response, int client_sock)
{
  std::lock_guard<std::mutex> lock(clients_mutex);

  for (const auto &session : client_sessions)
  {
    if (session.first != client_sock)
    { // Optionally avoid sending the message back to the sender
      chat::Response response_to_recipient;
      response_to_recipient.set_operation(chat::Operation::INCOMING_MESSAGE);
      response_to_recipient.set_message("Broadcast message incoming.");
      response_to_recipient.set_status_code(chat::StatusCode::OK);
      response_to_recipient.mutable_incoming_message()->CopyFrom(message_response);
      SPM(session.first, response_to_recipient);
    }
  }

  chat::Response response_to_sender;
  response_to_sender.set_message("Broadcast message sent successfully.");
  response_to_sender.set_status_code(chat::StatusCode::OK);
  SPM(client_sock, response_to_sender);
}

/**
 * SEND_MESSAGE auxiliary function
 */
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

/**
 * SEND_MESSAGE auxiliary function
 */
void send_direct_message(chat::Response &response_to_sender, chat::Response &response_to_recipient, chat::IncomingMessageResponse &message_response, int client_sock, int recipient_sock)
{
  message_response.set_type(chat::MessageType::DIRECT);
  response_to_recipient.set_message("Message incoming.");
  response_to_recipient.set_status_code(chat::StatusCode::OK);
  response_to_recipient.mutable_incoming_message()->CopyFrom(message_response);
  SPM(recipient_sock, response_to_recipient);

  response_to_sender.set_message("Message sent successfully.");
  response_to_sender.set_status_code(chat::StatusCode::OK);
  SPM(client_sock, response_to_sender);
}

/**
 * SEND_MESSAGE main function
 */
void handle_send_message(const chat::Request &request, int client_sock, chat::Operation operation)
{
  chat::Response response_to_sender;
  response_to_sender.set_operation(operation);

  chat::Response response_to_recipient;
  response_to_recipient.set_operation(chat::Operation::INCOMING_MESSAGE);
  chat::IncomingMessageResponse message_response = prepare_message_response(request, client_sock);

  if (request.send_message().recipient().empty())
  {
    send_broadcast_message(message_response, client_sock);
  }
  else
  {
    int recipient_sock = find_recipient_socket(request.send_message().recipient());
    if (recipient_sock != -1)
    {
      send_direct_message(response_to_sender, response_to_recipient, message_response, client_sock, recipient_sock);
    }
    else
    {
      response_to_sender.set_message("Recipient not found.");
      response_to_sender.set_status_code(chat::StatusCode::BAD_REQUEST);
      SPM(client_sock, response_to_sender);
    }
  }
}

/**
 * UPDATE_STATUS auxiliary function
 */
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
  SPM(client_sock, response);
}

/**
 * UNREGISTER_USER main function
 */
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
    SPM(client_sock, response);
  }
}

void handle_client(int client_sock)
{
  bool registered = false; // Flag to check if user is registered
  std::string username;    // Store username after registration
  bool running = true;

  try
  {
    while (running)
    {
      chat::Request request;
      if (RPM(client_sock, request) == false)
      {
        std::cerr << "Failed to read message from client. Closing connection." << std::endl;

        // Unregister user if registered
        if (registered)
        {
          unregister_user(client_sock, true);
        }
        break;
      }

      // Update last active time for the user if registered
      if (registered)
      {
        std::lock_guard<std::mutex> lock(clients_mutex);
        last_active[username] = std::chrono::system_clock::now();
      }

      // Handling different types of requests
      switch (request.operation())
      {
      case chat::Operation::REGISTER_USER:

        if (!registered)
        {
          const bool got_registered = handle_registration(request, client_sock, chat::Operation::REGISTER_USER);
          if (got_registered)
          {
            std::cout << "User registered successfully." << std::endl;
            username = request.register_user().username();
            registered = true;

            // Initialize last active time for the new user
            std::lock_guard<std::mutex> lock(clients_mutex);
            last_active[username] = std::chrono::system_clock::now();
          }
        }
        else
        {

          chat::Response response;
          response.set_message("User already registered.");
          response.set_status_code(chat::StatusCode::BAD_REQUEST);
          SPM(client_sock, response);
        }
        break;
      case chat::Operation::SEND_MESSAGE:
        if (registered)
        {
          handle_send_message(request, client_sock, chat::Operation::SEND_MESSAGE);
        }
        else
        {
          chat::Response response;
          response.set_message("User not registered.");
          response.set_status_code(chat::StatusCode::BAD_REQUEST);
          SPM(client_sock, response);
        }
        break;
      case chat::Operation::UPDATE_STATUS:
        if (registered)
        {
          update_status(request, client_sock, chat::Operation::UPDATE_STATUS);
        }
        else
        {
          chat::Response response;
          response.set_message("User not registered.");
          response.set_status_code(chat::StatusCode::BAD_REQUEST);
          SPM(client_sock, response);
        }
        break;
      case chat::Operation::GET_USERS:
        if (registered)
        {
          handle_get_users(request, client_sock, chat::Operation::GET_USERS);
        }
        else
        {
          chat::Response response;
          response.set_message("User not registered.");
          response.set_status_code(chat::StatusCode::BAD_REQUEST);
          SPM(client_sock, response);
        }
        break;
      case chat::Operation::UNREGISTER_USER:
        if (registered && username == request.unregister_user().username())
        {
          unregister_user(client_sock);
          running = false;
        }
        else
        {
          chat::Response response;
          response.set_message("User not registered or username mismatch.");
          response.set_status_code(chat::StatusCode::BAD_REQUEST);
          SPM(client_sock, response);
        }
        break;
      default:
        chat::Response response;
        response.set_message("Unknown request type.");
        response.set_status_code(chat::StatusCode::BAD_REQUEST);
        SPM(client_sock, response);
        break;
      }
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << "Exception in client thread: " << e.what() << " - Cleaning up session." << std::endl;
    if (registered)
    {
      unregister_user(client_sock, true);
    }
  }

  if (close(client_sock) == -1)
  {
    std::cerr << "Failed to close socket: " << strerror(errno) << std::endl;
  }
  else
  {
    std::cout << "Socket closed successfully." << std::endl;
  }
  std::cout << "Session ended and socket closed for client." << std::endl;
}

void monitor_user_activity() // TODO: consider handling like discord, if the user set it, then is immutable, but if the previous state was online, the the auto set may work.
{
  while (true)
  {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::lock_guard<std::mutex> lock(clients_mutex);
    auto now = std::chrono::system_clock::now();

    for (auto &entry : last_active)
    {
      const std::string &username = entry.first;
      auto last_activity = entry.second;

      if (std::chrono::duration_cast<std::chrono::seconds>(now - last_activity).count() > AUTO_OFFLINE_SECONDS)
      {
        if (user_status[username] != chat::UserStatus::OFFLINE)
        {
          user_status[username] = chat::UserStatus::OFFLINE;
          std::cout << "User " << username << " has been set to OFFLINE due to inactivity." << std::endl;
        }
      }
    }
  }
}

void terminationHandler()
{
  std::string input;
  while (true)
  {
    std::getline(std::cin, input);
    if (input == "exit")
    {
      running = false;
      break;
    }
  }

  // Close the server socket
  close(server_fd);
  std::cout << "Server terminated." << std::endl;
  exit(0); // Terminate the program
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

int main(int argc, char *argv[])
{
  if (argc != 3)
  {
    std::cerr << "Usage: " << argv[0] << " <port> <server_name>\n";
    return 1;
  }
  int port = std::stoi(argv[1]);
  std::string server_name = argv[2];

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == 0)
  {
    perror("Socket creation failed");
    return 1;
  }

  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
  {
    perror("setsockopt failed");
    return 1;
  }

  sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
  {
    perror("Bind failed");
    return 1;
  }

  if (listen(server_fd, 10) < 0)
  {
    perror("Listen failed");
    return 1;
  }

  std::cout << server_name << " listening on port " << port << std::endl;
  std::cout << "Write 'exit' to terminate the server." << std::endl;
  // Start the user activity monitoring thread
  std::thread(monitor_user_activity).detach();

  // Start the termination handler thread
  std::thread terminator(terminationHandler);
  terminator.detach();

  // Set up signal handler for SIGINT (Ctrl+C)
  signal(SIGINT, signalHandler);

  while (running)
  {
    int client_sock = accept(server_fd, NULL, NULL);
    if (client_sock < 0)
    {
      if (!running)
        break; // Exit if server is shutting down
      perror("Accept failed");
      continue;
    }

    std::thread client_thread(handle_client, client_sock);
    client_thread.detach();
  }

  // Clean up
  close(server_fd);
  return 0;
}
