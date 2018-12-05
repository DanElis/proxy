#ifndef PROXY_H
#define PROXY_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>



#define NONE           -1
#define FALSE           0
#define TRUE            1
#define REQUEST         2
#define RESPONSE        3
#define WAITCACHE       4
#define CONTINUE		5
#define SERVER_PORT     3001
#define CONNECTIONS     1024
#define ENTRIESNUM      1024
#define MESSAGESNUM     CONNECTIONS
#define STARTBUFFERSIZE 1048576



typedef struct{
	int   request_fd;
	char* buffer;
	int   size;
	int   max_size;
	int   type;
	int   entry_num;
} message;

typedef struct{
	char* hostname;
	int   hostname_size;
	char* content;
	int   content_size;
	int   complete;
	int   max_size;
} proxy_entry;

typedef struct{
	int listen_sd;
	struct pollfd* fds;
	int nfds;
	proxy_entry * entries;
	int nentries;
	message* messages;
	int nmsg;
	int current_size;
} proxy_server;

/*app functions*/

int  create_server(proxy_server*);
int  close_server(proxy_server*);
int  accept_connections(proxy_server*);
int  close_connection(proxy_server*, int);
void compress_array(proxy_server*);
int  get_request(message*, int);
int  get_response(message*, int, proxy_server*);
int  parse_request(message*, char*, char*);
int  create_connection(proxy_server*, char*, int);
int  transfer_response(proxy_server*, int);
int  find_in_cache(char*, int, proxy_server*);
int  is_complete_entry(int, proxy_server*);
int  get_from_cache(int, int, proxy_server*);
int  cache_entry_name(char*, int, proxy_server*, int);

#endif
