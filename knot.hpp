#pragma once
#include <string>

namespace knot
{
    // api
    bool connect( int &sockfd, const std::string &ip, const std::string &port, double timeout_secs = 600 );
    bool is_connected( int &sockfd, double timeout_secs = 600 );
    bool send( int &sockfd, const std::string &output, double timeout_secs = 600 );
    bool receive( int &sockfd, std::string &input, double timeout_secs = 600 );
    bool receive_www( int &sockfd, std::string &input, double timeout_secs = 600 );
    bool disconnect( int &sockfd, double timeout_secs = 600 );
    bool close_r( int &sockfd );
    bool close_w( int &sockfd );
    void sleep( double secs );

    // api, server side
    bool listen( int &sockfd, const std::string &bindip, const std::string &port, void (*delegate_callback)( int master_fd, int child_fd, std::string client_addr_ip, std::string client_addr_port ), unsigned backlog_queue = 1024 ); // @todo: if mask
    bool shutdown( int &sockfd );
    bool shutdown();
    // bool ban( ip/mask, true/false ); // @todo
    // bool limit( ip/mask, instances_per_ip, downspeed, upspeed ); // @todo

    // stats
    //size_t get_hits();     // @todo, number of requests
    //size_t get_visitors(); // @todo, number of unique visitors (per IP)
    //size_t get_watchers(); // @todo, sizeof set active connections filtered per IP
    size_t get_bytes_received();
    size_t get_bytes_sent();
      void reset_counters();

    // tools
    struct uri
    {
        struct {
            std::string url, protocol, hostname, address;
            std::string user, pass, host, ip, port;
            std::string full, path;
        } pretty;

        struct {
            int ip[6];
            int port;
        } inet;

        int ok;

        std::string print() const;
    };

    // tools, get ip (once connected)
    bool get_interface_address( int &sockfd, std::string &ip, std::string &port );

    // tools, resolve ip/url
    uri lookup( const std::string &url );
    uri lookup( const std::string &addr, unsigned port );
    uri lookup( const std::string &addr, const std::string &port );
    std::string encode( const std::string &url );
    std::string decode( const std::string &url );
}
