/* 
   A simple server in the internet domain using TCP
   Usage:./server port (E.g. ./server 10000 )
*/
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <stdlib.h>
#include <strings.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>

#define BUF_SIZE 65536

int sockfd; //descriptors rturn from socket and accept system calls
FILE *log_file; // 디버깅용 출력을 작성하는 로그 파일
const char *not_found_filename = "404.html"; // 서버에 해당 파일이 없을 때 대신 보낼 html 파일

typedef enum _request_method{
    GET = 0,
    POST,
    PUT,
    HEAD,
    OPTION
} request_method;
typedef enum _req_get_state {
    GET_HEADER_TYPE = 0,
    GET_HEADER_VALUE
} req_get_state;
typedef enum _header_type {
    METHOD = 0,
    HOST,
    USER_AGENT,
    ACCEPT,
    ACCEPT_LANGUAGE,
    ACCEPT_ENCODING,
    CONNECTION,
    UPGRADE_INSECURE_REQUEST,
    CACHE_CONTROL
} header_type;
typedef struct _request {
    request_method method;
    char *filename;
    char *filetype;
    char *version;
    char *host;
    char *user_agent;
    char *accept;
    char *accept_language;
    char *accept_encoding;
    char *connection;
    char *upgrade_insecure_request;
    char *cache_control;
} request;

int get_request(int socketfd, request *req);
int send_response(int socketfd, request *req);
int write_res_header(int socketfd, char *type, char *value, char *t);
void error(char *msg);
void my_handler(int signum);

int main(int argc, char *argv[]) {
    int portno;
    request *req;
    socklen_t clilen;

    /*sockaddr_in: Structure Containing an Internet Address*/
    struct sockaddr_in serv_addr, cli_addr;

    struct sigaction act;

    // 서버가 ctrl+c로 종료되어도 정상적으로 소켓을 닫을 수 있도록 sigaction을 설정해준다
    act.sa_handler = my_handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGINT, &act, NULL);

    // 디버깅을 위한 로그 파일을 연다
    log_file = fopen("log.txt", "w");

    // port 를 받지 못했다면 프로그램을 종료시킨다
    if (argc < 2)
        error("ERROR, no port provided\n");
    
    /*Create a new socket
    AF_INET: Address Domain is Internet 
    SOCK_STREAM: Socket Type is STREAM Socket */
    sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) 
        error("ERROR opening socket");
    
    // serv_addr 구조체를 초기화하고 알맞은 값을 채워넣는다
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = atoi(argv[1]); //atoi converts from String to Integer
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); //for the server the IP address is always the address that the server is running on
    serv_addr.sin_port = htons(portno); //convert from host to network byte order
    
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) //Bind the socket to the server address
        error("ERROR on binding");
    
    // loop를 돌면서 클라이언트의 연결 요청을 읽는다
    while (1) {
        listen(sockfd,5); // Listen for socket connections. Backlog queue (connections to wait) is 5
        
        // 클라이언트와 통신할 용도의 소켓을 새로 연결한다.
        clilen = sizeof(cli_addr);
        int newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) 
            error("ERROR on accept");

        // request 구조체를 초기화하고 클라이언트가 보낸 request를 받는다
        req = malloc(sizeof(request));
        get_request(newsockfd, req);

        // 클라이언트에게서 받은 request를 바탕으로 response를 작성하고 클라이언트에게 보낸다
        send_response(newsockfd, req);
        
        // 다음 request를 위해 메모리 할당을 풀어준다
        free(req);
        close(newsockfd);
    }
    close(sockfd);
    fclose(log_file);
    
    return 0; 
}

void error(char *msg) {
    perror(msg);
    exit(1);
}
void my_handler(int signum) {
    close(sockfd);
    fclose(log_file);
    fprintf(stderr, "server terminated by signal\n");
    exit(0);
}
int get_request(int socketfd, request *req) {
    int i, n, cursor;
    char buffer[BUF_SIZE];
    char parse_buffer[1024], *tmp;
    req_get_state state;
    header_type h_type;

    // 버퍼를 비우고 소켓에서 request 를 읽어 버퍼에 넣는다
    bzero(buffer, BUF_SIZE); // buffer를 비운다
    n = read(socketfd, buffer, BUF_SIZE-1);
    if (n < 0) error("ERROR reading from socket");
    fprintf(log_file, "Here is the message:=====\n%s\n=========================\n",buffer);

    i = cursor = 0;
    state = GET_HEADER_TYPE;
    
    while (buffer[i] != '\0' && i < BUF_SIZE) { // 문자열이 끝나거나 버퍼의 끝까지 읽으면 멈춤
        switch (state) { // 현재 커서가 위치한 문자열의 종류(헤더의 종류인지  헤더의 값인지)에 따라 다른 동작을 함
        case GET_HEADER_TYPE: // 헤더의 종류를 읽는 상태일 때
            if (buffer[i] == ' ') { // 줄이 시작되고 처음 공백 문자열을 받으면 == 헤더의 종류를 나타내는 문자열을 끝까지 읽으면
                // 헤더의 종류를 parse_buffer에 저장함
                bzero(parse_buffer, 1024);
                strncpy(parse_buffer, buffer+cursor, i-cursor);

                // 헤더의 종류가 무엇인지 저장함
                if        (!strcmp(parse_buffer, "GET")) {
                    h_type = METHOD;
                } else if (!strcmp(parse_buffer, "Host:")) {
                    h_type = HOST;
                } else if (!strcmp(parse_buffer, "User-Agent:")) {
                    h_type = USER_AGENT;
                } else if (!strcmp(parse_buffer, "Accept:")) {
                    h_type = ACCEPT;
                } else if (!strcmp(parse_buffer, "Accept-Language:")) {
                    h_type = ACCEPT_LANGUAGE;
                } else if (!strcmp(parse_buffer, "Accept-Encoding:")) {
                    h_type = ACCEPT_ENCODING;
                } else if (!strcmp(parse_buffer, "Connection:")) {
                    h_type = CONNECTION;
                } else if (!strcmp(parse_buffer, "Upgrade-Insecure-Requests:")) {
                    h_type = UPGRADE_INSECURE_REQUEST;
                } else if (!strcmp(parse_buffer, "Cache-Control:")) {
                    h_type = CACHE_CONTROL;
                }

                state = GET_HEADER_VALUE; // 상태를 헤더 값을 읽는 상태로 변경
                cursor = i+1; // 커서는 헤더 값의 처음에 둠
            }
            break;

        case GET_HEADER_VALUE: // 헤더의 값을 읽는 상태일때
            if (buffer[i] == '\r') { // 줄이 끝나면 == 헤더 값의 끝까지 왔으면
                // 헤더의 값을 parse_buffer에 저장함
                bzero(parse_buffer, 1024);
                strncpy(parse_buffer, buffer+cursor, i-cursor);
                
                switch (h_type) { // 헤더 종류에 맞춰 request 구조체의 값을 채운다.
                case METHOD: // 특별히 첫번째 줄일때는 url과 http버전을 분리하여 저장한다.
                    tmp = strrchr(parse_buffer, 'H');

                    req->version = malloc(sizeof(char)*(strlen(tmp)));
                    strcpy(req->version, tmp);

                    parse_buffer[tmp-parse_buffer-1] = '\0';

                    if (strrchr(parse_buffer, '/')-parse_buffer+1 == strlen(parse_buffer))
                        strcat(parse_buffer, "index.html");

                    req->filename = malloc(sizeof(char)*(strlen(parse_buffer)+1));
                    strcpy(req->filename, ".");
                    strcat(req->filename, parse_buffer);
                    break;

                case HOST:
                    req->host = malloc(sizeof(char)*(strlen(parse_buffer)));
                    strcpy(req->host, parse_buffer);
                    break;

                case USER_AGENT:
                    req->user_agent = malloc(sizeof(char)*(strlen(parse_buffer)));
                    strcpy(req->user_agent, parse_buffer);
                    break;

                case ACCEPT:
                    req->accept = malloc(sizeof(char)*(strlen(parse_buffer)));
                    strcpy(req->accept, parse_buffer);
                    break;

                case ACCEPT_LANGUAGE:
                    req->accept_language = malloc(sizeof(char)*(strlen(parse_buffer)));
                    strcpy(req->accept_language, parse_buffer);
                    break;

                case ACCEPT_ENCODING:
                    req->accept_encoding = malloc(sizeof(char)*(strlen(parse_buffer)));
                    strcpy(req->accept_encoding, parse_buffer);
                    break;

                case CONNECTION:
                    req->connection = malloc(sizeof(char)*(strlen(parse_buffer)));
                    strcpy(req->connection, parse_buffer);
                    break;

                case UPGRADE_INSECURE_REQUEST:
                    req->upgrade_insecure_request = malloc(sizeof(char)*(strlen(parse_buffer)));
                    strcpy(req->upgrade_insecure_request, parse_buffer);
                    break;

                case CACHE_CONTROL:
                    req->cache_control = malloc(sizeof(char)*(strlen(parse_buffer)));
                    strcpy(req->cache_control, parse_buffer);
                    break;

                default:
                    break;
                }
                state = GET_HEADER_TYPE; // 상태를 헤더 타입을 읽는 상태로 변경
                i++; // 뒤의 \n 문자는 넘어간다
                cursor = i+1; // 커서를 헤더 타입의 처음에 논다
            }
            break;
        
        default:
            break;
        }

        i++;
    }

    return 0;
}
int send_response(int socketfd, request *req) {
    int n, content_buffer_size;
    int content_file;
    char *content_buffer;
    char content_length[30], content_type[50];

    // 요청한 파일을 연다
    fprintf(log_file, "open %s file\n", req->filename);
    content_file = open(req->filename, O_RDONLY);

    //만약 파일이 존재하지 않으면
    if (content_file == -1 ) {
        fprintf(log_file, "File not exist\n");

        //content 대신 보낼 파일을 연다
        content_file = open(not_found_filename, O_RDONLY);
        if (content_file == -1) { // 없으면 임의의 문자열을 content_buffer에 저장한다
            content_buffer = malloc(sizeof(char)*(14));
            strcpy(content_buffer, "404 not found");
        } else {
            // content를 담을 버퍼의 크기를 알맞게 설정한다
            content_buffer_size = lseek(content_file, 0, SEEK_END);
            sprintf(content_length, "%d", content_buffer_size);
            content_buffer = malloc(sizeof(char)*(content_buffer_size+1));

            lseek(content_file, 0, SEEK_SET);
            n = read(content_file, content_buffer, content_buffer_size);
            if (n < 0) error("ERROR reading from socket");
        }

        // 헤더를 작성한다
        strcpy(content_type, "text/html; charset=utf-8");
        write_res_header(socketfd, req->version, "404 Not Found", " ");
        write_res_header(socketfd, "content-type", content_type, ": ");
        write_res_header(socketfd, "content-length", content_length, ": ");

        n = write(socketfd, "\r\n", 2);
        if (n < 0) error("ERROR writing to socket");

        // 데이터를 작성한다
        n = write(socketfd, content_buffer, content_buffer_size);
        if (n < 0) error("ERROR writing to socket");
        
        return -1;
    } else {
        // content를 담을 버퍼의 크기를 알맞게 설정한다
        content_buffer_size = lseek(content_file, 0, SEEK_END);
        sprintf(content_length, "%d", content_buffer_size);
        content_buffer = malloc(sizeof(char)*(content_buffer_size+1));

        // content를 읽어 버퍼에 저장한다
        lseek(content_file, 0, SEEK_SET);
        n = read(content_file, content_buffer, content_buffer_size);
        if (n < 0) error("ERROR reading from socket");

        // content-type을 정한다
        char *tmp = strrchr(req->filename, '.');
        if (strcmp(tmp, ".html") == 0) {
            strcpy(content_type, "text/html; charset=utf-8");
        } else if (strcmp(tmp, ".css") == 0) {
            strcpy(content_type, "text/css; charset=utf-8");
        } else if (strcmp(tmp, ".js") == 0) {
            strcpy(content_type, "text/javascript; charset=utf-8");
        } else if (strcmp(tmp, ".gif") == 0) {
            strcpy(content_type, "image/gif");
        } else if (strcmp(tmp, ".jpg") == 0 ||
                   strcmp(tmp, ".jpeg") == 0 ||
                   strcmp(tmp, ".jpe") == 0 ||
                   strcmp(tmp, ".jfif") == 0) {
            strcpy(content_type, "image/jpeg");
        } else if (strcmp(tmp, ".mp3") == 0) {
            strcpy(content_type, "audio/mpeg");
        } else if (strcmp(tmp, ".pdf") == 0) {
            strcpy(content_type, "application/pdf");
        }

        // 헤더를 작성한다
        write_res_header(socketfd, req->version, "200 OK", " ");
        write_res_header(socketfd, "content-type", content_type, ": ");
        write_res_header(socketfd, "content-length", content_length, ": ");

        n = write(socketfd, "\r\n", 2);
        if (n < 0) error("ERROR writing to socket");

        // 데이터를 작성한다
        n = write(socketfd, content_buffer, content_buffer_size);
        if (n < 0) error("ERROR writing to socket");
    }

    return 0;
}
int write_res_header(int socketfd, char *type, char *value, char *t) {
    int n;
    //헤더 타입, 값을 클라이언트에게 보낸다
    n = write(socketfd, type, strlen(type));
        if (n < 0) error("ERROR writing to socket");
    n = write(socketfd, t, strlen(t));
        if (n < 0) error("ERROR writing to socket");
    n = write(socketfd, value, strlen(value));
        if (n < 0) error("ERROR writing to socket");
    n = write(socketfd, "\r\n", 2);
        if (n < 0) error("ERROR writing to socket");
    return 0;
}