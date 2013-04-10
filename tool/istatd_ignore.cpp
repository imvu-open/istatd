
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

int main(int argc, char const *argv[])
{
    if (argc < 4 )
    {
        fprintf(stderr, "usage: istatd_ignore host port name\n");
        exit(1);
    }

    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0)
    {
        perror("socket()");
        exit(2);
    }
   
	fprintf(stderr, "looking up %s\n", argv[1]);
    
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

	fprintf(stderr, "connecting to %s:%d\n", argv[1], port);
    
    int c = connect(s, (struct sockaddr *)&inaddr, sizeof(inaddr));
    if (c < 0)
    {
        perror("connect()");
        exit(2);
    }
    int q = 0;

    std::stringstream ss;
    ss << "ignore " << argv[3] << "\n";

    std::string data(ss.str());
	
	fprintf(stderr, "sending ignore %s\n", data.c_str());

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
	fprintf(stderr, "waiting for acknowledgement\n");
    
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
	
	fprintf(stderr, "acknowledgement received\n");
    
    if (strncmp(buf, "ok", 2))
    {
        fprintf(stderr, "error processing ignore  istatd\n");
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
    
	fprintf(stdout, "ignore took %ld seconds\n", (long)(endTime - startTime));
   
    return 0;
}

