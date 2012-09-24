
#if !defined(istatd_Env_h)
#define istatd_Env_h

#include <typeinfo>
#include <map>
#include <string>

namespace istat
{
    class Env
    {
    public:
        template<typename T>
        static inline T &get() {
            return *reinterpret_cast<T *>(inst_.get_interface(typeid(T)));
        }
        template<typename T>
        static inline bool has() {
            return inst_.has_interface(typeid(T));
        }
        template<typename T, typename Q>
        static inline void set(Q &q) {
            inst_.set_interface(typeid(T), static_cast<T *>(&q));
        }
        static inline void clear() {
            inst_.clear_instance();
        }
    protected:
        //  protected for unit testing
        void *get_interface(std::type_info const &t);
        bool has_interface(std::type_info const &t);
        void set_interface(std::type_info const &t, void *p);
        void clear_instance();
        static Env inst_;
        std::map<std::string, void *> data_;
    };
}


#endif  //  Env_h

