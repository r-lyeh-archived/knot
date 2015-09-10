/* Knot is a lightweight and simple TCP network C++11 library with no dependencies.
 * - rlyeh, zlib/libpng licensed

 * URL encoder/decoder code is based on code by Fred Bulback.

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

#include <deque>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)

#   include <cassert>

#   ifndef _MSC_VER                    // MingW/GCC madness to get inet_pton() working
#       include <w32api.h>
#       undef WINVER
#       undef _WIN32_WINDOWS
#       undef _WIN32_WINNT
#       define WINVER                  WindowsVista
#       define _WIN32_WINDOWS          WindowsVista
#       define _WIN32_WINNT            WindowsVista
#   endif

//#   include <winsock2.h>
#   include <ws2tcpip.h>
#   include <windows.h>

#   pragma comment(lib,"ws2_32.lib")

#   define INIT()                    do { static WSADATA wsa_data; static const int init = WSAStartup( MAKEWORD(2, 2), &wsa_data ); } while(0)
//#   define SOCKET(A,B,C)             ::socket((A),(B),(C))
#   define ACCEPT(A,B,C)             ::accept((A),(B),(C))
#   define CONNECT(A,B,C)            ::connect((A),(B),(C))
#   define CLOSE(A)                  ::closesocket((A))
#   define RECV(A,B,C,D)             ::recv((A), (char *)(B), (C), (D))
#   define READ(A,B,C)               ::recv((A), (char *)(B), (C), (0))
#   define SELECT(A,B,C,D,E)         ::select((A),(B),(C),(D),(E))
#   define SEND(A,B,C,D)             ::send((A), (const char *)(B), (int)(C), (D))
#   define WRITE(A,B,C)              ::send((A), (const char *)(B), (int)(C), (0))
#   define GETSOCKOPT(A,B,C,D,E)     ::getsockopt((A),(B),(C),(char *)(D), (int*)(E))
#   define SETSOCKOPT(A,B,C,D,E)     ::setsockopt((A),(B),(C),(char *)(D), (int )(E))

#   define BIND(A,B,C)               ::bind((A),(B),(C))
#   define LISTEN(A,B)               ::listen((A),(B))
#   define SHUTDOWN(A)               ::shutdown((A),2)
#   define SHUTDOWN_R(A)             ::shutdown((A),0)
#   define SHUTDOWN_W(A)             ::shutdown((A),1)

#   define inet_pton InetPtonA

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

#   include <arpa/inet.h> //inet_addr, inet_pton
#   include <netinet/tcp.h> // TCP_NODELAY 

#   define INIT()                    do {} while(0)
//#   define SOCKET(A,B,C)             ::socket((A),(B),(C))
#   define ACCEPT(A,B,C)             ::accept((A),(B),(C))
#   define CONNECT(A,B,C)            ::connect((A),(B),(C))
#   define CLOSE(A)                  ::close((A))
#   define READ(A,B,C)               ::read((A),(B),(C))
#   define RECV(A,B,C,D)             ::recv((A), (void *)(B), (C), (D))
#   define SELECT(A,B,C,D,E)         ::select((A),(B),(C),(D),(E))
#   define SEND(A,B,C,D)             ::send((A), (const char *)(B), (C), (D))
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

#define CRLF "\r\n"

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
                INIT();
            }
        } startup;
        )
    }

    namespace
    {
        size_t bytes_sent = 0;
        size_t bytes_recv = 0;
        const std::string white_spaces( " \f\n\r\t\v" );
        

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
            int ret = SELECT(sockfd+1, &fds, NULL, NULL, &tv);
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

            int ret = SELECT(sockfd + 1, before_read ? &fds : NULL, after_write ? &fds : NULL, NULL, &tv);
            return ( ret == -1 ? sockfd = -1, TCP_ERROR : ret == 0 ? TCP_TIMEOUT : TCP_OK );
        }

        bool valid_method(std::string &method, unsigned valid_mask)
        {
            if( method == "GET")
                return valid_mask & RM_GET;
            else if( method == "POST")
                return valid_mask & RM_POST;
            else if( method == "HEAD")
                return valid_mask & RM_HEAD;
            else if( method == "PUT")
                return valid_mask & RM_PUT;
            else if( method == "DELETE")
                return valid_mask & RM_DELETE;
            else if( method == "TRACE")
                return valid_mask & RM_TRACE;
            else if( method == "OPTIONS")
                return valid_mask & RM_OPTIONS;
            else
                return 0;
        }

        std::string trim( std::string str, const std::string& trimChars = white_spaces )
        {
            std::string::size_type pos_end = str.find_last_not_of( trimChars );
            std::string::size_type pos_start = str.find_first_not_of( trimChars );

            return str.substr( pos_start, pos_end - pos_start + 1 );
        }

        void extract_headers( std::string input, std::map<std::string, std::string> &headers, int start)
        {
            std::string::size_type colon;
            std::string::size_type crlf;

            if ( ( colon = input.find(':', start) ) != std::string::npos && 
                 ( ( crlf = input.find(CRLF, start) ) != std::string::npos ) )
            {
                headers.insert( std::pair<std::string, std::string>( trim( input.substr(start, colon - start) ),
                                                                     trim( input.substr(colon+1, crlf - colon -1 ) ) )
                                );

                extract_headers(input, headers, crlf+2);
            }
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

            bool sucess = ( SELECT(0, 0, 0, &dummy, &tv) == 0 );
            CLOSE(s);
        })
        $welse({
            int rv = SELECT(0, 0, NULL, NULL, &tv);
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
                if ( (n = CONNECT(sockfd, (struct sockaddr *) saptr, salen)) < 0) {
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

                    if ( (n = SELECT(sockfd+1, &rset, &wset, NULL,
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
        int flags = $windows(0) $welse( MSG_NOSIGNAL );
        do
        {
            /*
            if( timeout_sec > 0.0 )
                if( select( sockfd, timeout_sec ) != TCP_OK )
                    return false; // error or timeout
            */

            // todo timeout_sec -= dt.s()
            bytes_sent = SEND( sockfd, out.c_str(), out.size(), flags );

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

        //SHUTDOWN_W( sockfd );

        return true;
    }

    bool close_r( int &sockfd ) {
        if( sockfd < 0 )
            return false;

        SHUTDOWN_R( sockfd );

        return true;
    }

    bool close_w( int &sockfd ) {
        if( sockfd < 0 )
            return false;

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
            int bytes_received = RECV( sockfd, &buffer[0], buffer.size(), 0 );

            if( bytes_received < 0 )
                return false;        // error or timeout

            knot::bytes_recv += bytes_received;

            input += buffer.substr( 0, bytes_received );

            if( bytes_received == 0 )
                return /*sockfd = -1,*/ true;   // ok! remote side closed connection
        }

        return true;
    }

    bool receive_www( int &sockfd, std::string &input, double timeout_sec, unsigned valid_method_mask )
    {
        std::string data;
        std::string request;
        std::map<std::string, std::string> headers;
        std::string location;

        return receive_www( sockfd, request, location, input, data, headers, timeout_sec, valid_method_mask );
    }

    // very simple implementation of RFC2616 (http://tools.ietf.org/html/rfc2616)
    bool receive_www( int &sockfd, std::string &request_method, std::string &raw_location, std::string &input, std::string &data, std::map<std::string, std::string> &headers, double timeout_sec, unsigned valid_method_mask )
    {
        if( sockfd < 0 )
            return false;

        input = std::string();
        data = std::string();
        request_method = std::string();

        bool receiving = true;
        int content_length = -1;
        int payload_received = 0;
        std::string::size_type first_crlf;

        while( receiving )
        {
            if( timeout_sec > 0.0 )
                if( select( sockfd, timeout_sec ) != TCP_OK )
                    return false;    // error or timeout

            // todo timeout_sec -= dt.s()
            std::string buffer( 4096, '\0' );
            int bytes_received = RECV( sockfd, &buffer[0], buffer.size(), 0 );

            if( bytes_received < 0 )
                return false;        // error or timeout

            knot::bytes_recv += bytes_received;

            if( content_length > -1 )
            {
                payload_received += bytes_received;
                data += buffer.substr( 0, bytes_received );
            }
            else
                input += buffer.substr( 0, bytes_received );

            if( bytes_received == 0 )
                return /*sockfd = -1,*/ true;   // ok! remote side closed connection

            // Get request type
            if( request_method.empty() )
            {
          // Don't have enough information
          if (input.find(CRLF) == std::string::npos)
        continue;

                std::string::size_type space_pos;
                request_method = input.substr( 0, ( ( space_pos = input.find( ' ' ) ) != std::string::npos )? space_pos : 0  );
                // Test valid request type
                if( !valid_method( request_method, valid_method_mask) )
                {
                    return false;
                }
                // Test protocol
                if( input.substr( ( first_crlf = input.find(CRLF) ) - 8, 8) != "HTTP/1.1" )
                    return false; // Bad protocol

                // get location
                raw_location = trim( input.substr(space_pos, first_crlf-8-space_pos) );
            }

            // try to find the first CRLFCRLF which indicates the end of headers and
            // find out if we have payload, only if "Content-length" header is set.
            // it's possible to have payload without Content-length, but we won't have
            // this case.
            std::string::size_type crlf_2 = input.find("\r\n\r\n");
            if( crlf_2 != std::string::npos && content_length == -1 )
            {
                extract_headers(input, headers, first_crlf+2);

                if ( !headers["Content-Length"].empty() )
                {
                    content_length = atoi( headers["Content-Length"].c_str() );
                    payload_received = input.length() - crlf_2;
                    data = input.substr(crlf_2+4);
                    input.erase(crlf_2);
                }
                else
                    receiving = false;
            }

            if( content_length > -1 && payload_received >= content_length )
            {
                receiving = false;
            }
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
                            std::thread( std::bind( *control->callback, control->master_fd, child_fd, client_addr_ip, client_addr_port ) ).detach();
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

namespace knot {

std::string encode( const std::string &str ) {
    auto to_hex = [](char code) -> char {
      static char hex[] = "0123456789abcdef";
      return hex[code & 15];
    };

    std::string out( str.size() * 3, '\0' );
    const char *pstr = str.c_str();
    char *buf = &out[0], *pbuf = buf;
    while (*pstr) {
        if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~')
            *pbuf++ = *pstr;
        else if (*pstr == ' ')
            *pbuf++ = '+';
        else
            *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 15);
        pstr++;
    }

    return out.substr( 0, pbuf - buf );
}

std::string decode( const std::string &str ) {
    auto from_hex = [](char ch) -> char {
      return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
    };

    const char *pstr = str.c_str();
    std::string out( str.size(), '\0' );
    char *buf = &out[0], *pbuf = buf;
    while (*pstr) {
        if (*pstr == '%') {
            if (pstr[1] && pstr[2]) {
                *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
                pstr += 2;
            }
        } else if (*pstr == '+') {
            *pbuf++ = ' ';
        } else {
            *pbuf++ = *pstr;
        }
        pstr++;
    }

    return out.substr( 0, pbuf - buf );
}

uri lookup( const std::string &addr, unsigned port )
{
    INIT();

    uri out;

    for( int i = 0; i < 6; ++i )
        out.inet.ip[i] = 0;
    out.inet.port = 0;

    auto &inet = out.inet;
    auto &pretty = out.pretty;

    // @todo: replace deprecated gethostbyname()
    struct hostent *h = gethostbyname( addr.c_str() );

    if( h == NULL)
        return out.ok = false, out;

    if( port > 65536 || port == 0 )
        return out.ok = false, out;

    char ch;
    std::stringstream ss( inet_ntoa(*((struct in_addr *)h->h_addr)) );
    ss >> inet.ip[0] >> ch >> inet.ip[1] >> ch >> inet.ip[2] >> ch >> inet.ip[3];
    inet.port = port;

    switch( port )
    {
        break; case    7: pretty.protocol = "echo://";
        break; case   13: pretty.protocol = "daytime://";
        break; case   17: pretty.protocol = "qotd://";
        break; case   20: pretty.protocol = "ftp-data://";
        break; case   21: pretty.protocol = "ftp://";
        break; case   22: pretty.protocol = "ssh://";
        break; case   23: pretty.protocol = "telnet://";
        break; case   25: pretty.protocol = "smtp://";
        break; case   37: pretty.protocol = "time://";
        break; case   53: pretty.protocol = "dns://";
        break; case   69: pretty.protocol = "tftp://";
        break; case   70: pretty.protocol = "gopher://";
        break; case   79: pretty.protocol = "finger://";
        break; case   80: pretty.protocol = "http://";
        break; case  110: pretty.protocol = "pop://";
        break; case  113: pretty.protocol = "ident://";
        break; case  119: pretty.protocol = "nntp://";
        break; case  123: pretty.protocol = "ntp://";
        break; case  138: pretty.protocol = "netbios://";
        break; case  143: pretty.protocol = "imap://";
        break; case  161: pretty.protocol = "snmp://";
        break; case  443: pretty.protocol = "https://";
        break; case  465: pretty.protocol = "smtps://";
        break; case  500: pretty.protocol = "ipsec://";
        break; case  513: pretty.protocol = "rlogin://";
        break; case  993: pretty.protocol = "imaps://";
        break; case  995: pretty.protocol = "pops://";
        break; case 1080: pretty.protocol = "socks://";
        break; case 3306: pretty.protocol = "mysql://";
        break; case 5000: pretty.protocol = "upnp://";
        break; case 5222: pretty.protocol = "xmpp://";
        break; case 5223: pretty.protocol = "xmpps://";
        break; case 5269: pretty.protocol = "xmpp-server://";
        break; case 6667: pretty.protocol = "irc://";
        break; case 8080: pretty.protocol = "http://";

        default: pretty.protocol = "";
    }

    pretty.hostname = h->h_name;
    pretty.ip = ss.str();

    std::stringstream ssp;
    ssp << port;
    pretty.port = ssp.str();
    pretty.address = addr;

    pretty.url = pretty.protocol + pretty.address + ':' + pretty.port;

#if 0
    std::cout << pretty.url << std::endl;
    std::cout << pretty.protocol << std::endl;
    std::cout << pretty.address << std::endl;
    std::cout << pretty.hostname << std::endl;
    std::cout << pretty.ip << std::endl;
    std::cout << pretty.port << std::endl;

    std::cout << inet.ip[0] << std::endl;
    std::cout << inet.ip[1] << std::endl;
    std::cout << inet.ip[2] << std::endl;
    std::cout << inet.ip[3] << std::endl;
    std::cout << inet.port << std::endl;
#endif

    return out.ok = true, out;
}

uri lookup( const std::string &addr, const std::string &port )
{
    unsigned p;
    if( !(std::stringstream(port) >> p) ) {
        uri u;
        u.ok = false;
        return u;
    }
    return lookup(addr,p);
}

namespace
{
    std::deque<std::string> split( const std::string &input, const std::string &delimiters ) {
        std::string str;
        std::deque<std::string> tokens;
        for( auto &ch : input ) {
            if( delimiters.find_first_of( ch ) != std::string::npos ) {
                if( str.size() ) tokens.push_back( str ), str = "";
                tokens.push_back( std::string() + ch );
            } else str += ch;
        }
        return str.empty() ? tokens : ( tokens.push_back( str ), tokens );
    }

    std::string left_of( const std::string &input, const std::string &substring ) {
        std::string::size_type pos = input.find( substring );
        return pos == std::string::npos ? input : input.substr(0, pos);
    }

    typedef std::map<std::string,std::string> decomposed;

    decomposed decompose( const std::string &full ) {

        decomposed map;

        map["full"] = full;
        map["host"] = std::string();
        map["port"] = std::string();
        map["user"] = std::string();
        map["pass"] = std::string();
        map["proto"] = std::string();

        std::deque<std::string> tokens = split(full, "/"); // http:, /, /, ..., /

        if( tokens.size() >= 4 && left_of(tokens[0], ":").size() > 0 ) {
            map["proto"] = tokens[0] + "//";

            tokens.pop_front(); // http:
            tokens.pop_front(); // /
            tokens.pop_front(); // /
        }

        if( tokens.size() > 0 && tokens[0] != "/" ) {
            std::deque<std::string> localhost = split( tokens[0], "@" );
            if( localhost.size() > 1 ) {
                std::deque<std::string> userpass = split( localhost[0], ":" );
                map["user"] = userpass[0];
                map["pass"] = userpass.size() > 1 ? userpass[2] : std::string();

                std::deque<std::string> hostport = split( localhost[2], ":" );
                map["host"] = hostport[0];
                map["port"] = hostport.size() > 1 ? hostport[2] : std::string();
            } else {
                std::deque<std::string> hostport = split( localhost[0], ":" );
                map["host"] = hostport[0];
                map["port"] = hostport.size() > 1 ? hostport[2] : std::string();
            }

            tokens.pop_front(); // user:pass@host:port
        }

        // path = sum of remaining tokens: /, ..., /, ..., [ /, ... ]
        for( auto &token : tokens )
            ( map["path"] = map["path"] ) += token;

        if( map["port"].empty() )
            map["port"] = "80";

        if( map["path"].empty() )
            map["path"] = "/";

        return map;
    }
}

uri lookup( const std::string &url )
{
    auto map = decompose(url);
    auto   u = lookup( map["host"], map["port"] );

    u.pretty.user = map["user"];
    u.pretty.pass = map["pass"];
    u.pretty.host = map["host"];
    u.pretty.port = map["port"];
    u.pretty.path = map["path"];
    u.pretty.full = map["full"];

    if( map["proto"].size() )
        u.pretty.protocol = map["proto"];

    return u;
}

std::string uri::print() const {
    std::stringstream ss;

#define $p(n) ss << #n << "=" << this->n << std::endl

    $p(ok);

    $p(pretty.full);
    $p(pretty.path);
    $p(pretty.url);
    $p(pretty.protocol);
    $p(pretty.hostname);
    $p(pretty.address);
    $p(pretty.user);
    $p(pretty.pass);
    $p(pretty.host);
    $p(pretty.ip);
    $p(pretty.port);

    $p(inet.ip[0]);
    $p(inet.ip[1]);
    $p(inet.ip[2]);
    $p(inet.ip[3]);
    $p(inet.ip[4]);
    $p(inet.ip[5]);
    $p(inet.port);

#undef $p

    return ss.str();
}
} // ::knot


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
//#undef SOCKET
#undef INIT
