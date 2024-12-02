#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "duckchat.h"
#include <netdb.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/select.h>
#include "raw.h"
#define BUFFER_SIZE 1024 

const char *hostname;
int sockfd;
char buffer[BUFFER_SIZE];
char active_channel[CHANNEL_MAX];

typedef struct Channel {
    char name[CHANNEL_MAX];
    struct Channel *next;
} Channel;

Channel *channels = NULL;
char user_input[BUFFER_SIZE];

void clear_prompt_line() {
    printf("\033[2K");       
    printf("\r");           
    fflush(stdout);          
}

void free_channels() {
    Channel *current = channels;
    while (current != NULL) {
        Channel *next = current->next;
        free(current);
        current = next;
    }
    channels = NULL;
}

void add_channel(char* channel_name){
    Channel * new_channel = (Channel * )malloc(sizeof(Channel));
    strncpy(new_channel->name, channel_name, CHANNEL_MAX-1);
    new_channel->name[CHANNEL_MAX-1] = '\0';
    new_channel->next = channels;
    channels = new_channel;
}

void remove_channel(char *channel_name) {
    Channel *current = channels;
    Channel *previous = NULL;
    while (current != NULL) {
        if (strncmp(current->name, channel_name, CHANNEL_MAX) == 0) {
            if (previous == NULL) {
                channels = current->next;
            } else {
                previous->next = current->next;
            }
            free(current);
            return;
        }
        previous = current;
        current = current->next;
    }
}

int is_channel(char* channel_name){
    Channel* current_channel = channels;
    while(current_channel != NULL){
        if(strcmp(current_channel->name, channel_name) == 0){
            return 1;
        }
        current_channel = current_channel->next;
    }
    return 0;
}

void handle_server_response(char *buffer) {
    struct text *response = (struct text *)buffer;
    clear_prompt_line();
    raw_mode();
    // Switch to raw mode to handle display without disrupting user input
    //response->txt_type = htonl(response->txt_type);
    switch (response->txt_type) {
        case TXT_SAY: {
            struct text_say *say_response = (struct text_say *)buffer;
            printf("\r[%s][%s]: %s\n", say_response->txt_channel, say_response->txt_username, say_response->txt_text);
            break;
        }
        case TXT_LIST: {
            struct text_list *list_response = (struct text_list *)buffer;
            printf("Active channels:\n");
                //list_response->txt_type = ntohl(list_response->txt_type);
                //list_response->txt_nchannels = ntohl(list_response->txt_nchannels);
            for (int i = 0; i < list_response->txt_nchannels; i++) {
                printf("  %s\n", list_response->txt_channels[i].ch_channel);
            }
            break;
        }
        case TXT_WHO: {
            struct text_who *who_response = (struct text_who *)buffer;
            printf("Users on channel %s:\n", who_response->txt_channel);
            for (int i = 0; i < who_response->txt_nusernames; i++) {
                printf("  %s\n", who_response->txt_users[i].us_username);
            }
            break;
        }
        case TXT_ERROR: {
            struct text_error *error_response = (struct text_error *)buffer;
            printf("Error: %s\n", error_response->txt_error);
            break;
        }
        default:
            fprintf(stderr, "Unknown response type: %d\n", response->txt_type);
    }

    cooked_mode();
    printf("> %s", user_input);  
    fflush(stdout);
}

int send_join(int sockfd, struct sockaddr_in *server_addr, char *channel) {
    struct request_join join_request;
    join_request.req_type = REQ_JOIN;
    strncpy(join_request.req_channel, channel, CHANNEL_MAX);

    if(is_channel(channel) == 0){
        add_channel(channel);
    }
    if (sendto(sockfd, &join_request, sizeof(join_request), 0, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("Error sending join request");
        return -1;
    }else{
        strncpy(active_channel, channel, CHANNEL_MAX);
    }
    return 1;
}

int send_login(int sockfd, struct sockaddr_in *server_addr, const char *username) {
    struct request_login login_request;
    login_request.req_type = REQ_LOGIN;
    strncpy(login_request.req_username, username, USERNAME_MAX);

    if (sendto(sockfd, &login_request, sizeof(login_request), 0, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("Error sending login request");
        return -1;
    }
    send_join(sockfd, server_addr, active_channel);

    return 1;
}

int send_list(int sockfd, struct sockaddr_in *server_addr) {
    struct request_list list_request;
    list_request.req_type = REQ_LIST;
    if (sendto(sockfd, &list_request, sizeof(list_request), 0, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("Error sending list request");
        return -1;
    }
    return 1;
}

int send_who(int sockfd, struct sockaddr_in *server_addr, const char *channel) {
    struct request_who who_request;
    who_request.req_type = REQ_WHO;
    strncpy(who_request.req_channel, channel, CHANNEL_MAX);

    if (sendto(sockfd, &who_request, sizeof(who_request), 0, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("Error sending who request");
        return -1;
    }
    return 1;
}

int send_leave(int sockfd, struct sockaddr_in *server_addr, const char *channel) {
    struct request_leave leave_request;
    leave_request.req_type = REQ_LEAVE;
    strncpy(leave_request.req_channel, channel, CHANNEL_MAX);

    if (sendto(sockfd, &leave_request, sizeof(leave_request), 0, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("Error sending leave request");
        return -1;
    }
    return 1;
}

int send_say(int sockfd, struct sockaddr_in *server_addr, const char *channel, const char *text) {
    struct request_say say_request;
    say_request.req_type = REQ_SAY;
    strncpy(say_request.req_channel, channel, CHANNEL_MAX);
    strncpy(say_request.req_text, text, SAY_MAX);

    if (sendto(sockfd, &say_request, sizeof(say_request), 0, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("Error sending say request");
        return -1;
    }
    return 1;
}

int send_logout(int sockfd, struct sockaddr_in *server_addr) {
    struct request_logout logout_request;
    logout_request.req_type = REQ_LOGOUT;

    if (sendto(sockfd, &logout_request, sizeof(logout_request), 0, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("Error sending logout request");
        
        return -1;
    }
    return 1;
}

void parse_data(char* input, int sockfd, struct sockaddr_in *server_addr, char *active_channel) {    
    char* token; 

    char input_copy[SAY_MAX];
    input[strcspn(input, "\n")] = '\0';
    strncpy(input_copy, input, SAY_MAX);
    token = strtok(input_copy, " ");

    if (token == NULL){
        printf(">");
        fflush(stdout);
        return;
    }

    if (strcmp(token, "/list") == 0) {
        send_list(sockfd, server_addr);

    } else if (strcmp(token, "/who") == 0) {
        char *channel = strtok(NULL, " "); //return next token, ie the channel
        if (channel) send_who(sockfd, server_addr, channel);
        else printf("Usage: /who <channel>\n");

    } else if (strcmp(token, "/join") == 0) {
        char *channel = strtok(NULL, " ");
        if (channel) {
            send_join(sockfd, server_addr, channel);
            strncpy(active_channel, channel, CHANNEL_MAX);
        } else {
            printf("Usage: /join <channel>\n");
        }

    } else if (strcmp(token, "/leave") == 0) {
        char *channel = strtok(NULL, " ");
        if (channel) send_leave(sockfd, server_addr, channel);
        else printf("Usage: /leave <channel>\n");

    } else if (strcmp(token, "/switch") == 0) {

        char *channel = strtok(NULL, " ");
        if (channel) {
            // In DuckChat, /switch does not require server interaction, just update active channel
            if(is_channel(channel)){
                strncpy(active_channel, channel, CHANNEL_MAX);
                printf("Switched to channel: %s\n", active_channel);
            }else{
                
                printf("You can only switch to a channel that you are a member of\n");
    
            }
        } else {
            printf("Usage: /switch <channel>\n");
        }
    } else if (strcmp(token, "/exit") == 0) {
        send_logout(sockfd, server_addr);
        printf("Exiting DuckChat...\n");
        free_channels();
        exit(0);
    }else if (token[0] == '/'){
        printf("invalid command\n");
    } else {   
        // If it's not a command, assume it's a message to send to the active channel
        if (strlen(active_channel) == 0) {
            printf("Error: No active channel. Join a channel first.\n");
        } else {
            send_say(sockfd, server_addr, active_channel, input);
        }
    }
    clear_prompt_line();
}

int main(int argc, char* argv[]){
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server-hostname> <server-port> <username>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    //get parameter values

    //check for localhost
    char arg[10];
    if(strcmp(argv[1], "localhost") == 0){
        strncpy(arg, "127.0.0.1", sizeof(arg));
        arg[sizeof(arg)-1] = '\0';
        hostname = arg;
    }else{
        hostname = argv[1];
    }
    int port = atoi(argv[2]);
    const char *username = argv[3];
    strncpy(active_channel, "Common", CHANNEL_MAX);
    // check if username is less or equal too max length 
    if (strlen(username) >= USERNAME_MAX) {
        fprintf(stderr, "Username must be less than %d characters.\n", USERNAME_MAX);
        exit(EXIT_FAILURE);
    }
    
    printf("hostname: %s, port: %d, username: %s \n", hostname, port, username);

    //create socket 
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; //family is ipv4
    server_addr.sin_port = htons(port); //convert host byte order to network byte order 
    socklen_t server_len = sizeof(server_addr);

    if (inet_pton(AF_INET, hostname, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server address\n");
        exit(EXIT_FAILURE);
    }
    
    if(send_login(sockfd, &server_addr, username) < 0){
        exit(EXIT_FAILURE);
    }

    fd_set read_fds;
    struct timeval timeout;
    int max_fd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;
    int input_pos = 0;
    
    printf(">");
    fflush(stdout);
    while(1){
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        timeout.tv_sec = 1; // 1-second timeout
        timeout.tv_usec = 0;
        int activity = select(max_fd+1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0 && errno != EINTR) {
            perror("select error");
            break;
        }else if(activity > 0){
            if (FD_ISSET(sockfd, &read_fds)) {
                int bytes_recieved = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&server_addr, &server_len);
                if  (bytes_recieved > 0){
                    buffer[bytes_recieved] = '\0';
                    handle_server_response(buffer);
                }
                if (bytes_recieved < 0) {
                    perror("recvfrom failed");
                    continue;
                }
            }
            if(FD_ISSET(STDIN_FILENO, &read_fds)){
                char ch;
                read(STDIN_FILENO, &ch, 1);  // Read a single character

                if (ch == '\n') { //newline execute input 
                    parse_data(user_input, sockfd, &server_addr, active_channel);
                    memset(user_input, 0, SAY_MAX); //clear user input. 
                    input_pos = 0;  
                } else if (ch == '\b') {  //backspace 
                    if (input_pos > 0) {
                        input_pos--; 
                        user_input[input_pos] = '\0'; 

                        printf("\b \b");  
                        clear_prompt_line();
                        printf("\r> %s", user_input); 
                        fflush(stdout); 
                    }
                } else if (input_pos < SAY_MAX - 1) { //handle edits
                    user_input[input_pos++] = ch;  
                }
                clear_prompt_line();
                printf("\r> %s", user_input); 
                fflush(stdout);
            }
        }
    }
}