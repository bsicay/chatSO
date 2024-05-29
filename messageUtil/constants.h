// constants.h
#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <sys/types.h> // For ssize_t

// Boolean flag for handling UNIQUE IP
constexpr bool HANDLE_UNIQUE_IP = false;

// Integer flag for handling auto offline status in seconds
constexpr int AUTO_OFFLINE_SECONDS = 5;

// Indicating the static size of the buffer
constexpr size_t BUFFER_SIZE = 64 * 1024;

// Indicating if using verbose mode
constexpr bool VERBOSE = false;

#endif // CONSTANTS_H
