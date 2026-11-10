#include <components/misc/crafteconomicprotocol.hpp>
#include <cassert>
#include <iostream>
int main()
{
    using namespace ArenaCraft;
    auto v=Value::dict();v["name"]="Роба: \"свет\"\n";v["price"]=1024;v["items"]=Value::list();v["items"].array.push_back(true);
    const auto decoded=Parser(encode(v)).parse();
    assert(decoded.at("name").text()==v.at("name").text() && decoded.at("price").integer()==1024);
    assert(decoded.at("items").array[0].yes());
    assert(Parser("\"\\ud83d\\ude00\"").parse().text()=="\xf0\x9f\x98\x80");
    for(const auto& bad:std::vector<std::string>{"{}x","[1,]","01","-","1e+","NaN","1e999","\"\\ud800\"","{\"x\":1,\"x\":2}",std::string(60001,' ')})
    {
        bool refused=false;try{Parser(bad).parse();}catch(...){refused=true;}assert(refused);
    }
    for(unsigned i=1;i<=16;++i)
    {
        const auto raw=std::string(i,'[')+"0"+std::string(i,']');assert(Parser(raw).parse().type==Value::Array);
    }
    bool refused=false;try{Parser(std::string(17,'[')+"0"+std::string(17,']')).parse();}catch(...){refused=true;}assert(refused);
    // Every truncated prefix must fail, except a complete number or whitespace-free literal.
    const auto raw=encode(v);
    for(std::size_t i=0;i<raw.size();++i)
    {refused=false;try{Parser(raw.substr(0,i)).parse();}catch(...){refused=true;}assert(refused);}
    std::cout<<"PASS bounded C++ JSON: Unicode, escaping, truncation, duplicates, numeric and depth limits\n";
}
