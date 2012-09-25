
/* this tool does not purport to emulate the full netcat; rather 
  it is a version that works for IMVU test scripts without having 
  a dependency on a specific installed version of netcat (the -q 
  option in particular).
  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>


void usage() {
  fprintf(stderr, "usage: istatd_netcat [-q timeout] host port\n");
  exit(1);
}

int connect_to(char const *host, unsigned short port) {
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);
  struct hostent *hent = gethostbyname(host);
  if (!hent || !hent->h_addr_list[0]) {
    fprintf(stderr, "host not found: %s\n", host);
    exit(1);
  }
  if (hent->h_length != sizeof(sin.sin_addr)) {
    fprintf(stderr, "wrong host address type: %s\n", host);
    exit(1);
  }
  memcpy(&sin.sin_addr, hent->h_addr_list[0], sizeof(sin.sin_addr));

  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    perror("socket()");
    exit(1);
  }
  int c = connect(s, (struct sockaddr *)&sin, sizeof(sin));
  if (c < 0) {
    fprintf(stderr, "connect(%s:%d) failed: %s\n", host, port, strerror(errno));
    exit(1);
  }
  return s;
}

int recvsome(int s, void *buf, int len) {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(s, &fds);
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 250000;
  int sres = select(s+1, &fds, 0, 0, &tv);
  if (sres < 0) {
    return sres;
  }
  if (!FD_ISSET(s, &fds)) {
    return 0;
  }
  int rres = recv(s, buf, len, 0);
  return rres == 0 ? -2 : rres;
}


int main(int argc, char const *argv[]) {

  int qflag = 0;
  char const *host = 0;
  unsigned short port = 0;

  /* parse arguments */

  while (argv[1]) {
    if (!strcmp(argv[1], "-q")) {
      if (!argv[2]) {
        usage();
      }
      qflag = atoi(argv[2]);
      if (qflag <= 0) {
        usage();
      }
      ++argv;
      --argc;
    }
    else if (argv[1][0] == '-') {
      usage();
    }
    else {
      if (!host) {
        host = argv[1];
      }
      else if (!port) {
        int i = atoi(argv[1]);
        if (i < 1 || i > 65535) {
          usage();
        }
        port = (unsigned short)i;
      }
    }
    ++argv;
    --argc;
  }
  if (!host) {
    usage();
  }
  if (!port) {
    usage();
  }

  /* connect to host */
  char buf[8192];
  int r, s = connect_to(host, port);

  /*  do an echo thing */
  while (true) {
    r = read(0, buf, sizeof(buf));
    if (r < 0) {
      perror("read(stdin)");
      exit(1);
    }
    if (r == 0) {
      break;
    }
    int off = 0;
    while (r > off) {
      int q = send(s, &buf[off], r-off, 0);
      if (q < 0) {
        perror("send()");
        exit(1);
      }
      if (q == 0) {
        break;
      }
      off += q;
    }
    r = recvsome(s, buf, sizeof(buf));
    if (r == -2) {  //  closed
      break;
    }
    if (r < 0) {
      perror("recv()");
      exit(1);
    }
    if (r > 0) {
      write(1, buf, r);
    }
  }
  shutdown(s, SHUT_WR);

  time_t now, then;
  time(&now);
  then = now + qflag;
  r = 1;
  while (now <= then || r > 0) {
    r = recvsome(s, buf, sizeof(buf));
    if (r == -2) {  //  closed
      break;
    }
    if (r < 0) {
      perror("recv()");
      exit(1);
    }
    if (r > 0) {
      write(1, buf, r);
    }
    time(&now);
  }
  close(s);
  return 0;
}

