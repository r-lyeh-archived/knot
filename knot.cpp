/* Knot is a lightweight and simple TCP network C++11 library with no dependencies.
 * Copyright (c) 2013 Mario 'rlyeh' Rodriguez

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.

 * References:
 * - [1] http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html#serialization
 * - [2] from unpv12e/lib/connect_nonb.c
 * - [3] some macros taken from SimpleSockets library
 * - [4] http://tangentsoft.net/wskfaq/articles/bsd-compatibility.html

 * - rlyeh ~~ listening to Crippled Black Phoenix / We Forgotten Who We Are
 */

#include "knot.hpp"

#include <errno.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <string>
#include <sstream>
#include <iostream>
#include <thread>
#include <future>

#ifdef _WIN32

#   include <cassert>

#   include <winsock2.h>
#   include <ws2tcpip.h>
#   include <windows.h>

#   pragma comment(lib,"ws2_32.lib")

#   define ACCEPT(A,B,C)             ::accept((A),(B),(C))
#   define CONNECT(A,B,C)            ::connect((A),(B),(C))
#   define CLOSE(A)                  ::closesocket((A))
#   define READ(A,B,C)               ::read((A),(B),(C))
#   define RECV(A,B,C,D)             ::recv((A), (char *)(B), (C), (D))
#   define SELECT(A,B,C,D,E)         ::select((A),(B),(C),(D),(E))
#   define SEND(A,B,C,D)             ::send((A), (const char *)(B), (int)(C), (D))
#   define WRITE(A,B,C)              ::write((A),(B),(C))
#   define GETSOCKOPT(A,B,C,D,E)     ::getsockopt((A),(B),(C),(char *)(D), (int*)(E))
#   define SETSOCKOPT(A,B,C,D,E)     ::setsockopt((A),(B),(C),(char *)(D), (int )(E))

#   define BIND(A,B,C)               ::bind((A),(B),(C))
#   define LISTEN(A,B)               ::listen((A),(B))
#   define SHUTDOWN(A)               ::shutdown((A),2)
#   define SHUTDOWN_R(A)             ::shutdown((A),0)
#   define SHUTDOWN_W(A)             ::shutdown((A),1)

    namespace
    {
        // fill missing api

        enum
        {
            F_GETFL = 0,
            F_SETFL = 1,

            O_NONBLOCK = 128 // dummy
        };

        int fcntl( int &sockfd, int mode, int value )
        {
            if( mode == F_GETFL ) // get socket status flags
                return 0; // original return current sockfd flags

            if( mode == F_SETFL ) // set socket status flags
            {
                u_long iMode = ( value & O_NONBLOCK ? 0 : 1 );

                bool result = ( ioctlsocket( sockfd, FIONBIO, &iMode ) == NO_ERROR );

                return 0;
            }

            return 0;
        }
    }

#else

#   include <fcntl.h>
#   include <sys/types.h>
#   include <sys/socket.h>
#   include <netdb.h>
#   include <unistd.h>    //close

#   include <arpa/inet.h> //inet_addr

#   define ACCEPT(A,B,C)             ::accept((A),(B),(C))
#   define CONNECT(A,B,C)            ::connect((A),(B),(C))
#   define CLOSE(A)                  ::close((A))
#   define READ(A,B,C)               ::read((A),(B),(C))
#   define RECV(A,B,C,D)             ::recv((A), (void *)(B), (C), (D))
#   define SELECT(A,B,C,D,E)         ::select((A),(B),(C),(D),(E))
#   define SEND(A,B,C,D)             ::send((A), (const int8 *)(B), (C), (D))
#   define WRITE(A,B,C)              ::write((A),(B),(C))
#   define GETSOCKOPT(A,B,C,D,E)     ::getsockopt((int)(A),(int)(B),(int)(C),(      void *)(D),(socklen_t *)(E))
#   define SETSOCKOPT(A,B,C,D,E)     ::setsockopt((int)(A),(int)(B),(int)(C),(const void *)(D),(int)(E))

#   define BIND(A,B,C)               ::bind((A),(B),(C))
#   define LISTEN(A,B)               ::listen((A),(B))
#   define SHUTDOWN(A)               ::shutdown((A),SHUT_RDWR)
#   define SHUTDOWN_R(A)             ::shutdown((A),SHUT_RD)
#   define SHUTDOWN_W(A)             ::shutdown((A),SHUT_WR)

#endif

#include <set>

namespace knot
{
    enum settings {
       threaded = true
    };

    namespace
    {
        volatile bool is_app_exiting = false;
        std::set<std::string> listening_ports;

        void exiting()
        {
            is_app_exiting = true;

            for( std::set<std::string>::iterator it = listening_ports.begin(), end = listening_ports.end(); it != end; ++it )
            {
                fprintf( stdout, "<knot/knot.cpp> says: listening port %s is about to close", it->c_str() );

                // dummy request
                int dummyfd;
                knot::connect( dummyfd, "127.0.0.1", *it );
            }
        }

        bool startup()
        {
#           ifdef _WIN32
                WSADATA wsaData;
                // Initialize Winsock
                int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
                if( iResult != 0 )
                {
                    printf( "WSAStartup failed: %d\n", iResult);
                    assert(!"WSAStartup failed!" );
                    return false;
                }
#           endif

            atexit( exiting );

            return true;
        }

        const bool initialized = startup();
    }

    namespace
    {
        size_t bytes_sent = 0;
        size_t bytes_recv = 0;

        // common stuff

        enum
        {
            TCP_OK = 0,
            TCP_ERROR = -1,
            TCP_TIMEOUT = -2
    //      TCP_DISCONNECTED = -3,
        };

        timeval as_timeval( double seconds )
        {
            timeval tv;
            tv.tv_sec = (int)(seconds);
            tv.tv_usec = (int)((seconds - (int)(seconds)) * 1000000.0);
            return tv;
        }

        int select( int &sockfd, double timeout )
        {
            // set up the file descriptor set
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sockfd, &fds);

            // set up the struct timeval for the timeout
            timeval tv = as_timeval( timeout );

            // wait until timeout or data received
            // if  tv = {n,m}, then select() waits up to n.m seconds
            // if  tv = {0,0}, then select() does polling
            // if &tv =  NULL, then select() waits forever
            int n = ::select(sockfd+1, &fds, NULL, NULL, &tv);
            return ( n == -1 ? sockfd = -1, TCP_ERROR : n == 0 ? TCP_TIMEOUT : TCP_OK );
        }
    }

    // api

    void sleep( double seconds )
    {
        timeval tv = as_timeval( seconds );

#       ifdef _WIN32
            SOCKET s = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP );

            fd_set dummy;
            FD_ZERO( &dummy );
            FD_SET( s, &dummy );

            bool sucess = ( ::select(0, 0, 0, &dummy, &tv) == 0 );
            closesocket(s);
#       else
            int rv = ::select(0, 0, NULL, NULL, &tv);
#       endif
    }

    bool connect( int &sockfd, const std::string &ip, const std::string &port, double timeout_sec )
    {
        struct local
        {
            // [2] from unpv12e/lib/connect_nonb.c
            static bool connect_nonb( int sockfd, const sockaddr *saptr, socklen_t salen, double timeout_sec )
            {
                int             flags, n, error;
                socklen_t       len;
                fd_set          rset, wset;
                struct timeval  tval;

                flags = fcntl(sockfd, F_GETFL, 0);
                fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

                error = 0;
                if ( (n = ::connect(sockfd, (struct sockaddr *) saptr, salen)) < 0)
                    if (errno != EINPROGRESS)
                        return false;

                /* Do whatever we want while the connect is taking place. */

                if (n == 0)
                    goto done;  /* connect completed immediately */

                    FD_ZERO(&rset);
                    FD_SET(sockfd, &rset);
                    wset = rset;
                    tval = as_timeval( timeout_sec );

                    if ( (n = ::select(sockfd+1, &rset, &wset, NULL,
                                     timeout_sec > 0 ? &tval : NULL)) == 0) {
                        CLOSE(sockfd);      /* timeout */
                        errno = ETIMEDOUT;
                        return false;
                    }

                if (FD_ISSET(sockfd, &rset) || FD_ISSET(sockfd, &wset)) {
                    len = sizeof(error);
                    if (GETSOCKOPT(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
                        return false;           /* Solaris pending error */
                } else
                    return false; //err_quit("select error: sockfd not set");

            done:
                fcntl(sockfd, F_SETFL, flags);  /* restore file status flags */

                if (error) {
                    CLOSE(sockfd);      /* just in case */
                    errno = error;
                    return false;
                }
                return true;
            }
        };

        // connect to www.example.com port 80 (http)
        addrinfo hints, *servinfo;

        // first, load up address structs with getaddrinfo():
        memset( &hints, 0, sizeof( hints ) );
        hints.ai_family = AF_UNSPEC;     // use IPv4 or IPv6, whichever
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

        if( getaddrinfo( ip.c_str(), port.c_str(), &hints, &servinfo ) != 0 )
            return sockfd = -1, false;

        // make a socket
        sockfd = socket( servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol );

        // connect
        bool result = local::connect_nonb( sockfd, servinfo->ai_addr, servinfo->ai_addrlen, timeout_sec );
        freeaddrinfo( servinfo );

        return result;
    }

    bool get_interface_address( int &sockfd, std::string &ip, std::string &port )
    {
       ip = port = std::string();

       if( sockfd < 0 )
         return false;

       struct sockaddr_in addr;
       socklen_t size = sizeof( sockaddr );

       getsockname( sockfd, (struct sockaddr *)&addr, &size );

       ip = inet_ntoa( addr.sin_addr );

       std::stringstream ss;
       ss << addr.sin_port;
       ss >> port;

       return true;
    }

    bool send( int &sockfd, const std::string &output, double timeout_sec )
    {
        if( sockfd < 0 )
            return false;

        std::string out = output;

        int bytes_sent;

        do
        {
            /*
            if( timeout_sec > 0.0 )
                if( select( sockfd, timeout_sec ) != TCP_OK )
                    return false; // error or timeout
            */

            // todo timeout_sec -= dt.s()

            bytes_sent = ::send( sockfd, out.c_str(), out.size(), 0 );

            if( bytes_sent == 0 )
                return false;   // error! (?)

            if( bytes_sent < 0 )
                return false;   // error

            knot::bytes_sent += bytes_sent;

            out = out.substr( bytes_sent );
        }
        while( out.size() > 0 );

        SHUTDOWN_W( sockfd );

        return true;
    }

    bool receive( int &sockfd, std::string &input, double timeout_sec )
    {
        if( sockfd < 0 )
            return false;

        input = std::string();

        bool receiving = true;

        while( receiving )
        {
            if( timeout_sec > 0.0 )
                if( select( sockfd, timeout_sec ) != TCP_OK )
                    return false;    // error or timeout

            // todo timeout_sec -= dt.s()

            std::string buffer( 4096, '\0' );
            int bytes_received = ::recv( sockfd, &buffer[0], buffer.size(), 0 );

            if( bytes_received < 0 )
                return false;        // error or timeout

            knot::bytes_recv += bytes_received;

            input += buffer.substr( 0, bytes_received );

            if( bytes_received == 0 )
                return /*sockfd = -1,*/ true;   // ok! remote side closed connection
        }

        return true;
    }

    // similar to receive() but looks for '\r\n' terminator at end of while loop
    bool receive_www( int &sockfd, std::string &input, double timeout_sec )
    {
        if( sockfd < 0 )
            return false;

        input = std::string();

        bool receiving = true;

        while( receiving )
        {
            if( timeout_sec > 0.0 )
                if( select( sockfd, timeout_sec ) != TCP_OK )
                    return false;    // error or timeout

            // todo timeout_sec -= dt.s()

            std::string buffer( 4096, '\0' );
            int bytes_received = ::recv( sockfd, &buffer[0], buffer.size(), 0 );

            if( bytes_received < 0 )
                return false;        // error or timeout

            knot::bytes_recv += bytes_received;

            input += buffer.substr( 0, bytes_received );

            if( bytes_received == 0 )
                return /*sockfd = -1,*/ true;   // ok! remote side closed connection

            if( input.substr( input.size() - 4 ) == "\r\n\r\n" )
                receiving = false;
        }

        return true;
    }

    bool close( int &sockfd, double timeout_sec )
    {
        if( sockfd < 0 )
            return true;

        bool success = ( CLOSE( sockfd ) == 0 );
        sockfd = -1;

        return success;
    }

    bool listen( int &fd, const std::string &_port, void (*callback)( int master_fd, int child_fd, std::string client_addr_ip, std::string client_addr_port ), unsigned backlog_queue )
    {
        unsigned port;
        {
            std::stringstream ss( _port );
            if( !(ss >> port) )
                return "error: invalid port number", false;
        }

        struct sockaddr_in stSockAddr;
        fd = ::socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

        if( fd == -1 )
            return "error: cannot create socket", false;

        memset(&stSockAddr, 0, sizeof(stSockAddr));

        stSockAddr.sin_family = AF_INET;
        stSockAddr.sin_port = htons( port );
        stSockAddr.sin_addr.s_addr = INADDR_ANY;

        if( BIND( fd, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr) ) == -1 )
        {
            CLOSE(fd);
            return "error: bind failed", false;
        }

        if( LISTEN( fd, backlog_queue ) == -1 )
        {
            CLOSE( fd );
            return "error: listen failed", false;
        }

        struct worker
        {
            static void job( volatile bool *ready, int master_fd, void (*callback)( int master_fd, int child_fd, std::string client_addr_ip, std::string client_addr_port ) )
            {
                *ready = true;

                try {

                    while( !is_app_exiting )
                    {
                        struct sockaddr_in client_addr;
                        int client_len = sizeof(client_addr);
                        memset( &client_addr, 0, client_len );

                        int child_fd = ACCEPT( master_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_len );

                        if( is_app_exiting )
                            return;

                        if( child_fd < 0 )
                            continue; // CLOSE(master_fd) and return instead? die("accept() failed"); ?

                        const char *client_addr_ip = inet_ntoa( client_addr.sin_addr );
                        std::string client_addr_port;

                        std::stringstream ss;
                        ss << ntohs( client_addr.sin_port );
                        ss >> client_addr_port;

                        if( settings::threaded )
                            std::thread( *callback, master_fd, child_fd, client_addr_ip, client_addr_port ).detach();
                        else
                            (*callback)( master_fd, child_fd, client_addr_ip, client_addr_port );

                        /* this should be done inside callback!

                        if( SHUTDOWN( child_fd ) == -1 )
                        {
                            CLOSE( child_fd );
                            "error: cannot shutdown socket";
                            return;
                        }

                        CLOSE( child_fd );

                        */
                    }
                }
                catch(...) {

                }
            }
        };

        // 2013.04.30.17:49 @r-lyeh says: My Ubuntu Linux setup passes this C++11
        // block *only* when -lpthread is specified at linking stage. Go figure {
        try {
            volatile bool ready = false;

            std::thread( &worker::job, &ready, fd, callback ).detach();

            while( !ready )
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

            listening_ports.insert( _port );

            return true;
        }
        catch(...) {
        }
        // }
        CLOSE( fd );
        return "cannot launch listening thread. forgot -lpthread?", false;
    }

    // stats
    size_t get_bytes_received()
    {
        return bytes_recv;
    }
    size_t get_bytes_sent()
    {
        return bytes_sent;
    }
    void reset_counters()
    {
        bytes_recv = bytes_sent = 0;
        // @todo: reset stats timers here
    }
} // knot::
