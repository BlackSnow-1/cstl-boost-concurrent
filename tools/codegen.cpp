// SPDX-License-Identifier: MIT
#include <cstl/concurrent_unordered_map.hpp>
#include <cstdint>
using Facade=cstl::concurrent_unordered_map<std::uint64_t,std::uint64_t>;
using Native=boost::concurrent_flat_map<std::uint64_t,std::uint64_t,std::hash<std::uint64_t>>;
static_assert(sizeof(Facade)==sizeof(Native),"Facade must add no per-map storage");
extern "C" __declspec(noinline) bool cstl_read(Facade* m,const std::uint64_t& k,std::uint64_t* out){return m->cvisit(k,[&](const auto& v){*out=v;});}
extern "C" __declspec(noinline) bool boost_read(Native* m,const std::uint64_t& k,std::uint64_t* out){return m->cvisit(k,[&](const auto& e){*out=e.second;})!=0;}
extern "C" __declspec(noinline) bool cstl_update(Facade* m,const std::uint64_t& k){return m->visit(k,[](auto& v){++v;});}
extern "C" __declspec(noinline) bool boost_update(Native* m,const std::uint64_t& k){return m->visit(k,[](auto& e){++e.second;})!=0;}
extern "C" __declspec(noinline) bool cstl_insert(Facade* m,const std::uint64_t& k,std::uint64_t v){return m->try_emplace(k,v);}
extern "C" __declspec(noinline) bool boost_insert(Native* m,const std::uint64_t& k,std::uint64_t v){return m->try_emplace(k,v);}
#ifdef CSTL_CODEGEN_STRING
#include <string>
using StringFacade=cstl::concurrent_unordered_map<std::string,std::uint64_t>;
using StringNative=boost::concurrent_flat_map<std::string,std::uint64_t,std::hash<std::string>>;
extern "C" __declspec(noinline) bool cstl_string_read(StringFacade* m,const std::string& k,std::uint64_t* out){return m->cvisit(k,[&](const auto& v){*out=v;});}
extern "C" __declspec(noinline) bool boost_string_read(StringNative* m,const std::string& k,std::uint64_t* out){return m->cvisit(k,[&](const auto& e){*out=e.second;})!=0;}
extern "C" __declspec(noinline) bool cstl_string_update(StringFacade* m,const std::string& k){return m->visit(k,[](auto& v){++v;});}
extern "C" __declspec(noinline) bool boost_string_update(StringNative* m,const std::string& k){return m->visit(k,[](auto& e){++e.second;})!=0;}
extern "C" __declspec(noinline) bool cstl_string_insert(StringFacade* m,const std::string& k,std::uint64_t v){return m->try_emplace(k,v);}
extern "C" __declspec(noinline) bool boost_string_insert(StringNative* m,const std::string& k,std::uint64_t v){return m->try_emplace(k,v);}
#endif
#ifdef CSTL_CODEGEN_MAIN
#include <iostream>
#ifdef CSTL_CODEGEN_STRING
#define CSTL_READ cstl_string_read
#define BOOST_READ boost_string_read
#define CSTL_UPDATE cstl_string_update
#define BOOST_UPDATE boost_string_update
#define CSTL_INSERT cstl_string_insert
#define BOOST_INSERT boost_string_insert
#else
#define CSTL_READ cstl_read
#define BOOST_READ boost_read
#define CSTL_UPDATE cstl_update
#define BOOST_UPDATE boost_update
#define CSTL_INSERT cstl_insert
#define BOOST_INSERT boost_insert
#endif
int main(){
    volatile auto cr=reinterpret_cast<std::uintptr_t>(&CSTL_READ),br=reinterpret_cast<std::uintptr_t>(&BOOST_READ);
    volatile auto cu=reinterpret_cast<std::uintptr_t>(&CSTL_UPDATE),bu=reinterpret_cast<std::uintptr_t>(&BOOST_UPDATE);
    volatile auto ci=reinterpret_cast<std::uintptr_t>(&CSTL_INSERT),bi=reinterpret_cast<std::uintptr_t>(&BOOST_INSERT);
    std::cout<<"{\"read_folded\":"<<(cr==br?"true":"false")<<",\"update_folded\":"<<(cu==bu?"true":"false")<<",\"insert_folded\":"<<(ci==bi?"true":"false")<<"}\n";
}
#endif
