#include "csapp.h"

int open_listenfd(int port);
void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
	             char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
	int listenfd, connfd, port, clientlen;
	struct sockaddr_in clientaddr;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}
	port = atoi(argv[1]);

	listenfd = Open_listenfd(port);

	while (1) {
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		doit(connfd);
		close(connfd);
	}
}

/*
int open_listenfd(int port)
{
	int listenfd, optval = 1;
	struct sockaddr_in serveraddr;

	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1;

	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
		(const void *)&optval, sizeof(int)) < 0)
		return -1;

	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)port);

	if (bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0)
		return -1;

	if (listen(listenfd, LISTENQ) < 0)
		return -1;

	return listenfd;
}*/

void doit(int fd)
{
	int is_static;
	struct stat sbuf;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char filename[MAXLINE], cgiargs[MAXLINE];
	rio_t rio;

	rio_readinitb(&rio, fd);
	rio_readlineb(&rio, buf, MAXLINE);
	sscanf(buf, "%s %s %s", method, uri, version);
	if (strcasecmp(method, "GET")) {
		clienterror(fd, method, "501", "Not Implemented",
			"Tiny does not implement this method");
		return;
	}

	if (is_static) {
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
			clienterror(fd, filename, "403", "Forbidden",
				"Tiny couldn't read the file");
			return;
		}
		serve_static(fd, filename, sbuf.st_size);
	}
	else {
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
			clienterror(fd, filename, "403", "forbidden",
				"Tiny couldn't run the CGI program");
			return;
		}
		serve_dynamic(fd, filename, cgiargs);
	}
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
	char buf[MAXLINE], body[MAXBUF];

	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

	sprintf(buf, "HTTP/1.1 %s %s\r\n", errnum, shortmsg);
	rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length:%d\r\n\r\n", (int)strlen(body));
	rio_writen(fd, buf, strlen(buf));
	rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp)
{
	char buf[MAXLINE];

	rio_readlineb(rp, buf, MAXLINE);
	while (strcmp(buf, "\r\n")) {
		rio_readlineb(rp, buf, MAXLINE);
		printf("%s", buf);
	}
	return;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
	char *ptr = NULL;

	if (!strstr(uri, "cgi-bin")) {
		strcpy(cgiargs, "");
		strcpy(filename, ".");
		strcat(filename, uri);
		
		if (uri[strlen(uri)-1] == '/')
			strcat(filename, "home.html");
		
		return 1;
	}
	else {
		ptr = index(uri, '?');
		if (ptr != NULL) {
			strcpy(cgiargs, ptr+1);
			*ptr = '\0';
		}
		else
			strcpy(cgiargs, "");
		
		strcpy(filename, ".");
		strcat(filename, uri);
		
		return 0;
	}
}

void serve_static(int fd, char *filename, int filesize)
{
	int srcfd;
	char *srcp, filetype[MAXLINE], buf[MAXBUF];

	get_filetype(filename, filetype);
	sprintf(buf, "HTTP/1.1 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
	sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
	rio_writen(fd, buf, strlen(buf));

	srcfd = Open(filename, O_RDONLY, 0);
	srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
	Close(srcfd);
	rio_writen(fd, srcp, filesize);
	Munmap(srcp, filesize);
}

void get_filetype(char *filename, char *filetype)
{
	if (strstr(filename, ".html"))
		strcpy(filetype, "text/html");
	else if (strstr(filename, ".gif"))
		strcpy(filetype, "image/gif");
	else if (strstr(filename, ".jpg"))
		strcpy(filetype, "image/jpg");
	else
		strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs)
{
	char buf[MAXLINE], *emptylist[] = {NULL};

	sprintf(buf, "HTTP/1.1 200 OK\r\n");
	rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Server: Tiny Web Server\r\n");
	rio_writen(fd, buf, strlen(buf));

	if (Fork() == 0) {
		setenv("QUERY_STRING", cgiargs, 1);
		Dup2(fd, STDOUT_FILENO);
		Execve(filename, emptylist, environ);
	}
	Wait(NULL);
}

















