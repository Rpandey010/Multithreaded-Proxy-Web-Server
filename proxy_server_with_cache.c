#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define MAX_CLIENTS 10
#define MAX_BYTES 4096

typedef struct cache_element cache_element;

struct cache_element {
    char* data;
    int len;
    char* url;
    time_t lru_time_track;
    cache_element* next;
};

cache_element* find(char* url);
int add_cache_element(char* data, int size, char* url);
void remove_cache_element();

int port_number = 8080;
int proxy_socketId;
pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;

cache_element* head;
int cache_size;


//Send Error Message Function---------------------------------------------------------
int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}

int connectRemoteServer(char* host_addr, int port_num){
    // Creating Socket for remote server ---------------------------

    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket < 0){
        printf("Failed to create a socket\n");
        return -1;
    }

    // Get host by the name or ip address provided -----------------
    struct hostent* host = gethostbyname(host_addr);
    if(host == NULL){
        fprintf(stderr, "Host not found\n");
        return -1;
    }

    // inserts ip address and port number of host in struct `server_addr`
	struct sockaddr_in server_addr;

	bzero((char*)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port_num);

	// bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr,host->h_length);
    bcopy(host->h_addr_list[0], (char *)&server_addr.sin_addr.s_addr, host->h_length);

    // Connect to the remote server --------------------------------
    if(connect(remoteSocket, (struct sockaddr*)&server_addr, (size_t)sizeof(server_addr) < 0)){
        fprintf(stderr, "Failed to connect to the remote server\n");
        return -1;
    }
    // free(host_addr);
    return remoteSocket;
}

//Handle Request Function-----------------------------------------------------------------
int handle_request(int clientSockedId, ParsedRequest *request, char* tempReq){
    char *buf = (char *)malloc(sizeof(char)*MAX_BYTES);
    strcpy(buf, "GET");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");
    
    size_t len = strlen(buf);

    if(ParsedHeader_set(request, "Connection", "close") < 0){
        printf("Set header not working, Failed to set the connection\n");
    }

    if(ParsedHeader_get(request, "Host") == NULL){
        if(ParsedHeader_set(request, "Host", request->host) < 0){
            printf("Failed to set the host\n");
        }
    }

    if(ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0){
        printf("Failed to unparse the headers\n");
    }

    int serve_port = 80;
    if(request->port != NULL){
        serve_port = atoi(request->port);
    }
    int remoteSocketId = connectRemoteServer(request->host, serve_port);
    if(remoteSocketId < 0){
        printf("Failed to connect to the remote server\n");
        return -1;
    }
    int bytes_send = send(remoteSocketId, buf, strlen(buf), 0);
    bzero(buf, MAX_BYTES);

    bytes_send = recv(remoteSocketId, buf, MAX_BYTES - 1, 0);
    char * temp_buffer = (char*)malloc(sizeof(char)*MAX_BYTES);
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while(bytes_send > 0){
        bytes_send = send(clientSockedId, buf, bytes_send, 0);
        for(int i = 0; i < bytes_send/sizeof(char); i++){
            temp_buffer[temp_buffer_index] = buf[i];
            temp_buffer_index++;
        }
        temp_buffer_size += MAX_BYTES;
        temp_buffer = (char*)realloc(temp_buffer, temp_buffer_size);
        if(bytes_send < 0){
            perror("Failed to send the data to the client\n");
            break;
        }
        bzero(buf, MAX_BYTES);
        bytes_send = recv(remoteSocketId, buf, MAX_BYTES - 1, 0);
    }
    temp_buffer[temp_buffer_index] = '\0';
    free(buf);
    add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
    free(temp_buffer);
    close(remoteSocketId);
    return 0;
}    

//Function to check the HTTP version---------------------------------- 1:27:28

int checkHTTPversion(char *msg)
{
	int version = -1;

	if(strncmp(msg, "HTTP/1.1", 8) == 0)
	{
		version = 1;
	}
	else if(strncmp(msg, "HTTP/1.0", 8) == 0)			
	{
		version = 1;										// Handling this similar to version 1.1
	}
	else
		version = -1;

	return version;
}

//THREAD FUNCTION-------------------------------------------------------
void *thread_fn(void *socketNew){
    sem_wait(&semaphore);     // Acquire semaphore lock. If unavailable, wait until it is.
    int p;
    sem_getvalue(&semaphore, p);
    printf("Semaphore value: %d\n", p);
    int *t = (int*) socketNew;
    int socket = *t;          // Socket is socket descriptor of the connected Client
    int bytes_send_client, len;    // Bytes Transferred
 
    char *buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    bzero(buffer, MAX_BYTES);  //to remove garbage values
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);   // Receiving the Request of client by proxy server

    while(bytes_send_client > 0){
        len = strlen(buffer);
        //loop until u find "\r\n\r\n" in the buffer
        if(strstr(buffer, "\r\n\r\n") == NULL){
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }else{
            break;
        }
    }

    char *tempReq = (char*)malloc(strlen(buffer)*sizeof(char)+1);
    for(int i = 0; i<strlen(buffer); i++){
        tempReq[i] = buffer[i];
    }
    struct cache_element* temp = find(tempReq);
    if(temp != NULL){
        int size = temp->len/sizeof(char);
        int pos = 0;
        char response[MAX_BYTES];
        while(pos<size){
            bzero(response, MAX_BYTES);
            for(int i = 0; i<MAX_BYTES; i++){
                response[i] = temp->data[i];
                pos++;
            }
            send(socket, response, MAX_BYTES, 0);
        }
        printf("Data retrieved from the cache\n");
        printf("%s\n\n", response);
    } else if(bytes_send_client > 0){
        len = strlen(buffer);
        ParsedRequest *request = ParsedRequest_create();
        
        if(ParsedRequest_parse(request, buffer, len) < 0){
            printf("Parsing failed\n");
            exit(1);
        }else{
            bzero(buffer, MAX_BYTES);
            if(!strcmp(request->method, "GET")){
                if(request->host && request->path && checkHTTPversion(request->version)==1){
                    bytes_send_client = handle_request(socket, request, tempReq);
                    if(bytes_send_client == -1){
                        sendErrorMessage(socket, 500);
                    }
                }else{
                    sendErrorMessage(socket, 500);
                }
            }else{
                printf("This code doesn't spport any method apart from GET\n");
            }
        }
        ParsedRequest_destroy(request);
    }else if(bytes_send_client == 0){
        printf("Client is disconnected\n");
    }
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    sem_post(&semaphore);
    sem_getvalue(&semaphore, p);
    printf("Semaphore post value is %d\n", p);
    free(tempReq);
    return NULL;
}

int main(int argc, char* argv[]){
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);
    if(argv == 2){
        port_number = atoi(argv[1]);
    } else {
        printf("Too few arguments\n");
        exit(1);
    }

    printf("Starting Proxy server at port %d\n", port_number);
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);

    if (proxy_socketId < 0){
        perror("Failed to create a socket\n");
        exit(1);
    }
    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0){
        perror("Failed to set socket options\n");
    }

    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr) < 0)){
        perror("Failed to bind the socket Port is not available\n");
        exit(1);
    }
    printf("Binding on port %d\n", port_number);
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if(listen_status < 0){
        perror("Failed to listen on the socket\n");
        exit(1);
    }
    int i = 0;
    int Connected_socketId[MAX_CLIENTS];

    while(1){
        bzero((char*)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, (socklen_t*)&client_len);
        if(client_socketId < 0){
            printf("Not able to connect");
            exit(1);
        }
        else{
            Connected_socketId[i] = client_socketId;
        }
        struct sockaddr_in * client_pt = (struct sockaddr_in *)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        printf("Client is connected with port number %d and IP address %s\n", ntohs(client_addr.sin_port), str);

        pthread_create(&tid[i], NULL, thread_fn, (void *)&Connected_socketId[i]);
        i++;
    }
    close(proxy_socketId);
    return 0;
}


cache_element* find(char* url){

// Checks for url in the cache if found returns pointer to the respective cache element or else returns NULL
    cache_element* site=NULL;
	//sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Remove Cache Lock Acquired %d\n",temp_lock_val); 
    if(head!=NULL){
        site = head;
        while (site!=NULL)
        {
            if(!strcmp(site->url,url)){
				printf("LRU Time Track Before : %ld", site->lru_time_track);
                printf("\nurl found\n");
				// Updating the time_track
				site->lru_time_track = time(NULL);
				printf("LRU Time Track After : %ld", site->lru_time_track);
				break;
            }
            site=site->next;
        }       
    }
	else {
    printf("\nurl not found\n");
	}
	//sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
    return site;
}