#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
 
/* base directory */
char *base_directory;
 
#define RECV_TIMEOUT 5
#define RECV_RETRIES 5
 
/* tftp opcode mnemonic */
enum opcode {
     RRQ=1,
     WRQ,
     DATA,
     ACK,
     ERROR
};
 
/* tftp transfer mode */
enum mode {
     NETASCII=1,
     OCTET
};
 
 
 
/* tftp message structure */
typedef union {
 
     uint16_t opcode;
 
     struct {
          uint16_t opcode; /* RRQ or WRQ */            
          uint8_t filename_and_mode[514];
     } request;     
 
     struct {
          uint16_t opcode; /* DATA */
          uint16_t block_number;
          uint8_t data[512];
     } data;
 
     struct {
          uint16_t opcode; /* ACK */            
          uint16_t block_number;
     } ack;
 
     struct {
          uint16_t opcode; /* ERROR */    
          uint16_t error_code;
          int8_t error_string[512];
     } error;
 
} tftp_message;
 
void cld_handler(int sig);
ssize_t tftp_send_data(int s, uint16_t block_number, uint8_t *data, ssize_t dlen, struct sockaddr_in *sock, socklen_t slen);
ssize_t send_ack(int s, uint16_t block_number, struct sockaddr_in *sock, socklen_t slen);
ssize_t send_error(int s, int error_code, char *error_string, struct sockaddr_in *sock, socklen_t slen);
ssize_t recv_message(int s, tftp_message *m, struct sockaddr_in *sock, socklen_t *slen);
void handle_request(tftp_message *m, ssize_t len, struct sockaddr_in *client_sock, socklen_t slen);