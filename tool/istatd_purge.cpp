
#include <istat/istattime.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <time.h>

#include <string>
#include <sstream>
#include <boost/lexical_cast.hpp>



int verbose = 0;


int main(int argc, char const *argv[])
{
    if (argv[1] && !strcmp(argv[1], "-v"))
    {
        ++argv;
        --argc;
        verbose = 1;
    }
    if (argv[1] && !strcmp(argv[1], "-q"))
    {
        ++argv;
        --argc;
        verbose = -1;
    }
    size_t maxOld = (argc >= 4 ? boost::lexical_cast<size_t>(argv[3]) : 0);
    time_t maxAge = (argc >= 5 ? boost::lexical_cast<time_t>(argv[4]) : 0);
    if (argc < 3 || argc > 5)
    {
        fprintf(stderr, "usage: istatd_purge host port [maxOld [maxAge]]\n");
        exit(1);
    }
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0)
    {
        perror("socket()");
        exit(2);
    }
    if (verbose > 0)
    {
        fprintf(stderr, "looking up %s\n", argv[1]);
    }
    struct hostent *hent = gethostbyname(argv[1]);
    if (!hent)
    {
        fprintf(stderr, "gethostbyname(%s) failed\n", argv[1]);
        exit(2);
    }
    int port = atoi(argv[2]);
    if (port < 1 || port > 65535)
    {
        fprintf(stderr ,"%s is a bad port\n", argv[2]);
        exit(2);
    }
    struct sockaddr_in inaddr;
    memset(&inaddr, 0, sizeof(inaddr));
    inaddr.sin_family = AF_INET;
    memcpy(&inaddr.sin_addr, hent->h_addr_list[0], 4);
    inaddr.sin_port = htons(port);
    if (verbose > 0)
    {
        fprintf(stderr, "connecting to %s:%d\n", argv[1], port);
    }
    int c = connect(s, (struct sockaddr *)&inaddr, sizeof(inaddr));
    if (c < 0)
    {
        perror("connect()");
        exit(2);
    }
    int q = 0;
    if (verbose > 0)
    {
        fprintf(stderr, "sending purge\n");
    }
    std::stringstream ss;
    ss << "purge " << maxOld << "," << maxAge << "\n";
    std::string data(ss.str());
    while ((size_t)q < data.size())
    {
        int r = ::send(s, &data[q], data.size()-q, 0);
        if (r < 0)
        {
            perror("send()");
            exit(2);
        }
        q += r;
    }
    if (verbose > 0)
    {
        fprintf(stderr, "waiting for acknowledgement\n");
    }
    time_t startTime;
    istat::istattime(&startTime);
    char buf[10];
    q = 0;
    while (q < 3)
    {
        int r = ::recv(s, &buf[q], 1, 0);
        if (r < 0)
        {
            perror("recv()");
            exit(2);
        }
        q = q + r;
    }
    if (verbose > 0)
    {
        fprintf(stderr, "acknowledgement received\n");
    }
    if (strncmp(buf, "ok", 2))
    {
        fprintf(stderr, "error purging connection info in istatd\n");
        fprintf(stderr, "%.3s", buf);
        while (true)
        {
            int r = ::recv(s, &buf[0], 1, 0);
            if (r == 1)
            {
                fprintf(stderr, "%.1s", buf);
                if (buf[0] == '\n')
                {
                    break;
                }
            }
            else
            {
                break;
            }
        }
        exit(3);
    }
    time_t endTime;
    istat::istattime(&endTime);
    if (verbose >= 0)
    {
        fprintf(stdout, "purge took %ld seconds\n", (long)(endTime - startTime));
    }
    return 0;
}

