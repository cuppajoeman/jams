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

using PatternMap = std::unordered_map<std::string, std::vector<std::string>>;

struct Arrangement {
  std::vector<std::string> sequence;
};

struct PatternData {
  std::string name;
  unsigned int start_bar;
  unsigned int num_repeats;
};

struct JamFileData {
  PatternMap pattern_name_to_bars;
  std::unordered_map<std::string, unsigned int> pattern_name_to_channel;
  std::vector<PatternData> arrangement;

  friend std::ostream &operator<<(std::ostream &os, const JamFileData &data) {
    os << "\n=== Parsed Pattern Bars ===\n";
    for (const auto &[pattern_name, bars] : data.pattern_name_to_bars) {
      os << "Pattern " << pattern_name << ":\n";
      for (const std::string &bar : bars) {
        os << "  " << bar << "\n";
      }
    }
    os << "=======================\n\n";

    os << "\n=== Parsed Pattern Channels ===\n";
    for (const auto &[pattern_name, channel] : data.pattern_name_to_channel) {
      os << "Pattern " << pattern_name << ", channel:  " << channel << "\n";
    }
    os << "=======================\n\n";

    os << "=== Parsed Arrangement ===\n";
    for (const auto &entry : data.arrangement) {
      os << "{ \"" << entry.name << "\", " << entry.start_bar << ", "
         << entry.num_repeats << " }\n";
    }
    os << "===========================\n";

    return os;
  }
};

std::unordered_map<std::string, std::string>
parse_legend_to_symbol_to_note(std::istream &in);
std::pair<PatternMap, std::unordered_map<std::string, unsigned int>>
parse_patterns(std::istream &in,
               const std::unordered_map<std::string, std::string> &legend);
Arrangement parse_arrangement(std::istream &in);
std::vector<std::string>
parse_grid_pattern(const std::vector<std::string> &lines,
                   const std::unordered_map<std::string, std::string> &legend,
                   const std::string &pattern_name);
JamFileData load_jam_file(const std::string &path);

#endif // JAM_FILE_PARSING_HPP
