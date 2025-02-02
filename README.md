# Server-to-Server-Distributed-Chat-System

## Overview
Server-to-Server-Distributed-Chat-System is a distributed chat system that enables seamless communication between multiple servers, allowing users on different servers to interact as if they were on the same instance. This project extends the functionality of a basic chat implementation by introducing an efficient server-to-server protocol that dynamically forms message distribution trees, ensuring optimized message propagation and loop prevention.

## Features
- **Decentralized Server Network:** Supports multiple interconnected servers.
- **Optimized Message Routing:** Only relevant servers receive messages, reducing redundant traffic.
- **Loop Prevention:** Implements unique message identifiers to detect and prevent loops.
- **Soft-State Mechanism:** Periodic join messages maintain server connections and automatically remove inactive servers.
- **Efficient User and Channel Management:** Tracks users and their subscriptions to facilitate smooth interactions.
- **Robust Error Handling:** Handles invalid packets and prevents crashes due to malformed requests.

## Installation
### Prerequisites
- **Operating System:** Linux (Tested on ix-dev.cs.uoregon.edu)
- **Compiler:** GCC (GNU Compiler Collection)
- **Libraries:**
  - POSIX Sockets
  - `<arpa/inet.h>`
  - `<sys/socket.h>`
  - `<netinet/in.h>`
  - `<fcntl.h>`
  - `<time.h>`

### Compilation
Clone the repository and navigate to the project directory:
```sh
$ git clone <repository_url>
$ cd Server-to-Server-Distributed-Chat-System
```
Compile the server using:
```sh
$ make
```
This will generate the executable `server_chat`.

## Usage
### Starting a Server
To start a standalone server:
```sh
$ ./server_chat <server_ip> <port>
```
To start a server with neighbors for inter-server communication:
```sh
$ ./server_chat <server_ip> <port> <neighbor_ip_1> <neighbor_port_1> <neighbor_ip_2> <neighbor_port_2> ...
```
Example:
```sh
$ ./server_chat 127.0.0.1 4000 127.0.0.1 5000 127.0.0.1 6000
```

### Client Interaction
Clients communicate with the server via UDP messages, supporting the following operations:
- **Login**: Users connect to the server.
- **Join Channel**: Users subscribe to chat rooms.
- **Leave Channel**: Users leave chat rooms.
- **List Channels**: Retrieves active channels.
- **Who**: Lists users in a channel.
- **Say**: Sends a message to a channel.
- **Logout**: Users disconnect from the server.

## Technical Details
### Data Structures
- **Users:**
  - Maintained in a linked list with username and address information.
- **Channels:**
  - Tracks users subscribed to each channel and their count.
- **Neighbors:**
  - Stores adjacent servers and their channel subscriptions.
- **Message ID Tracking:**
  - Prevents message rebroadcast loops by maintaining a list of recent message IDs.

### Message Flow
1. **User joins a channel:**
   - Server notifies neighbors of the new subscription.
   - If a neighbor is not subscribed, it forwards the message to its neighbors.
2. **User sends a message:**
   - Message propagates along the subscription tree efficiently.
   - If a loop is detected, the extra link is removed.
3. **Server Pruning:**
   - Servers remove themselves if they have no users and only one subscribed neighbor.
   - Soft-state mechanism ensures inactive servers automatically disconnect.

### Logging and Debugging
The server outputs status messages for debugging:
```sh
127.0.0.1:4000 127.0.0.1:5000 send JOIN Common
127.0.0.1:5000 127.0.0.1:6000 recv MESSAGE Agthorr Common "hello"
127.0.0.1:6000 127.0.0.1:5000 send LEAVE Common
```

## Testing
The project includes scripts to test interoperability between multiple servers. Ensure the server is running and connect clients to verify:
```sh
$ ./client 127.0.0.1 4000 username
```
To simulate multiple servers, start multiple instances with different ports and test message propagation.



