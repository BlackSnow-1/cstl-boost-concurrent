// SPDX-License-Identifier: MIT
#include <cstl/concurrent_unordered_map.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include "baseline_map.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <type_traits>

// A direct Boost control. Same public semantics and default Hash as the facade.
template<class K> struct native_map {
    boost::concurrent_flat_map<K, std::uint64_t, std::hash<K>> m;
    void reserve(std::size_t n) { m.reserve(n); }
    BOOST_FORCEINLINE bool try_emplace(const K& k, std::uint64_t v) { return m.try_emplace(k,v); }
    template<class F> BOOST_FORCEINLINE bool visit(const K& k,F f) { return m.visit(k,[f=std::move(f)](auto& e)mutable{ f(e.second); }); }
    template<class F> BOOST_FORCEINLINE bool cvisit(const K& k,F f) const { return m.cvisit(k,[f=std::move(f)](const auto& e)mutable{ f(e.second); }); }
    template<class F> BOOST_FORCEINLINE bool try_emplace_or_visit(const K& k,F f,std::uint64_t v) {
        return m.try_emplace_or_visit(k,v,[f=std::move(f)](auto& e)mutable{f(e.second);});
    }
    BOOST_FORCEINLINE bool insert_or_assign(const K& k,std::uint64_t v) {return m.insert_or_assign(k,v);}
    BOOST_FORCEINLINE std::size_t erase(const K& k) {return m.erase(k);}
    auto snapshot() const { std::vector<std::pair<K,std::uint64_t>> r; m.cvisit_all([&](const auto& e){r.emplace_back(e.first,e.second);}); return r; }
    auto size() const {return m.size();}
};

struct gate {
    std::mutex mutex; std::condition_variable cv; int ready=0; bool start=false;
    void wait() { std::unique_lock<std::mutex> l(mutex); ++ready; cv.notify_all(); cv.wait(l,[&]{return start;}); }
};
enum class Work { read, miss, update, upsert, mixed, churn, insert, growth };
template<class Map,class Key,Work W> void run(int threads,int ops,int count) {
    Map map; constexpr bool insertion=W==Work::insert || W==Work::growth;
    if constexpr(W!=Work::growth) map.reserve(insertion ? std::size_t(threads)*ops : count);
    auto make_key=[](std::uint64_t n)->Key {
        if constexpr(std::is_same_v<Key,std::string>) return "customer-record-"+std::to_string(n);
        else return n;
    };
    std::vector<Key> keys; keys.reserve(count*2);
    for(int k=0;k<count*2;++k) keys.push_back(make_key(k));
    if constexpr(!insertion) for(int k=0;k<count;++k) map.try_emplace(keys[k],1);
    // Precompute insertion keys so key generation is outside the timed region.
    std::vector<Key> new_keys;
    if constexpr(insertion) for(std::uint64_t k=0;k<std::uint64_t(threads)*ops;++k) new_keys.push_back(make_key(k));
    gate g; std::vector<std::thread> workers;
    std::vector<std::uint64_t> sums(threads),success(threads);
    for(int t=0;t<threads;++t) workers.emplace_back([&,t]{
        std::uint64_t sum=0,ok=0;
        g.wait();
        for(int i=0;i<ops;++i) {
            const std::uint64_t n=std::uint64_t(i)*2654435761u+std::uint64_t(t)*1013904223u;
            const auto& key=keys[n%count];
            if constexpr(W==Work::read) { if(map.cvisit(key,[&](const auto& v){sum+=v;})) ++ok; }
            else if constexpr(W==Work::miss) { if(!map.cvisit(keys[count+n%count],[](const auto&){})) ++ok; }
            else if constexpr(W==Work::update) { if(map.visit(key,[](auto& v){++v;})) ++ok; }
            else if constexpr(W==Work::upsert) { map.try_emplace_or_visit(key,[](auto& v){++v;},1); ++ok; }
            else if constexpr(W==Work::mixed) { if(i%10<8) {if(map.cvisit(key,[&](const auto& v){sum+=v;}))++ok;} else {if(map.visit(key,[](auto& v){++v;}))++ok;} }
            else if constexpr(W==Work::churn) { map.erase(key); map.try_emplace(key,1); ++ok; }
            else if constexpr(W==Work::insert || W==Work::growth) { if(map.try_emplace(new_keys[std::size_t(t)*ops+i],1)) ++ok; }
            else throw std::runtime_error("unknown workload");
        }
        sums[t]=sum; success[t]=ok;
    });
    {std::unique_lock<std::mutex> l(g.mutex);g.cv.wait(l,[&]{return g.ready==threads;});}
    const auto begin=std::chrono::steady_clock::now();
    {std::lock_guard<std::mutex> l(g.mutex); g.start=true;} g.cv.notify_all();
    for(auto& t:workers)t.join();
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    std::uint64_t total=0,checksum=0,value_sum=0;
    for(auto v:success)total+=v;
    for(auto v:sums)checksum+=v;
    for(const auto& e:map.snapshot())value_sum+=e.second;
    const auto expected=std::uint64_t(threads)*ops;
    if(total!=expected) throw std::runtime_error("operation count mismatch");
    if constexpr(insertion) if(map.size()!=expected || value_sum!=expected)throw std::runtime_error("insert mismatch");
    if constexpr(W==Work::update || W==Work::upsert) if(value_sum!=std::uint64_t(count)+expected)throw std::runtime_error("lost updates");
    if constexpr(W==Work::mixed) if(value_sum!=std::uint64_t(count)+std::uint64_t(threads)*(ops/10*2+std::max(0,ops%10-8)))throw std::runtime_error("mixed lost updates");
    if constexpr(W==Work::read) if(checksum!=expected)throw std::runtime_error("read mismatch");
    std::cout<<seconds<<","<<double(expected)/seconds/1e6<<","<<checksum<<","<<value_sum<<"\n";
}
template<class Map,class Key>void select_work(const std::string& work,int threads,int ops,int keys){
#define RUN_WORK(name) if(work==#name){run<Map,Key,Work::name>(threads,ops,keys);return;}
    RUN_WORK(read) RUN_WORK(miss) RUN_WORK(update) RUN_WORK(upsert)
    RUN_WORK(mixed) RUN_WORK(churn) RUN_WORK(insert) RUN_WORK(growth)
#undef RUN_WORK
    throw std::runtime_error("unknown workload");
}
template<class Key>void dispatch(const std::string& backend,const std::string& work,int threads,int ops,int keys){
    if(backend=="cstl")select_work<cstl::concurrent_unordered_map<Key,std::uint64_t>,Key>(work,threads,ops,keys);
    else if(backend=="boost")select_work<native_map<Key>,Key>(work,threads,ops,keys);
    else if(backend=="baseline")select_work<cstl_baseline::concurrent_unordered_map<Key,std::uint64_t>,Key>(work,threads,ops,keys);
    else throw std::runtime_error("unknown backend");
}
int main(int argc,char** argv){
    try {
        if(argc!=7)return 2;
        int threads=std::stoi(argv[3]),ops=std::stoi(argv[4]),keys=std::stoi(argv[5]);
        if(threads<1 || threads>256 || ops<1 || ops>10000000 || keys<1 || keys>10000000)return 2;
        if(std::string(argv[6])=="string")dispatch<std::string>(argv[1],argv[2],threads,ops,keys);
        else if(std::string(argv[6])=="integer")dispatch<std::uint64_t>(argv[1],argv[2],threads,ops,keys);
        else return 2;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
