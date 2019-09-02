#include "tftpserv.h"
 
void cld_handler(int sig) {
     int status;
     wait(&status);
}
 
ssize_t tftp_send_data(int s, uint16_t block_number, uint8_t *data, ssize_t dlen, struct sockaddr_in *sock, socklen_t slen)
{
     tftp_message message;
     ssize_t c;
 
     message.opcode = htons(DATA);
     message.data.block_number = htons(block_number);
     memcpy(message.data.data, data, dlen);
 
     if ((c = sendto(s, &message, 4 + dlen, 0, (struct sockaddr *) sock, slen)) < 0) {
          perror("server: sendto()");
     }
 
     return c;
}
 
ssize_t send_ack(int s, uint16_t block_number, struct sockaddr_in *sock, socklen_t slen)
{
     tftp_message message;
     ssize_t c;
 
     message.opcode = htons(ACK);
     message.ack.block_number = htons(block_number);
 
     if ((c = sendto(s, &message, sizeof(message.ack), 0, (struct sockaddr *) sock, slen)) < 0) {
          perror("server: sendto()");
     }
 
     return c;
}
 
ssize_t send_error(int s, int error_code, char *error_string, struct sockaddr_in *sock, socklen_t slen)
{
     tftp_message m;
     ssize_t c;
 
     if(strlen(error_string) >= 512) {
          fprintf(stderr, "server: send_error(): error string too long\n");
          return -1;
     }
 
     m.opcode = htons(ERROR);
     m.error.error_code = error_code;
     strcpy(m.error.error_string, error_string);
 
     if ((c = sendto(s, &m, 4 + strlen(error_string) + 1, 0,
                     (struct sockaddr *) sock, slen)) < 0) {
          perror("server: sendto()");
     }
 
     return c;
}
 
ssize_t recv_message(int s, tftp_message *m, struct sockaddr_in *sock, socklen_t *slen)
{
     ssize_t c;
 
     if ((c = recvfrom(s, m, sizeof(*m), 0, (struct sockaddr *) sock, slen)) < 0
          && errno != EAGAIN) {
          perror("server: recvfrom()");
     }
 
     return c;
}
 
void handle_request(tftp_message *m, ssize_t len, struct sockaddr_in *client_sock, socklen_t slen)
{
     int s;
     struct protoent *pp;
     struct timeval tv;
 
     char *filename, *mode_s, *end;
     FILE *fd;
 
     int mode;
     uint16_t opcode;
 
     /* open new socket, on new port, to handle client request */
 
     if ((pp = getprotobyname("udp")) == 0) {
          fprintf(stderr, "server: getprotobyname() error\n");
          exit(1);
     }
 
     if ((s = socket(AF_INET, SOCK_DGRAM, pp->p_proto)) == -1) {
          perror("server: socket()");
          exit(1);
     }
 
     tv.tv_sec  = RECV_TIMEOUT;
     tv.tv_usec = 0;
 
     if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
          perror("server: setsockopt()");
          exit(1);
     }
 
     /* parse client request */
 
     filename = m->request.filename_and_mode;
     end = &filename[len - 2 - 1];
 
     if (*end != '\0') {
          printf("%s.%u: invalid filename or mode\n",
                 inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
          send_error(s, 0, "invalid filename or mode", client_sock, slen);
          exit(1);
     }
 
     mode_s = strchr(filename, '\0') + 1; 
 
     if (mode_s > end) {
          printf("%s.%u: transfer mode not specified\n",
                 inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
          send_error(s, 0, "transfer mode not specified", client_sock, slen);
          exit(1);
     }
 
     if(strncmp(filename, "../", 3) == 0 || strstr(filename, "/../") != NULL ||
        (filename[0] == '/' && strncmp(filename, base_directory, strlen(base_directory)) != 0)) {
          printf("%s.%u: filename outside base directory\n",
                 inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
          send_error(s, 0, "filename outside base directory", client_sock, slen);
          exit(1);
     }
 
     opcode = ntohs(m->opcode);
     fd = fopen(filename, opcode == RRQ ? "r" : "w"); 
 
     if (fd == NULL) {
          perror("server: fopen()");
          send_error(s, errno, strerror(errno), client_sock, slen);
          exit(1);
     }
 
     mode = strcasecmp(mode_s, "netascii") ? NETASCII :
          strcasecmp(mode_s, "octet")    ? OCTET    :
          0;
 
     if (mode == 0) {
          printf("%s.%u: invalid transfer mode\n",
                 inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
          send_error(s, 0, "invalid transfer mode", client_sock, slen);
          exit(1);
     }
 
     printf("%s.%u: request received: %s '%s' %s\n", 
            inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port),
            ntohs(m->opcode) == RRQ ? "get" : "put", filename, mode_s);
 
     if (opcode == RRQ) {
          tftp_message m;
 
          uint8_t data[512];
          ssize_t dlen, c;
 
          uint16_t block_number = 0;
          
          int countdown;
          int to_close = 0;
          
          while (!to_close) {
 
               dlen = fread(data, 1, sizeof(data), fd);
               block_number++;
               
               if (dlen < 512) { // last data block to send
                    to_close = 1;
               }
 
               for (countdown = RECV_RETRIES; countdown; countdown--) {
 
                    c = tftp_send_data(s, block_number, data, dlen, client_sock, slen);
                
                    if (c < 0) {
                         printf("%s.%u: transfer killed\n",
                                inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                         exit(1);
                    }
 
                    c = recv_message(s, &m, client_sock, &slen);
                     
                    if (c >= 0 && c < 4) {
                         printf("%s.%u: message with invalid size received\n",
                                inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                         send_error(s, 0, "invalid request size", client_sock, slen);
                         exit(1);
                    }
 
                    if (c >= 4) {
                         break;
                    }
 
                    if (errno != EAGAIN) {
                         printf("%s.%u: transfer killed\n",
                                inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                         exit(1);
                    }
 
               }
 
               if (!countdown) {
                    printf("%s.%u: transfer timed out\n",
                           inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                    exit(1);
               }
 
               if (ntohs(m.opcode) == ERROR)  {
                    printf("%s.%u: error message received: %u %s\n",
                           inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port),
                           ntohs(m.error.error_code), m.error.error_string);
                    exit(1);
               }
 
               if (ntohs(m.opcode) != ACK)  {
                    printf("%s.%u: invalid message during transfer received\n",
                           inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                    send_error(s, 0, "invalid message during transfer", client_sock, slen);
                    exit(1);
               }
               
               if (ntohs(m.ack.block_number) != block_number) { // the ack number is too high
                    printf("%s.%u: invalid ack number received\n", 
                           inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                    send_error(s, 0, "invalid ack number", client_sock, slen);
                    exit(1);
               }
 
          }
 
     }
 
     else if (opcode == WRQ) {
 
          tftp_message m;
 
          ssize_t c;
 
          uint16_t block_number = 0;
          
          int countdown;
          int to_close = 0;
 
          c = send_ack(s, block_number, client_sock, slen);
           
          if (c < 0) {
               printf("%s.%u: transfer killed\n",
                      inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
               exit(1);
          }
 
          while (!to_close) {
 
               for (countdown = RECV_RETRIES; countdown; countdown--) {
 
                    c = recv_message(s, &m, client_sock, &slen);
 
                    if (c >= 0 && c < 4) {
                         printf("%s.%u: message with invalid size received\n",
                                inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                         send_error(s, 0, "invalid request size", client_sock, slen);
                         exit(1);
                    }
 
                    if (c >= 4) {
                         break;
                    }
 
                    if (errno != EAGAIN) {
                         printf("%s.%u: transfer killed\n",
                                inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                         exit(1);
                    }
 
                    c = send_ack(s, block_number, client_sock, slen);
               
                    if (c < 0) {
                         printf("%s.%u: transfer killed\n",
                                inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                         exit(1);
                    }
 
               }
 
               if (!countdown) {
                    printf("%s.%u: transfer timed out\n",
                           inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                    exit(1);
               }
 
               block_number++;
 
               if (c < sizeof(m.data)) {
                    to_close = 1;
               }
 
               if (ntohs(m.opcode) == ERROR)  {
                    printf("%s.%u: error message received: %u %s\n",
                           inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port),
                           ntohs(m.error.error_code), m.error.error_string);
                    exit(1);
               }
 
               if (ntohs(m.opcode) != DATA)  {
                    printf("%s.%u: invalid message during transfer received\n",
                           inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                    send_error(s, 0, "invalid message during transfer", client_sock, slen);
                    exit(1);
               }
               
               if (ntohs(m.ack.block_number) != block_number) {
                    printf("%s.%u: invalid block number received\n", 
                           inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                    send_error(s, 0, "invalid block number", client_sock, slen);
                    exit(1);
               }
 
               c = fwrite(m.data.data, 1, c - 4, fd);
 
               if (c < 0) {
                    perror("server: fwrite()");
                    exit(1);
               }
 
               c = send_ack(s, block_number, client_sock, slen);
           
               if (c < 0) {
                    printf("%s.%u: transfer killed\n",
                           inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
                    exit(1);
               }
 
          }
 
     }
 
     printf("%s.%u: transfer completed\n",
            inet_ntoa(client_sock->sin_addr), ntohs(client_sock->sin_port));
 
     fclose(fd);
     close(s);
 
     exit(0);
}