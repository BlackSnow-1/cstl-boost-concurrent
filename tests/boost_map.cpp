// SPDX-License-Identifier: MIT
#include <cstl/concurrent_unordered_map.hpp>
#include <unordered_map>
#include <random>
#include <cstdint>
#include <cctype>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)

void randomized(){
    cstl::concurrent_unordered_map<int,int> actual;
    std::unordered_map<int,int> expected;
    std::mt19937 rng(20261104);
    for(int i=0;i<200000;++i){
        int key=int(rng()%2048),value=int(rng()%10000);
        switch(rng()%8){
        case 0: CHECK(actual.try_emplace(key,value)==expected.emplace(key,value).second);break;
        case 1: CHECK(actual.insert_or_assign(key,value)==expected.insert_or_assign(key,value).second);break;
        case 2: CHECK(actual.erase(key)==expected.erase(key));break;
        case 3:{auto a=actual.find(key);auto b=expected.find(key);CHECK(bool(a)==(b!=expected.end()));if(a)CHECK(*a==b->second);break;}
        case 4:{auto b=expected.find(key); bool erased=b!=expected.end() && b->second%2==0;CHECK(actual.erase_if(key,[](int n){return n%2==0;})==erased);if(erased)expected.erase(b);break;}
        case 5:{auto b=expected.emplace(key,value);CHECK(actual.try_emplace_or_visit(key,[](int& n){++n;},value)==b.second);if(!b.second)++b.first->second;break;}
        case 6:{auto b=expected.find(key);CHECK(actual.visit(key,[](int& n){n+=2;})==(b!=expected.end()));if(b!=expected.end())b->second+=2;break;}
        case 7:actual.reserve(rng()%4096);break;
        }
        if(i%1000==0){CHECK(actual.size()==expected.size());for(auto& e:actual.snapshot())CHECK(expected.at(e.first)==e.second);}
    }
}
struct Payload {
    static inline std::atomic<int> live{0}; int n;
    explicit Payload(int v):n(v){if(v<0)throw std::runtime_error("construct");++live;}
    Payload(const Payload&)=delete;
    Payload(Payload&& x)noexcept:n(x.n){++live;x.n=-99;}
    Payload& operator=(Payload&& x)noexcept{n=x.n;x.n=-99;return *this;}
    ~Payload(){--live;}
};
void exceptions_and_moves(){
    {cstl::concurrent_unordered_map<std::string,Payload> m;
    bool threw=false;try{m.try_emplace("bad",-1);}catch(const std::runtime_error&){threw=true;}CHECK(threw);CHECK(m.empty());
    CHECK(m.try_emplace("one",1));Payload unused(2);CHECK(!m.try_emplace("one",std::move(unused)));CHECK(unused.n==2);
    CHECK(!m.try_emplace_or_visit("one",[](auto& v){++v.n;},std::move(unused)));CHECK(unused.n==2);
    for(int kind=0;kind<3;++kind){threw=false;try{
        if(kind==0)m.visit("one",[](auto&){throw std::runtime_error("visit");});
        if(kind==1)m.cvisit("one",[](const auto&){throw std::runtime_error("cvisit");});
        if(kind==2)m.erase_if("one",[](const auto&)->bool{throw std::runtime_error("predicate");});
    }catch(const std::runtime_error&){threw=true;}CHECK(threw);CHECK(m.cvisit("one",[](const auto& v){CHECK(v.n==2);}));}
    m.reserve(10000);CHECK(m.contains("one"));m.clear();CHECK(m.empty());}
    CHECK(Payload::live==0);
    cstl::concurrent_unordered_map<std::string,int> m; std::string k="key";CHECK(m.try_emplace(std::move(k),9));CHECK(m.find("key")==9);
    const std::pair<const std::string,int> value{"copy",4};CHECK(m.insert(value));CHECK(!m.insert(value));CHECK(m.insert({"move",5}));
}
void concurrent_atomic(){
    cstl::concurrent_unordered_map<int,int> m;
    constexpr int threads=12,ops=50000;
    std::vector<std::thread> workers;
    for(int t=0;t<threads;++t)workers.emplace_back([&]{for(int i=0;i<ops;++i)m.try_emplace_or_visit(i%8,[](int& n){++n;},1);});
    for(auto& w:workers)w.join();
    int sum=0;for(auto e:m.snapshot())sum+=e.second;CHECK(sum==threads*ops);
    workers.clear();std::atomic<int> erased{0};
    for(int t=0;t<threads;++t)workers.emplace_back([&]{for(int i=0;i<8;++i)if(m.erase_if(i,[](int n){return n>0;}))++erased;});
    for(auto& w:workers)w.join();CHECK(erased==8);CHECK(m.empty());
}
struct FoldHash{std::size_t operator()(const std::string& s)const{std::size_t h=0;for(unsigned char c:s)h=h*131+std::tolower(c);return h;}};
struct FoldEqual{bool operator()(const std::string& a,const std::string& b)const{if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(std::tolower((unsigned char)a[i])!=std::tolower((unsigned char)b[i]))return false;return true;}};
void custom_hash(){
    cstl::concurrent_unordered_map<std::string,int,FoldHash,FoldEqual,8> m;
    CHECK(m.try_emplace("Alpha",3));CHECK(!m.try_emplace("ALPHA",9));CHECK(m.find("alpha")==3);CHECK(m.erase("aLpHa")==1);
    cstl::concurrent_unordered_map<int,int,std::hash<int>,std::equal_to<int>,0> single;
    CHECK(single.try_emplace(1,2));CHECK(single.find(1)==2);
}
struct StatefulVisitor { int count=0; void operator()(int& n){++count;++n;} };
struct RvalueVisitor { void operator()(int& n)&&{++n;} };
void visitor_forms(){
    cstl::concurrent_unordered_map<int,int> m;CHECK(m.try_emplace(1,0));
    StatefulVisitor state;CHECK(m.visit(1,state));CHECK(state.count==1);
    CHECK(m.visit(1,[p=std::make_unique<int>(2)](int& n){n+=*p;}));
    CHECK(m.visit(1,RvalueVisitor{}));CHECK(m.find(1)==4);
    CHECK(m.cvisit(1,[p=std::make_unique<int>(4)](const int& n){CHECK(n==*p);}));
    CHECK(!m.try_emplace_or_visit(1,[p=std::make_unique<int>(2)](int& n){n+=*p;},0));
    CHECK(m.erase_if(1,[p=std::make_unique<int>(6)](const int& n){return n==*p;}));
}
struct CountedHash {
    inline static std::atomic<int> calls{0};
    std::size_t operator()(std::uint64_t n)const noexcept{++calls;return static_cast<std::size_t>(n*2654435761u);}
};
struct MoveKey {
    inline static int live=0;int value;
    explicit MoveKey(int n):value(n){++live;}
    MoveKey(const MoveKey&)=delete;
    MoveKey(MoveKey&& x)noexcept:value(x.value){x.value=-1;++live;}
    ~MoveKey(){--live;}
    friend bool operator==(const MoveKey& a,const MoveKey& b){return a.value==b.value;}
};
struct MoveKeyHash {std::size_t operator()(const MoveKey& k)const noexcept{return std::hash<int>{}(k.value);}};
void prehash_and_relocation(){
    cstl::concurrent_unordered_map<std::uint64_t,std::uint64_t,CountedHash> counted(256);
    for(std::uint64_t i=0;i<32;++i)CHECK(counted.try_emplace(i,i));
    CountedHash::calls=0;CHECK(counted.try_emplace(32,32));CHECK(CountedHash::calls==1);
    CountedHash::calls=0;CHECK(!counted.try_emplace(1,99));CHECK(CountedHash::calls==1);
    CountedHash::calls=0;CHECK(counted.cvisit(1,[](auto n){CHECK(n==1);}));CHECK(CountedHash::calls==1);
    CountedHash::calls=0;CHECK(!counted.try_emplace_or_visit(1,[](auto& n){++n;},99));CHECK(CountedHash::calls==1);
    {cstl::concurrent_unordered_map<MoveKey,int,MoveKeyHash> m;
     for(int i=0;i<1000;++i)CHECK(m.try_emplace(MoveKey(i),i));
     MoveKey duplicate(42);CHECK(!m.try_emplace(std::move(duplicate),99));CHECK(duplicate.value==42);
     for(int i=0;i<1000;++i)CHECK(m.find(MoveKey(i))==i);
     m.reserve(4000);CHECK(m.size()==1000);m.clear();CHECK(m.empty());CHECK(MoveKey::live==1);}
    CHECK(MoveKey::live==0);
    cstl::concurrent_unordered_map<std::uint64_t,std::uint64_t,std::hash<std::uint64_t>,std::equal_to<std::uint64_t>,0> growing;
    for(std::uint64_t i=0;i<200000;++i)CHECK(growing.try_emplace(i,i^0xabcdefu));
    CHECK(growing.size()==200000);
    for(std::uint64_t i=0;i<200000;++i)CHECK(growing.find(i)==(i^0xabcdefu));
}
int main(){try{randomized();exceptions_and_moves();concurrent_atomic();custom_hash();visitor_forms();prehash_and_relocation();std::cout<<"PASS: 6 extended map suites (200000 differential operations, exception/move lifetime, 600000 atomic updates, custom hash, visitor categories)\n";}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
