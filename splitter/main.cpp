
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include <vector>
#include <string>


int dport = -1;
int listenSock = -1;
std::vector<sockaddr> targets;
std::vector<int> splits;
char inbuf[32768];


void usage()
{
    fprintf(stderr, "usage: splitter port destination:port [destination:port ...]\n");
    exit(1);
}

void closeall(int sock)
{
    if (sock >= 0)
    {
        ::close(sock);
    }
    for (std::vector<int>::iterator ptr(splits.begin()), end(splits.end());
        ptr != end; ++ptr)
    {
        ::close(*ptr);
    }
}


void setup_splits(int sock)
{
    for (size_t i = 0; i != targets.size(); ++i)
    {
        int ns = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ns < 0)
        {
            perror("splitd: child socket() failed");
            closeall(sock);
            exit(3);
        }
        int err = ::connect(ns, (sockaddr const *)&targets[i], sizeof(sockaddr_in));
        if (err < 0)
        {
            perror("splitd: child connect() failed");
            unsigned char const *ap = (unsigned char const *)&((sockaddr_in const *)&targets[i])->sin_addr;
            fprintf(stderr, "%d.%d.%d.%d:%d\n", ap[0], ap[1], ap[2], ap[3],
                ntohs(((sockaddr_in const *)&targets[i])->sin_port));
            closeall(sock);
            exit(3);
        }
        splits.push_back(ns);
    }
}

void listen_loop(int sock)
{
    while (true)
    {
        fd_set rdset;
        FD_ZERO(&rdset);
        FD_SET(sock, &rdset);
        int mx = sock;
        for (std::vector<int>::iterator ptr(splits.begin()), end(splits.end());
            ptr != end; ++ptr)
        {
            FD_SET(*ptr, &rdset);
            if (*ptr > sock)
            {
                mx = *ptr;
            }
        }
        //  wait until something happens
        int i = ::select(mx + 1, &rdset, 0, 0, 0);
        if (i <= 0)
        {
            perror("splitd: select() failed");
            closeall(sock);
            exit(3);
        }
        if (FD_ISSET(sock, &rdset))
        {
            //  forward from the incoming connection to each outgoing connection
            int n = ::recv(sock, inbuf, sizeof(inbuf), 0);
            if (n <= 0)
            {
                perror("splitd: lost incoming connection");
                closeall(sock);
                exit(3);
            }
            for (std::vector<int>::iterator ptr(splits.begin()), end(splits.end());
                ptr != end; ++ptr)
            {
                if (::send(*ptr, inbuf, n, 0) < n)
                {
                    perror("splitd: forward recipient has fallen behind");
                    closeall(sock);
                    exit(3);
                }
            }
        }
        bool first = true;
        for (std::vector<int>::iterator ptr(splits.begin()), end(splits.end());
            ptr != end; ++ptr)
        {
            //  empty each outgoing return, but only forward back to the 
            //  incoming connection if it's from the first outgoing
            if (FD_ISSET(*ptr, &rdset))
            {
                int n = ::recv(*ptr, inbuf, sizeof(inbuf), 0);
                if (n <= 0)
                {
                    perror("splitd: lost forward connectionn");
                    closeall(sock);
                    exit(3);
                }
                if (first)
                {
                    if (::send(sock, inbuf, n, 0) < n)
                    {
                        perror("splitd: incoming connection has failedn");
                        closeall(sock);
                        exit(3);
                    }
                    first = false;
                }
            }
        }
    }
}


void forward_child(int sock)
{
    setup_splits(sock);
    listen_loop(sock);
}


bool decode_addr(char const *name, sockaddr *sa)
{
    memset(sa, 0, sizeof(*sa));
    ((sockaddr_in *)sa)->sin_family = AF_INET;
    char const *colon = strrchr(name, ':');
    if (!colon)
    {
        fprintf(stderr, "destination must be name:port\n");
        return false;
    }
    ((sockaddr_in *)sa)->sin_port = htons(atoi(colon+1));
    if (((sockaddr_in *)sa)->sin_port == 0)
    {
        fprintf(stderr, "bad host port: %s\n", colon+1);
        return false;
    }
    std::string s(name, colon);
    hostent *hent = gethostbyname2(s.c_str(), AF_INET);
    if (!hent)
    {
        fprintf(stderr, "bad host name: %s\n", s.c_str());
        return false;
    }
    memcpy(&((sockaddr_in *)sa)->sin_addr, hent->h_addr_list[0],
        sizeof(((sockaddr_in *)sa)->sin_addr));
    return true;
}

void setup_listener(unsigned short port)
{
    sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("splitd: cannot create listening socket");
        exit(1);
    }
    int one = 1;
    if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
    {
        perror("splitd: setsockopt(SO_REUSEADDR) failed");
        exit(1);
    }
    one = 1;
    if (::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0)
    {
        perror("splitd: setsockopt(TCP_NODELAY) failed");
        exit(1);
    }
    int err = ::bind(sock, (sockaddr const *)&sin, sizeof(sin));
    if (err < 0)
    {
        perror("splitd: bind() failed");
        exit(1);
    }
    err = ::listen(sock, 32);
    if (err < 0)
    {
        perror("splitd: listen() failed");
        exit(1);
    }
    listenSock = sock;
}

int main(int argc, char const *argv[])
{
    if (argc < 3)
    {
        usage();
    }

    //  decode listen port argument
    dport = atoi(argv[1]);
    if (dport < 1 || dport > 65535)
    {
        usage();
    }

    //  decode forward port arguments
    for (int i = 2; i != argc; ++i)
    {
        sockaddr sin;
        if (!decode_addr(argv[i], &sin))
        {
            usage();
        }
        targets.push_back(sin);
    }

    //  setup listening socket
    setup_listener(dport);

    //  accept loop
    int nfail = 0;
    while (true)
    {
        struct sockaddr a;
        socklen_t len = sizeof(a);
        int sock = accept(listenSock, (sockaddr *)&a, &len);
        if (sock < 0)
        {
            perror("splitd: accept() failed");
            ++nfail;
            if (nfail == 10)
            {
                exit(2);
            }
        }
        else
        {
            nfail = 0;
            pid_t child = fork();
            if (child == -1)
            {
                perror("splitd: fork() failed");
                close(sock);
                sleep(1);
            }
            else if (!child)
            {
                close(listenSock);
                forward_child(sock);
            }
            else
            {
                //  I don't care about the child -- just accept again
                close(sock);
            }
        }
    }
}

