This code emulates a simple TFTP server and requires the use of a native TFTP client. Both RRQ and 
WRQ are supported for binary files and netascii files. 

server.c handles the basic functions for a connection between a client a server and also allows for 
multiple server connections. tftpserv.c goes further and handles all necessary tftp functions.




Running the program: 

"make server" will compile and build the programs. The TFTP server can then be run by executing
"./server [base directory] [port number]" for example, "./server .. 8080". The native TFTP client will 
then need to run and the same port number should be specified. 

At this point you can begin transferring files. 




Testing the program: 
The program was tested according to the Submission Guidelines and operated well under all tests. 
Three clients were able to connect and transfer a 40MB file and the wrap around feature worked. All 
binary and netascii file transfers that were tested were successful. The sever does not allow for 
files that do not exist and is able to timeout if the client exits in the middle of a transfer. Basic 
tests are shown in the testcase.pdf

