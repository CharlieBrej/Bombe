#include "Misc.h"
#include "SaveState.h"
#include <assert.h>
#define assert_exp(x) do { if (f.get() != x) throw (std::runtime_error("Unexpected character"));} while (false)

static void skip_whitespace(std::istream& f)
{
    while (true)
    {
        int c = f.peek();
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            f.get();
        else if (c == 0xEF)
        {
            f.get();f.get();f.get();
        }
        else
            break;
    }
}

std::string SaveObject::to_string()
{
    std::ostringstream stream;
    save(stream);
    return stream.str();
}
SaveObject* SaveObject::load(std::string& input)
{
    std::istringstream stream(input);
    return SaveObject::load(stream);
}

SaveObject* SaveObject::load(std::istream& f)
{
    skip_whitespace(f);
    char c = f.peek();
    if (c == '{')
        return new SaveObjectMap(f);
    if (c == 'n')
        return new SaveObjectNull(f);
    if (c == 'f')
    {
        assert_exp('f');
        assert_exp('a');
        assert_exp('l');
        assert_exp('s');
        assert_exp('e');
        return new SaveObjectNumber(0);
    }
    if (c == 't')
    {
        assert_exp('t');
        assert_exp('r');
        assert_exp('u');
        assert_exp('e');
        return new SaveObjectNumber(1);
    }
    if (c == '[')
        return new SaveObjectList(f);
    if (c == '"')
        return new SaveObjectString(f);
    if ((c >= '0' && c <= '9') || c == '-')
        return new SaveObjectNumber(f);
    printf("%d\n", (int)(unsigned char)c);
    throw(std::runtime_error("Parse Error"));
}

static void append_utf8(std::string& str, uint32_t codepoint)
{
    if (codepoint <= 0x7f)
        str.push_back(codepoint);
    else if (codepoint <= 0x7ff)
    {
        str.push_back(0xc0 | (codepoint >> 6));
        str.push_back(0x80 | (codepoint & 0x3f));
    }
    else if (codepoint <= 0xffff)
    {
        str.push_back(0xe0 | (codepoint >> 12));
        str.push_back(0x80 | ((codepoint >> 6) & 0x3f));
        str.push_back(0x80 | (codepoint & 0x3f));
    }
    else if (codepoint <= 0x10ffff)
    {
        str.push_back(0xf0 | (codepoint >> 18));
        str.push_back(0x80 | ((codepoint >> 12) & 0x3f));
        str.push_back(0x80 | ((codepoint >> 6) & 0x3f));
        str.push_back(0x80 | (codepoint & 0x3f));
    }
    else
        throw(std::runtime_error("Invalid Unicode escape"));
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw(std::runtime_error("Invalid Unicode escape"));
}

static uint32_t parse_unicode_escape(std::istream& f)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; i++)
    {
        int c = f.get();
        if (c == EOF)
            throw(std::runtime_error("Unexpected end of string"));
        value = (value << 4) | hex_digit(c);
    }
    return value;
}

static std::string parse_string(std::istream& f)
{
    std::string str;
    assert_exp('\"');
    while (true)
    {
        int c = f.get();
        if (c == EOF)
            throw(std::runtime_error("Unexpected end of string"));
        if (c == '\"')
            return str;
        if (c < 0x20)
            throw(std::runtime_error("Unescaped control character"));
        if (c != '\\')
        {
            str.push_back(c);
            continue;
        }

        c = f.get();
        if (c == EOF)
            throw(std::runtime_error("Unexpected end of string"));
        switch (c)
        {
        case '\"': str.push_back('\"'); break;
        case '\'': str.push_back('\''); break;
        case '\\': str.push_back('\\'); break;
        case '/': str.push_back('/'); break;
        case 'b': str.push_back('\b'); break;
        case 'f': str.push_back('\f'); break;
        case 'n': str.push_back('\n'); break;
        case 'r': str.push_back('\r'); break;
        case 't': str.push_back('\t'); break;
        case 'u':
        {
            uint32_t codepoint = parse_unicode_escape(f);
            if (codepoint >= 0xd800 && codepoint <= 0xdbff)
            {
                if (f.get() != '\\' || f.get() != 'u')
                    throw(std::runtime_error("Invalid Unicode surrogate pair"));
                uint32_t low = parse_unicode_escape(f);
                if (low < 0xdc00 || low > 0xdfff)
                    throw(std::runtime_error("Invalid Unicode surrogate pair"));
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
            }
            else if (codepoint >= 0xdc00 && codepoint <= 0xdfff)
                throw(std::runtime_error("Invalid Unicode surrogate pair"));
            append_utf8(str, codepoint);
            break;
        }
        default: throw(std::runtime_error("Invalid string escape"));
        }
    }
}

static void save_string(std::ostream& f, const std::string& str)
{
    f << '\"';
    for (unsigned char c : str)
    {
        switch (c)
        {
        case '\b': f << "\\b"; break;
        case '\f': f << "\\f"; break;
        case '\n': f << "\\n"; break;
        case '\r': f << "\\r"; break;
        case '\t': f << "\\t"; break;
        case '\"': f << "\\\""; break;
        case '\\': f << "\\\\"; break;
        default:
            if (c < 0x20)
                f << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 0xf];
            else
                f << c;
        }
    }
    f << '\"';
}
SaveObjectString::SaveObjectString(std::istream& f)
{
    str = parse_string(f);
}

std::string SaveObjectString::get_string()
{
    return str;
}

void SaveObjectString::save(std::ostream& f)
{
    save_string(f, str);
}

SaveObjectMap::SaveObjectMap(std::istream& f)
{
    assert_exp('{');
    while (true)
    {
        skip_whitespace(f);
        if (f.peek() == '}')
            break;
        std::string key = parse_string(f);
        skip_whitespace(f);
        assert_exp(':');
        SaveObject* obj = SaveObject::load(f);
        add_item(key, obj);
        skip_whitespace(f);
        if (f.peek() == '}')
            break;
        assert_exp(',');
    }
    assert_exp('}');
}
SaveObjectMap::~SaveObjectMap()
{
    for(std::map<std::string, SaveObject*>::iterator it = omap.begin();it != omap.end();++it)
        delete it->second;
}

void SaveObjectMap::add_item(std::string key, SaveObject* value)
{
    assert(!omap[key]);
    omap[key]=value;
}

SaveObject* SaveObjectMap::get_item(std::string key)
{
    if (omap.find(key) == omap.end())
    {
        std::cout << key << "\n";
        throw(std::runtime_error("Bad map key"));
    }
    return omap[key];
}

int64_t SaveObjectMap::get_num(std::string key)
{
    if (omap.find(key) != omap.end())
        return omap[key]->get_num();
    std::cout << "failed indexing for an int with key:" << key << "\n";
    return 0;
}
void SaveObjectMap::get_num(std::string key, int& value)
{
    if (omap.find(key) == omap.end())
        throw(std::runtime_error("Bad map key"));
    value = omap[key]->get_num();
}
void SaveObjectMap::add_num(std::string key, int64_t value)
{
    add_item(key, new SaveObjectNumber(value));
}
void SaveObjectMap::add_string(std::string key, std::string value)
{
    add_item(key, new SaveObjectString(value));
}
void SaveObjectMap::get_string(std::string key, std::string& value)
{
    if (omap.find(key) == omap.end())
        throw(std::runtime_error("Bad map key"));
    value = omap[key]->get_string();
}

std::string SaveObjectMap::get_string(std::string key)
{
    if (omap.find(key) == omap.end())
        throw(std::runtime_error("Bad map key"));
    return omap[key]->get_string();
}

bool SaveObjectMap::has_key(std::string key)
{
    return omap.find(key) != omap.end();
}

void SaveObjectMap::save(std::ostream& f)
{
    f.put('{');
    bool first = true;
    for (std::map<std::string, SaveObject*>::iterator it=omap.begin(); it!=omap.end(); ++it)
    {
        if (!first)
            f << ',';
        first = false;
        save_string(f, it->first);

        f << ':';
        it->second->save(f);
    }
    f.put('}');
};

void SaveObjectMap::pretty_print(std::ostream& f, int indent)
{
    f.put('\n');
    f << std::string(indent, ' ');
    f.put('{');
    bool first = true;
    for (std::map<std::string, SaveObject*>::iterator it=omap.begin(); it!=omap.end(); ++it)
    {
        if (!first)
            f << ',';
        f.put('\n');
        f << std::string(indent + 2, ' ');
        first = false;
        save_string(f, it->first);

        f << ':';
        it->second->pretty_print(f, indent + 4);
    }
    f.put('\n');
    f << std::string(indent, ' ');
    f.put('}');
}

SaveObject* SaveObjectMap::dup()
{
    SaveObjectMap* rep = new SaveObjectMap;
    for (std::map<std::string, SaveObject*>::iterator it=omap.begin(); it!=omap.end(); ++it)
    {
        rep->add_item(it->first, it->second->dup());
    }
    return rep;
};

SaveObjectList::SaveObjectList(std::istream& f)
{
    assert_exp('[');
    while (true)
    {
        skip_whitespace(f);
        if (f.peek() == ']')
            break;
        SaveObject* obj = SaveObject::load(f);
        add_item(obj);
        skip_whitespace(f);
        if (f.peek() == ']')
            break;
        assert_exp(',');
    }
    assert_exp(']');
}

SaveObjectList::~SaveObjectList()
{
    for(std::vector<SaveObject*>::iterator it = olist.begin(); it != olist.end(); it++)
        delete *it;
}

void SaveObjectList::add_item(SaveObject* value)
{
    olist.push_back(value);
}

SaveObject* SaveObjectList::get_item(unsigned index)
{
    if (index >= get_count())
        throw(std::runtime_error("Bad list index"));
    return olist[index];
}

unsigned SaveObjectList::get_count()
{
    return olist.size();
}

void SaveObjectList::add_num(int64_t value)
{
    add_item(new SaveObjectNumber(value));
}

int64_t SaveObjectList::get_num(int index)
{
    return get_item(index)->get_num();
}

void SaveObjectList::save(std::ostream& f)
{
    f.put('[');
    bool first = true;
    for (std::vector<SaveObject*>::iterator it=olist.begin(); it!=olist.end(); ++it)
    {
        if (!first)
            f << ',';
        first = false;
        (*it)->save(f);
    }
    f.put(']');
}

void SaveObjectList::pretty_print(std::ostream& f, int indent)
{
    f.put('[');
    bool first = true;
    for (std::vector<SaveObject*>::iterator it=olist.begin(); it!=olist.end(); ++it)
    {
        if (!first)
            f << ',';
        first = false;
        (*it)->pretty_print(f, indent);
    }
    f.put(']');
}

void SaveObjectList::add_string(std::string value)
{
    add_item(new SaveObjectString(value));
}

std::string SaveObjectList::get_string(int index)
{
    if (index >= (int)olist.size())
        return "";
    return get_item(index)->get_string();
}


SaveObject* SaveObjectList::dup()
{
    SaveObjectList* rep = new SaveObjectList;
    for (std::vector<SaveObject*>::iterator it=olist.begin(); it!=olist.end(); ++it)
    {
        rep->add_item((*it)->dup());
    }
    return rep;
};

void SaveObjectList::pop_back()
{
    delete olist.back();
    olist.pop_back();
}

SaveObjectNull::SaveObjectNull(std::istream& f)
{
    assert_exp('n');
    assert_exp('u');
    assert_exp('l');
    assert_exp('l');
}

void SaveObjectNull::save(std::ostream& f)
{
    f << "null";
}
