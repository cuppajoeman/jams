#include <RtMidi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "miniaudio/miniaudio.h"

#include "jam_file_parsing.hpp"
#include "music_elements.hpp"

std::string midi_to_pitch_class(int midi_note) {
  const int base_midi = 60; // MIDI note number for '0'
  if (midi_note < 0 || midi_note > 127)
    throw std::out_of_range("MIDI note must be between 0 and 127");

  int diff = midi_note - base_midi;
  int pitch_class = (diff % 12 + 12) % 12; // ensures pitch_class is always 0–11
  int octave_shift = diff / 12;

  std::string suffix;
  if (octave_shift > 0) {
    suffix = std::string(octave_shift, '\'');
  } else if (octave_shift < 0) {
    suffix = std::string(-octave_shift, ',');
  }

  return std::to_string(pitch_class) + suffix;
}

struct MIDIEvent {
  std::vector<unsigned char> message;
  double timestamp;
};

std::vector<MIDIEvent> recorded_events;
std::mutex event_mutex;
auto start_time = std::chrono::high_resolution_clock::now();
std::atomic<bool> keep_recording{true};

void midi_callback(double deltatime, std::vector<unsigned char> *message,
                   void *userData) {
  if (!keep_recording.load())
    return;

  std::lock_guard<std::mutex> lock(event_mutex);

  double time_since_start =
      std::chrono::duration<double>(std::chrono::high_resolution_clock::now() -
                                    start_time)
          .count();

  recorded_events.push_back({*message, time_since_start});

  std::cout << "Received MIDI message at " << time_since_start << "s: ";
  for (unsigned char byte : *message) {
    std::cout << std::hex << (int)byte << " ";
  }
  std::cout << std::dec << std::endl;
}

void quantize_events(std::vector<MIDIEvent> &events, double bpm, int total_bars,
                     int subdivisions_per_beat) {

  double seconds_per_beat = 60.0 / bpm;
  double seconds_per_subdiv = seconds_per_beat / subdivisions_per_beat;
  int beats_per_bar = 4;
  int total_beats = total_bars * beats_per_bar;

  // grid: total_beats x subdivisions_per_beat
  std::vector<std::vector<std::vector<int>>> grid(
      total_beats, std::vector<std::vector<int>>(subdivisions_per_beat));

  for (auto &event : events) {
    double q_time =
        std::round(event.timestamp / seconds_per_subdiv) * seconds_per_subdiv;
    int total_subdiv_index = static_cast<int>(q_time / seconds_per_subdiv);

    int beat_index = total_subdiv_index / subdivisions_per_beat;
    int subdiv_index = total_subdiv_index % subdivisions_per_beat;

    if (beat_index >= total_beats)
      continue;

    // Only include Note On messages with velocity > 0
    if (event.message.size() >= 3) {
      unsigned char status = event.message[0];
      unsigned char note = event.message[1];
      unsigned char velocity = event.message[2];

      if ((status & 0xF0) == 0x90 && velocity > 0) {
        grid[beat_index][subdiv_index].push_back(note);
      }
    }

    event.timestamp = q_time;
  }

  std::cout << "\nQuantized MIDI Grid:\n";

  for (int beat = 0; beat < total_beats; ++beat) {
    if (beat % beats_per_bar == 0)
      std::cout << "|";
    const auto &subdivs = grid[beat];
    for (int subdiv = 0; subdiv < subdivisions_per_beat; ++subdiv) {
      const auto &notes = subdivs[subdiv];
      if (notes.empty()) {
        std::cout << "- ";
      } else {
        std::cout << "(";
        for (size_t i = 0; i < notes.size(); ++i) {
          std::cout << midi_to_pitch_class(notes[i]);
          if (i != notes.size() - 1)
            std::cout << " ";
        }
        std::cout << ") ";
      }
    }
    std::cout << "|";
    if ((beat + 1) % beats_per_bar == 0)
      std::cout << "\n";
  }
}

int main() {

  ma_result result;
  ma_engine engine;

  result = ma_engine_init(NULL, &engine);
  if (result != MA_SUCCESS) {
    return -1;
  }

  bool recorder = true;
  recorder = false;

  if (recorder) {

    int num_bars = 4;
    int subdivision = 4;
    double bpm = 120.0;

    std::cout << "Enter number of bars to record: ";
    std::cin >> num_bars;
    std::cout << "Enter subdivision per bar (e.g., 4 for quarter notes, 8 for "
                 "eighth notes): ";
    std::cin >> subdivision;
    std::cout << "Enter BPM: ";
    std::cin >> bpm;

    std::thread metronome_thread([&]() {
      double seconds_per_beat = 60.0 / bpm;
      // NOTE: assuming 4 beats per bar
      double seconds_per_bar = seconds_per_beat * 4;
      double subdivision_duration = seconds_per_bar / subdivision;

      auto metronome_start = std::chrono::high_resolution_clock::now();
      int current_beat = 0;

      // // Metronome for intro (first 4 beats)
      // for (int intro_beat = 0; intro_beat < 4; ++intro_beat) {
      //   auto tick_start = std::chrono::high_resolution_clock::now();
      //
      //   if (intro_beat % subdivision == 0) {
      //     ma_engine_play_sound(&engine, "tock.mp3", NULL);
      //   } else {
      //     ma_engine_play_sound(&engine, "tick.mp3", NULL);
      //   }
      //
      //   std::this_thread::sleep_until(
      //       tick_start +
      //       std::chrono::duration<double>(subdivision_duration));
      // }
      //
      // Start logging after intro (skip the first 4 beats)
      while (keep_recording.load()) {
        auto tick_start = std::chrono::high_resolution_clock::now();

        // Check if it's the first beat of a bar
        if (current_beat % subdivision == 0) {
          ma_engine_play_sound(&engine, "tock.mp3", NULL);
        } else {
          ma_engine_play_sound(&engine, "tick.mp3", NULL);
        }

        current_beat++;
        std::this_thread::sleep_until(
            tick_start + std::chrono::duration<double>(subdivision_duration));
      }
    });

    double seconds_per_bar = (60.0 / bpm) * 4;
    double total_duration = seconds_per_bar * (num_bars + 1);

    try {
      RtMidiIn midiin;

      if (midiin.getPortCount() == 0) {
        std::cerr << "No MIDI input ports available.\n";
        return 1;
      }

      midiin.openPort(0);
      midiin.setCallback(&midi_callback);
      midiin.ignoreTypes(false, false, false);

      start_time = std::chrono::high_resolution_clock::now();
      std::cout << "Recording for " << total_duration << " seconds...\n";

      std::thread timer_thread([&]() {
        std::this_thread::sleep_for(
            std::chrono::duration<double>(total_duration));
        keep_recording = false;
      });

      // Timer will be started right after the intro bar (which doesn’t count as
      // a real bar)

      // main thread just keeps going until keep recording is done
      while (keep_recording) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      timer_thread.join();
      metronome_thread.join();
      std::cout << "Recording finished.\n";

      std::lock_guard<std::mutex> lock(event_mutex);
      quantize_events(recorded_events, bpm, num_bars + 1, subdivision);

    } catch (RtMidiError &error) {
      error.printMessage();
      return 1;
    }

  } else {
    Sequencer sequencer;
    JamFileData jam_data = load_jam_file("song.jam");

    std::cout << "jam file: " << jam_data << std::endl;

    for (const PatternData &data : jam_data.arrangement) {
      const auto &bar_sequence = jam_data.pattern_name_to_bars.at(data.name);
      const auto &bar_channel = jam_data.pattern_name_to_channel.at(data.name);
      Pattern p(bar_sequence, bar_channel, jam_data.bpm, false,
                data.num_repeats, data.start_bar);
      sequencer.add(p);
    }

    sequencer.set_bpm(jam_data.bpm);
    while (true) {
      sequencer.process_current_bar();
    }
  }

  return 0;
}
