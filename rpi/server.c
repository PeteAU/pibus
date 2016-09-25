#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>

#include "mainloop.h"
#include "slist.h"
#include "ibus.h"

#define set_blocking(sok) fcntl(sok, F_SETFL, 0)
#define set_nonblocking(sok) fcntl(sok, F_SETFL, O_NONBLOCK)

typedef struct _connection
{
	int socket;
	int tag;
	int pos;
#define CMD_SIZ 256
	char cmd[CMD_SIZ];
}
connection;


static SList *connect_list = NULL;
static int listen_tag = -1;
static int server_socket = -1;


static void server_send_data(const char *msg, int length)
{
	SList *list;
	connection *conn;

	/* send it to every connected client */
	list = connect_list;
	while (list)
	{
		conn = list->data;
		write(conn->socket, msg, length);
		list = list->next;
	}
}

static void server_send_hex(const char *prefix, const unsigned char *msg, int length)
{
	int i;
	int prefix_len;
	int pos;
	char buf[CMD_SIZ];

	prefix_len = strlen(prefix);
	strcpy(buf, prefix);
	pos = prefix_len;

	for (i = 0; (i < length) && (i < (sizeof(buf) - (prefix_len + 1)) / 2); i++)
	{
		sprintf(buf + pos, "%02x", msg[i]);
		pos += 2;
	}

	buf[pos++] = '\n';

	server_send_data(buf, pos);
}

static void server_handle_command(char *cmd, int length)
{
	//printf("cmd: |%.*s|\n", length, cmd);

	if (memcmp(cmd, "tx ", 3) == 0)
	{
		//printf("tx: |%s|\n", cmd + 3);
		if (ibus_send_ascii(cmd + 3) != 0)
		{
			server_send_hex("error ", (unsigned char *)"\1", 1);
		}
		return;
	}

	server_send_hex("error ", (unsigned char *)"", 1);
}

void server_handle_message(const unsigned char *msg, int length)
{
	server_send_hex("rx ", msg, length);
}

void server_notify_tx(const unsigned char *msg, int length)
{
	server_send_hex("tx ", msg, length);
}

static void server_disconnect(connection *conn)
{
	connect_list = slist_remove(connect_list, conn);
	mainloop_input_remove(conn->tag);
	close(conn->socket);
	free(conn);
}

static void server_read(int condition, connection *conn)
{
	int r;
	unsigned char c;

	while (1)
	{
		r = read(conn->socket, &c, 1);

		if (r == 0)
		{
			//printf("Disconnect ifd=%d e=EOF\n", conn->socket);
			server_disconnect(conn);
			return;
		}

		if (r == -1)
		{
			if (errno != EWOULDBLOCK)
			{
				//printf("Disconnect ifd=%d e=%d %s\n", conn->socket, errno, strerror(errno));
				server_disconnect(conn);
			}
			return;
		}

		switch (c)
		{
			case '\n':
				conn->cmd[conn->pos] = 0;
				server_handle_command(conn->cmd, conn->pos);
				conn->pos = 0;
				break;
			case '\r':
				break;
			default:
				conn->cmd[conn->pos] = c;
				if (conn->pos < (CMD_SIZ - 1))
				{
					conn->pos++;
				}
		}
	}
}

static void server_add_connection(int sock)
{
	connection *conn;

	set_nonblocking(sock);

	conn = calloc(1, sizeof(connection));
	conn->socket = sock;
	conn->tag = mainloop_input_add(sock, FIA_READ|FIA_EX, (void *)server_read, conn);

	connect_list = slist_prepend(connect_list, conn);
}

static void server_accept(int condition, void *userdata)
{
	int new_sock;
	socklen_t sin_size;
	struct sockaddr_in client_addr;

	sin_size = sizeof (struct sockaddr_in);
	new_sock = accept(server_socket, (struct sockaddr *) &client_addr, &sin_size);
	if (new_sock != -1)
	{
		//printf("New connection from (%s : %d)\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
		server_add_connection(new_sock);
	}
}

int server_init(int port)
{
	struct sockaddr_in server_addr;
	socklen_t opt;

	if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		return 1;
	}

	opt = 1;
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)) == -1)
	{
		close(server_socket);
		server_socket = -1;
		return 2;
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);//inet_addr("127.0.0.1");

	if (bind(server_socket, (struct sockaddr *) &server_addr, sizeof (struct sockaddr)) == -1)
	{
		printf("errno=%d %s\n", errno, strerror(errno));
		close(server_socket);
		server_socket = -1;
		return 3;
	}

	set_nonblocking(server_socket);
	listen(server_socket, 1);
	set_blocking(server_socket);

	listen_tag = mainloop_input_add(server_socket, FIA_READ|FIA_EX, server_accept, NULL);

	return 0;
}

void server_cleanup()
{
	/*if (listen_tag != -1)
	{
		mainloop_input_remove(listen_tag);
		listen_tag = -1;
	}

	if (server_socket != -1)
	{
		close(server_socket);
		server_socket = -1;
	}*/
}

