#include "proxy.h"

/*app funtions*/

int create_server(proxy_server* server){
	int on = 1;
	int rc;
	struct sockaddr_in addr;

	int listen_sd = socket(AF_INET, SOCK_STREAM, 0);
	if(listen_sd < 0){
		return -1;
	}

	rc = setsockopt(listen_sd, SOL_SOCKET,  SO_REUSEADDR, (char *)&on, sizeof(on));
	if(rc < 0){
		close(listen_sd);
		return -1;
	}

	//rc = ioctl(listen_sd, FIONBIO, (char *)&on);
	int flags = fcntl(listen_sd, F_GETFL, 0);
	rc = fcntl(listen_sd, F_SETFL, flags | O_NONBLOCK);
	if(rc < 0){
		close(listen_sd);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port        = htons(SERVER_PORT);
	rc = bind(listen_sd, (struct sockaddr *)&addr, sizeof(addr));
	if(rc < 0){
		close(listen_sd);
		return -1;
	}

	rc = listen(listen_sd, 32);
	if(rc < 0){
		close(listen_sd);
		return -1;
	}

	server->listen_sd     = listen_sd;
	server->fds           = (struct pollfd*)malloc(sizeof(struct pollfd) * CONNECTIONS);
	memset(server->fds, 0, sizeof(server->fds) * CONNECTIONS);
	server->fds[0].fd     = listen_sd;
	server->fds[0].events = POLLIN;
	server->nfds          = 1;
	server->entries       = (proxy_entry*)malloc(sizeof(proxy_entry) * ENTRIESNUM);
	server->nentries      = 0;
	server->messages      = (message*)malloc(sizeof(message) * MESSAGESNUM);
	server->nmsg          = MESSAGESNUM;

	return 0;
}

int close_server(proxy_server* server){
	int status = 0;

	for(int i = 0; i < server->nfds; i++){
		if(server->fds[i].fd >= 0){
			if(close(server->fds[i].fd) == -1){
				status = -1;
			}
		}
	}

	for(int i = 0; i < server->nmsg; i++){
		free(server->messages[i].buffer);
	}

	free(server->messages);
	free(server->entries);
	free(server->fds);

	return status;
}

int accept_connections(proxy_server* server){
	int new_sd;
	int rc;
	int on = 1;
	do{
		new_sd = accept(server->listen_sd, NULL, NULL);
		if(new_sd < 0){
			if(errno != EWOULDBLOCK){
				return -1;
			}
			break;
		}
		//rc = ioctl(new_sd, FIONBIO, (char*)&on);
		int flags = fcntl(new_sd, F_GETFL, 0);
		rc = fcntl(new_sd, F_SETFL, flags | O_NONBLOCK);
		if(rc < 0){
			close(new_sd);
			continue;
		}
		else{
			printf("  New incoming connection - %d\n", new_sd);
			server->fds[server->nfds].fd = new_sd;
			server->fds[server->nfds].events = POLLIN;
			server->messages[server->nfds].request_fd = -1;
			server->messages[server->nfds].buffer = (char*)calloc(STARTBUFFERSIZE, sizeof(char));
			server->messages[server->nfds].size = 0;
			server->messages[server->nfds].max_size = STARTBUFFERSIZE;
			server->messages[server->nfds].type = REQUEST;
			server->nfds += 1;
		}
	}
	while(new_sd != -1);

	return 0;
}

int close_connection(proxy_server* server, int num){
	printf("close_connection()\n");
	int status = 0;

	if(close(server->fds[num].fd) == -1){
		status = -1;
	}

	server->fds[num].fd = -1;
	server->fds[num].events = 0;
	free(server->messages[num].buffer);
	server->messages[num].buffer = NULL;

	return status;
}

void compress_array(proxy_server* server){
	int i;
	int j;

	for(i = 0; i < server->nfds; i++){
		if(server->fds[i].fd == -1){
			for(j = i; j < server->nfds; j++){
				server->fds[j] = server->fds[j+1];
				server->messages[j] = server->messages[j + 1];
				if(i < server->messages[j].request_fd){
					server->messages[j].request_fd -= 1;
				}
			}
			server->nfds -= 1;
		}
	}
}

int get_request(message* request, int fd){
	int rc;
	do{
		rc = recv(fd, request->buffer + request->size, 1024, 0);

		request->size += rc;

		if(request->size + 1024 >= request->max_size){
			request->max_size *= 2;
			// printf("%d\n", request->max_size);
			printf("realloc get request buffer %d\n", request->max_size);

			request->buffer = (char*)realloc(request->buffer, request->max_size * sizeof(char));
		}

		if(rc < 0){
			request->size += 1;
			if(errno != EWOULDBLOCK){
				perror("  recv() failed");
				return -1;
			}
			else if(errno == EWOULDBLOCK){
				char* end = strstr(request->buffer, "\r\n\r\n");
				if(end == NULL){
					printf("not ended %d %d\n", request->size, fd);
					return 1;
				}
				char* status_line_end = strchr(request->buffer, '\n');
				int len = status_line_end - request->buffer;
				request->buffer[len - 2] = '0';
				char* con = strstr(request->buffer, "Connection: ");
				if(con != NULL){
					con += 12;
					*con = "close";
					con += 5;
					for(int i = con - request->buffer; i < request->size - 5; i++){
						request->buffer[i] = request->buffer[i + 5];
					}
					request->size -= 5;
				}

				request->type = NONE;

				return 2;
			}
		}
		else if(rc == 0){
			printf("  Connection closed\n");
			request->type = NONE;
			return 0;
		}
	}
	while(TRUE);
}

int parse_request(message* request, char* hostname, char* request_head){
	char* request_body = strchr(request->buffer, '\n');

	if(request_body != NULL){
		int length = request_body - request->buffer;
		char* requesth = (char*)malloc(length + 1);
		strncpy(request_head, request->buffer, length);
		strncpy(requesth, request->buffer, length);

		request_head[length] = '\0';
		requesth[length] = '\0';

		char *meth = strtok(requesth, " ");
		// meth[strlen(meth)] = '\0';
		// strncpy(method, meth, strlen(meth));
		char *url = strtok(NULL, " ");
		char *vers = strtok(NULL, "\n\0");
		// vers[strlen(vers) - 1] = '\0';
		// strncpy(version, vers, strlen(vers));

		char *name = strstr(url, "://");

		if(name == NULL){
			length = vers - url;
			strncpy(hostname, url, length);
			hostname[length] = '\0';
		}
		else{
			char *name_end = strchr(name + 3, '/');

			if(name_end == NULL){
				length = strlen(name + 3);
			}
			else{
				length = name_end - (name + 3);
			}

			strncpy(hostname, name + 3, length);
			hostname[length] = '\0';
		}

		// printf("%s\n %lu", request_head, strlen(request_head));
		free(requesth);
	}
	else{
		return -1;
	}

	return 0;
}

int create_connection(proxy_server* server, char* hostname, int fd_num){
	int rc;
	int on = 1;

	struct hostent * host_info = gethostbyname(hostname);

	if(host_info == NULL){
		printf("  gethostbyname() failed\n");
		return -1;
	}

	struct sockaddr_in destinationAddress;

	destinationAddress.sin_family = AF_INET;
	destinationAddress.sin_port = htons(80);
	memcpy(&destinationAddress.sin_addr, host_info->h_addr, host_info->h_length);

	int destnation = socket(AF_INET, SOCK_STREAM, 0);
	if(destnation < 0){
		return -1;
	}

	//rc = ioctl(destnation, FIONBIO, (char *)&on);
	int flags = fcntl(destnation, F_GETFL, 0);
	rc = fcntl(destnation, F_SETFL, flags | O_NONBLOCK);
	if(rc < 0){
		close(destnation);
		return -1;
	}
	int con = connect(destnation, (struct sockaddr *)&destinationAddress, sizeof(destinationAddress));
	if(con < 0){
		if(errno != EINPROGRESS){
			close(destnation);
			return -1;
		}
	}
	server->fds[server->nfds].fd = destnation;
	server->fds[server->nfds].events = POLLOUT;
	server->messages[server->nfds].request_fd = fd_num;
	server->messages[server->nfds].buffer = (char*)calloc(server->messages[fd_num].max_size, sizeof(char));
	server->messages[server->nfds].size = server->messages[fd_num].size;
	server->messages[server->nfds].max_size = server->messages[fd_num].max_size;
	server->messages[server->nfds].type = RESPONSE;
	memcpy(server->messages[server->nfds].buffer, server->messages[fd_num].buffer, server->messages[fd_num].size * sizeof(char));
	memset(server->messages[fd_num].buffer, 0, server->messages[fd_num].size * sizeof(char));
	server->messages[fd_num].size = 0;
	server->fds[fd_num].events = 0;
	server->messages[server->nfds].entry_num = server->messages[fd_num].entry_num;
	server->nfds += 1;

	return 0;
}

int get_response(message* response, int fd, proxy_server* server){
	int rc;
	do{
		rc = recv(fd, response->buffer + response->size, 1024, 0);

		response->size += rc;

		if(response->size + 1024 >= response->max_size){
			printf("realloc response buffer %d %d\n", response->size, response->max_size);
			response->max_size *= 2;

			response->buffer = (char*)realloc(response->buffer, response->max_size * sizeof(char));
		}

		int len = response->size - server->entries[response->entry_num].content_size;
		if(len > 0){
			if(response->size > server->entries[response->entry_num].max_size){
				server->entries[response->entry_num].max_size *= 2;
				printf("realloc entry buffer %d\n", server->entries[response->entry_num].max_size);

				server->entries[response->entry_num].content = (char*)realloc(server->entries[response->entry_num].content, server->entries[response->entry_num].max_size);
			}
			memcpy(server->entries[response->entry_num].content + server->entries[response->entry_num].content_size, response->buffer + response->size - len, len);
			server->entries[response->entry_num].content_size = response->size;
		}

		if(rc < 0){
			response->size += 1;
			if(errno != EWOULDBLOCK){
				perror("  recv() failed");
				return -1;
			}
			else if(errno == EWOULDBLOCK){
				printf("not ended %d %d\n", response->size, fd);
				return 1;
			}
		}
		else if(rc == 0){
			printf("  Connection closed\n");
			printf("%d %d\n", response->size, server->entries[response->entry_num].content_size);
			if(is_complete_entry(response->entry_num, server) == 0){
				response->buffer[7] = '0';
				server->entries[response->entry_num].content[7] = '0';
				server->entries[response->entry_num].complete = 1;
			}
			// for(int i = 0; i < server->entries[response->entry_num].content_size; i++){
			//   printf("%c", server->entries[response->entry_num].content[i]);
			// }
			// printf("\n");
			return 0;
		}
	}
	while(TRUE);
}

int transfer_response(proxy_server* server, int fd_num){
	printf("transfer_response\n");

	server->messages[server->messages[fd_num].request_fd].request_fd = -1;
	server->messages[server->messages[fd_num].request_fd].size = server->messages[fd_num].size;
	if(server->messages[server->messages[fd_num].request_fd].max_size < server->messages[fd_num].max_size){
		printf("realloc request buffer %d\n", server->messages[server->messages[fd_num].request_fd].max_size);

		server->messages[server->messages[fd_num].request_fd].buffer = (char*)realloc(server->messages[server->messages[fd_num].request_fd].buffer, server->messages[fd_num].max_size);
	}
	server->messages[server->messages[fd_num].request_fd].max_size = server->messages[fd_num].max_size;
	server->messages[server->messages[fd_num].request_fd].type = RESPONSE;
	memcpy(server->messages[server->messages[fd_num].request_fd].buffer, server->messages[fd_num].buffer, server->messages[fd_num].size * sizeof(char));
	server->fds[server->messages[fd_num].request_fd].events = POLLOUT;

	return 0;
}

int find_in_cache(char* name, int size, proxy_server* server){
	printf("\t\tfind_in_cache()\n");
	int i;
	int find = 0;

	for(i = 0; i < server->nentries; i++){
		// printf("\t%s\n\t\t%s\n", name, server->entries[i].hostname);
		if(strncmp(name, server->entries[i].hostname, size) == 0){
			find = 1;
			break;
		}
	}
	if(find == 1){
		return i;
	}

	return -1;
}

int is_complete_entry(int entry_num, proxy_server* server){
	if(server->entries[entry_num].complete == 1){
		return 1;
	}
	else{
		return 0;
	}
}

int get_from_cache(int entry_num, int fd_num, proxy_server* server){
	printf("\t\tget_from_cache()\n");

	memset(server->messages[fd_num].buffer, 0, server->messages[fd_num].max_size);
	if(server->messages[fd_num].max_size < server->entries[entry_num].content_size){
		printf("realloc req in cache buffer %d\n", server->messages[fd_num].max_size);

		server->messages[fd_num].buffer = (char*)realloc(server->messages[fd_num].buffer, server->entries[entry_num].content_size);
	}
	memcpy(server->messages[fd_num].buffer, server->entries[entry_num].content, server->entries[entry_num].content_size);
	server->messages[fd_num].size = server->entries[entry_num].content_size;
	server->fds[fd_num].events = POLLOUT;

	return 0;
}

int cache_entry_name(char* name, int size, proxy_server* server, int fd_num){
	printf("\t\tcache_entry_name()\n");

	server->entries[server->nentries].hostname = (char*)malloc(size);
	server->entries[server->nentries].hostname_size = size;
	server->entries[server->nentries].content = (char*)malloc(STARTBUFFERSIZE);
	server->entries[server->nentries].content_size = 0;
	server->entries[server->nentries].complete = 0;
	server->entries[server->nentries].max_size = STARTBUFFERSIZE;
	memcpy(server->entries[server->nentries].hostname, name, size);
	server->messages[fd_num].entry_num = server->nentries;
	server->nentries += 1;

	return 0;
}
