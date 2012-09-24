
#include <istat/strfunc.h>
#include <istat/Header.h>
#include <istat/Bucket.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <fstream>
#include <string.h>

void usage()
{
    std::cerr << "usage: istatd_fileinfo path/to/counterfile [...]" << std::endl;
    exit(1);
}

int file_info(std::string const &path)
{
    std::ifstream is(path.c_str(), std::ios::binary);
    istat::Header h;
    is.read((char *)&h, sizeof(h));
    if (memcmp(h.magic, istat::file_magic, sizeof(h.magic)))
    {
        std::cerr << path << ": bad magic value (not a counter file)" << std::endl;
        return 1;
    }
    if (h.rd_version > istat::CUR_HDR_VERSION)
    {
        std::cerr << path << ": version " << h.rd_version << " cannot be read by version "
            << istat::CUR_HDR_VERSION << " tools." << std::endl;
        return 1;
    }

    std::cout << path << ":" << std::endl;
    std::cout << "                  cr_version= " << h.cr_version << std::endl;
    std::cout << "                  rd_version= " << h.rd_version << std::endl;
    std::cout << "                cfg_interval= " << h.cfg_interval << std::endl;
    std::cout << "                        unit= " << h.unit << std::endl;
    std::cout << "                   page_size= " << h.page_size << std::endl;
    std::cout << "                  page_count= " << h.page_count << std::endl;
    std::cout << "                first_bucket= " << h.first_bucket << std::endl;
    std::cout << "                 last_bucket= " << h.last_bucket << std::endl;
    std::cout << "                   last_time= " << istat::iso_8601_datetime(h.last_time) << " (" << h.last_time << ")"  << std::endl;
    std::cout << "              cumulative_sum= " << h.cumulative_sum << std::endl;
    std::cout << "           cumulative_sum_sq= " << h.cumulative_sum_sq << std::endl;
    std::cout << "            cumulative_count= " << h.cumulative_count << std::endl;
    std::cout << "  last_cumulative_clear_time= " << istat::iso_8601_datetime(h.last_cumulative_clear_time) << " (" << h.last_cumulative_clear_time << ")" << std::endl;
    std::cout << "            file_create_time= " << istat::iso_8601_datetime(h.file_create_time) << " (" << h.file_create_time << ")" << std::endl;
    std::cout << "                      lambda= " << h.lambda << std::endl;
    std::cout << "                      season= " << h.season << std::endl;
    std::cout << "                 fixed_count= " << h.fixed_count << std::endl;
    std::cout << "                       flags= 0x" << std::hex << h.flags << std::endl;
    std::cout << "                  bucketsize= " << std::dec << sizeof(istat::Bucket) << std::endl;

    return 0;
}

int main(int argc, char const *argv[])
{
    int err = 0;

    if (argc < 2)
    {
        usage();
    }
    for (int i = 1; i < argc; ++i)
    {
        try
        {
            err += file_info(argv[i]);
        }
        catch (std::exception const &x)
        {
            std::cerr << argv[i] << ": " << x.what() << std::endl;
            ++err;
        } catch (...)
        {
            std::cerr << argv[i] << ": unknown exception" << std::endl;
            ++err;
        }
    }
    return err;
}

