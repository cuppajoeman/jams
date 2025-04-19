#include "jam_file_parsing.hpp"

std::string trim(const std::string &s) {
  size_t start = s.find_first_not_of(" \t");
  size_t end = s.find_last_not_of(" \t");
  return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

bool line_should_be_skipped(const std::string &line) {
  // Trim leading whitespace to handle lines like "   # comment"
  size_t first_non_space = line.find_first_not_of(" \t");
  if (first_non_space == std::string::npos)
    return true;
  if (line[first_non_space] == '#')
    return true;
  return false;
}

std::unordered_map<std::string, std::string>
parse_legend_to_symbol_to_note(std::istream &in) {
  std::unordered_map<std::string, std::string> legend;
  std::string line;
  while (std::getline(in, line)) {
    if (line_should_be_skipped(line))
      continue;
    if (line.find("LEGEND END") != std::string::npos)
      break;
    std::regex entry_regex(R"((.*?):\s*(\d+[',]*))");
    std::smatch match;
    if (std::regex_search(line, match, entry_regex)) {
      std::string name = match[1];
      legend[name] = match[2];
    }
  }
  return legend;
}

std::vector<std::string>
flatten_bar_strings(const std::vector<std::string> &lines) {
  std::vector<std::string> bars;

  for (const std::string &line : lines) {
    std::stringstream ss(line);
    std::string bar;

    while (std::getline(ss, bar, '|')) {
      // Trim leading and trailing whitespace
      size_t start = bar.find_first_not_of(" \t");
      size_t end = bar.find_last_not_of(" \t");

      if (start != std::string::npos && end != std::string::npos) {
        bars.push_back(bar.substr(start, end - start + 1));
      }
    }
  }

  return bars;
}

std::pair<PatternMap, std::unordered_map<std::string, unsigned int>>
parse_patterns(
    std::istream &in,
    const std::unordered_map<std::string, std::string> &symbol_to_midi_note) {
  PatternMap pattern_name_to_bars;
  std::unordered_map<std::string, unsigned int> pattern_name_to_channel;

  std::string line, current_pattern_name;
  std::vector<std::string> current_bars;

  std::regex header_regex(R"(^\s*([A-Za-z0-9_]+)\((\d+)\))");

  auto flush_current = [&]() {
    if (current_pattern_name.empty())
      return;

    bool is_grid = false;
    for (const std::string &l : current_bars) {
      if (l.find('x') != std::string::npos) {
        is_grid = true;
        break;
      }
    }

    if (is_grid) {
      pattern_name_to_bars[current_pattern_name] = parse_grid_pattern(
          current_bars, symbol_to_midi_note, current_pattern_name);
    } else {
      pattern_name_to_bars[current_pattern_name] =
          flatten_bar_strings(current_bars);
    }

    current_pattern_name.clear();
    current_bars.clear();
  };

  while (std::getline(in, line)) {

    if (line_should_be_skipped(line))
      continue;

    if (line.find("PATTERNS END") != std::string::npos)
      break;

    std::string trimmed = trim(line);
    if (trimmed.empty())
      continue;

    bool found_new_pattern = trimmed.back() == ':';
    if (found_new_pattern) {
      flush_current();

      std::string header = trimmed.substr(0, trimmed.length() - 1);

      std::smatch match;
      if (std::regex_match(header, match, header_regex)) {
        current_pattern_name = match[1];
        pattern_name_to_channel[current_pattern_name] = std::stoul(match[2]);
      } else {
        current_pattern_name = header;
      }
    } else {
      current_bars.push_back(trimmed);
    }
  }

  flush_current();

  return {std::move(pattern_name_to_bars), std::move(pattern_name_to_channel)};
}

std::vector<PatternData> parse_arrangement(
    std::istream &in,
    const std::unordered_map<std::string, std::vector<std::string>>
        &pattern_name_to_bars) {

  std::vector<std::string> lines;
  unsigned int num_bars_per_block = 0;
  std::string line;

  while (std::getline(in, line)) {

    if (line_should_be_skipped(line))
      continue;

    if (line.find("ARRANGEMENT END") != std::string::npos)
      break;

    if (line.find("num_bars_per_block") != std::string::npos) {
      auto eq_pos = line.find('=');
      if (eq_pos != std::string::npos)
        num_bars_per_block = std::stoi(line.substr(eq_pos + 1));
    } else {
      lines.push_back(line);
    }
  }

  if (num_bars_per_block == 0) {
    throw std::runtime_error(
        "Missing num_bars_per_block in ARRANGEMENT section");
  }

  // Map from bar position to pattern name
  std::vector<PatternData> raw_entries;

  for (const std::string &line : lines) {
    for (size_t i = 0; i < line.size(); ++i) {
      char c = line[i];
      if (c == ' ' || c == '\t')
        continue;

      std::string pattern_name(1, c);
      if (pattern_name_to_bars.find(pattern_name) == pattern_name_to_bars.end())
        continue;

      unsigned int pattern_bar_count =
          pattern_name_to_bars.at(pattern_name).size();
      unsigned int start_bar =
          static_cast<unsigned int>(i * num_bars_per_block);

      raw_entries.push_back({pattern_name, start_bar, 1});
    }
  }

  // Sort entries by start_bar
  std::sort(raw_entries.begin(), raw_entries.end(),
            [](const PatternData &a, const PatternData &b) {
              return a.start_bar < b.start_bar;
            });

  std::vector<PatternData> grouped;
  for (const auto &entry : raw_entries) {
    bool merged = false; // To track if a match was found and merged

    for (auto &group : grouped) {
      unsigned int num_bars_in_pattern =
          pattern_name_to_bars.at(entry.name).size();
      unsigned int last_end_bar =
          group.start_bar + (group.num_repeats * num_bars_in_pattern);

      if (group.name == entry.name && last_end_bar == entry.start_bar) {
        group.num_repeats += entry.num_repeats;
        merged = true;
        break; // Exit the loop once merged
      }
    }

    if (!merged) {
      grouped.push_back(entry);
    }
  }

  return grouped;
}


std::vector<std::string> parse_grid_pattern(
    const std::vector<std::string> &lines,
    const std::unordered_map<std::string, std::string> &symbol_to_midi_note,
    const std::string &pattern_name) {
  std::vector<std::vector<std::string>> grid; // [instrument][steps]
  std::vector<std::string> midi_numbers;

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

    if (symbol_to_midi_note.count(instrument) == 0) {
      throw std::runtime_error("Instrument '" + instrument +
                               "' not found in legend for pattern '" +
                               pattern_name + "'");
    }

    std::string midi = symbol_to_midi_note.at(instrument);
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

    std::vector<std::string> hits;
    for (size_t j = 0; j < grid.size(); ++j) {
      if (grid[j][i] == "x") {
        hits.push_back(midi_numbers[j]);
      }
    }

    if (!hits.empty()) {
      current_bar += "(";
      for (size_t k = 0; k < hits.size(); ++k) {
        current_bar += hits[k];
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

JamFileData load_jam_file(const std::string &path) {
  std::ifstream file(path);
  std::string line;

  std::unordered_map<std::string, std::string> legend_symbol_to_midi_note;
  PatternMap pattern_name_to_bars;
  std::unordered_map<std::string, unsigned int> pattern_name_to_channel;
  std::vector<PatternData> arrangement;

  while (std::getline(file, line)) {

    if (line_should_be_skipped(line))
      continue;

    if (line.find("LEGEND START") != std::string::npos) {
      legend_symbol_to_midi_note = parse_legend_to_symbol_to_note(file);
    } else if (line.find("PATTERNS START") != std::string::npos) {
      auto tup = parse_patterns(file, legend_symbol_to_midi_note);
      pattern_name_to_bars = tup.first;
      pattern_name_to_channel = tup.second;
    } else if (line.find("ARRANGEMENT START") != std::string::npos) {
      arrangement = parse_arrangement(file, pattern_name_to_bars);
    }
  }

  return {pattern_name_to_bars, pattern_name_to_channel, arrangement};
}
