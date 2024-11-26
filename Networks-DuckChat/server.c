#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "duckchat.h"
#include <cerrno>
#include <fcntl.h>
#include <sys/select.h>



#define BUFFER_SIZE 1024
#define MAX_USERS 100
#define MAX_CHANNELS 100

typedef struct User {
    char username[USERNAME_MAX];
    struct sockaddr_in addr;
    struct User *next;  
} User;

typedef struct UserList {
    User *head;         
} UserList;

typedef struct Channel {
    char name[CHANNEL_MAX];
    struct UserList user_list;              
    struct Channel* next_channel;  
    int user_count;
} Channel;

UserList users = {NULL};
Channel *channels;
int user_count = 0;
int channel_count = 0;

User* find_user_by_name(UserList *user_list, char *username) {
    User *current = user_list->head;
    while (current != NULL) {
        if (strncmp(current->username, username, USERNAME_MAX) == 0) {
            return current;  // User found
        }
        current = current->next;
    }
    return NULL;
}

int remove_user_from_list(UserList *user_list, char *username) {
    //remove all users of a specific name in order to deal with duplicates for users who did not sign out
    User *current = user_list->head;
    User *previous = NULL;
    int removed_count = 0;

    while (current != NULL) {
        if (strncmp(current->username, username, USERNAME_MAX) == 0) {
            User *to_delete = current;

            if (previous == NULL) {
        
                user_list->head = current->next;
            } else {
                previous->next = current->next;
            }
            current = current->next;  
            free(to_delete);  
            printf("User %s removed\n", username);
            removed_count++;
        } else {
            previous = current;
            current = current->next;
        }
    }
    if (removed_count == 0) {
        printf("User %s not found\n", username);
        return 0;  
    }
    return removed_count;  
}

int remove_user_from_channel(Channel *channel, char *username) {
    remove_user_from_list(&(channel->user_list), username);
    channel->user_count -= 1;

    if (channel->user_count == 0) {
        Channel *current = channels;
        Channel *previous = NULL;
        // Search for
        while (current != NULL) {
            if (current == channel) {
                if (previous == NULL) {
                    channels = current->next_channel;
                } else {
                    previous->next_channel = current->next_channel;
                }

                free(current);
                printf("Channel %s deleted\n", channel->name);
                channel_count--;  // Update global channel count
                return 1;  // Success
            }
            previous = current;
            current = current->next_channel;
        }
    }
    printf("User %s removed from channel %s.\n", username, channel->name);
    return 1;  
}

int add_user(UserList *user_list, const char *username, struct sockaddr_in addr) {
    User *current = user_list->head;

    // Check if the user already exists in the list
    while (current) {
        if (strncmp(current->username, username, USERNAME_MAX) == 0) {
            // Update the existing user's address
            current->addr = addr;
            printf("User %s reconnected and updated.\n", username);
            return 0;
        }
        current = current->next;
    }
    //create new user 
    User *new_user = (User *)malloc(sizeof(User));
    if (!new_user) {
        perror("Failed to allocate memory for new user");
        return -1;
    }

    strncpy(new_user->username, username, USERNAME_MAX - 1);
    new_user->username[USERNAME_MAX - 1] = '\0';  
    new_user->addr = addr;
    new_user->next = user_list->head; 
    user_list->head = new_user;

    printf("User %s added to list.\n", username);
    return 1;
}

User* find_user_by_address(UserList *user_list, struct sockaddr_in *addr) {
    User *current = user_list->head;
    while (current) {
        if (current->addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            current->addr.sin_port == addr->sin_port) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

Channel* join_channel(char *channel_name, User *user) {
    Channel *current = channels;

    // Search for the channel in the linked list
    while (current != NULL) {
        if (strcmp(current->name, channel_name) == 0) {
            if(add_user(&(current->user_list), user->username, user->addr)){
                current->user_count++;
            }
            printf("User %s joined existing channel %s\n", user->username, channel_name);
            return current;
        }
        current = current->next_channel;
    }

    // Channel does not exist, create a new channel
    Channel *new_channel = (Channel *)malloc(sizeof(Channel));
    if (!new_channel) {
        return NULL;
    }
    channel_count += 1;
    strncpy(new_channel->name, channel_name, CHANNEL_MAX - 1);
    new_channel->name[CHANNEL_MAX - 1] = '\0';
    new_channel->user_list.head = NULL;
    add_user(&(new_channel->user_list), user->username, user->addr);
    new_channel->user_count = 1;
    new_channel->next_channel = channels;
    channels = new_channel;

    printf("User %s created and joined new channel %s\n", user->username, channel_name);
    return new_channel;
}

Channel* find_channel_by_name(char *channel_name) {
    Channel *current = channels;
    while (current != NULL) {
        if (strcmp(current->name, channel_name) == 0) {
            return current;
        }
        current = current->next_channel;
    }
    return NULL;
}

void send_error(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *error_message) {
    struct text_error response;
    response.txt_type = TXT_ERROR;

    strncpy(response.txt_error, error_message, SAY_MAX - 1);
    response.txt_error[SAY_MAX - 1] = '\0'; 

    if (sendto(sockfd, &response, sizeof(struct text_error), 0, (struct sockaddr *)client_addr, client_len) < 0) {
        perror("Error sending error response");
    } else {
        printf("Error sent to client %s:%d: %s\n", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), response.txt_error);
    }
}

int handle_login(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len,  request_login *buffer){
    char* username = buffer->req_username;
    add_user(&users, username, *client_addr);
    return 1;
}

int handle_logout(int sockfd, struct sockaddr_in *client_addr, struct request_logout * buffer){
    Channel *current_channel = channels;

    User *user = find_user_by_address(&users, client_addr);
    char *username = user->username;
    while (current_channel != NULL) {
        remove_user_from_channel(current_channel, username);
        current_channel = current_channel->next_channel;
    }
    remove_user_from_list(&users, username);
    return 1;
}

int handle_join(int sockfd, struct sockaddr_in *client_addr,  struct request_join *buffer){
    char* channel = buffer->req_channel;
    User *user = find_user_by_address(&users, client_addr);
    join_channel(channel, user);
    return 1;
}

int handle_leave(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len,  struct request_leave *buffer){
    char* channel_name = buffer->req_channel;
    Channel* channel = find_channel_by_name(channel_name);
    if(channel == NULL){
        send_error(sockfd, client_addr, client_len, "Channel does not exist");
        return -1;
    }
    User *user = find_user_by_address(&(channel->user_list), client_addr);
    char* username = user->username;
    remove_user_from_channel(channel, username);
    return 1;
}

int handle_say(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, struct request_say *buffer){

    struct text_say *response = (struct text_say *)malloc(sizeof(struct text_say));

    response->txt_type = TXT_SAY;
    char* channel_name = buffer->req_channel;
    char* say = buffer->req_text;
    User *user = find_user_by_address(&users, client_addr);
    strncpy(response->txt_channel, buffer->req_channel, CHANNEL_MAX);
    Channel* channel = find_channel_by_name(channel_name);
    if(channel == NULL){
        send_error(sockfd, client_addr, client_len, "Channel does not exist");
        return -1;
    }
    strncpy(response->txt_username, user->username, USERNAME_MAX);
    strncpy(response->txt_text, say, SAY_MAX);

    User* current_user = channel->user_list.head;
    while(current_user != NULL){
        if (sendto(sockfd, response, sizeof(struct text_say), 0, (struct sockaddr *)&current_user->addr, sizeof(current_user->addr)) < 0) {
            perror("Error sending say response");
        }
        current_user = current_user->next;
    }
    printf("say request sent \n");
    return 1;
}

int handle_list(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len){

    struct text_list *response;
    response = (struct text_list *)malloc(sizeof(struct text_list) + sizeof(struct channel_info) * channel_count);
    response->txt_type = TXT_LIST;
    response->txt_nchannels = channel_count;
    
    Channel *current_channel = channels;

    int i = 0;
    while (current_channel != NULL) {
        strncpy(response->txt_channels[i].ch_channel, current_channel->name, CHANNEL_MAX - 1);
        response->txt_channels[i].ch_channel[CHANNEL_MAX - 1] = '\0'; 
        i++;
        current_channel = current_channel->next_channel;
    }

     if (sendto(sockfd, response, sizeof(struct text_list) + sizeof(struct channel_info) * channel_count, 0, (struct sockaddr *)client_addr, client_len) < 0) {
        perror("Error sending list response");
        return -1;
    }
    free(response);
    printf("List response sent with %d channels.\n", channel_count);
    return 1;
}

int handle_who(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, struct request_who *buffer){

    char* channel_ch = buffer->req_channel;
    Channel* channel  = find_channel_by_name(channel_ch);
    if (channel == NULL) {
        send_error(sockfd, client_addr, client_len, "Channel does not exist");
        return -1;
    }
    struct text_who *response;
    int user_c = channel->user_count;

    response = (struct text_who *)malloc(sizeof(struct text_who) + sizeof(struct user_info) * user_c);
    response->txt_type = TXT_WHO;
    response->txt_nusernames = user_c;
    strncpy(response->txt_channel, channel_ch, CHANNEL_MAX-1);
    response->txt_channel[CHANNEL_MAX-1] = '\0';

    int i = 0;
    User* current_user = channel->user_list.head;

    while (current_user != NULL) {
        strncpy(response->txt_users[i].us_username, current_user->username, USERNAME_MAX - 1);
        response->txt_users[i].us_username[USERNAME_MAX - 1] = '\0'; 
        i++;
        current_user = current_user->next;
    }

     if (sendto(sockfd, response, sizeof(struct text_who) + sizeof(struct user_info) * user_c, 0, (struct sockaddr *)client_addr, client_len) < 0) {
        perror("Error sending list response");
        return -1;
    }
    free(response);
    printf("Who response sent with %d users.\n", user_c);
    return 1;
}

void handle_request(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, char *buffer) {
    struct request *req = (struct request*)buffer;

    switch (req->req_type) {
        case REQ_LOGIN:
            handle_login(sockfd, client_addr, client_len, (struct request_login *)buffer);
            break;
        case REQ_LOGOUT:
            handle_logout(sockfd, client_addr, (struct request_logout *)buffer);
            break;
        case REQ_JOIN:
            handle_join(sockfd, client_addr, (struct request_join *)buffer);
            break;
        case REQ_LEAVE:
            handle_leave(sockfd, client_addr, client_len, (struct request_leave *)buffer);
            break;
        case REQ_SAY:
            handle_say(sockfd, client_addr, client_len, (struct request_say *)buffer);
            break;
        case REQ_LIST:
            handle_list(sockfd, client_addr, client_len);
            break;
        case REQ_WHO:
            handle_who(sockfd, client_addr, client_len, (struct request_who *)buffer);
            break;
        default:
            break;
    }
}
    
int main(int argc, char *argv[]){

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    //Create socket 
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    char arg[10];
    if(strcmp(argv[1], "localhost") == 0){
        strncpy(arg, "127.0.0.1", sizeof(arg));
        arg[sizeof(arg)-1] = '\0';
        server_addr.sin_addr.s_addr = inet_addr(arg);
    }else{
        server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    }
    
    server_addr.sin_port = htons(atoi(argv[2]));
    server_addr.sin_addr.s_addr = INADDR_ANY;

     if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Server started on %s:%s\n", argv[1], argv[2]);

    fd_set read_fds;
    struct timeval timeout;

     while (1) {
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        timeout.tv_sec = 1; // 1-second timeout
        timeout.tv_usec = 0;
        int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0 && errno != EINTR) {
            perror("select error");
            break;
        }else if(activity > 0){
            if (FD_ISSET(sockfd, &read_fds)) {
                int bytes_recieved = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
                if  (bytes_recieved > 0){
                    handle_request(sockfd, &client_addr, client_len, buffer);
                }
                if (bytes_recieved < 0) {
                    perror("recvfrom failed");
                    continue;
                }
            }else{
                printf("Waiting for client data...\n");
            }
        }
    }

    close(sockfd);
    return 0;
}



