
#if !defined(daemon_ShardedMap_h)
#define daemon_ShardedMap_h

#include "threadfunc.h"
#include <boost/lexical_cast.hpp>
#include <boost/noncopyable.hpp>
#include <string>
#include <string.h>

//  Implement a map of maps, used to ease lock contention.
template<typename Key> inline unsigned char shard_index(Key const &k);

template<typename T> class LockThing;

template<typename Map>
class ShardedMap : public boost::noncopyable
{
public:
    typedef Map map_type;
    typedef typename Map::key_type Key;
    typedef ShardedMap<Map> self_type;

    struct iterator
    {
        iterator(ShardedMap &sm, unsigned char index, Key const &key) :
            index_(index), isEnd_(false), key_(key), sm_(&sm)
        {
        }
        iterator() :
            index_(0), isEnd_(true), key_(Key()), sm_((ShardedMap *)0)
        {
        }
        bool operator!=(iterator const &i) const
        {
            return !(*this == i);
        }
        bool operator==(iterator const &i) const
        {
            if (isEnd_)
            {
                return i.isEnd_;
            }
            return sm_ == i.sm_ && index_ == i.index_ && key_ == i.key_;
        }
        //  The problem is that the value that I'm referencing may be 
        //  legitimately removed, or some other value inserted, while 
        //  I'm iterating. Thus, I assume the default value is the 
        //  lowest possible, and will never actually be a valid value.
        //  That way, I can use a copying "move between spaces over the 
        //  value" semantic, instead of a "point at a value" semantic.
        //  The position in the map will be re-derived based on the 
        //  (copied) key value each time through the iterator.
        bool move_next(typename Map::key_type &oKey, typename Map::mapped_type &oValue)
        {
            if (isEnd_ || !sm_)
            {
                return false;
            }
        try_bin:
            {
                grab aholdof(sm_->locks_[index_]);
                typename Map::iterator ptr(sm_->maps_[index_].lower_bound(key_));
                if (ptr == sm_->maps_[index_].end())
                {
                    goto next_bin;
                }
                if ((*ptr).first == key_)
                {
                    ptr = sm_->maps_[index_].upper_bound(key_);
                    if (ptr == sm_->maps_[index_].end())
                    {
                        goto next_bin;
                    }
                }
                oKey = (*ptr).first;
                oValue = (*ptr).second;
                key_ = (*ptr).first;
                return true;
            }
    next_bin:
            if (index_ == 255)
            {
                *this = ShardedMap::end();
                return false;
            }
            ++index_;
            key_ = Key();
            goto try_bin;
        }
    private:
        unsigned char index_;
        bool isEnd_;
        Key key_;
        ShardedMap *sm_;
    };

    ShardedMap()
    {
        memset(sizes_, 0, sizeof(sizes_));
    }

    ::lock &lock(Key const &k)
    {
        return locks_[shard_index(k)];
    }

    map_type &map(Key const &k)
    {
        unsigned char ix = shard_index(k);
        map_type &ret(maps_[ix]);
        sizes_[ix] = ret.size();
        return ret;
    }

    iterator begin()
    {
        iterator ret(*this, 0, Key());
        return ret;
    }

    static iterator end()
    {
        iterator ret;
        return ret;
    }

    //  There is no way to get an accurate instantaneous count 
    //  because of race conditions. Unless you lock ALL the shards 
    //  at the same time, which is very expensive;
    size_t approximate_size()
    {
        size_t ret = 0;
        for (int i = 0; i != 256; ++i)
        {
            ret = ret + sizes_[i];
        }
        return ret;
    }

    size_t exact_size()
    {
        size_t ret = 0;
        for (int i = 0; i != 256; ++i)
        {
            grab aholdof(locks_[i]);
            sizes_[i] = maps_[i].size();
            ret = ret + sizes_[i];
        }
        return ret;
    }

    std::pair<map_type&, ::lock&> get_from_index(int i)
    {
        if (i < 0 || i > 255)
        {
            throw std::runtime_error("Map index out of range");
        }
        return std::pair<map_type&, ::lock&>(maps_[i], locks_[i]);
    }

    int dimension()
    {
        return 256;
    }

private:
    friend struct iterator;
    Map maps_[256];
    ::lock locks_[256];
    size_t sizes_[256];
};

template<> inline unsigned char shard_index<std::string>(std::string const &k)
{
    unsigned int val = 0;
    for (size_t i = 0, n = k.size(); i != n; ++i)
    {
        val = val * 19U + (unsigned char)k[i] + 127U;
    }
    return (unsigned char)(val & 0xff);
}

#endif  //  daemon_ShardedMap_h


