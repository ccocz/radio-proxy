#include <thread>
#include "UDPServer.h"

UDPServer::UDPServer(po::variables_map &args) {
    this->args = args;
    local_port = args["Port"].as<in_port_t>();
    local_dotted_address = args["address"].as<std::string>();
    this->timeout = args["timeoutclient"].as<int>();
}

void UDPServer::connect() {
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        syserr("socket");
    }
    ipMreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (!args["address"].defaulted()) {
        if (inet_aton(&local_dotted_address[0], &ipMreq.imr_multiaddr) == 0) {
            std::cerr << "ERROR: inet_aton - invalid multicast address" << std::endl;
            exit(1);
        }
    }
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&ipMreq, sizeof(ipMreq)) < 0) {
        syserr("setsockopt");
    }
    local_address.sin_family = AF_INET;
    local_address.sin_addr.s_addr = htonl(INADDR_ANY);
    local_address.sin_port = htons(local_port);
    if (bind(sock, (struct sockaddr*)&local_address, sizeof(local_address)) < 0) {
        syserr("bind");
    }
    // close and drop membership
}

[[noreturn]] void UDPServer::update_clients() {
    /* wait for request from client */
    struct sockaddr_in sender;
    socklen_t fromlen = sizeof(sender);
    buffer.resize(B_SIZE);
    while (true) {
        ssize_t rcv_len = recvfrom(sock, &buffer, B_SIZE, 0, (struct sockaddr *) &sender, &fromlen);
        if (rcv_len < 0) {
            syserr("recvfrom");
        }
        uint16_t type;
        memcpy(&type, &buffer[0], 2);
        type = ntohs(type);
        mutex.lock();
        ban_clients();
        if (type == 1) {
            /* DISCOVER */
            /* update client time */
            update_client_time(UDPClient(sender), 1);
            /* send IAM response back */
            std::string response = IAM_response();
            if (sendto(sock, &response[0], response.length(), 0, (struct sockaddr*)&sender,
                    sizeof(sender)) != (ssize_t)response.length()) {
                syserr("sento");
            }
        } else if (type == 3) {
            /* KEEPALIVE */
            update_client_time(UDPClient(sender), 0);
        }
        mutex.unlock();

    }

}

bool UDPServer::deliver_data(std::string &data, bool meta) {
    std::string response = data_response(data, meta);
    mutex.lock();
    for (UDPClient &udpClient : clients) {
        if (!udpClient.is_banned()) {
            struct sockaddr_in sender = udpClient.get_addr();
            if (sendto(sock, &response[0], response.length(), 0, (struct sockaddr*)&sender,
                       sizeof(sender)) != (ssize_t)response.length()) {
                syserr("sento");
            }
        }
    }
    mutex.unlock();
}

void UDPServer::update_client_time(UDPClient udpClient, bool discover) {
    for (UDPClient &udpClient1 : clients) {
        if (udpClient1.is_equal(udpClient)) {
            if (!udpClient1.is_banned()) {
                auto now = std::chrono::system_clock::now();
                udpClient1.set_last_request(std::chrono::system_clock::to_time_t(now));
            }
            return;
        }
    }
    if (discover) {
        clients.push_back(udpClient);
    }
}

std::string UDPServer::IAM_response() {
    std::string response;
    /* type */
    uint16_t type = htons(2);
    /* length and the information about radio */
    uint16_t length = args["host"].as<std::string>().length() +
            args["resource"].as<std::string>().length();
    std::string info = args["host"].as<std::string>() + args["resource"].as<std::string>();
    /* 4 bytes for header and the rest of info */
    response.resize(4 + length);
    length = htons(length);
    /* encrypt message to client */
    memcpy(&response[0], &type, sizeof(type));
    memcpy(&response[sizeof(type)], &length, sizeof(length));
    memcpy(&response[sizeof(type) + sizeof(length)], &info, info.length());
    return response;
}

void UDPServer::ban_clients() {
    for (UDPClient &udpClient : clients) {
        if (!udpClient.is_banned()) {
            time_t start = udpClient.get_last_request();
            time_t end = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            time_t elapsed_seconds = end - start;
            if (elapsed_seconds > timeout) {
                udpClient.ban();
            }
        }
    }
}

std::string UDPServer::data_response(std::string &data, bool meta) {
    std::string response;
    /* type */
    uint16_t type;
    if (!meta) {
        type = htons(4);
    } else {
        type = htons(6);
    }
    /* length and the information about radio */
    uint16_t length = data.length();
    /* 4 bytes for header and the rest for data */
    response.resize(4 + length);
    length = htons(length);
    /* encrypt message to client */
    memcpy(&response[0], &type, sizeof(type));
    memcpy(&response[sizeof(type)], &length, sizeof(length));
    memcpy(&response[sizeof(type) + sizeof(length)], &data, data.length());
    return response;

}