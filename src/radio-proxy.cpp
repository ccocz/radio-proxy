//
// Created by resul on 21.05.2020.
//

#include <boost/program_options.hpp>
#include <iostream>
#include <csignal>
#include <netdb.h>
#include <poll.h>
#include "err.h"

namespace po = boost::program_options;

void signal_handler(int signum) {
    std::cerr << "caught signal: " << signum << std::endl;
    exit(1);
}

po::variables_map validate_args(int argc, char **argv) {
    po::options_description description("usage");
    description.add_options()
            ("host,h", po::value<std::string>()->required(), "Host")
            ("resource,r", po::value<std::string>()->required(), "Resource")
            ("port,p", po::value<uint16_t>()->required(), "Port")
            ("meta,m", po::value<std::string>()->default_value("no"), "Meta-data")
            ("timeout,t", po::value<int>()->default_value(5), "Wait timeout");
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, description), vm);
        po::notify(vm);
    } catch (po::error &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        std::cerr << description << std::endl;
        exit(1);
    }
    return vm;
}

int initialize_tcp(std::string host, std::string port) {
    /* split host and port */
    int sock;
    struct addrinfo addr_hints{};
    struct addrinfo *addr_result;
    int err;
    /* 'converting' host/port in string to struct addrinfo */
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    err = getaddrinfo(&host[0], &port[0], &addr_hints, &addr_result);
    if (err == EAI_SYSTEM) { // system error
        syserr("getaddrinfo: %s", gai_strerror(err));
    }
    else if (err != 0) { // other error (host not found, etc.)
        fatal("getaddrinfo: %s", gai_strerror(err));
    }
    /* initialize socket according to getaddrinfo results */
    sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (sock < 0) {
        syserr("socket");
    }
    /* connect socket to the server */
    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0) {
        syserr("connect");
    }
    freeaddrinfo(addr_result);
    return sock;
}

std::string icy_request(const po::variables_map &args) {
    std::vector<std::string> headers;
    headers.push_back("GET " + args["resource"].as<std::string>() + " HTTP/1.0");
    if (args["meta"].as<std::string>() == "yes") {
        headers.emplace_back("Icy-MetaData:1");
    }
    headers.emplace_back("Host: " + args["host"].as<std::string>());
    headers.emplace_back("Connection: close");
    std::string end("\r\n");
    std::string request;
    for (std::string &header : headers) {
        request += header;
        request += end;
    }
    request += end;
    return request;
}

std::vector<std::string> get_headers(int tcp_fd) {
    std::vector<std::string> headers;
    std::string header;
    int read_size;
    char ch;
    while ((read_size = read(tcp_fd, &ch, 1))) {
        if (read_size < 0) {
            syserr("reading from socket");
        }
        char _back = header.empty() ? ' ' : header.back();
        header += ch;
        /* end of the current header */
        if (_back == '\r' && ch == '\n') {
            headers.push_back(header);
            header.clear();
        }
        if (!headers.empty() && headers.back() == "\r\n") {
            break;
        }
    }
    return headers;
}

void parse_raw_response(int tcp_fd) {
    std::vector<std::string> headers = get_headers(tcp_fd);
    if (headers[0].find("200") == std::string::npos) {
        std::cerr << "not ok response: " << headers[0] << std::endl;
        exit(1);
    }
    int read_size;
    char ch;
    while ((read_size = read(tcp_fd, &ch, 1))) {
        if (read_size < 0) {
            syserr("reading from socket");
        }
        std::cout << ch;
    }
}

int meta_interval(std::vector<std::string> &headers) {
    /* search for icy-metaint header among headers */
    std::string interval;
    for (std::string &header : headers) {
        if (header.find("icy-metaint") == std::string::npos) {
            continue;
        }
        interval = header;
        break;
    }
    /* parse size from icy-metaint header */
    std::string token = interval.substr(interval.find(':') + 1, interval.size() - 1);
    std::stringstream stream(token);
    int size;
    if (!(stream >> size)) {
        std::cerr << "invalid metadata size" << std::endl;
    }
    return size;
}

void meta_data_block(int tcp_fd) {
    char temp = 0;
    int read_size;
    read_size = read(tcp_fd, &temp, 1);
    if (read_size < 0) {
        syserr("reading from socket");
    } else if (read_size == 0) {
        std::cerr << "meta-data block size not found on socket" << std::endl;
        exit(1);
    }
    int byte_count = 0;
    char ch;
    int metadata_length = temp * 16;
    while (byte_count != metadata_length && (read_size = read(tcp_fd, &ch, 1))) {
        if (read_size < 0) {
            syserr("reading from the socket");
        }
        std::cerr << ch;
        byte_count++;
    }
    if (byte_count != metadata_length) {
        std::cerr << "metadata is not sent completely";
        exit(1);
    }
}

void parse_response_meta(int tcp_fd) {
    std::vector<std::string> headers = get_headers(tcp_fd);
    int metadata_interval = meta_interval(headers);
    int read_size;
    int byte_count = 0;
    char ch;
    while ((read_size = read(tcp_fd, &ch, 1))) {
        if (read_size < 0) {
            syserr("reading from tcp socket");
        }
        std::cout << ch;
        byte_count++;
        if (byte_count == metadata_interval) {
            meta_data_block(tcp_fd);
            byte_count = 0;
        }
    }
}

void wait_response(int tcp_fd, int timeout, const std::string &meta) {
    /* create poll to wait for 5 seconds */
    struct pollfd pfds[1];
    pfds[0].fd = tcp_fd;
    pfds[0].events = POLLIN;
    int num_events = poll(pfds, 1, timeout * 1000);
    if (num_events == 0) {
        std::cout << "timeout server stopped working" << std::endl;
    } else {
        if (pfds[0].revents & POLLIN) {
            if (meta == "no") {
                parse_raw_response(tcp_fd);
            } else {
                parse_response_meta(tcp_fd);
            }
        } else {
            std::cout << "unexpected event occured: " << pfds[0].revents << std::endl;
        }
    }
}

int main(int argc, char **argv) {
    std::signal(SIGINT, signal_handler);
    po::variables_map args = validate_args(argc, argv);
    int tcp_fd = initialize_tcp(args["host"].as<std::string>(),
            std::to_string(args["port"].as<uint16_t>()));
    std::string request = icy_request(args);
    if (write(tcp_fd, &request[0], request.length()) < 0) {
        syserr("writing to socket");
    }
    while (true) {
        wait_response(tcp_fd, args["timeout"].as<int>(), args["meta"].as<std::string>());
    }
    return 0;
}