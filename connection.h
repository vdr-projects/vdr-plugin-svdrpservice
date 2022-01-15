/*
 * connection.h: SVDRP connection
 *
 * See the README file for copyright information and how to reach the author.
 */

#ifndef _SVDRPSERVICE_CONNECTION__H
#define _SVDRPSERVICE_CONNECTION__H

#include <vdr/thread.h>
#include <vdr/tools.h>
#include "svdrpservice.h"

#define MAX_SVDRP_CONNECTIONS 8

#if VDRVERSNUM < 10503
class cCharSetConv;
#endif

class cSvdrpConnection: public cMutex {
	private:
		char*		serverIp;
		unsigned short	serverPort;
		cCharSetConv	*convIn;
		cCharSetConv	*convOut;
		cFile		file;
		char*		buffer;
		unsigned int	bufSize;
		int		refCount;
		bool		shared;

		int		Connect();
		bool		ReadLine(int TimeoutMs);
	public:
		cSvdrpConnection(const char *ServerIp, unsigned short ServerPort, bool Shared);
		virtual ~cSvdrpConnection();

		bool		HasDestination(const char *ServerIp, unsigned short ServerPort) const;
		bool		IsShared() const { return shared; };

		bool		Open();
		void		Close();
		void		Abort();

		void		AddRef() { refCount++; }
		int		DelRef() { return --refCount; }

		bool		Send(const char *Cmd, bool Reconnect = true);
		unsigned short	Receive(cList<cLine>* List = NULL, bool Connecting = false);

};

#endif //_SVDRPSERVICE_CONNECTION__H
