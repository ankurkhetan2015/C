#include "tftpserv.h"
 
int main(int argc, char *argv[])
{
     int s;
     uint16_t port = 0;
     struct protoent *pp;
     struct servent *ss;
     struct sockaddr_in server_sock;
 
     if (argc < 2) {
          printf("usage:\n\t%s [base directory] [port]\n", argv[0]);
          exit(1);
     }
 
     base_directory = argv[1];
 
     if (chdir(base_directory) < 0) {
          perror("server: chdir()");
          exit(1);
     }
 
     if (argc > 2) {
          if (sscanf(argv[2], "%hu", &port)) {
               port = htons(port);
          } else {
               fprintf(stderr, "error: invalid port number\n");
               exit(1);
          }
     } else {
          if ((ss = getservbyname("tftp", "udp")) == 0) {
               fprintf(stderr, "server: getservbyname() error\n");
               exit(1);
          }
 
     }
 
     if ((pp = getprotobyname("udp")) == 0) {
          fprintf(stderr, "server: getprotobyname() error\n");
          exit(1);
     }
 
     if ((s = socket(AF_INET, SOCK_DGRAM, pp->p_proto)) == -1) {
          perror("server: socket() error");
          exit(1);
     }
 
     server_sock.sin_family = AF_INET;
     server_sock.sin_addr.s_addr = htonl(INADDR_ANY);
     server_sock.sin_port = port ? port : ss->s_port;
 
     if (bind(s, (struct sockaddr *) &server_sock, sizeof(server_sock)) == -1) {
          perror("server: bind()");
          close(s);
          exit(1);
     }
 
     signal(SIGCLD, (void *) cld_handlertf) ;
 
     printf("tftp server: listening on %d\n", ntohs(server_sock.sin_port));
 
     while (1) {
          struct sockaddr_in client_sock;
          socklen_t slen = sizeof(client_sock);
          ssize_t len;
 
          tftp_message message;
          uint16_t opcode;
 
          if ((len = recv_message(s, &message, &client_sock, &slen)) < 0) {
               continue;
          }
 
          if (len < 4) { 
               printf("%s.%u: request with invalid size received\n",
                      inet_ntoa(client_sock.sin_addr), ntohs(client_sock.sin_port));
               send_error(s, 0, "invalid request size", &client_sock, slen);
               continue;
          }
 
          opcode = ntohs(message.opcode);
 
          if (opcode == RRQ || opcode == WRQ) {
 
               /* spawn a child process to handle the request */
 
               if (fork() == 0) {
                    handle_request(&message, len, &client_sock, slen);
                    exit(0);
               }
 
          }
 
          else {
               printf("%s.%u: invalid request received: opcode \n", 
                      inet_ntoa(client_sock.sin_addr), ntohs(client_sock.sin_port),
                      opcode);
               send_error(s, 0, "invalid opcode", &client_sock, slen);
          }
 
     }
 
     close(s);
 
     return 0;
}