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
#include <time.h>

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

typedef struct channel_sub{
    char name[CHANNEL_MAX];
    struct channel_sub *next;
    time_t last_renewed;
}channel_sub;

typedef struct Neighbor {
    struct sockaddr_in addr;
    channel_sub* subscriptions;
    struct Neighbor *next;
} Neighbor;

typedef struct MessageID {
    uint64_t id;
    time_t timestamp;
    struct MessageID *next;
} MessageID;

//global list of message ids
MessageID *message_ids = NULL;

//global list of all neighbors
Neighbor *neighbors = NULL;
//global list of all subscriptions
channel_sub* subscriptions = NULL;

UserList users = {NULL};
Channel *channels;
int user_count = 0;
int channel_count = 0;
struct sockaddr_in server_addr;
struct sockaddr_in server_addr_for_ip_display;

Neighbor* find_neighbor_by_address(struct sockaddr_in *addr){
    Neighbor* current = neighbors;
    while (current) {
        if (current->addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            current->addr.sin_port == addr->sin_port) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

Channel* find_channel_by_name(char *channel_name){
    Channel *current = channels;
    while (current != NULL) {
        if (strcmp(current->name, channel_name) == 0) {
            return current;
        }
        current = current->next_channel;
    }
    return NULL;
}

int is_subscribed(Neighbor *neighbor, const char *channel_name) {
    
    channel_sub *subscription = neighbor->subscriptions;
    while (subscription) {
        if (strcmp(subscription->name, channel_name) == 0) {
            return 1;
        }
        subscription = subscription->next;
    }
    return 0; 
}

void send_s2s_leave(int sockfd, struct sockaddr_in *addr, const char *channel_name) {

    struct s2s_leave leave_message;

    leave_message.req_type = S2S_LEAVE; 
    strncpy(leave_message.channel, channel_name, CHANNEL_MAX - 1);
    leave_message.channel[CHANNEL_MAX - 1] = '\0';

    if (sendto(sockfd, &leave_message, sizeof(leave_message), 0, (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        printf("Error sending S2S Leave");
    } else {
        printf("%s:%d %s:%d send S2S Leave %s\n", inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port), inet_ntoa(addr->sin_addr), ntohs(addr->sin_port),channel_name);
    }
    
}

//here is a function to check if the conditions for pruning have been met 
int should_send_leave(char *channel_name) { 

    Channel *channel = find_channel_by_name(channel_name);
    int sub_neighbors = 0;
    int at_least_1_local_user = 0;

    //if channel exists and has local users 
    if (channel && channel->user_list.head) {
        at_least_1_local_user = 1;
    }

    //count subscribed users to channel
    Neighbor *current = neighbors;
    while (current) {
        if (is_subscribed(current, channel_name)) {
            sub_neighbors++;
        }
        current = current->next;
    }
    
    return ((at_least_1_local_user == 0) && (sub_neighbors == 1));
}

void leave_channel(int sockfd, Neighbor *neighbor, char *channel_name) {
    if (!neighbor || !channel_name) return;

    // Remove the channel subscription from the neighbor
    channel_sub *current = neighbor->subscriptions;
    channel_sub *prev = NULL;

    while (current) {
        if (strcmp(current->name, channel_name) == 0) {
     
            if (prev == NULL) {
                neighbor->subscriptions = current->next; // Remove head
            } else {
                prev->next = current->next; // Remove middle or tail
            }

            /*printf("Neighbor %s:%d unsubscribed from channel %s\n",
                   inet_ntoa(neighbor->addr.sin_addr), ntohs(neighbor->addr.sin_port), channel_name);*/
            free(current);

            // Check if thi

            return; 
        }
        prev = current;
        current = current->next;
    }

    printf("Neighbor %s:%d was not subscribed to channel %s\n",
           inet_ntoa(neighbor->addr.sin_addr), ntohs(neighbor->addr.sin_port), channel_name);
}

void handle_s2s_leave(int sockfd, struct sockaddr_in *sender, struct s2s_leave *buffer) {
    if (!sender || !buffer) return;

    char *channel_name = buffer->channel;

    // Find the neighbor corresponding to the sender
    Neighbor *neighbor = find_neighbor_by_address(sender);
    if (!neighbor) {
        printf("unknown neighbor");
        return;
    }

    printf("%s:%d %s:%d recv S2S Leave %s\n", inet_ntoa(server_addr_for_ip_display.sin_addr), ntohs(server_addr.sin_port), inet_ntoa(sender->sin_addr), ntohs(sender->sin_port),
           channel_name);

    // Remove the neighbor's subscription to the channel
    leave_channel(sockfd, neighbor, channel_name);

}

void add_channel_to_neighbor(Neighbor *neighbor, char* channel_name){
    channel_sub* current = neighbor->subscriptions; 
    while (current){
        if(strcmp(current->name, channel_name) == 0){
            return; 
        }
        current = current->next;
    }
    channel_sub* new_sub = (channel_sub*)malloc(sizeof(channel_sub));
    if(!new_sub){
        printf("Failed ");
        return;
    }

    strncpy(new_sub->name, channel_name, CHANNEL_MAX-1);
    new_sub->name[CHANNEL_MAX-1] = '\0';
    new_sub->next = neighbor->subscriptions;
    neighbor->subscriptions = new_sub;
}

void subscribe_all_neighbors(char* channel_name){
    Neighbor* current = neighbors;
    while(current){
        add_channel_to_neighbor(current, channel_name);
        current = current->next;
    }
}

void broadcast_s2s_join(int sockfd, struct sockaddr_in *sender ,char* channel_name, int is_soft_join){
    struct request_join join_message;
    join_message.req_type = S2S_JOIN;
    strncpy(join_message.req_channel, channel_name, CHANNEL_MAX-1);
    join_message.req_channel[CHANNEL_MAX-1] = '\0';

    Neighbor *current = neighbors;

    //evalute address for printing 
    struct sockaddr_in actual_addr;
    socklen_t addr_len = sizeof(actual_addr);
    getsockname(sockfd, (struct sockaddr *)&actual_addr, &addr_len);

    //if the sender is NULL than the broadcast was triggered by a local join 
    while(current){
        if(find_neighbor_by_address(&(current->addr))){
            if (!sender || current->addr.sin_addr.s_addr != sender->sin_addr.s_addr || current->addr.sin_port != sender->sin_port){
                sendto(sockfd, &join_message, sizeof(join_message), 0, (struct sockaddr*)&current->addr, sizeof(current->addr));
                if(is_soft_join){
                    printf("%s:%d %s:%d send S2S soft Join %s\n",
                   inet_ntoa(server_addr_for_ip_display.sin_addr), ntohs(actual_addr.sin_port),
                   inet_ntoa(current->addr.sin_addr), ntohs(current->addr.sin_port),
                   channel_name);
                }else{
                 printf("%s:%d %s:%d send S2S Join %s\n",
                   inet_ntoa(server_addr_for_ip_display.sin_addr), ntohs(actual_addr.sin_port),
                   inet_ntoa(current->addr.sin_addr), ntohs(current->addr.sin_port),
                   channel_name);
                }

            }
        }
        current = current->next; 
    }
}

void broadcast_s2s_say(int sockfd, struct s2s_say* message, struct sockaddr_in* sender) {

    Neighbor *current = neighbors;
    while (current) {
        //dont send too sender
        if (sender && ntohl(current->addr.sin_addr.s_addr) == ntohl(sender->sin_addr.s_addr) && ntohs(current->addr.sin_port) == ntohs(sender->sin_port)) {
            current = current->next;
            continue;
        }
        if (is_subscribed(current, message->txt_channel) && (!sender || current->addr.sin_addr.s_addr != sender->sin_addr.s_addr || current->addr.sin_port != sender->sin_port)) {
            if (sendto(sockfd, message, sizeof(*message), 0, (struct sockaddr*)&current->addr, sizeof(current->addr)) < 0) {
                perror("Error broadcasting S2S_SAY");
            } else {
                printf("%s:%d %s:%d send S2S_SAY %s \"%s\"\n", inet_ntoa(server_addr_for_ip_display.sin_addr), ntohs(server_addr.sin_port), inet_ntoa(current->addr.sin_addr), ntohs(current->addr.sin_port),
                    message->txt_channel, message->txt_text);

            }
        }else{
            printf("checked, not subscribed!");
        }
        current = current->next;
    }
}

int add_channel_sub(char* channel_name){
    channel_sub* current = subscriptions; 
    while (current){
        if(strcmp(current->name, channel_name) == 0){
            return 0; 
        }
        current = current->next;
    }
    channel_sub* new_sub = (channel_sub*)malloc(sizeof(channel_sub));
    if(!new_sub){
        printf("Failed bad things happend, everybody take cover");
        return -1;
    }

    strncpy(new_sub->name, channel_name, CHANNEL_MAX-1);
    new_sub->name[CHANNEL_MAX-1] = '\0';
    new_sub->next = subscriptions;
    new_sub->last_renewed = time(NULL);
    subscriptions = new_sub;
    //printf("channel added to server: %s\n", channel_name);
    return 1;
}

int remove_channel_sub(char *channel_name) {

    channel_sub *current = subscriptions;
    channel_sub *prev = NULL;

    // Traverse the subscriptions list
    while (current) {
        if (strcmp(current->name, channel_name) == 0) { 
            if (prev == NULL) {
                subscriptions = current->next; 
            } else {
                prev->next = current->next; 
            }

            free(current); 
            return 1; 
        }

        prev = current;  
        current = current->next; 
    }

    // If the channel is not found in the list
    printf("channel %s not found in server subscriptions\n", channel_name);
    return 0; 
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

void handle_s2s_join(int sockfd, struct sockaddr_in *sender, struct request_join *buffer){

    char* channel_name = buffer->req_channel;
    Neighbor* send_neighbor = find_neighbor_by_address(sender);
    if (send_neighbor) {//check if there is a neighbor ie if its a join sent from a noneighbor 
        channel_sub *current = subscriptions;
        while (current) {
            if (strcmp(current->name, channel_name) == 0) {
                current->last_renewed = time(NULL);
                break;
            }
            current = current->next;
        }
    }

    if(find_neighbor_by_address(sender)){//check if neighbor exists
        add_channel_to_neighbor(send_neighbor, channel_name);// still subscribe neighbor even if channel already exists 
        if(add_channel_sub(channel_name)){
            subscribe_all_neighbors(channel_name); //subscribe everybody if this is a new channel join 
            broadcast_s2s_join(sockfd, sender ,channel_name, 0); //broadcast since this is a new join
        }
    }
    printf("%s:%d %s:%d recv S2S Join %s\n", inet_ntoa(server_addr_for_ip_display.sin_addr), ntohs(server_addr.sin_port), inet_ntoa(sender->sin_addr), ntohs(sender->sin_port),
           channel_name);

}

void send_error(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *error_message) {
    struct text_error response;
    response.txt_type = TXT_ERROR;

    strncpy(response.txt_error, error_message, SAY_MAX - 1);
    response.txt_error[SAY_MAX - 1] = '\0'; 

    if (sendto(sockfd, &response, sizeof(struct text_error), 0, (struct sockaddr *)client_addr, client_len) < 0) {
        printf("Error sending error response lol");
    } else {
        printf("Error sent to client %s:%d: %s\n", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), response.txt_error);
    }
}

uint64_t generate_id(){
    uint64_t id;
    FILE *urandom = fopen("/dev/urandom", "r");
    fread(&id, sizeof(id), 1, urandom);
    fclose(urandom);
    return id;
}

void add_message_id(uint64_t id) {
    MessageID *new_id = (MessageID*)malloc(sizeof(MessageID));
    new_id->id = id;
    new_id->timestamp = time(NULL);
    new_id->next = message_ids;
    message_ids = new_id;
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

    struct s2s_say s2s_message;
    s2s_message.req_type = S2S_SAY;
    s2s_message.id = generate_id();
    strncpy(s2s_message.txt_channel, channel_name, CHANNEL_MAX);
    strncpy(s2s_message.txt_username, user->username, USERNAME_MAX);
    strncpy(s2s_message.txt_text, say, SAY_MAX);

    add_message_id(s2s_message.id); // Prevent rebroadcast of the same message
    broadcast_s2s_say(sockfd, &s2s_message, NULL);
    
    return 1;
}

int message_id_exists(uint64_t id) {
    MessageID *current = message_ids;
    while (current) {
        if (current->id == id) return 1;
        current = current->next;
    }
    return 0;
}

void handle_s2s_say(int sockfd, struct sockaddr_in *sender, struct s2s_say*buffer){
    Neighbor *sender_neighbor = find_neighbor_by_address(sender);
    //check for duplicates 
    if (message_id_exists(buffer->id)) {
        
        
        printf("%s:%d %s:%d recv duplicate S2S_SAY %s \"%s\"\n",
               inet_ntoa(server_addr_for_ip_display.sin_addr), ntohs(server_addr.sin_port),
               inet_ntoa(sender->sin_addr), ntohs(sender->sin_port),
               buffer->txt_channel, buffer->txt_text);

    if (sender_neighbor) {
        leave_channel(sockfd, sender_neighbor, buffer->txt_channel);
        send_s2s_leave(sockfd, sender, buffer->txt_channel);
    }
        return;
    }

    // Add the message ID to prevent future duplicates
    add_message_id(buffer->id);

    // Log the received message
    printf("%s:%d %s:%d recv S2S_SAY %s \"%s\"\n", inet_ntoa(server_addr_for_ip_display.sin_addr), ntohs(server_addr.sin_port), inet_ntoa(sender->sin_addr), ntohs(sender->sin_port),
        buffer->txt_channel, buffer->txt_text);

    Channel* channel = find_channel_by_name(buffer->txt_channel);
    if (channel) {
        User* current_user = channel->user_list.head;
        struct text_say response;
        response.txt_type = TXT_SAY;
        strncpy(response.txt_channel, buffer->txt_channel, CHANNEL_MAX);
        strncpy(response.txt_username, buffer->txt_username, USERNAME_MAX);
        strncpy(response.txt_text, buffer->txt_text, SAY_MAX);

        while (current_user) {
            if (sendto(sockfd, &response, sizeof(response), 0,
                       (struct sockaddr*)&current_user->addr, sizeof(current_user->addr)) < 0) {
                printf("Error sending S2S_SAY to local user");
            }
            current_user = current_user->next;
        }
    }

    //if there is nowhere too forward leave
    if (should_send_leave(buffer->txt_channel)) {
        leave_channel(sockfd, sender_neighbor, buffer->txt_channel);
        send_s2s_leave(sockfd, sender, buffer->txt_channel);
        remove_channel_sub(buffer->txt_channel);
        return;
    }
    broadcast_s2s_say(sockfd, buffer, sender);
}

void add_neighbor(char* ip, int port){

    char resolved_ip[16];
    if (strcmp(ip, "localhost") == 0) {
        strncpy(resolved_ip, "127.0.0.1", sizeof(resolved_ip));
        resolved_ip[sizeof(resolved_ip) - 1] = '\0';
    }
    
    Neighbor* neighbor_new = (Neighbor*)malloc(sizeof(Neighbor));
    neighbor_new->addr.sin_family = AF_INET;
    neighbor_new->addr.sin_port = htons(port);

    inet_pton(AF_INET, resolved_ip, &neighbor_new->addr.sin_addr);

    neighbor_new->next = neighbors;
    neighbors = neighbor_new;
    printf("Added neighbor %s:%d\n", resolved_ip, port);
}

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

Channel* join_channel(int sockfd, char *channel_name, User *user) {
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

    if(add_channel_sub(channel_name)){
        subscribe_all_neighbors(channel_name);
        broadcast_s2s_join(sockfd, NULL ,channel_name, 0);
    }
    
    return new_channel;
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
    join_channel(sockfd, channel, user);
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
        case S2S_JOIN:
            handle_s2s_join(sockfd, client_addr, (struct request_join *)buffer);
            break;
        case S2S_LEAVE:
            handle_s2s_leave(sockfd, client_addr, (struct s2s_leave*)buffer);
            break;
        case S2S_SAY: 
            handle_s2s_say(sockfd, client_addr, (struct s2s_say *)buffer);
            break;
        default:
            break;  
    }
}

int main(int argc, char *argv[]){

    if (argc < 3 || (argc % 2 != 1)) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd;
    struct sockaddr_in client_addr;
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
        server_addr_for_ip_display.sin_addr.s_addr = inet_addr(arg);
    }else{
        server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    }
    
    server_addr.sin_port = htons(atoi(argv[2]));

    //use this if you need to accept connections from outside the network 
    server_addr.sin_addr.s_addr = INADDR_ANY;



    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Server started on %s:%s\n", argv[1], argv[2]);

    //add neighbors 
    for(int i = 3; i< argc; i+= 2){
        char *n_ip = argv[i];
        int n_port = atoi(argv[i+1]);
        add_neighbor(n_ip, n_port);
    }

    fd_set read_fds;
    struct timeval timeout;
    time_t last_renewal = 0;

     while (1) {
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        timeout.tv_sec = 1; // 1-second timeout
        timeout.tv_usec = 0;

         time_t now = time(NULL);

        //send join every 60 seconds.
        if (difftime(now,last_renewal) >= 60) {
            channel_sub *current = subscriptions;
            while (current) {
                subscribe_all_neighbors(current->name);
                broadcast_s2s_join(sockfd, NULL, current->name, 1);
                current = current->next;
            }
            last_renewal = now;
        }
        
        channel_sub * current = subscriptions;
        // Check for expired subscriptions
        while (current) {
            
            if (difftime(now,current->last_renewed) > 120) {
                printf("now - current %f", difftime(now, current->last_renewed));
                
                //loop through every neighbor, check if it is subscribed and if it is send a leave
                Neighbor *neighbor = neighbors;
                while (neighbor) {
                    if (is_subscribed(neighbor, current->name)) { 
                        send_s2s_leave(sockfd, &neighbor->addr, current->name); //send leave 
                        leave_channel(sockfd, neighbor, current->name); //remove locally in forwarding table 
                    }
                    neighbor = neighbor->next; 
                }
                remove_channel_sub(current->name); //channel did not receive soft join so remove it from subscriptions list
            }
            current = current->next;
        }

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





