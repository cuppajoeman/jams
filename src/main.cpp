#include <RtMidi.h> // Assuming RtMidi is already available
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread> // Include thread for std::this_thread
#include <unordered_map>
#include <vector>

constexpr double epsilon = 1e-3;

unsigned int bpm_new = 60;

struct MidiEvent {
  int note;        // Note value (e.g., MIDI pitch)
  double velocity; // Velocity (0.0 - 1.0), unused if note off
  bool is_note_on; // True if "note on", false if "note off"
  std::chrono::steady_clock::time_point time; // Time of the event
};

class MidiEventNext {
public:
  int channel;
  unsigned int bar_index;
  int note;        // Note value (e.g., MIDI pitch)
  double velocity; // Velocity (0.0 - 1.0), unused if note off
  unsigned int midi_velocity;
  bool is_note_on; // True if "note on", false if "note off"
  std::chrono::duration<double> bar_time_offset_sec;

public:
  MidiEventNext(int channel, unsigned int bar_index, int note, double velocity,
                bool is_note_on,
                std::chrono::duration<double> bar_time_offset_sec =
                    std::chrono::duration<double>(0.0))
      : channel(channel), bar_index(bar_index), note(note), velocity(velocity),
        midi_velocity(static_cast<unsigned int>(velocity * 127)),
        is_note_on(is_note_on), bar_time_offset_sec(bar_time_offset_sec) {}

  friend std::ostream &operator<<(std::ostream &os,
                                  const MidiEventNext &event) {
    double seconds = event.bar_time_offset_sec.count();

    os << "MidiEventNext { "
       << "channel: " << event.channel << ", bar_index: " << event.bar_index
       << ", note: " << event.note << ", velocity: " << std::fixed
       << std::setprecision(2) << event.velocity << ", midi_velocity: " << event.midi_velocity  
       << ", is_note_on: " << (event.is_note_on ? "true" : "false")
       << ", bar_time_offset_sec: " << std::setprecision(3) << seconds << " }";
    return os;
  }
};


class Bar {
public:
  std::vector<MidiEventNext> note_on_midi_events;
  double bar_duration_sec;
  double bar_element_duration_sec;

  bool is_valid_format(const std::string &str) {
    std::regex pattern(R"((\(\s*\d+[',]*\s*(\d+[',]*\s*)*\)\s*)+)");
    return std::regex_match(str, pattern);
  }

  int apply_octave_modifiers(const std::string &note_str) {
    std::regex base_note_regex(R"((\d+)([',]*))");
    std::smatch match;
    if (std::regex_match(note_str, match, base_note_regex)) {
      int note = std::stoi(match[1].str());
      const std::string &modifiers = match[2].str();

      int mod = 0;
      for (char c : modifiers) {
        if (c == '\'')
          mod += 12;
        else if (c == ',')
          mod -= 12;
      }
      return note + mod;
    }
    return -1; // Invalid
  }

public:
  Bar(const std::string &pattern, int channel) {
    if (channel < 1 || channel > 16) {
      std::cerr << "Invalid channel number! Defaulting to channel 1.\n";
      channel = 1;
    }

    if (!is_valid_format(pattern)) {
      std::cerr << "Invalid pattern format!\n";
      return;
    }

    // Match each parenthesized group
    std::regex group_regex(R"(\(([^)]*)\))");
    auto groups_begin =
        std::sregex_iterator(pattern.begin(), pattern.end(), group_regex);
    auto groups_end = std::sregex_iterator();

    int num_matches = std::distance(groups_begin, groups_end);

    bar_duration_sec = (double)60 / bpm_new;
    bar_element_duration_sec = bar_duration_sec / num_matches;

        std::cout << "bar_element_duration_sec " << bar_element_duration_sec  << std::endl;

    std::regex note_regex(R"(\d+[',]*)");

    unsigned int bar_index = 0;
    for (std::sregex_iterator it = groups_begin; it != groups_end; ++it) {
      std::string group_content = (*it)[1].str(); // inside the parens

      auto notes_begin = std::sregex_iterator(group_content.begin(),
                                              group_content.end(), note_regex);
      auto notes_end = std::sregex_iterator();

      for (std::sregex_iterator nit = notes_begin; nit != notes_end; ++nit) {
        int note = apply_octave_modifiers(nit->str());
        int midi_note = note + 60;
        auto time_offset =
            std::chrono::duration<double>(bar_index * bar_element_duration_sec);
        MidiEventNext note_on_me(channel, bar_index, midi_note, 0.5, true,
                                 time_offset);
        std::cout << "event: " << note_on_me << std::endl;
        note_on_midi_events.push_back(note_on_me);
      }

      bar_index++;
    }
  }

  friend std::ostream &operator<<(std::ostream &os, const Bar &bar) {
    os << "Bar {\n";
    for (const auto &event : bar.note_on_midi_events) {
      os << "  " << event << "\n";
    }
    os << "}";
    return os;
  }
};

class BarSequence {
  bool loop_forever;
  unsigned int num_repetitions;
  unsigned int channel;

  std::string trim(const std::string &s) {
    auto begin = s.begin();
    while (begin != s.end() && std::isspace(*begin))
      ++begin;

    auto end = s.end();
    do {
      --end;
    } while (std::distance(begin, end) > 0 && std::isspace(*end));

    return std::string(begin, end + 1);
  }

public:
  std::vector<Bar> bars;
  BarSequence(const std::string &bar_sequence_str, unsigned int channel,
              bool loop_forever, unsigned int num_repetitions = 0)
      : loop_forever(loop_forever), num_repetitions(num_repetitions),
        channel(channel) {

    std::stringstream ss(bar_sequence_str);
    std::string bar_str;

    while (std::getline(ss, bar_str, '|')) {
      bar_str = trim(bar_str);
      if (!bar_str.empty()) {
        bars.emplace_back(bar_str, channel);
      }
    }
  }

  friend std::ostream &operator<<(std::ostream &os, const BarSequence &seq) {
    os << "BarSequence {\n"
       << "  loop_forever: " << (seq.loop_forever ? "true" : "false") << ",\n"
       << "  num_repetitions: " << seq.num_repetitions << ",\n"
       << "  channel: " << seq.channel << ",\n"
       << "  bars: [\n";

    for (const auto &bar : seq.bars) {
      os << "    " << bar << ",\n";
    }

    os << "  ]\n"
       << "}";
    return os;
  }
};

struct NoteCollectionSequence {
  int channel;
  // the times are measrued
  std::vector<MidiEvent> relative_midi_events;
};

size_t count_bar_elements(const std::string &bar) {
  size_t count = 0;

  for (size_t i = 0; i < bar.size(); ++i) {
    char c = bar[i];

    if (c == 'x' or c == '-') {
      ++count;
    } else if (c == '(') {
      // Skip to the closing ')'
      size_t j = i + 1;
      while (j < bar.size() && bar[j] != ')') {
        ++j;
      }
      i = j;   // Move to closing parenthesis
      ++count; // Count this group as one step
    }
  }

  return count;
}

void print_channel_to_note_events(
    const std::unordered_map<int, std::vector<MidiEvent>>
        &channel_to_note_events,
    const std::chrono::steady_clock::time_point &bar_start_time) {

  const double threshold = 1e-6; // Adjust this threshold as needed

  for (const auto &entry : channel_to_note_events) {
    std::cout << "Channel: " << entry.first << std::endl;
    for (const auto &event : entry.second) {
      auto relative_time =
          std::chrono::duration_cast<std::chrono::duration<double>>(
              event.time - bar_start_time)
              .count();

      // If the relative time is smaller than the threshold, print 0
      if (std::abs(relative_time) < threshold) {
        relative_time = 0;
      }

      std::cout << "  Time: " << relative_time << " s, "
                << "Note: " << event.note << ", "
                << "Velocity: " << event.velocity << ", "
                << (event.is_note_on ? "Note On" : "Note Off") << std::endl;
    }
  }
}

class Sequencer {
public:
  std::vector<BarSequence> bar_sequences;

  Sequencer() {
    try {
      midi_out = std::make_unique<RtMidiOut>();
      if (midi_out->getPortCount() > 0) {
        midi_out->openPort(0); // open first available port
      } else {
        midi_out->openVirtualPort("Sequencer Output");
      }
    } catch (RtMidiError &error) {
      std::cerr << "MIDI setup error: " << error.getMessage() << std::endl;
      exit(1);
    }
  }

  void add(const BarSequence &bar_seq) { bar_sequences.push_back(bar_seq); }

  void add_sequence(const std::string &pattern, int channel) {
    std::lock_guard<std::mutex> lock(mutex);
    if (channel < 1 || channel > 16) {
      std::cerr << "Invalid channel number!" << std::endl;
      return;
    }

    // Split pattern into bars and add to the sequences map
    std::vector<std::string> bars;
    std::string current_bar;
    for (char c : pattern) {
      if (c == '|') {
        if (!current_bar.empty()) {
          bars.push_back(current_bar);
          current_bar.clear();
        }
      } else {
        current_bar += c;
      }
    }
    if (!current_bar.empty()) {
      bars.push_back(current_bar); // Add last bar if any
    }

    channel_to_pattern_bars[channel - 1] = bars;
    current_ticks[channel - 1] = 0;
  }

  void add_note_collection_sequence(const std::string &pattern, int channel) {
    std::lock_guard<std::mutex> lock(mutex);
    if (channel < 1 || channel > 16) {
      std::cerr << "Invalid channel number!" << std::endl;
      return;
    }

    // Split pattern into bars and add to the sequences map
    std::vector<std::string> bars;
    std::string current_bar;
    for (char c : pattern) {
      if (c == '|') {
        if (!current_bar.empty()) {
          bars.push_back(current_bar);
          current_bar.clear();
        }
      } else {
        current_bar += c;
      }
    }
    if (!current_bar.empty()) {
      bars.push_back(current_bar); // Add last bar if any
    }

    channel_to_pattern_bars[channel - 1] = bars;
    current_ticks[channel - 1] = 0;
  }

  void set_bpm(double bpm) {
    using namespace std::chrono;
    tick_duration = duration_cast<nanoseconds>(duration<double>(60.0 / bpm));

    // Convert tick_duration back to seconds (as double) for printing
    double seconds = duration<double>(tick_duration).count();
    std::cout << "Tick duration: " << seconds << " seconds\n";
  }

  void tick() {
    using namespace std::chrono;

    std::chrono::steady_clock::time_point bar_start_time =
        std::chrono::steady_clock::now();

    std::chrono::steady_clock::time_point next_bar_time =
        bar_start_time + tick_duration;

    std::chrono::duration<double> elapsed_seconds =
        next_bar_time - bar_start_time;

    std::cout << "Elapsed bar time: " << elapsed_seconds.count()
              << " seconds\n";

    // Precompute note on/off times for all active channels for the first bar
    std::unordered_map<int, std::vector<MidiEvent>>
        channel_to_current_bar_midi_events;

    recompute_note_events(channel_to_current_bar_midi_events);

    print_channel_to_note_events(channel_to_current_bar_midi_events,
                                 bar_start_time);

    // Start processing the bar
    while (steady_clock::now() < next_bar_time) {
      steady_clock::time_point now = steady_clock::now();

      // Handle note events for each channel
      for (int channel = 1; channel <= 16; ++channel) {
        if (channel_to_current_bar_midi_events.find(channel) ==
                channel_to_current_bar_midi_events.end() ||
            channel_to_current_bar_midi_events[channel].empty())
          continue;

        for (auto &event : channel_to_current_bar_midi_events[channel]) {
          auto event_time_ms =
              duration_cast<milliseconds>(event.time.time_since_epoch())
                  .count();
          auto now_time_ms =
              duration_cast<milliseconds>(now.time_since_epoch()).count();

          if (event.time <= now) {
            if (event.is_note_on) {
              // Play note on
              send_note_on(event.note, 50,
                           channel); // Assuming note and velocity
            } else {
              // Turn note off
              send_note_off(event.note, channel);
            }

            // Mark event as processed
            event.time = steady_clock::time_point::max(); // Processed
          }
        }
      }

      std::this_thread::sleep_for(milliseconds(1));
    }

    std::cout << "just finished a bar" << std::endl;

    // Move to the next bar after current one finishes
    bar_start_time = next_bar_time;
    next_bar_time =
        bar_start_time +
        std::chrono::duration_cast<std::chrono::nanoseconds>(tick_duration);

    // Move to the next tick for each channel
    for (int channel = 1; channel <= 16; ++channel) {
      if (!channel_to_pattern_bars[channel - 1].empty()) {
        ++current_ticks[channel - 1];
      }
    }
  }

  unsigned int current_bar_index = 0;

  void process_current_bar() {
    using namespace std::chrono;

    steady_clock::time_point bar_start_time = steady_clock::now();
    steady_clock::time_point next_bar_time = bar_start_time + tick_duration;

    std::cout << "Tick duration: "
              << std::chrono::duration_cast<std::chrono::duration<double>>(
                     tick_duration)
                     .count()
              << " seconds\n";

    // Custom hash for time_point to use in unordered_map
    struct time_point_hash {
      std::size_t operator()(const steady_clock::time_point &tp) const {
        return std::hash<std::int64_t>()(
            duration_cast<nanoseconds>(tp.time_since_epoch()).count());
      }
    };

    // Change unordered_map to map time_point to a vector of MidiEventNext
    std::unordered_map<steady_clock::time_point, std::vector<MidiEventNext>,
                       time_point_hash>
        time_to_midi_events;

    // Build the time_to_midi_events map
    for (auto &bar_seq : bar_sequences) {
      std::cout << "Processing bar sequence...\n";
      const auto &current_bar =
          bar_seq.bars[current_bar_index % bar_seq.bars.size()];

      std::cout << "Current bar index: " << current_bar_index
                << ", Number of bars in sequence: " << bar_seq.bars.size()
                << '\n';

      for (const auto &note_on_event : current_bar.note_on_midi_events) {
        steady_clock::time_point note_on_time =
            bar_start_time + duration_cast<steady_clock::duration>(
                                 note_on_event.bar_time_offset_sec);

        std::cout << "Note ON event - Note: " << note_on_event.note
                  << ", Time: " << note_on_time.time_since_epoch().count()
                  << " ns\n";

        // Add note on event
        time_to_midi_events[note_on_time].push_back(note_on_event);

        // Create and add note off event
        steady_clock::time_point note_off_time =
            note_on_time + std::chrono::duration_cast<steady_clock::duration>(
                               std::chrono::duration<double>(
                                   current_bar.bar_element_duration_sec - epsilon));

        MidiEventNext note_off_event(note_on_event.channel, note_on_event.bar_index, note_on_event.note,
                                      0, false);

        std::cout << "Note OFF event - Note: " << note_on_event.note
                  << ", Time: " << note_off_time.time_since_epoch().count()
                  << " ns\n";

        time_to_midi_events[note_off_time].push_back(note_off_event);
      }
    }

    std::cout << "Time to MIDI events map built. Total events: "
              << time_to_midi_events.size() << '\n';

    // Process all MIDI events scheduled within this bar
    while (steady_clock::now() < next_bar_time) {
      steady_clock::time_point now = steady_clock::now();

      // Iterate over all events and trigger those that are due
      for (auto it = time_to_midi_events.begin();
           it != time_to_midi_events.end();) {
        if (it->first <= now) {
          for (const MidiEventNext &event : it->second) {
            if (event.is_note_on) {
              send_note_on(event.note, event.midi_velocity,
                           event.channel); // Assume velocity 50
              std::cout << "Sending NOTE ON: " << event << "\n";
            } else {
              send_note_off(event.note, event.channel);
              std::cout << "Sending NOTE OFF: " << event << "\n";
            }
          }

          // Erase processed events for the time point
          it = time_to_midi_events.erase(it);
        } else {
          ++it;
        }
      }

      std::this_thread::sleep_for(milliseconds(1));
    }

        current_bar_index ++;
    std::cout << "Just finished a bar\n";
  }

private:
  void recompute_note_events(
      std::unordered_map<int, std::vector<MidiEvent>> &channel_to_note_events) {
    using namespace std::chrono;

    std::cout << "[recompute_note_events] Start recomputing note events.\n";

    steady_clock::time_point current_time = steady_clock::now();

    // For each active channel, compute the note on and off times for the
    // current bar
    for (int channel = 1; channel <= 16; ++channel) {
      if (channel_to_pattern_bars[channel - 1].empty())
        continue;

      std::cout << "  Processing channel " << channel << "...\n";

      // the problem is here with current ticks
      const std::string &current_bar =
          channel_to_pattern_bars[channel - 1]
                                 [current_ticks[channel - 1] %
                                  channel_to_pattern_bars[channel - 1].size()];

      std::cout << "processing bar: " << current_bar << std::endl;

      double substep_seconds =
          duration_cast<duration<double>>(tick_duration).count() /
          count_bar_elements(current_bar);

      std::cout << "sss: " << substep_seconds << std::endl;

      steady_clock::time_point bar_start_time = current_time;

      size_t bar_index = 0;

      for (size_t i = 0; i < current_bar.size(); ++i) {
        char bar_symbol = current_bar[i];

        if (bar_symbol == '-') {
          bar_index++;
          continue;
        }

        steady_clock::time_point note_on_time =
            bar_start_time + duration_cast<steady_clock::duration>(
                                 duration<double>(bar_index * substep_seconds));
        std::cout << "just computed note on time with bar_index = " << bar_index
                  << std::endl;

        steady_clock::time_point note_off_time =
            note_on_time + duration_cast<steady_clock::duration>(
                               duration<double>(substep_seconds - epsilon));

        float velocity = 0.4;
        if (bar_symbol == 'x') {
          MidiEvent note_on_event = {/*note*/ 60, /*velocity*/ velocity,
                                     /*is_note_on*/ true, note_on_time};
          MidiEvent note_off_event = {/*note*/ 60, /*velocity*/ velocity,
                                      /*is_note_on*/ false, note_off_time};

          channel_to_note_events[channel].push_back(note_on_event);
          std::cout << "    Added NOTE ON  (note: 60) at time "
                    << duration_cast<milliseconds>(note_on_time - current_time)
                           .count()
                    << " ms\n";

          channel_to_note_events[channel].push_back(note_off_event);
          std::cout << "    Added NOTE OFF (note: 60) at time "
                    << duration_cast<milliseconds>(note_off_time - current_time)
                           .count()
                    << " ms\n";

          ++bar_index; // advance only when a time step is used
        } else if (bar_symbol == '(') {

          size_t j = i + 1;
          std::vector<int> notes;
          while (j < current_bar.size() && current_bar[j] != ')') {
            if (isdigit(current_bar[j]) || current_bar[j] == '-') {
              int start = j;
              while (j < current_bar.size() &&
                     (isdigit(current_bar[j]) || current_bar[j] == '-')) {
                ++j;
              }
              int note = std::stoi(current_bar.substr(start, j - start));

              int modification = 0;
              while (j < current_bar.size() &&
                     (current_bar[j] == '\'' || current_bar[j] == ',')) {
                if (current_bar[j] == '\'') {
                  modification += 12;
                } else if (current_bar[j] == ',') {
                  modification -= 12;
                }
                ++j;
              }

              note += modification;
              notes.push_back(note);
            } else {
              ++j;
            }
          }

          for (int note : notes) {
            note += 60;
            MidiEvent note_on_event = {note, velocity, true, note_on_time};
            MidiEvent note_off_event = {note, velocity, false, note_off_time};

            channel_to_note_events[channel].push_back(note_on_event);
            std::cout << "    Added NOTE ON  (note: " << note << ") at time "
                      << duration_cast<milliseconds>(note_on_time -
                                                     current_time)
                             .count()
                      << " ms\n";

            channel_to_note_events[channel].push_back(note_off_event);
            std::cout << "    Added NOTE OFF (note: " << note << ") at time "
                      << duration_cast<milliseconds>(note_off_time -
                                                     current_time)
                             .count()
                      << " ms\n";
          }

          i = j;       // Skip to after closing ')'
          ++bar_index; // Treat group as one time step
        }
      }
    }

    std::cout << "[recompute_note_events] Done recomputing note events.\n";
  }

  void send_note_on(int note, int velocity = 100, int channel = 1) {
    if (channel < 1 || channel > 16) {
      return; // Handle invalid channel number
    }
    // MIDI message format: 0x90 + channel (0-indexed), note, velocity
    std::vector<unsigned char> message = {
        static_cast<unsigned char>(0x90 + (channel - 1)),
        static_cast<unsigned char>(note), static_cast<unsigned char>(velocity)};
    midi_out->sendMessage(&message);
  }

  void send_note_off(int note, int channel = 1) {
    if (channel < 1 || channel > 16) {
      return; // Handle invalid channel number
    }
    // MIDI message format: 0x80 + channel (0-indexed), note, 0 (velocity)
    std::vector<unsigned char> message = {
        static_cast<unsigned char>(0x80 + (channel - 1)),
        static_cast<unsigned char>(note), 0};
    midi_out->sendMessage(&message);
  }

  std::unique_ptr<RtMidiOut> midi_out;
  std::unordered_map<int, std::vector<std::string>>
      channel_to_pattern_bars; // Map of sequences for each channel
  std::unordered_map<int, size_t>
      current_ticks; // Map of current tick for each channel
  std::chrono::nanoseconds tick_duration{
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::duration<double>{0.5})};
  std::mutex mutex;
};

int main() {

  Sequencer sequencer;
  // sequencer.add_sequence("x-x-|x-x-|x-x-|x-x-", 1);
  // sequencer.add_sequence("x-x-|--x-|--x-|--x-", 2);
  // sequencer.add_sequence("----|x---|x---|x---", 3);

  // sequencer.add_note_collection_sequence(
  //     // "| (0 4 7 11 | (9, 0 4 7) | (2,, 5 9 0) | (7, 11 2 5) |", 1);
  //     "| (0 4 7 11) | (9, 0 4 7) | (2,, 5 9 0) | (7, 11 2 5) | (4, 7 11 2) |
  //     "
  //     "(9, 1 4 7) | (2 5 9 0) | (7, 11 2 5 8) ",
  //     1);

  // sequencer.add_note_collection_sequence(
  //     "| (0,,,) (1,,,) (2,,,) (3,,,) | (4,,,) (5,,,) (6,,,) (7,,,) | (8,,,)
  //     (9,,,) (10,,,) (11,,,) | "
  //     "| (0,,) (1,,) (2,,) (3,,) | (4,,) (5,,) (6,,) (7,,) | (8,,) (9,,)
  //     (10,,) (11,,) | "
  //     "| (0,) (1,) (2,) (3,) | (4,) (5,) (6,) (7,) | (8,) (9,) (10,) (11,) |
  //     "
  //     "| (0) (1) (2) (3) | (4) (5) (6) (7) | (8) (9) (10) (11) | "
  //     "| (0') (1') (2') (3') | (4') (5') (6') (7') | (8') (9') (10') (11') |
  //     "
  //     "| (0'') (1'') (2'') (3'') | (4'') (5'') (6'') (7'') | (8'') (9'')
  //     (10'') (11'') | "
  //     "| (0''') (1''') (2''') (3''') | (4''') (5''') (6''') (7''') | (8''')
  //     (9''') (10''') (11''') | ", 1);

  // sequencer.add_note_collection_sequence(
  //     " (8''') (9''') (10''') (11''') ",
  //     1);

  // sequencer.add_note_collection_sequence(
  //     "(0, 2 7 4') | (2 7 9 0' 2'') | (0, 2 9 11 4') | (2 5 4' 9 2)  ", 1);
  //
  // sequencer.add_note_collection_sequence(
  //     "(0) | (2) | (9) | (4) | (2)  (4) | (11) | (0')  (7) | (2)", 2);
  //
  // // sequencer.add_sequence("x-x|x-x-|x-x|x-x-", 3);
  // sequencer.add_sequence("xxx|x-x|xxxx|x-x-", 3);

  // Bar b("(5' 2,, 3'') (0 4 7 11)", 2);
  // BarSequence bs("(5 2 4) (0 4 7 11) | (2 5 9 0)", 1, true);
   
  BarSequence bs("(0, 2 7 4') | (2 7 9 0' 2'') | (0, 2 9 11 4') | (2 5 4' 9 2)  ", 1, true);
  BarSequence bs2("(0) | (2'') | (4') | (7) | (11,)", 3, true);

  sequencer.add(bs);
  sequencer.add(bs2);

  std::cout << "bs: " << bs << std::endl;

  sequencer.set_bpm(60);
  while (true) {
    sequencer.process_current_bar();
  }

  return 0;
}
