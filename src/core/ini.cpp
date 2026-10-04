#include "ini.h"
#include <ctype.h>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "sysdev.h"

static std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

IniFile::Section *IniFile::find(const char *section)
{
    for (Section &s : sections_)
        if (!strcasecmp(s.name.c_str(), section)) return &s;
    return NULL;
}

const IniFile::Section *IniFile::find(const char *section) const
{
    for (const Section &s : sections_)
        if (!strcasecmp(s.name.c_str(), section)) return &s;
    return NULL;
}

bool IniFile::load(const char *path)
{
    sections_.clear();
    std::ifstream in(sysdev::nativePath(path));
    if (!in) return false;

    std::string line, current;
    bool inSection = false;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            size_t close = line.find(']');
            if (close == std::string::npos) continue;
            current = trim(line.substr(1, close - 1));
            inSection = true;
            if (!find(current.c_str())) {
                sections_.push_back(Section());
                sections_.back().name = current;
            }
            continue;
        }
        size_t eq = line.find('=');
        if (!inSection || eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        if (key.empty()) continue;
        set(current.c_str(), key.c_str(), trim(line.substr(eq + 1)));
    }
    return true;
}

bool IniFile::save(const char *path) const
{
    std::ofstream out(sysdev::nativePath(path));
    if (!out) return false;
    out << "; Open'Roo settings.  Edit while the game is not running.\n";
    for (const Section &s : sections_) {
        out << "\n[" << s.name << "]\n";
        for (const auto &kv : s.items)
            out << kv.first << (kv.second.empty() ? " =" : " = ") << kv.second << "\n";
    }
    out.close();
    return !out.fail();
}

bool IniFile::has(const char *section, const char *key) const
{
    const Section *s = find(section);
    if (!s) return false;
    for (const auto &kv : s->items)
        if (!strcasecmp(kv.first.c_str(), key)) return true;
    return false;
}

std::string IniFile::get(const char *section, const char *key, const char *fallback) const
{
    const Section *s = find(section);
    if (s)
        for (const auto &kv : s->items)
            if (!strcasecmp(kv.first.c_str(), key)) return kv.second;
    return fallback;
}

int IniFile::getInt(const char *section, const char *key, int fallback) const
{
    std::string v = get(section, key, "");
    char *end = NULL;
    long n = strtol(v.c_str(), &end, 0);
    return (v.empty() || *end) ? fallback : (int)n;
}

float IniFile::getFloat(const char *section, const char *key, float fallback) const
{
    std::string v = get(section, key, "");
    char *end = NULL;
    float f = strtof(v.c_str(), &end);
    return (v.empty() || *end) ? fallback : f;
}

void IniFile::set(const char *section, const char *key, const std::string &value)
{
    Section *s = find(section);
    if (!s) {
        sections_.push_back(Section());
        s = &sections_.back();
        s->name = section;
    }
    for (auto &kv : s->items)
        if (!strcasecmp(kv.first.c_str(), key)) { kv.second = value; return; }
    s->items.push_back(std::make_pair(std::string(key), value));
}

void IniFile::setInt(const char *section, const char *key, int value)
{
    set(section, key, std::to_string(value));
}

void IniFile::setFloat(const char *section, const char *key, float value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", value);
    set(section, key, buf);
}

std::vector<std::pair<std::string, std::string> > IniFile::entries(const char *section) const
{
    const Section *s = find(section);
    return s ? s->items : std::vector<std::pair<std::string, std::string> >();
}

void IniFile::removeSection(const char *section)
{
    for (size_t i = 0; i < sections_.size(); i++)
        if (!strcasecmp(sections_[i].name.c_str(), section)) {
            sections_.erase(sections_.begin() + i);
            return;
        }
}
