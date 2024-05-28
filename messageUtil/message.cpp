// message.cpp
#include "message.h"
#include <iostream> // For std::cerr
#include <vector>   // For std::vector
#include <cstring>  // For memcpy
#include <unistd.h> // For ssize_t
#include <cerrno>   // For errno

bool send_response(int sock, const google::protobuf::Message &message)
{
  std::string output;
  message.SerializeToString(&output);

  // Ensure the message fits in the buffer
  if (output.size() > BUFFER_SIZE)
  {
    std::cerr << "Message size exceeds buffer capacity. Size: " << output.size() << ", Buffer Capacity: " << BUFFER_SIZE << std::endl;
    return false;
  }

  // Send the message directly
  ssize_t sentBytes = send(sock, output.data(), output.size(), 0);
  if (sentBytes < 0)
  {
    perror("send failed");
    return false;
  }
  else if (sentBytes < output.size())
  {
    std::cerr << "Not all data was sent. Sent " << sentBytes << " of " << output.size() << " bytes." << std::endl;
    return false;
  }

  if (VERBOSE)
    std::cerr << "Sent " << sentBytes << " bytes successfully." << std::endl;

  return true;
}

bool receive_request(int sock, google::protobuf::Message &message)
{
  std::vector<char> buffer(BUFFER_SIZE);

  // Read the data into the buffer
  ssize_t bytesRead = recv(sock, buffer.data(), buffer.size(), 0);
  if (bytesRead <= 0)
  {
    if (bytesRead < 0)
      perror("recv failed");
    else
      std::cerr << "Connection closed by peer." << std::endl;
    return false; // Handle errors or disconnection
  }

  // Parse the received data
  if (!message.ParseFromArray(buffer.data(), bytesRead))
  {
    std::cerr << "Failed to parse the message. Bytes read: " << bytesRead << std::endl;
    return false;
  }

  if (VERBOSE)
    std::cerr << "Received " << bytesRead << " bytes successfully." << std::endl;
  return true;
}
