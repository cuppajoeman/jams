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

#include "jam_file_parsing.hpp"
#include "music_elements.hpp"

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
  //

  std::vector<std::string> seq = {
      "(0, 2 7 4') | (2 7 9 0' 2'') | (0, 2 9 11 4') | (2 5 4' 9 2)  ",
      "(2 4 9 0') | (2 7 9 0' 2'') | (5, 2 0' 4') | (0,, 4, 7 4' 2'')  "};

  std::vector<std::string> low_seq = {"(0,,) | (2,,) | (9,,,) | (7,,,)  ",
                                      "(4,,) | (2,,) | (5,,) | (0,,)  "};

  // BarSequence bs(seq, 1, false, 2);
  // bs.start_bar_index = 2 * bs.bars.size();
  // BarSequence bs_low(low_seq, 1, false, 4, 0);
  //
  // sequencer.add(bs);
  // sequencer.add(bs_low);
  //
  // sequencer.set_bpm(60);
  // while (true) {
  //   sequencer.process_current_bar();
  // }

  // TODO was working on this jam file and making sure that the | (...) - |
  // syntax is accepted, so we need to do that as well, once that's in we have a
  // vector of strings, in otherwords what we have above and thus we can pass
  // those in to create bar sequences?
  load_jam_file("song.jam");

  return 0;
}
