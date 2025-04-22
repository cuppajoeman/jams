#ifndef MUSIC_ELEMENTS_HPP
#define MUSIC_ELEMENTS_HPP

#include <RtMidi.h>
#include <chrono>
#include <cstddef>
#include <functional>
#include <iostream>
#include <mutex>
#include <regex>
#include <string>
#include <thread>

#include "rt_midi_utils/rt_midi_utils.hpp"

constexpr double epsilon = 1e-3;
unsigned int bpm_new = 120;

// Custom hash for time_point to use in unordered_map
struct time_point_hash {
  std::size_t
  operator()(const std::chrono::steady_clock::time_point &tp) const {
    return std::hash<std::int64_t>()(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            tp.time_since_epoch())
            .count());
  }
};

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
       << std::setprecision(2) << event.velocity
       << ", midi_velocity: " << event.midi_velocity
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
    // (?:\s*\d+[',]*\s*)+: use a non-matching group for  2,, 3' 99' (at least
    // one number in there)
    // \( ... \) wrap literal parens around it
    // or just a literal hiphen
    // wrap the whole thing in (...\s*)+ match one more more of these objects
    // with spaces between them
    std::regex pattern(R"((\s*((\((?:\s*\d+[',]*\s*)+\))|-)\s*)+)");
    // std::cout << str << std::endl;
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

    std::regex p(R"(\((?:\s*\d+[',]*\s*)*\)|-)");

    // Match each parenthesized group or hiphen
    std::regex group_regex(R"(\(([^)]*)\)|-)");
    auto groups_begin =
        std::sregex_iterator(pattern.begin(), pattern.end(), group_regex);
    auto groups_end = std::sregex_iterator();

    int num_matches = std::distance(groups_begin, groups_end);

    bar_duration_sec = (double)60 / bpm_new;
    bar_element_duration_sec = bar_duration_sec / num_matches;

    //std::cout << "bar_element_duration_sec " << bar_element_duration_sec
    //          << std::endl;

    std::regex note_regex(R"(\d+[',]*)");

    unsigned int bar_index = 0;
    for (std::sregex_iterator it = groups_begin; it != groups_end; ++it) {
      std::string group_content = (*it)[1].str(); // inside the parens

      if (group_content == "-") {
        // it's a rhythmic rest
        continue;
      }

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
        // std::cout << "event: " << note_on_me << std::endl;
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

class Pattern {
public:
  bool loop_forever; // loops forever starting from the start index? right now
                     // it starts from 0, kinda bad
  unsigned int num_repetitions;
  unsigned int current_repetition = 0;
  unsigned int channel;
  unsigned int start_bar_index;

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

  bool can_play_bar_from_bar_sequence(unsigned int bar_index) {

    if (loop_forever) {
      return true;
    }

    // if we had | (0 2 4 7) (4) | (2) (5) |, then this bar seq has two bars,
    // suppose we want to play it for two repetitions starting from bar 0
    // then our last bar to play on would be 3, therefore in the equation below
    // we have 0 + 2 * 2 - 1 = 3, which explains the -1
    unsigned int last_bar_to_play_on =
        start_bar_index + num_repetitions * bars.size() - 1;

    return start_bar_index <= bar_index and bar_index <= last_bar_to_play_on;
  }

  Pattern(const std::string &bar_sequence_str, unsigned int channel,
          bool loop_forever, unsigned int num_repetitions = 0,
          unsigned int start_bar_index = 0)
      : loop_forever(loop_forever), num_repetitions(num_repetitions),
        channel(channel), start_bar_index(start_bar_index) {
    parse_and_store_bars({bar_sequence_str});
  }

  Pattern(const std::vector<std::string> &bar_sequence_vec,
          unsigned int channel, bool loop_forever,
          unsigned int num_repetitions = 0, unsigned int start_bar_index = 0)
      : loop_forever(loop_forever), num_repetitions(num_repetitions),
        channel(channel), start_bar_index(start_bar_index) {
    parse_and_store_bars(bar_sequence_vec);
  }

  friend std::ostream &operator<<(std::ostream &os, const Pattern &seq) {
    os << "Pattern {\n"
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

private:
  void parse_and_store_bars(const std::vector<std::string> &bar_sequences) {
    for (const auto &bar_seq : bar_sequences) {
      std::stringstream ss(bar_seq);
      std::string bar_str;

      while (std::getline(ss, bar_str, '|')) {
        bar_str = trim(bar_str);
        if (!bar_str.empty()) {
          bars.emplace_back(bar_str, channel);
        }
      }
    }
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
  std::vector<Pattern> bar_sequences;
  unsigned int largest_end_bar_for_any_pattern = 0;

  Sequencer() {
    RtMidiOut *raw_midi_out = nullptr;
    if (!initialize_midi_output(raw_midi_out)) {
      std::cerr << "MIDI setup error: Failed to initialize MIDI output\n";
      exit(1);
    }

    midi_out = std::unique_ptr<RtMidiOut>(raw_midi_out);
  }

  void pause() {
    std::lock_guard<std::mutex> lock(mutex);
    is_paused = true;
    std::cout << "Sequencer paused.\n";
  }

  void resume() {
    std::lock_guard<std::mutex> lock(mutex);
    is_paused = false;
    std::cout << "Sequencer resumed.\n";
  }

  void reset_to_start() {
    std::lock_guard<std::mutex> lock(mutex);
    sequencer_bar_index = 0;
    for (auto &seq : bar_sequences) {
      seq.current_repetition = 0;
    }
    std::cout << "Sequencer reset to start.\n";
  }

  void clear_all_data() {
    std::lock_guard<std::mutex> lock(mutex);
    bar_sequences.clear();
    largest_end_bar_for_any_pattern = 0;
    sequencer_bar_index = 0;
    std::cout << "Sequencer cleared.\n";
  }

  void add(const Pattern &bar_seq) {
    bar_sequences.push_back(bar_seq);
    auto end_bar_index = bar_seq.start_bar_index + bar_seq.bars.size();
    if (end_bar_index > largest_end_bar_for_any_pattern)
      largest_end_bar_for_any_pattern = end_bar_index;
  }

  void set_bpm(double bpm) {
    using namespace std::chrono;
    tick_duration = duration_cast<nanoseconds>(duration<double>(60.0 / bpm));

    // Convert tick_duration back to seconds (as double) for printing
    double seconds = duration<double>(tick_duration).count();
    std::cout << "Tick duration: " << seconds << " seconds\n";
  }

  unsigned int sequencer_bar_index = 0;

  std::unordered_map<std::chrono::steady_clock::time_point,
                     std::vector<MidiEventNext>, time_point_hash>
  generate_note_events_for_current_bar_for_all_bar_sequences(
      std::chrono::steady_clock::time_point bar_start_time) {

    std::unordered_map<std::chrono::steady_clock::time_point,
                       std::vector<MidiEventNext>, time_point_hash>
        time_to_midi_events;

    // Build the time_to_midi_events map
    for (auto &bar_seq : bar_sequences) {

      if (not bar_seq.can_play_bar_from_bar_sequence(sequencer_bar_index)) {
        continue;
      }

      // std::cout << "Processing bar sequence...\n";
      const auto &current_bar =
          bar_seq.bars[sequencer_bar_index % bar_seq.bars.size()];

      // std::cout << "Current bar index: " << sequencer_bar_index
      //           << ", Number of bars in sequence: " << bar_seq.bars.size()
      //           << '\n';

      for (const auto &note_on_event : current_bar.note_on_midi_events) {
        std::chrono::steady_clock::time_point note_on_time =
            bar_start_time +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                note_on_event.bar_time_offset_sec);

        // std::cout << "Note ON event - Note: " << note_on_event.note
        //           << ", Time: " << note_on_time.time_since_epoch().count()
        //           << " ns\n";

        // Add note on event
        time_to_midi_events[note_on_time].push_back(note_on_event);

        // Create and add note off event
        std::chrono::steady_clock::time_point note_off_time =
            note_on_time +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(
                    current_bar.bar_element_duration_sec - epsilon));

        MidiEventNext note_off_event(note_on_event.channel,
                                     note_on_event.bar_index,
                                     note_on_event.note, 0, false);

        // std::cout << "Note OFF event - Note: " << note_on_event.note
        //           << ", Time: " << note_off_time.time_since_epoch().count()
        //           << " ns\n";

        time_to_midi_events[note_off_time].push_back(note_off_event);
      }

      if (sequencer_bar_index != 0 and
          sequencer_bar_index % bar_seq.bars.size() == 0)
        bar_seq.current_repetition++;
    }

    return time_to_midi_events;
  }

  void process_current_bar() {
    using namespace std::chrono;

    if (is_paused) {
      // std::cout << "Sequencer is paused. Skipping bar processing.\n";
      return;
    }

    steady_clock::time_point bar_start_time = steady_clock::now();
    steady_clock::time_point next_bar_time = bar_start_time + tick_duration;

    // std::cout << "processing bar: " << sequencer_bar_index << std::endl;
    // std::cout << "Tick duration: "
    //           << std::chrono::duration_cast<std::chrono::duration<double>>(
    //                  tick_duration)
    //                  .count()
    //           << " seconds\n";

    // Change unordered_map to map time_point to a vector of MidiEventNext
    std::unordered_map<steady_clock::time_point, std::vector<MidiEventNext>,
                       time_point_hash>
        time_to_midi_events =
            generate_note_events_for_current_bar_for_all_bar_sequences(
                bar_start_time);

    // std::cout << "Time to MIDI events map built. Total events: "
    //           << time_to_midi_events.size() << '\n';

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
              // std::cout << "Sending NOTE ON: " << event << "\n";
            } else {
              send_note_off(event.note, event.channel);
              // std::cout << "Sending NOTE OFF: " << event << "\n";
            }
          }

          // Erase processed events for the time point
          it = time_to_midi_events.erase(it);
        } else {
          ++it;
        }
      }

      // std::this_thread::sleep_for(milliseconds(1));
    }

    sequencer_bar_index++;
    sequencer_bar_index %= largest_end_bar_for_any_pattern;
    // std::cout << "Just finished a bar\n";
  }

private:
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
  std::chrono::nanoseconds tick_duration{
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::duration<double>{0.5})};
  std::mutex mutex;
  bool is_paused = false;
};

#endif // MUSIC_ELEMENTS_HPP
