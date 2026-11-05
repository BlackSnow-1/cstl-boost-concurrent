// SPDX-License-Identifier: MIT
#include <cstl/concurrent_unordered_map.hpp>
#include "baseline_map.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
struct memory_stats {std::uint64_t peak_rss=0,private_bytes=0;};
memory_stats memory_usage(){
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof(p);
    if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p)))throw std::runtime_error("memory query failed");
    return {p.PeakWorkingSetSize,p.PrivateUsage};
#else
    rusage r{};if(getrusage(RUSAGE_SELF,&r))throw std::runtime_error("memory query failed");
#if defined(__APPLE__)
    return {static_cast<std::uint64_t>(r.ru_maxrss),0};
#else
    return {static_cast<std::uint64_t>(r.ru_maxrss)*1024,0};
#endif
#endif
}
template<class K>K make_key(std::size_t n){if constexpr(std::is_same_v<K,std::string>)return "customer-record-"+std::to_string(n);else return n;}
template<class M,class K>void measure(std::size_t n,bool reserve){
    auto before=memory_usage();M map;if(reserve)map.reserve(n);
    for(std::size_t i=0;i<n;++i)if(!map.try_emplace(make_key<K>(i),1))throw std::runtime_error("duplicate insert");
    std::uint64_t sum=0;
    for(std::size_t i=0;i<n;++i)if(!map.cvisit(make_key<K>(i),[&](auto v){sum+=v;}))throw std::runtime_error("missing key");
    if(sum!=n||map.size()!=n)throw std::runtime_error("incorrect result");
    auto after=memory_usage();
    std::cout<<"{\"peak_rss\":"<<after.peak_rss<<",\"private_bytes\":"<<after.private_bytes<<",\"before_private_bytes\":"<<before.private_bytes<<",\"container_sizeof\":"<<sizeof(M)<<",\"size\":"<<map.size()<<",\"checksum\":"<<sum<<"}\n";
}
template<class K>void dispatch(const std::string& backend,std::size_t n,bool reserve){
    if(backend=="cstl")measure<cstl::concurrent_unordered_map<K,std::uint64_t>,K>(n,reserve);
    else if(backend=="baseline")measure<cstl_baseline::concurrent_unordered_map<K,std::uint64_t>,K>(n,reserve);
    else throw std::runtime_error("unknown backend");
}
int main(int argc,char** argv){
    try{if(argc!=5)return 2;auto n=std::stoull(argv[4]);if(!n||n>10000000)return 2;
        std::string mode=argv[3];if(mode!="insert"&&mode!="growth")return 2;
        if(std::string(argv[2])=="integer")dispatch<std::uint64_t>(argv[1],n,mode=="insert");
        else if(std::string(argv[2])=="string")dispatch<std::string>(argv[1],n,mode=="insert");else return 2;
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
