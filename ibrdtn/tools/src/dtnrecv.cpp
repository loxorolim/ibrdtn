/*
 * dtnrecv.cpp
 *
 *  Created on: 06.11.2009
 *      Author: morgenro
 */

#include "config.h"
#include <ibrdtn/api/Client.h>
#include <ibrdtn/api/FileBundle.h>
#include <ibrcommon/net/tcpclient.h>
#include <ibrcommon/thread/Mutex.h>
#include <ibrcommon/thread/MutexLock.h>
#include <ibrcommon/Logger.h>

#include <csignal>
#include <sys/types.h>
#include <iostream>

void print_help()
{
	cout << "-- dtnrecv (IBR-DTN) --" << endl;
	cout << "Syntax: dtnrecv [options]"  << endl;
	cout << "* optional parameters *" << endl;
	cout << " -h|--help            display this text" << endl;
	cout << " --file <filename>    write the incoming data to the a file instead of the standard output" << endl;
	cout << " --name <name>        set the application name (e.g. filetransfer)" << endl;
	cout << " --timeout <seconds>  receive timeout in seconds" << endl;
	cout << " --count <number>     receive that many bundles" << endl;
	cout << " --group <group>      join a group" << endl;
	cout << " -U <socket>     use UNIX domain sockets" << endl;
}

dtn::api::Client *_client = NULL;
ibrcommon::tcpclient *_conn = NULL;

int h = 0;
bool _stdout = true;

void term(int signal)
{
	if (!_stdout)
	{
		std::cout << h << " bundles received." << std::endl;
	}

	if (signal >= 1)
	{
		if (_client != NULL)
		{
			_client->close();
			_conn->close();
		}
	}
}

int main(int argc, char *argv[])
{
	// logging options
	const unsigned char logopts = ibrcommon::Logger::LOG_DATETIME | ibrcommon::Logger::LOG_LEVEL;

	// error filter
	unsigned char loglevel = 0;

	// catch process signals
	signal(SIGINT, term);
	signal(SIGTERM, term);

	int ret = EXIT_SUCCESS;
	string filename = "";
	string name = "filetransfer";
	dtn::data::EID group;
	int timeout = 0;
	int count   = 1;
	ibrcommon::File unixdomain;

	for (int i = 0; i < argc; i++)
	{
		string arg = argv[i];

		// print help if requested
		if (arg == "-h" || arg == "--help")
		{
			print_help();
			return ret;
		}

		if (arg == "--logging")
		{
			loglevel |= ibrcommon::Logger::LOGGER_ALL ^ ibrcommon::Logger::LOGGER_DEBUG;
		}

		if (arg == "--debug")
		{
			loglevel |= ibrcommon::Logger::LOGGER_DEBUG;
		}

		if (arg == "--name" && argc > i)
		{
			name = argv[i + 1];
		}

		if (arg == "--file" && argc > i)
		{
			filename = argv[i + 1];
			_stdout = false;
		}

		if (arg == "--timeout" && argc > i)
		{
			timeout = atoi(argv[i + 1]);
		}

		if (arg == "--group" && argc > i)
		{
			group = std::string(argv[i + 1]);
		}

		if (arg == "--count" && argc > i) 
		{
			count = atoi(argv[i + 1]);
		}

		if (arg == "-U" && argc > i)
		{
			if (++i > argc)
			{
				std::cout << "argument missing!" << std::endl;
				return -1;
			}

			unixdomain = ibrcommon::File(argv[i]);
		}
	}

	if (loglevel > 0)
	{
		// add logging to the cerr
		ibrcommon::Logger::addStream(std::cerr, loglevel, logopts);
	}

	try {
		// Create a stream to the server using TCP.
		ibrcommon::tcpclient conn;

		// check if the unixdomain socket exists
		if (unixdomain.exists())
		{
			// connect to the unix domain socket
			conn.open(unixdomain);
		}
		else
		{
			// connect to the standard local api port
			conn.open("127.0.0.1", 4550);

			// enable nodelay option
			conn.enableNoDelay();
		}

		// Initiate a client for synchronous receiving
		dtn::api::Client client(name, group, conn);

		// export objects for the signal handler
		_conn = &conn;
		_client = &client;

		// Connect to the server. Actually, this function initiate the
		// stream protocol by starting the thread and sending the contact header.
		client.connect();

		std::fstream file;

		if (!_stdout)
		{
			std::cout << "Wait for incoming bundle... " << std::endl;
			file.open(filename.c_str(), ios::in|ios::out|ios::binary|ios::trunc);
			file.exceptions(std::ios::badbit | std::ios::eofbit);
		}

		for(h = 0; h < count; h++)
		{
			// receive the bundle
			dtn::api::Bundle b = client.getBundle(timeout);

			// get the reference to the blob
			ibrcommon::BLOB::Reference ref = b.getData();

			// write the data to output
			if (_stdout)
			{
				std::cout << ref.iostream()->rdbuf() << std::flush;
			}
			else
			{
				// write data to temporary file
				try {
					std::cout << "Bundle received (" << (h + 1) << ")." << endl;

					file << ref.iostream()->rdbuf();
				} catch (const ios_base::failure&) {

				}
			}
		}

		if (!_stdout)
		{
			file.close();
			std::cout << "done." << std::endl;
		}

		// Shutdown the client connection.
		client.close();

		// close the tcp connection
		conn.close();
	} catch (const dtn::api::ConnectionTimeoutException&) {
		std::cerr << "Timeout." << std::endl;
		ret = EXIT_FAILURE;
	} catch (const dtn::api::ConnectionAbortedException&) {
		std::cerr << "Aborted." << std::endl;
		ret = EXIT_FAILURE;
	} catch (const dtn::api::ConnectionException&) {
	} catch (const std::exception &ex) {
		std::cerr << "Error: " << ex.what() << std::endl;
		ret = EXIT_FAILURE;
	}

	return ret;
}