
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <time.h>



int verbose = 0;


int main(int argc, char const *argv[])
{
    if (argv[1] && !strcmp(argv[1], "-v"))
    {
        ++argv;
        --argc;
        verbose = 1;
    }
    if (argc != 3)
    {
        fprintf(stderr, "usage: istatd_stat host port\n");
        exit(1);
    }
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0)
    {
        perror("socket()");
        exit(2);
    }
    if (verbose)
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
    if (verbose)
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
    if (verbose)
    {
        fprintf(stderr, "sending stat\n");
    }
    while (q < 6)
    {
        int r = ::send(s, "stats\n" + q, 6 - q, 0);
        if (r < 0)
        {
            perror("send()");
            exit(2);
        }
        q += r;
    }
    if (verbose)
    {
        fprintf(stderr, "waiting for data\n");
    }
    char buf[1024];
    bool done = false;
    q = 0;
    while (q < 1024 && !done)
    {
        int r = ::recv(s, &buf[q], 1, 0);
        if (r < 0)
        {
            perror("recv()");
            exit(2);
        }
        q = q + r;
        int off = 0;
        if (verbose)
        {
            for (int i = 0; i < q; i += 16)
            {
                for (int j = 0; j < 16; ++j)
                {
                    if (i + j < q)
                    {
                        fprintf(stderr, "%02x ", (unsigned char)buf[i + j]);
                    }
                    else
                    {
                        fprintf(stderr, "   ");
                    }
                }
                for (int j = 0; j < 16 && i + j < q; ++j)
                {
                    char ch = buf[i + j];
                    if (ch < 32 || ch > 126)
                    {
                        ch = '.';
                    }
                    fprintf(stderr, "%c", ch);
                }
                fprintf(stderr, "\n");
            }
        }
        for (int i = 0; i < q; ++i)
        {
            if (buf[i] == '\n')
            {
                if (i == 0 || buf[i-1] != '\r')
                {
                    fprintf(stderr, "WARNING: bad line ending from istatd\n");
                }
                if (((i == 3) && !strncmp(buf, "ok\r\n", 4)) ||
                    ((i > 4) && !strncmp(&buf[i-5], "\r\nok\r\n", 6)))
                {
                    done = true;
                    break;
                }
                if (off < i - 1)
                {
                    fprintf(stdout, "%.*s\n", i - off - 1, &buf[off]);
                }
                off = i + 1;
            }
        }
        if (off > 0)
        {
            if (off < q)
            {
                memmove(buf, &buf[off], q - off);
            }
            q -= off;
        }
    }
    if (verbose)
    {
        fprintf(stderr, "data received\n");
    }
    return 0;
}

