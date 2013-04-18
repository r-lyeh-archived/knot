#pragma once
#include <string>

namespace knot
{
    // api
    bool connect( int &sockfd, const std::string &ip, const std::string &port, double timeout_secs = 600 );
    bool send( int &sockfd, const std::string &output, double timeout_secs = 600 );
    bool receive( int &sockfd, std::string &input, double timeout_secs = 600 );
    bool receive_www( int &sockfd, std::string &input, double timeout_secs = 600 );
    bool close( int &sockfd, double timeout_secs = 600 );
    void sleep( double secs );

    // api, server side
    bool listen( int &sockfd, const std::string &port, void (*delegate_callback)( int master_fd, int child_fd, std::string client_addr_ip, std::string client_addr_port ), unsigned backlog_queue = 1024 ); // @todo: if mask
    // bool ban( ip/mask, true/false ); // @todo
    // bool limit( ip/mask, downrate, uprate ); // @todo

    // stats
    size_t get_bytes_received();
    size_t get_bytes_sent();
      void reset_counters();

    // tools, get ip (once connected)
    bool get_interface_address( int &sockfd, std::string &ip, std::string &port );
}
