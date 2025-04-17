#include "jam_file_parsing.hpp"

std::string trim(const std::string &s) {
  size_t start = s.find_first_not_of(" \t");
  size_t end = s.find_last_not_of(" \t");
  return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

LegendMap parse_legend(std::istream &in) {
  LegendMap legend;
  std::string line;
  while (std::getline(in, line)) {
    if (line.find("LEGEND END") != std::string::npos)
      break;
    std::regex entry_regex(R"((.*?):\s*(\d+))");
    std::smatch match;
    if (std::regex_search(line, match, entry_regex)) {
      std::string name = match[1];
      int midi = std::stoi(match[2]);
      legend[name] = midi;
    }
  }
  return legend;
}

PatternMap parse_patterns(std::istream &in, const LegendMap &legend) {
  PatternMap patterns;
  std::string line, current_pattern_name;
  std::vector<std::string> current_lines;

  // here we're just defining a lambda
  auto flush_current = [&]() {
    if (current_pattern_name.empty())
      return;
    bool is_grid = false;

    for (const std::string &l : current_lines) {
      std::cout << "looking at line: " << l << std::endl;
      if (l.find('x') != std::string::npos) {
        std::cout << "its's a grid!" << std::endl;
        is_grid = true;
        break;
      }
    }

    if (is_grid) {
      std::cout << "about to parse on the following: " << std::endl;
      for (const auto &line : current_lines) {
        std::cout << line << std::endl;
      }
      patterns[current_pattern_name] =
          parse_grid_pattern(current_lines, legend, current_pattern_name);
    } else {
      patterns[current_pattern_name] = current_lines;
    }

    current_pattern_name.clear();
    current_lines.clear();
  };

  // now we iterate over the input
  while (std::getline(in, line)) {
    if (line.find("PATTERNS END") != std::string::npos)
      break;

    std::string trimmed = trim(line);
    if (trimmed.empty())
      continue;

    std::cout << "iterating on line: " << trimmed << std::endl;

    bool found_new_pattern = trimmed.back() == ':';
    if (found_new_pattern) {
      flush_current();
      current_pattern_name = trimmed.substr(0, trimmed.length() - 1);
      std::cout << "starting new pattern for: " << current_pattern_name
                << std::endl;
    } else {
      current_lines.push_back(trimmed);
    }
  }

  flush_current(); // Handle last pattern

  return patterns;
}

Arrangement parse_arrangement(std::istream &in) {
  Arrangement arr;
  std::string line;
  while (std::getline(in, line)) {
    if (line.find("ARRANGEMENT END") != std::string::npos)
      break;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) {
      if (!token.empty()) {
        arr.sequence.push_back(token);
      }
    }
  }
  return arr;
}

std::vector<std::string>
parse_grid_pattern(const std::vector<std::string> &lines,
                   const LegendMap &legend, const std::string &pattern_name) {
  std::vector<std::vector<std::string>> grid; // [instrument][steps]
  std::vector<int> midi_numbers;

  std::cout << "Parsing pattern: " << pattern_name << std::endl;

  for (const std::string &line : lines) {
    std::cout << "Processing line: " << line << std::endl;
    std::smatch match;
    std::regex line_regex(R"(\((.*?)\)\s*(\|.+))");
    if (!std::regex_search(line, match, line_regex)) {
      throw std::runtime_error("Invalid grid line in pattern " + pattern_name +
                               ": " + line);
    }

    std::string instrument = match[1];
    std::string bar_data = match[2];
    std::cout << "  Instrument: " << instrument << std::endl;
    std::cout << "  Bar data: " << bar_data << std::endl;

    if (legend.count(instrument) == 0) {
      throw std::runtime_error("Instrument '" + instrument +
                               "' not found in legend for pattern '" +
                               pattern_name + "'");
    }

    int midi = legend.at(instrument);
    std::cout << "  MIDI number: " << midi << std::endl;
    midi_numbers.push_back(midi);

    std::vector<std::string> steps;
    std::regex step_regex(R"(\|([x\-]+))");
    auto begin =
        std::sregex_iterator(bar_data.begin(), bar_data.end(), step_regex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
      std::string segment = (*it)[1];
      std::cout << "    Found segment: " << segment << std::endl;
      for (char ch : segment) {
        steps.emplace_back(1, ch);
      }
    }

    std::cout << "  Total steps for " << instrument << ": " << steps.size()
              << std::endl;
    grid.push_back(steps);
  }

  size_t num_steps = grid[0].size();
  std::cout << "Total steps in pattern: " << num_steps << std::endl;

  std::vector<std::string> bars;
  std::string current_bar;
  const int steps_per_bar = 4;

  for (size_t i = 0; i < num_steps; ++i) {
    if (i % steps_per_bar == 0) {
      current_bar = "| ";
      std::cout << "Starting new bar at step " << i << std::endl;
    }

    std::vector<int> hits;
    for (size_t j = 0; j < grid.size(); ++j) {
      if (grid[j][i] == "x") {
        hits.push_back(midi_numbers[j]);
      }
    }

    if (!hits.empty()) {
      current_bar += "(";
      for (size_t k = 0; k < hits.size(); ++k) {
        current_bar += std::to_string(hits[k]);
        if (k + 1 < hits.size())
          current_bar += " ";
      }
      current_bar += ") ";
      std::cout << "  Step " << i << ": hit(s) " << current_bar << std::endl;
    } else {
      current_bar += "- ";
      std::cout << "  Step " << i << ": no hits" << std::endl;
    }

    if ((i + 1) % steps_per_bar == 0) {
      current_bar += "| ";
      bars.push_back(current_bar);
      std::cout << "Completed bar: " << current_bar << std::endl;
    }
  }

  std::cout << "Final number of bars: " << bars.size() << std::endl;

  return bars;
}

void load_jam_file(const std::string &path) {
  std::ifstream file(path);
  std::string line;

  LegendMap legend;
  PatternMap patterns;
  Arrangement arrangement;

  while (std::getline(file, line)) {
    if (line.find("LEGEND START") != std::string::npos) {
      legend = parse_legend(file);
    } else if (line.find("PATTERNS START") != std::string::npos) {
      // legend must be defined by the time we get here
      patterns = parse_patterns(file, legend);

      std::cout << "\n=== Parsed Patterns ===\n";
      for (const auto &[pattern_name, bars] : patterns) {
        std::cout << "Pattern " << pattern_name << ":\n";
        for (const std::string &bar : bars) {
          std::cout << "  " << bar << "\n";
        }
      }
      std::cout << "=======================\n\n";

    } else if (line.find("ARRANGEMENT START") != std::string::npos) {
      arrangement = parse_arrangement(file);
    }
  }

  // Example: Convert the arrangement to actual sequences
  for (const std::string &pattern_id : arrangement.sequence) {
    if (patterns.count(pattern_id)) {
      const auto &pattern_lines = patterns[pattern_id];
      for (const std::string &bar : pattern_lines) {
        std::cout << "Bar from " << pattern_id << ": " << bar << "\n";
        // Possibly tokenize and feed to BarSequence
      }
    }
  }
}
