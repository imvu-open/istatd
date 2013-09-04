#include "Bucketizer.h"
#include "Logs.h"

// The bucket[0] will be the "oldest" and bucket[BUCKET_COUNT-1] is the "now" bucket
Bucketizer::Bucketizer(time_t &time) {
    now = time - (time % 10);
    for (unsigned int i = 0 ; i < BUCKET_COUNT; i++) {
        buckets[i] = istat::Bucket(true);
    }
}

Bucketizer::Bucketizer(time_t &time, istat::Bucket const &b) {
    now = time - (time % 10);
    for (unsigned int i = 0 ; i < BUCKET_COUNT; i++) {
        buckets[i] = istat::Bucket(true);
    }
    this->update(b);
}

void Bucketizer::update(istat::Bucket const &o) {
    time_t time = o.time();
    time = time - (time % 10); // make a 10 second offset bucket

    // this will be 0 for furthest back in time
    int offset = ((time - now) / 10) + (BUCKET_COUNT-2);
    // assume now is between 50 and 59, time is cast to "50", BUCKET_COUNT is 5
    // time
    // 20 --> 0
    // 30 --> 1
    // 40 --> 2
    // 50 --> 3
    // 60 --> 4
    LogDebug << "Bucketizer::updating offset time:" << time << " with value " << o.sum() << "at offset" << offset << "delta" << (time - now);
    if ((offset < 0) || (offset > ((int) BUCKET_COUNT-1))) {
        LogNotice << "Bucketizer::update bad time:" << time << "not close enough to " << now << "at offset" << offset << "delta" << (time - now);
        // error
    } else {
        buckets[(unsigned int) offset].update(o);
    }
}


istat::Bucket const & Bucketizer::get(int i) {
    return buckets[i];
}

