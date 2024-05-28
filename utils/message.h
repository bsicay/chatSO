// message.h
#ifndef MESSAGE_H
#define MESSAGE_H

#include "constants.h"
#include <vector>
#include <string>
#include <cstdint>                   // For uint32_t
#include <sys/types.h>               // For ssize_t
#include <sys/socket.h>              // For send, recv, and MSG_WAITALL
#include <netinet/in.h>              // For htonl, ntohl
#include <google/protobuf/message.h> // For Google Protobuf

bool send_response(int sock, const google::protobuf::Message &message); // SPM: Send Protobuf Message
bool receive_request(int sock, google::protobuf::Message &message);       // RPM: Receive Protobuf Message

#endif // MESSAGE_H
