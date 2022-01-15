#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#include "connection.h"

#if DEBUG == 1
#define DEBUG_PRINTF(args...) fprintf(stderr, args)
#elif DEBUG == 2
#define DEBUG_PRINTF(args...) dsyslog(args)
#else
#define DEBUG_PRINTF(args...)
#endif

int cSvdrpConnection::Connect(const char *ServerIp, unsigned short ServerPort) {
	if (!ServerIp) {
		errno = EINVAL;
		LOG_ERROR;
		return -1;
	}

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(ServerPort);
	if (!::inet_aton(ServerIp, &server_addr.sin_addr)) {
		LOG_ERROR;
		return -1;
	}

	int sock = ::socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		LOG_ERROR;
		return -1;
	}
	if (::connect(sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
		LOG_ERROR;
		return -1;
	}

	// set nonblocking
	int flags = ::fcntl(sock, F_GETFL, 0);
	if (flags < 0 || ::fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
		LOG_ERROR;
		::close(sock);
		return -1;
	}
	return sock;
}

cSvdrpConnection::cSvdrpConnection(const char *ServerIp, unsigned short ServerPort, bool Shared):
		serverPort(ServerPort), refCount(0), shared(Shared) {
	serverIp = ServerIp ? ::strdup(ServerIp) : NULL;
}

cSvdrpConnection::~cSvdrpConnection(void) {
	Close();
	if (serverIp)
		free(serverIp);
}

bool cSvdrpConnection::HasDestination(const char *ServerIp, unsigned short ServerPort) const {
	if (ServerIp == NULL || serverIp == NULL)
		return false;
	return serverPort == ServerPort && !strcmp(serverIp, ServerIp);
}

bool cSvdrpConnection::Open() {
	if (file.IsOpen()) {
		//TODO: make sure the connection is still alive (e.g. send STAT command)
	}
	if (!file.IsOpen()) {
		int fd = Connect(serverIp, serverPort);
		if (fd < 0)
			return false;

		if (!file.Open(fd)) {
			::close(fd);
			return false;
		}
		
		// check for greeting
		if (Receive() != 220) {
			Close();
			return false;
		}

		isyslog("SvdrpService: connected to %s:%u", serverIp, serverPort);
	}
	return true;
}

void cSvdrpConnection::Close(void) {
	if (Send("QUIT\r\n", false))
		Receive();
	file.Close();
}

void cSvdrpConnection::Abort(void) {
	file.Close();
}

bool cSvdrpConnection::Send(const char *Cmd, bool Reconnect) {
	if (!Cmd)
		return false;

	if (Reconnect && !file.IsOpen())
		Open();
	if (!file.IsOpen())
		return false;

	DEBUG_PRINTF("SEND %s", Cmd);
	unsigned int len = ::strlen(Cmd);
	if (safe_write(file, Cmd, len) < 0) {
		LOG_ERROR;
		Abort();
		return false;
	}
	return true;
}

unsigned short cSvdrpConnection::Receive(cList<cLine>* List) {
	while (ReadLine()) {
		char *tail;
		long int code = ::strtol(buffer, &tail, 10);
		if (tail - buffer == 3 &&
				code >= 100 && code <= 999 &&
				(*tail == ' ' || *tail =='-')) {
			if (List)
				List->Add(new cLine(buffer + 4));
			if (*tail == ' ')
				return (unsigned short) code;
		}
		else {
			esyslog("SvdrpService: invalid reply from %s: '%s'", serverIp, buffer);
			Close();
			break;
		}
	}
	if (List)
		List->Clear();
	return 0;
}

bool cSvdrpConnection::ReadLine() {
	if (!file.IsOpen())
		return false;

	unsigned int tail = 0;
	bool ok = true;
	while (ok && file.Ready(true)) {
		unsigned char c;
		int r = safe_read(file, &c, 1);
		if (r > 0) {
			if (c == '\n' || c == 0x00) {
				// strip trailing whitespace:
				while (tail > 0 && strchr(" \t\r\n", buffer[tail - 1]))
					buffer[--tail] = 0;

				// line complete, make sure the string is terminated
				buffer[tail] = 0;
				DEBUG_PRINTF("READ %s\n", buffer);
				return true;
			}
			else if (c == 0x04 && tail == 0) {
				// end of file (only at beginning of line)
				ok = false;
			}
			else if ((c <= 0x1F || c == 0x7F) && c != 0x09) {
				// ignore control characters
				}
			else if (tail < sizeof(buffer) - 1) {
				buffer[tail++] = c;
				buffer[tail] = 0;
			}
			else {
				esyslog("SvdrpService: line too long in reply from %s: '%s'", serverIp, buffer);
				ok = false;
			}
		}
		else if (r < 0) {
			esyslog("SvdrpService: lost connection to %s", serverIp);
			ok = false;
		}
		else {
			esyslog("SvdrpService: timeout waiting for reply from %s", serverIp);
			ok = false;
		}
	}
	
	buffer[0] = 0;
	Abort();
	return false;
}
