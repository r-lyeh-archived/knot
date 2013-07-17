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

#include <errno.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include <future>
#include <iostream>
#include <memory>
#include <map>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)

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

#   define $windows $yes
#   define $welse   $no

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

#   define $windows $no
#   define $welse   $yes

#endif

#define $yes(...) __VA_ARGS__
#define $no(...)

#include "knot.hpp"

namespace knot
{
    enum settings {
       threaded = true
    };

    namespace
    {
        struct control_t {
            int master_fd;
            std::string port;
            volatile bool ready;
            volatile bool exiting;
            volatile bool finished;
            void (*callback)( int master_fd, int child_fd, std::string client_addr_ip, std::string client_addr_port );
        };

        std::map<int,control_t *> listeners;

        $windows(
        struct initialize_winsock {
            initialize_winsock() {
                WSADATA wsa_data;
                int result = WSAStartup( MAKEWORD(2, 2), &wsa_data );
                bool failed = ( result != 0 );
            }
        } startup;
        )
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
            int ret = ::select(sockfd+1, &fds, NULL, NULL, &tv);
            return ( ret == -1 ? sockfd = -1, TCP_ERROR : ret == 0 ? TCP_TIMEOUT : TCP_OK );
        }

        int wait4data(int &sockfd, bool before_read, bool after_write, double timeout)
        {
            // set up the file descriptor set
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sockfd, &fds);

            // set up the struct timeval for the timeout
            timeval tv = as_timeval( timeout );

            int ret = ::select(sockfd + 1, before_read ? &fds : NULL, after_write ? &fds : NULL, NULL, &tv);
            return ( ret == -1 ? sockfd = -1, TCP_ERROR : ret == 0 ? TCP_TIMEOUT : TCP_OK );
        }
    }

    // api

    void sleep( double seconds )
    {
        timeval tv = as_timeval( seconds );

        $windows({
            SOCKET s = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP );

            fd_set dummy;
            FD_ZERO( &dummy );
            FD_SET( s, &dummy );

            bool sucess = ( ::select(0, 0, 0, &dummy, &tv) == 0 );
            closesocket(s);
        })
        $welse({
            int rv = ::select(0, 0, NULL, NULL, &tv);
        })
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
                if ( (n = ::connect(sockfd, (struct sockaddr *) saptr, salen)) < 0) {
/*                  if (errno != EINPROGRESS)
						fprintf(stdout, "n: %d, errno: %s\n", n, strerror(errno)); */
                    if (errno != EINPROGRESS)
                        return false;
				}

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

    bool is_connected( int &sockfd, double timeout_sec )
    {
        if( sockfd < 0 )
            return false;

        char buff;
        return ::recv(sockfd, &buff, 1, MSG_PEEK) != 0;
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

            /*
            if error = EWOULDBLOCK {
                wait4data( sockfd, false, true, timeout );
                if( sock_fd < 0 )
                    return false; // error
                continue;
            }
            */

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

            /*
                wait4data( sockfd, true, false, timeout_sec );
                if( sock_fd < 0 )
                    return false; // error
            */

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

    bool disconnect( int &sockfd, double timeout_sec )
    {
        if( sockfd < 0 )
            return true;

        bool success = ( CLOSE( sockfd ) == 0 );
        sockfd = -1;

        return success;
    }

    bool listen( int &fd, const std::string &_bindip, const std::string &_port, void (*callback)( int master_fd, int child_fd, std::string client_addr_ip, std::string client_addr_port ), unsigned backlog_queue )
    {
        unsigned port;
        {
            if( !(std::stringstream( _port ) >> port) )
                return "error: invalid port number", false;
            if( !port )
                return "error: invalid port number", false;
        }

        std::string bindip = ( _bindip.empty() ? std::string("0.0.0.0") : _bindip );

        struct sockaddr_in stSockAddr;
        fd = ::socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

        if( fd == -1 )
            return "error: cannot create socket", false;

        memset(&stSockAddr, 0, sizeof(stSockAddr));

        inet_pton(AF_INET, bindip.c_str(), &(stSockAddr.sin_addr));

        stSockAddr.sin_family = AF_INET;
        stSockAddr.sin_port = htons( port );
        //stSockAddr.sin_addr.s_addr = INADDR_ANY;

        $welse({
            int yes = 1;
            if ( SETSOCKOPT( fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int) ) == -1 )
            {}
        })

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
            static void job( control_t *control )
            {
                control->ready = true;

                try {

                    while( !control->exiting )
                    {
                        struct sockaddr_in client_addr;
                        int client_len = sizeof(client_addr);
                        memset( &client_addr, 0, client_len );

                        int child_fd = ACCEPT( control->master_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_len );

                        if( control->exiting )
                            break;

                        if( child_fd < 0 )
                            continue; // return instead? CLOSE(control->master_fd) && die("accept() failed"); ?

                        const char *client_addr_ip = inet_ntoa( client_addr.sin_addr );
                        std::string client_addr_port;

                        std::stringstream ss;
                        ss << ntohs( client_addr.sin_port );
                        ss >> client_addr_port;

                        if( settings::threaded )
                            std::thread( *control->callback, control->master_fd, child_fd, client_addr_ip, client_addr_port ).detach();
                        else
                            (*control->callback)( control->master_fd, child_fd, client_addr_ip, client_addr_port );

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

                control->finished = true;
            }
        };

        // 2013.04.30.17:49 @r-lyeh says: My Ubuntu Linux setup passes this C++11
        // block *only* when -lpthread is specified at linking stage. Go figure {
        try {
            control_t *c = new control_t();
            c->ready = false;
            c->exiting = false;
            c->finished = false;
            c->master_fd = fd;
            c->callback = callback;
            c->port = _port;

            std::thread( &worker::job, c ).detach();

            while( !c->ready )
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

            listeners[ fd ] = c;
            return true;
        }
        catch(...) {
        }
        // }
        CLOSE( fd );
        return "cannot launch listening thread. forgot -lpthread?", false;
    }

    bool shutdown( int &sockfd ) {
        if( sockfd < 0 )
            return "invalid socket", false;

        if( listeners.find(sockfd) == listeners.end() )
            return "invalid socket", false;

        auto *listener = listeners[ sockfd ];
        listener->exiting = true;
        while( !listener->finished ) {
            CLOSE( sockfd );
            int dummy_fd; // dummy request
            knot::connect( dummy_fd, "localhost", listener->port, 0.25 );
            knot::connect( dummy_fd, "127.0.0.1", listener->port, 0.25 );
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        delete listener;
        listeners.erase( listeners.find( sockfd ) );

        sockfd = -1;
        return true;
    }

    bool shutdown() {
        bool ok = true;
        while( listeners.size() ) {
            int master_fd = listeners.begin()->second->master_fd;
            ok &= knot::shutdown(master_fd);
        }
        return ok;
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

#undef $no
#undef $yes

#undef $welse
#undef $windows

#undef SHUTDOWN_W
#undef SHUTDOWN_R
#undef SHUTDOWN
#undef LISTEN
#undef BIND

#undef SETSOCKOPT
#undef GETSOCKOPT
#undef WRITE
#undef SEND
#undef SELECT
#undef RECV
#undef READ
#undef CLOSE
#undef CONNECT
#undef ACCEPT
