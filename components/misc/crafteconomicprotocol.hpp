// ArenaMP Craft and Economic wire values. Original GPL-3.0-or-later code.
// Bounded JSON, independent of engine/UI and third-party JSON libraries.
#ifndef ARENA_CRAFT_ECONOMIC_PROTOCOL_HPP
#define ARENA_CRAFT_ECONOMIC_PROTOCOL_HPP
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
namespace ArenaCraft
{
    constexpr const char* Prefix = "@@ARENA_CE1@@";
    struct Value
    {
        enum Type { Null, Boolean, Number, String, Array, Object } type = Null;
        bool boolean = false;
        double number = 0;
        std::string string;
        std::vector<Value> array;
        std::map<std::string, Value> object;
        Value() = default;
        Value(bool v) : type(Boolean), boolean(v) {}
        Value(int v) : type(Number), number(v) {}
        Value(unsigned v) : type(Number), number(v) {}
        Value(double v) : type(Number), number(v) {}
        Value(std::string v) : type(String), string(std::move(v)) {}
        Value(const char* v) : Value(std::string(v)) {}
        static Value list() { Value v; v.type=Array; return v; }
        static Value dict() { Value v; v.type=Object; return v; }
        Value& operator[](const std::string& k) { type=Object; return object[k]; }
        const Value& at(const std::string& k) const
        {
            static const Value empty;
            const auto i=object.find(k); return i==object.end() ? empty : i->second;
        }
        int integer(int fallback=0) const
        {
            return type==Number && std::isfinite(number) && number>=-2147483647 && number<=2147483647
                && std::floor(number)==number ? static_cast<int>(number) : fallback;
        }
        std::string text(const std::string& fallback={}) const { return type==String ? string : fallback; }
        bool yes() const { return type==Boolean && boolean; }
    };
    inline std::string quote(const std::string& text)
    {
        static constexpr char hex[]="0123456789abcdef";
        std::string out="\"";
        for (unsigned char c : text)
        {
            if (c=='"' || c=='\\') { out+='\\';out+=c; }
            else if (c<32) { out+="\\u00";out+=hex[c>>4];out+=hex[c&15]; }
            else out+=c;
        }
        return out+'"';
    }
    inline std::string encode(const Value& v, unsigned depth=0)
    {
        if (depth>16) throw std::runtime_error("JSON depth limit");
        switch (v.type)
        {
            case Value::Null: return "null";
            case Value::Boolean: return v.boolean ? "true" : "false";
            case Value::String: return quote(v.string);
            case Value::Number:
            {
                if (!std::isfinite(v.number)) throw std::runtime_error("non-finite JSON number");
                std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<v.number;return out.str();
            }
            case Value::Array:
            {
                std::string out="[";for (const auto& x:v.array) {if(out.size()>1)out+=',';out+=encode(x,depth+1);}return out+"]";
            }
            case Value::Object:
            {
                std::string out="{";for (const auto& x:v.object) {if(out.size()>1)out+=',';out+=quote(x.first)+":"+encode(x.second,depth+1);}return out+"}";
            }
        }
        throw std::runtime_error("invalid JSON type");
    }
    class Parser
    {
        const std::string& s;
        std::size_t p=0, nodes=0;
        void fail() const { throw std::runtime_error("invalid CraftEconomic JSON"); }
        void space() { while(p<s.size() && (s[p]==' ' || s[p]=='\t' || s[p]=='\n' || s[p]=='\r')) ++p; }
        bool take(char c) { space();if(p<s.size() && s[p]==c){++p;return true;}return false; }
        unsigned hex4()
        {
            unsigned out=0;
            for(unsigned i=0;i<4;++i)
            {
                if(p>=s.size())fail();
                const char c=s[p++];unsigned n;
                if(c>='0'&&c<='9')n=c-'0';else if(c>='a'&&c<='f')n=c-'a'+10;else if(c>='A'&&c<='F')n=c-'A'+10;else {fail();return 0;}
                out=(out<<4)|n;
            }
            return out;
        }
        void utf8(std::string& out,unsigned n)
        {
            if(n<128)out+=char(n);
            else if(n<2048){out+=char(0xc0|(n>>6));out+=char(0x80|(n&63));}
            else if(n<65536){out+=char(0xe0|(n>>12));out+=char(0x80|((n>>6)&63));out+=char(0x80|(n&63));}
            else{out+=char(0xf0|(n>>18));out+=char(0x80|((n>>12)&63));out+=char(0x80|((n>>6)&63));out+=char(0x80|(n&63));}
        }
        std::string string()
        {
            if(!take('"'))fail();
            std::string out;
            while(p<s.size())
            {
                const unsigned char c=s[p++];
                if(c=='"')return out;
                if(c<32)fail();
                if(c!='\\'){out+=c;continue;}
                if(p>=s.size())fail();
                switch(s[p++])
                {
                    case '"':out+='"';break;case '\\':out+='\\';break;case '/':out+='/';break;
                    case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
                    case 'u':
                    {
                        unsigned n=hex4();
                        if(n>=0xd800&&n<=0xdbff)
                        {
                            if(p+2>s.size()||s[p++]!='\\'||s[p++]!='u')fail();
                            const unsigned low=hex4();if(low<0xdc00||low>0xdfff)fail();n=0x10000+((n-0xd800)<<10)+(low-0xdc00);
                        }
                        else if(n>=0xdc00&&n<=0xdfff)fail();
                        utf8(out,n);break;
                    }
                    default:fail();
                }
            }
            fail();return {};
        }
        Value value(unsigned depth)
        {
            if(depth>16 || ++nodes>4096)fail();
            space();if(p>=s.size())fail();
            if(s[p]=='"')return Value(string());
            if(take('{'))
            {
                auto out=Value::dict();if(take('}'))return out;
                do {const auto k=string();if(!take(':')||out.object.count(k))fail();out[k]=value(depth+1);}while(take(','));
                if(!take('}'))fail();
                return out;
            }
            if(take('['))
            {
                auto out=Value::list();if(take(']'))return out;
                do {out.array.push_back(value(depth+1));}while(take(','));if(!take(']'))fail();return out;
            }
            if(s.compare(p,4,"null")==0){p+=4;return Value();}
            if(s.compare(p,4,"true")==0){p+=4;return Value(true);}
            if(s.compare(p,5,"false")==0){p+=5;return Value(false);}
            const auto begin=p;
            if(s[p]=='-')++p;
            if(p>=s.size())fail();
            if(s[p]=='0')++p;
            else {if(s[p]<'1'||s[p]>'9')fail();while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;}
            if(p<s.size()&&s[p]=='.') {++p;const auto d=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(p==d)fail();}
            if(p<s.size()&&(s[p]=='e'||s[p]=='E')){++p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;const auto d=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(p==d)fail();}
            std::istringstream in(s.substr(begin,p-begin));in.imbue(std::locale::classic());double n=0;in>>n;if(!in || !std::isfinite(n))fail();return Value(n);
        }
    public:
        explicit Parser(const std::string& input):s(input){}
        Value parse() {if(s.size()>60000)fail();auto out=value(0);space();if(p!=s.size())fail();return out;}
    };
}
#endif
