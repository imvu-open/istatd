
#include "istat/strfunc.h"
#include <istat/Header.h>
#include <iostream>
#include <string>
#include <list>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>

time_t age = 100000;
time_t now;
int64_t minPageSize;

void usage()
{
    std::string ival(istat::seconds_to_interval(age));
    std::cerr << "usage: istatd_lint [--age " << ival << "] /path/to/store" << std::endl;
    std::cerr << "istatd_lint will print warnings about files that are not istatd files," << std::endl;
    std::cerr << "and files that have not had data written to them for a certain time." << std::endl;
    std::cerr << "istatd_lint does not follow symlinks in the scanned directory." << std::endl;
    std::cerr << "Errors are printed to stderr; warnings about files to stdout." << std::endl;
    std::cerr << "Warning line format is 'W: <info>: <pathname>'" << std::endl;
    exit(1);
}

void scanfile(std::string const &path)
{
    int i = ::open(path.c_str(), O_RDONLY);
    if (i < 0)
    {
        std::cerr << "E: could not open file: " << path << std::endl;
    }
    else
    {
        istat::Header hdr;
        long l = ::read(i, &hdr, sizeof(hdr));
        if (l != sizeof(hdr))
        {
            std::cout << "W: short file: " << path << std::endl;
        }
        else if (memcmp(hdr.magic, istat::file_magic, sizeof(hdr.magic)))
        {
            std::cout << "W: not an istatd file: " << path << std::endl;
        }
        else if (hdr.page_size & (hdr.page_size - 1))
        {
            std::cout << "W: bad page size in header: " << path << std::endl;
        }
        else if (hdr.page_size < minPageSize)
        {
            std::cout << "W: too small page size in header: " << path << std::endl;
        }
        else
        {
            int64_t size = ::lseek(i, 0, 2);
            if (size < hdr.page_count * hdr.page_size)
            {
                std::cout << "W: file is too small: " << path << std::endl;
            }
            else if (hdr.last_time < now - age)
            {
                std::cout << "W: file exceeds age: " << path << std::endl;
            }
        }
        ::close(i);
    }
}

void scandir(std::string const &rootPath)
{
    std::list<std::string> subdirs;
    struct stat stbuf;
    subdirs.push_back(rootPath);
    while (!subdirs.empty())
    {
        std::string path(subdirs.front());
        subdirs.pop_front();
        std::list<std::string> files;

        DIR *dir = opendir(path.c_str());
        if (!dir)
        {
            std::cerr << "E: could not open directory: " << path << std::endl;
            return;
        }

        while (struct dirent *dent = readdir(dir))
        {
            if (dent->d_name[0] == '.')
            {
                continue;
            }
            std::string item(path + "/" + dent->d_name);
            if (-1 < lstat(item.c_str(), &stbuf))
            {
                if (S_ISREG(stbuf.st_mode))
                {
                    files.push_back(item);
                }
                else if (S_ISDIR(stbuf.st_mode))
                {
                    subdirs.push_back(item);
                }
                else
                {
                    //  ignore this item
                }
            }
        }

        closedir(dir);

        for (std::list<std::string>::iterator ptr(files.begin()), end(files.end());
            ptr != end;
            ++ptr)
        {
            scanfile(*ptr);
        }
    }
}

int main(int argc, char const *argv[])
{
    if (argv[1] && !strcmp(argv[1], "--age"))
    {
        if (!argv[2] || ((age = istat::interval_to_seconds(argv[2])) < 10))
        {
            usage();
        }
        argv += 2;
        argc -= 2;
    }
    if (argc != 2 || argv[1][0] == '-')
    {
        usage();
    }
    time(&now);
    minPageSize = sysconf(_SC_PAGESIZE);
    scandir(argv[1]);
    return 0;
}

