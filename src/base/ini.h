/* A small INI file: [sections] of "key = value" lines, ';' or '#' comments.
 *
 * Section and key names compare without regard to case; their order and
 * original spelling are kept.  Writing drops comments, so a file that has
 * been through the game holds only what it knows.  A file is read, changed
 * and written whole, which lets the settings and the key bindings share one
 * file without either clobbering the other. */
#pragma once
#include <string>
#include <utility>
#include <vector>

class IniFile {
public:
    /* False if the file cannot be read (the object is then empty). */
    bool load(const char *path);
    /* False if the file cannot be written. */
    bool save(const char *path) const;

    bool        has(const char *section, const char *key) const;
    std::string get(const char *section, const char *key, const char *fallback = "") const;
    /* The value as a number, or fallback if it is missing or not one. */
    int         getInt(const char *section, const char *key, int fallback) const;
    float       getFloat(const char *section, const char *key, float fallback) const;

    void set(const char *section, const char *key, const std::string &value);
    void setInt(const char *section, const char *key, int value);
    void setFloat(const char *section, const char *key, float value);

    /* A section's keys and values in file order; empty if it is missing. */
    std::vector<std::pair<std::string, std::string> > entries(const char *section) const;
    void removeSection(const char *section);

private:
    struct Section {
        std::string name;
        std::vector<std::pair<std::string, std::string> > items;
    };
    Section       *find(const char *section);
    const Section *find(const char *section) const;

    std::vector<Section> sections_;
};
