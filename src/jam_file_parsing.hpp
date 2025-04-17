#ifndef JAM_FILE_PARSING_HPP
#define JAM_FILE_PARSING_HPP

#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

struct LegendEntry {
  std::string name;
  int midi_number;
};

using LegendMap = std::unordered_map<std::string, int>;
using PatternMap = std::unordered_map<std::string, std::vector<std::string>>;

struct Arrangement {
  std::vector<std::string> sequence;
};

LegendMap parse_legend(std::istream &in);
PatternMap parse_patterns(std::istream &in, const LegendMap &legend);
Arrangement parse_arrangement(std::istream &in);
std::vector<std::string>
parse_grid_pattern(const std::vector<std::string> &lines,
                   const LegendMap &legend, const std::string &pattern_name);
void load_jam_file(const std::string &path);

#endif // JAM_FILE_PARSING_HPP
