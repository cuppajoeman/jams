#include "jam_file_parsing.hpp"
#include <algorithm>
#include <iostream>
#include <numeric> // std::lcm (C++17)
#include <random>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

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

// Compute the LCM of a vector of integers
int compute_lcm(const std::vector<int> &values) {
  return std::accumulate(values.begin(), values.end(), 1,
                         [](int a, int b) { return std::lcm(a, b); });
}

// Rescale a single segment like "x-x-" to a new step count
std::vector<std::string> rescale_segment(const std::string &segment,
                                         int target_steps) {
  int original_steps = static_cast<int>(segment.size());
  std::vector<std::string> result(target_steps, "-");

  for (int i = 0; i < original_steps; ++i) {
    if (segment[i] == 'x') {
      // Compute mapped index in rescaled version
      int idx = i * target_steps / original_steps;
      result[idx] = "x";
    }
  }
  return result;
}

std::vector<std::string> parse_grid_pattern(
    const std::vector<std::string> &lines,
    const std::unordered_map<std::string, std::string> &symbol_to_midi_note,
    const std::string &pattern_name) {
  std::vector<std::vector<std::string>> grid; // [instrument][steps]
  std::vector<std::string> midi_numbers;

  std::cout << "Parsing pattern: " << pattern_name << std::endl;

  std::vector<std::vector<std::string>> all_segments;
  std::vector<int> bar_lengths;

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

    std::regex step_regex(R"(\|([x\-]+))");
    auto begin =
        std::sregex_iterator(bar_data.begin(), bar_data.end(), step_regex);
    auto end = std::sregex_iterator();

    std::vector<std::string> instrument_steps;
    std::vector<std::string> instrument_segments;
    for (auto it = begin; it != end; ++it) {
      std::string segment = (*it)[1];
      instrument_segments.push_back(segment);
      bar_lengths.push_back(static_cast<int>(segment.size()));
      std::cout << "    Found segment: " << segment << std::endl;
    }

    all_segments.push_back(instrument_segments);
  }

  int lcm = compute_lcm(bar_lengths);
  std::cout << "Rescaling all bars to LCM step count: " << lcm << std::endl;

  for (size_t i = 0; i < all_segments.size(); ++i) {
    std::vector<std::string> rescaled_steps;

    for (const std::string &segment : all_segments[i]) {
      std::vector<std::string> upscaled = rescale_segment(segment, lcm);
      rescaled_steps.insert(rescaled_steps.end(), upscaled.begin(),
                            upscaled.end());
    }

    std::cout << "  Total steps for instrument " << i << ": "
              << rescaled_steps.size() << std::endl;
    grid.push_back(rescaled_steps);
  }

  size_t num_steps = grid[0].size();
  std::cout << "Total steps in pattern: " << num_steps << std::endl;

  std::vector<std::string> bars;
  std::string current_bar;
  const int steps_per_bar = lcm;

  for (size_t i = 0; i < num_steps; ++i) {
    if (i % steps_per_bar == 0) {
      current_bar = "| ";
      std::cout << "Starting new bar at step " << i << std::endl;
    }

    std::vector<std::string> combined_hits;
    for (size_t j = 0; j < grid.size(); ++j) {
      if (grid[j][i] == "x") {
        combined_hits.push_back(midi_numbers[j]);
      }
    }

    if (!combined_hits.empty()) {
      current_bar += "(";
      for (size_t k = 0; k < combined_hits.size(); ++k) {
        current_bar += combined_hits[k];
        if (k + 1 < combined_hits.size())
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

std::vector<LayerChoices> parse_generative(std::istream &stream) {
  std::vector<LayerChoices> result;
  std::string line;
  LayerChoices current_layer;
  bool in_layer_block = false;

  std::cout << "Starting parse_generative..." << std::endl;

  while (std::getline(stream, line)) {
    std::cout << "Read line: \"" << line << "\"" << std::endl;

    if (line.find("GENERATIVE END") != std::string::npos) {
      std::cout << "Found GENERATIVE END marker. Stopping parse." << std::endl;
      break;
    }

    std::string trimmed = line;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

    if (trimmed.empty()) {
      std::cout << "Skipping empty line after trim." << std::endl;
      continue;
    }

    size_t first_dash = line.find('-');
    size_t leading_spaces = (first_dash != std::string::npos) ? first_dash : 0;

    if (trimmed[0] == '-' && trimmed.find(':') != std::string::npos) {
      if (leading_spaces == 0) {
        std::cout << "Found new layer header: \"" << trimmed << "\""
                  << std::endl;

        if (!current_layer.empty()) {
          std::cout << "Storing current layer with " << current_layer.size()
                    << " entries." << std::endl;
          result.push_back(std::move(current_layer));
          current_layer.clear();
        }

        in_layer_block = true;
      } else {
        auto colon_pos = trimmed.find(':');
        std::string name = trimmed.substr(0, colon_pos);
        std::string count_str = trimmed.substr(colon_pos + 1);

        name.erase(0, name.find_first_not_of(" \t-"));
        // name.erase(name.find_last_not_of(" \t") + 1);
        count_str.erase(0, count_str.find_first_not_of(" \t"));
        count_str.erase(count_str.find_last_not_of(" \t") + 1);

        if (name.empty() or name == " ") {
          name = " ";
          // std::cout << "Skipping entry with empty name." << std::endl;
          // continue;
        }

        try {
          unsigned count = static_cast<unsigned>(std::stoul(count_str));
          std::cout << "Adding (\"" << name << "\", " << count
                    << ") to current layer." << std::endl;
          current_layer.emplace_back(name, count);
        } catch (const std::invalid_argument &) {
          std::cout << "Invalid count \"" << count_str << "\" for entry \""
                    << name << "\", skipping." << std::endl;
        }
      }
    } else {
      std::cout << "Skipping non-layer, non-pattern line: \"" << trimmed << "\""
                << std::endl;
    }
  }

  if (!current_layer.empty()) {
    std::cout << "Storing final layer with " << current_layer.size()
              << " entries." << std::endl;
    result.push_back(std::move(current_layer));
  }

  std::cout << "Finished parsing. Total layers: " << result.size() << std::endl;
  return result;
}

AllSequences duplicate_sequence_elements(const AllSequences &input, int n) {
  AllSequences output;
  output.reserve(input.size());

  for (const auto &sequence : input) {
    Sequence duplicated;
    duplicated.reserve(sequence.size() * n);

    for (const auto &element : sequence) {
      for (int i = 0; i < n; ++i) {
        duplicated.push_back(element);
      }
    }

    output.push_back(std::move(duplicated));
  }

  return output;
}

// Function to sample a string based on weight
std::string sample_string(const LayerChoices &choices, std::mt19937 &rng) {
  unsigned total_weight = 0;
  for (const auto &[_, weight] : choices) {
    total_weight += weight;
  }

  std::uniform_int_distribution<> dist(1, total_weight);
  unsigned r = dist(rng);

  unsigned cumulative = 0;
  for (const auto &[str, weight] : choices) {
    cumulative += weight;
    if (r <= cumulative) {
      return str;
    }
  }

  // Fallback (shouldn't happen if weights are valid)
  return choices.back().first;
}

std::string to_multiline_string(const AllSequences &sequences) {
  std::ostringstream oss;

  for (size_t channel = 0; channel < sequences.size(); ++channel) {
    for (const auto &symbol : sequences[channel]) {
      oss << symbol;
    }
    oss << '\n';
  }

  return oss.str();
}

AllSequences generate_sequences(const std::vector<LayerChoices> &channels,
                                int target_length) {
  AllSequences sequences;
  std::random_device rd;
  std::mt19937 rng(rd());

  for (const auto &channel : channels) {
    Sequence sequence;
    for (int i = 0; i < target_length; ++i) {
      sequence.push_back(sample_string(channel, rng));
    }
    sequences.push_back(sequence);
  }

  return sequences;
}

JamFileData load_jam_file(const std::string &path) {
  std::ifstream file(path);
  std::string line;

  std::stringstream legend_stream;
  std::stringstream patterns_stream;
  std::stringstream arrangement_stream;
  std::stringstream generative_stream;

  std::stringstream *current_stream = nullptr;

  bool manual_arrangement = false;
  while (std::getline(file, line)) {
    if (line_should_be_skipped(line))
      continue;

    if (line.find("LEGEND START") != std::string::npos) {
      current_stream = &legend_stream;
    } else if (line.find("PATTERNS START") != std::string::npos) {
      current_stream = &patterns_stream;
    } else if (line.find("ARRANGEMENT START") != std::string::npos) {
      manual_arrangement = true;
      current_stream = &arrangement_stream;
    } else if (line.find("GENERATIVE START") != std::string::npos) {
      current_stream = &generative_stream;
    } else if (current_stream) {
      *current_stream << line << '\n';
    }
  }

  auto legend_symbol_to_midi_note =
      parse_legend_to_symbol_to_note(legend_stream);
  auto [pattern_name_to_bars, pattern_name_to_channel] =
      parse_patterns(patterns_stream, legend_symbol_to_midi_note);

  auto layers_of_pattern_to_weight = parse_generative(generative_stream);

  std::vector<PatternData> arrangement;
  if (manual_arrangement) {
    arrangement = parse_arrangement(arrangement_stream, pattern_name_to_bars);
  } else { // generative

    int target_length = 20; // temp bad remove me

    AllSequences result =
        generate_sequences(layers_of_pattern_to_weight, target_length);
    AllSequences duplicated_result = duplicate_sequence_elements(result, 4);

    std::string multiline_input = to_multiline_string(duplicated_result);
    multiline_input = "ARRANGEMENT START\nnum_bars_per_block = 4\n" +
                      multiline_input + "\nARRANGEMENT END";
    std::cout << "generated arrangement" << std::endl;
    std::cout << multiline_input << std::endl;
    std::istringstream in_stream(multiline_input);

    std::cout << "before" << std::endl;
    arrangement = parse_arrangement(in_stream, pattern_name_to_bars);
    std::cout << "after" << std::endl;

    // Print the result
    for (size_t i = 0; i < result.size(); ++i) {
      std::cout << "Channel " << i << ": ";
      for (const auto &s : result[i]) {
        std::cout << s << " ";
      }
      std::cout << '\n';
    }

    // Print the result
    for (size_t i = 0; i < duplicated_result.size(); ++i) {
      std::cout << "Channel " << i << ": ";
      for (const auto &s : duplicated_result[i]) {
        std::cout << s << " ";
      }
      std::cout << '\n';
    }
  }

  return {pattern_name_to_bars, pattern_name_to_channel, arrangement,
          layers_of_pattern_to_weight};
}
