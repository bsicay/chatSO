#include "./messageUtil/chat.pb.h" // Include the generated protobuf header
#include "./messageUtil/message.h"
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
#include <atomic>
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
std::atomic<bool> in_input_mode{false};
std::atomic<bool> waiting_response{false};
std::atomic<bool> terminate_execution{false};
std::atomic<bool> streaming_mode{false};
std::mutex cout_mutex;
std::deque<std::string> message_buffer; // Buffer for messages received during input mode

// TODO: add identifier uuid to each request and response to match them

void terminationHandler(int sock, std::string username, std::thread &listener)
{
  while (!terminate_execution)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cout << "Connection terminated abruptly." << std::endl;
  // If by some reason the listener thread is still running, stop it
  running = false;
  if (listener.joinable())
  {
    listener.join(); // Wait for the listener thread to finish
  }
  // Close the socket
  close(sock);

  std::cout << "Exiting..." << std::endl;
  exit(0); // Terminate the program
}

void flush_message_buffer()
{
  std::lock_guard<std::mutex> lock(cout_mutex);
  while (!message_buffer.empty())
  {
    std::cout << message_buffer.front() << std::endl;
    message_buffer.pop_front();
  }
}

void messageListener(int sock)
{
  while (running)
  {
    chat::Response response;
    if (receive_request(sock, response))
    {
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::string message;
      if (response.status_code() != chat::StatusCode::OK)
      {
        message = RED "Server error: " + response.message() + RESET;
      }
      else
      {
        switch (response.operation())
        {
        case chat::Operation::INCOMING_MESSAGE:
          if (response.has_incoming_message())
          {
            const auto &msg = response.incoming_message();
            std::string type = (msg.type() == chat::MessageType::BROADCAST) ? "Broadcast" : "Direct";
            message = BLUE + type + " message from " + msg.sender() + ": " + msg.content() + RESET;
          }
          break;
        case chat::Operation::GET_USERS:
          if (response.has_user_list())
          {
            const auto &user_list = response.user_list();
            if (user_list.type() == chat::UserListType::SINGLE)
            {
              message = std::string(MAGENTA) + "User info: ";
            }
            else
            {
              message = std::string(MAGENTA) + "Users online: ";
            }
            for (const auto &user : user_list.users())
            {
              std::string status;
              switch (user.status())
              {
              case chat::UserStatus::ONLINE:
                status = "ONLINE";
                break;
              case chat::UserStatus::BUSY:
                status = "BUSY";
                break;
              case chat::UserStatus::OFFLINE:
                status = "OFFLINE";
                break;
              default:
                status = "UNKNOWN";
              }

              message += user.username() + " " + status + ", ";
            }
            message += RESET;
          }
          break;
        default:
          message = "SERVER: " + response.message();
          break;
        }
      }

      if (response.operation() == chat::Operation::INCOMING_MESSAGE)
      {
        if (streaming_mode)
        {
          std::cout << message << std::endl;
        }
        else
        {
          message_buffer.push_back(message);
        }
      }
      else
      {
        std::cout << message << std::endl;

        if (waiting_response)
        {
          waiting_response = false;
        }
      }
    }
    else
    {
      break;
    }
  }
  terminate_execution = true;
}

void displayHelp()
{
  std::cout << GREEN;
  std::cout << "\nCommands list:\n";
  std::cout << "    send <message>\n";
  std::cout << "    sendto <recipient> <message>\n";
  std::cout << "    status <status>\n";
  std::cout << "    list\n";
  std::cout << "    info <username>\n";
  std::cout << "    help\n";
  std::cout << "    stream\n";
  std::cout << "    exit\n\n";
  std::cout << RESET;
}

void handleBroadcastMessage(int sock, const std::string &message)
{
  chat::Request request;
  request.set_operation(chat::Operation::SEND_MESSAGE);
  auto *msg = request.mutable_send_message();
  msg->set_content(message);

  send_response(sock, request);
}

void handleDirectMessage(int sock, const std::string &recipient, const std::string &message)
{
  chat::Request request;
  request.set_operation(chat::Operation::SEND_MESSAGE);
  auto *msg = request.mutable_send_message();
  msg->set_content(message);
  msg->set_recipient(recipient);

  send_response(sock, request);
}

bool handleChangeStatus(int sock, const std::string &status)
{
  chat::Request request;
  request.set_operation(chat::Operation::UPDATE_STATUS);
  auto *status_request = request.mutable_update_status();

  if (status == "ONLINE")
  {
    status_request->set_new_status(chat::UserStatus::ONLINE);
  }
  else if (status == "BUSY")
  {
    status_request->set_new_status(chat::UserStatus::BUSY);
  }
  else if (status == "OFFLINE")
  {
    status_request->set_new_status(chat::UserStatus::OFFLINE);
  }
  else
  {
    std::cout << "Invalid status: Valid ones are: ONLINE, BUSY & OFFLINE\n";
    return false;
  }

  send_response(sock, request);
  return true;
}

void handleListUsers(int sock)
{
  chat::Request request;
  request.set_operation(chat::Operation::GET_USERS);
  auto *user_list = request.mutable_get_users();

  send_response(sock, request);
}

void handleGetUserInfo(int sock, const std::string &username)
{
  chat::Request request;
  request.set_operation(chat::Operation::GET_USERS);
  auto *user_list = request.mutable_get_users();
  user_list->set_username(username);

  send_response(sock, request);
}

void handleUnregisterUser(int sock, const std::string &username)
{
  chat::Request request;
  request.set_operation(chat::Operation::UNREGISTER_USER);
  auto *unregister_user = request.mutable_unregister_user();
  unregister_user->set_username(username);

  send_response(sock, request);
}

int main(int argc, char *argv[])
{
  if (argc != 4)
  {
    std::cerr << "Usage: " << argv[0] << " <server IP> <server port> <username>\n";
    return 1;
  }

  std::string server_ip = argv[1];
  int server_port = std::stoi(argv[2]);
  std::string username = argv[3];

  int sock = 0;
  struct sockaddr_in serv_addr;

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    std::cerr << "Socket creation error \n";
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(server_port);

  if (inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr) <= 0)
  {
    std::cerr << "Invalid address/ Address not supported \n";
    return -1;
  }

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    std::cerr << "Connection Failed \n";
    return -1;
  }

  // Register the user first
  chat::Request request;
  request.set_operation(chat::Operation::REGISTER_USER);
  auto *new_user = request.mutable_register_user();
  new_user->set_username(username);

  send_response(sock, request);

  chat::Response response;
  if (receive_request(sock, response))
  {
    if (response.status_code() != chat::StatusCode::OK)
    {
      std::cout << RED "ERROR: " + response.message() + RESET << std::endl;
      return -1;
    }

    std::cout << "SERVER: " << response.message() << std::endl;
  }
  else
  {
    std::cerr << "Connection closed." << std::endl;
    close(sock);
    return -1;
  }

  std::thread listener(messageListener, sock);
  listener.detach();

  // Start the termination handler thread
  std::thread terminator(terminationHandler, sock, username, std::ref(listener));
  terminator.detach(); // Detach the thread so it can run independently

  int choice = 0;

  displayHelp();
  do
  {
    in_input_mode = true; // Set input mode to true to suppress messageListener output
    waiting_response = true;
    std::string command;
    std::cout << ">> ";
    std::getline(std::cin, command);

    std::istringstream iss(command);
    std::vector<std::string> words;
    std::string word;

    while (iss >> word)
    {
      words.push_back(word);
    }

    size_t length = words.size();

    if (length == 0)
    {
      std::cout << "Invalid choice, please try again.\n";
    }
    else if (words[0] == "send")
    {
      if (length < 2)
      {
        std::cout << "Invalid command. Usage: send <message>\n";
        waiting_response = false;
      }
      else
      {
        std::string message = command.substr(command.find(" ") + 1);
        handleBroadcastMessage(sock, message);
      }
    }
    else if (words[0] == "sendto")
    {
      if (length < 3)
      {
        std::cout << "Invalid command. Usage: sendto <recipient> <message>\n";
        waiting_response = false;
      }
      else
      {
        std::string recipient = words[1];
        std::string message = command.substr(command.find(recipient) + recipient.length() + 1);
        handleDirectMessage(sock, recipient, message);
      }
    }
    else if (words[0] == "status")
    {
      if (length != 2)
      {
        std::cout << "Invalid command. Usage: status <status>\n";
        waiting_response = false;
      }
      else
      {
        const bool accepted_status = handleChangeStatus(sock, words[1]);
        if (accepted_status)
        {
          waiting_response = true;
        }
        else
        {
          waiting_response = false;
        }
      }
    }
    else if (words[0] == "list")
    {
      if (length != 1)
      {
        std::cout << "Invalid command. Usage: list\n";
        waiting_response = false;
      }
      else
      {
        handleListUsers(sock);
      }
    }
    else if (words[0] == "info")
    {
      if (length != 2)
      {
        std::cout << "Invalid command. Usage: info <username>\n";
        waiting_response = false;
      }
      else
      {
        handleGetUserInfo(sock, words[1]);
      }
    }
    else if (words[0] == "help")
    {
      if (length != 1)
      {
        std::cout << "Invalid command. Usage: help\n";
      }
      else
      {
        displayHelp();
      }
      waiting_response = false;
    }
    else if (words[0] == "stream")
    {
      if (length != 1)
      {
        std::cout << "Invalid command. Usage: stream\n";
      }
      else
      {
        if (!streaming_mode)
        {
          flush_message_buffer();
        }
        streaming_mode = !streaming_mode;
        std::cout << "Streaming mode: " << (streaming_mode ? "ON" : "OFF") << std::endl;
        flush_message_buffer();
      }
      waiting_response = false;
    }
    else if (words[0] == "exit")
    {
      if (length != 1)
      {
        std::cout << "Invalid command. Usage: exit\n";
      }
      else
      {
        // This is the exit option and special handling is required
        // 1. Stop running the listener thread
        running = false;
        if (listener.joinable())
        {
          listener.join(); // Wait for the listener thread to finish
        }
        // 2. Send the unregister request
        handleUnregisterUser(sock, username);
        // 3. Wait for the server to respond
        if (receive_request(sock, response))
        {
          std::cout << "SERVER: " << response.message() << std::endl;
        }
        waiting_response = false;
        break;
      }
    }
    else
    {
      std::cout << "Invalid choice, please try again.\n";
      waiting_response = false;
    }

    in_input_mode = false; // Reset input mode after action is handled
    while (waiting_response)
    {
      if (terminate_execution)
      {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    flush_message_buffer();
    if (terminate_execution)
    {
      break;
    }
  } while (choice != 8);

  // If by some reason the listener thread is still running, stop it
  running = false;
  if (listener.joinable())
  {
    listener.join(); // Wait for the listener thread to finish
  }
  // Close the socket
  close(sock);

  std::cout << "Exiting..." << std::endl;
  return 0;
}
