
#include <istat/Env.h>
#include <stdexcept>

namespace istat {

    Env Env::inst_;

    void *Env::get_interface(std::type_info const &t) {
        std::map<std::string, void *>::iterator ptr(data_.find(t.name()));
        if (ptr == data_.end()) {
            throw std::runtime_error(std::string("Interface not present in Env: ") + t.name());
        }
        return (*ptr).second;
    }

    bool Env::has_interface(std::type_info const &t) {
        std::map<std::string, void *>::iterator ptr(data_.find(t.name()));
        if (ptr == data_.end()) {
            return false;
        }
        return true;
    }

    void Env::set_interface(std::type_info const &t, void *ptr) {
        data_[t.name()] = ptr;
    }

    void Env::clear_instance() {
        data_.clear();
    }
}

