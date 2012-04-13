/*
 * udpsocket.cpp
 *
 *  Created on: 02.03.2010
 *      Author: morgenro
 */

#include "ibrcommon/config.h"
#include "ibrcommon/net/udpsocket.h"
#include "ibrcommon/Logger.h"
#include <sys/socket.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

#ifndef HAVE_BZERO
#define bzero(s,n) (memset((s), '\0', (n)), (void) 0)
#endif

namespace ibrcommon
{
	udpsocket::udpsocket() throw (SocketException)
	{
	}

	udpsocket::~udpsocket()
	{
		_socket.close();
	}

	void udpsocket::shutdown()
	{
		_socket.close();
	}

	int udpsocket::receive(char* data, size_t maxbuffer)
	{
		struct sockaddr_in clientAddress;
		socklen_t clientAddressLength = sizeof(clientAddress);

		std::list<int> fds;
		_socket.select(fds, NULL);
		int fd = fds.front();

		// data waiting
		return recvfrom(fd, data, maxbuffer, MSG_WAITALL, (struct sockaddr *) &clientAddress, &clientAddressLength);
	}

	int udpsocket::receive(char* data, size_t maxbuffer, std::string &address, unsigned int &port)
	{
		struct sockaddr_storage addr;
		socklen_t fromlen = sizeof(addr);
		char ipstr[INET6_ADDRSTRLEN];

		std::list<int> fds;
		_socket.select(fds, NULL);
		int fd = fds.front();

		// data waiting
		int ret = recvfrom(fd, data, maxbuffer, MSG_WAITALL, (struct sockaddr *) &addr, &fromlen);

		if ( addr.ss_family == AF_INET )
		{
			// IPv4
			inet_ntop(addr.ss_family, &((struct sockaddr_in *)&addr)->sin_addr, ipstr, sizeof ipstr);
			address = std::string(ipstr);
			port = ntohs(((struct sockaddr_in *)&addr)->sin_port);
		}
		else if ( addr.ss_family == AF_INET6 )
		{
			// IPv6
			inet_ntop(addr.ss_family, &((struct sockaddr_in6 *)&addr)->sin6_addr, ipstr, sizeof ipstr);
			address = std::string(ipstr);
			port = ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
		}
		else
		{
			address = "unknown";
			port = 0;
		}

		return ret;
	}

	int udpsocket::send(const ibrcommon::vaddress &addr, const unsigned int port, const char *data, const size_t length)
	{
		int stat = -1;

		// iterate over all sockets
		std::list<int> fds = _socket.get();

		for (std::list<int>::const_iterator iter = fds.begin(); iter != fds.end(); iter++)
		{
			try {
				ssize_t ret = 0;
				int flags = 0;

				struct addrinfo hints, *ainfo;
				memset(&hints, 0, sizeof hints);

				hints.ai_socktype = SOCK_DGRAM;
				ainfo = addr.addrinfo(&hints, port);

				ret = ::sendto(*iter, data, length, flags, ainfo->ai_addr, ainfo->ai_addrlen);

				freeaddrinfo(ainfo);

				// if the send was successful, set the correct return value
				if (ret != -1)
				{
					stat = ret;

					if ((!addr.isBroadcast()) && (!addr.isMulticast()))
					{
						// stop here
						break;
					}
				}
			} catch (const ibrcommon::vsocket_exception&) {
				IBRCOMMON_LOGGER_DEBUG(5) << "can not send message to " << addr.toString() << IBRCOMMON_LOGGER_ENDL;
			}
		}

		return stat;
	}
}