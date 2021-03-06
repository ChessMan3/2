/*
  SugaR, a UCI chess playing engine derived from Stockfish
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2017 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  SugaR is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  SugaR is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cassert>
#include <ostream>

#include <thread>
#include "evaluate.h"
#include "misc.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

using std::string;

UCI::OptionsMap Options; // Global object

namespace UCI {

/// 'On change' actions, triggered by an option's value change
void on_clear_hash(const Option&) { Search::clear(); }
void on_eval(const Option&) { Eval::init(); }
void on_hash_size(const Option& o) { TT.resize(o); }
void on_large_pages(const Option& o) { TT.resize(o); }  // warning is ok, will be removed
void on_logger(const Option& o) { start_logger(o); }
void on_threads(const Option&) { Threads.read_uci_options(); }
void on_tb_path(const Option& o) { Tablebases::init(o); }


/// Our case insensitive less() function as required by UCI protocol
bool CaseInsensitiveLess::operator() (const string& s1, const string& s2) const {

  return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(),
         [](char c1, char c2) { return tolower(c1) < tolower(c2); });
}


/// init() initializes the UCI options to their hard-coded default values

void init(OptionsMap& o) {

  const int MaxHashMB = Is64Bit ? 1024 * 1024 : 2048;

  unsigned int n = std::thread::hardware_concurrency();
  if (!n) n = 1;
  
  o["Tactical Mode"]          << Option(false);
  o["Debug Log File"]         << Option("", on_logger);
  o["Contempt"]               << Option(0, -100, 100);
  o["Threads"]                << Option(n, 1, 512, on_threads);
  o["Hash"]                   << Option(16, 1, MaxHashMB, on_hash_size);
  o["Clear Hash"]             << Option(on_clear_hash);
  o["Ponder"]                 << Option(false);
  o["Material(mg)"]           << Option(100, 0, 300, on_eval);
  o["Material(eg)"]           << Option(100, 0, 300, on_eval);
  o["Imbalance(mg)"]          << Option(100, 0, 300, on_eval);
  o["Imbalance(eg)"]          << Option(100, 0, 300, on_eval);
  o["PawnStructure(mg)"]      << Option(100, 0, 300, on_eval);
  o["PawnStructure(eg)"]      << Option(100, 0, 300, on_eval);
  o["Mobility(mg)"]           << Option(100, 0, 300, on_eval);
  o["Mobility(eg)"]           << Option(100, 0, 300, on_eval);
  o["PassedPawns(mg)"]        << Option(100, 0, 300, on_eval);
  o["PassedPawns(eg)"]        << Option(100, 0, 300, on_eval);
  o["KingSafety(mg)"]         << Option(100, 0, 300, on_eval);
  o["KingSafety(eg)"]         << Option(100, 0, 300, on_eval);
  o["Threats(mg)"]            << Option(100, 0, 300, on_eval);
  o["Threats(eg)"]            << Option(100, 0, 300, on_eval);
  o["Space"]                  << Option(100, 0, 300, on_eval);
  o["Razoring"]               << Option(true);
  o["Futility"]               << Option(true);
  o["NullMove"]               << Option(true);
  o["ProbCut"]                << Option(true);
  o["Pruning"]                << Option(true);
  o["LMR"]                    << Option(true);
  o["MaxLMR"]                 << Option(10, 0, 20);
  o["MultiPV"]                << Option(1, 1, 500);
  o["Skill Level"]            << Option(20, 0, 20);
  o["Move Overhead"]          << Option(30, 0, 5000);
  o["Minimum Thinking Time"]  << Option(20, 0, 5000);
  o["Large Pages"]            << Option(true, on_large_pages);
  o["Slow Mover"]             << Option(89, 10, 1000);
  o["nodestime"]              << Option(0, 0, 10000);
  o["UCI_Chess960"]           << Option(false);
  o["SyzygyPath"]             << Option("<empty>", on_tb_path);
  o["SyzygyProbeDepth"]       << Option(1, 1, 100);
  o["Syzygy50MoveRule"]       << Option(true);
  o["SyzygyProbeLimit"]       << Option(6, 0, 6);
}


/// operator<<() is used to print all the options default values in chronological
/// insertion order (the idx field) and in the format defined by the UCI protocol.

std::ostream& operator<<(std::ostream& os, const OptionsMap& om) {

  for (size_t idx = 0; idx < om.size(); ++idx)
      for (const auto& it : om)
          if (it.second.idx == idx)
          {
              const Option& o = it.second;
              os << "\noption name " << it.first << " type " << o.type;

              if (o.type != "button")
                  os << " default " << o.defaultValue;

              if (o.type == "spin")
                  os << " min " << o.min << " max " << o.max;

              break;
          }

  return os;
}


/// Option class constructors and conversion operators

Option::Option(const char* v, OnChange f) : type("string"), min(0), max(0), on_change(f)
{ defaultValue = currentValue = v; }

Option::Option(bool v, OnChange f) : type("check"), min(0), max(0), on_change(f)
{ defaultValue = currentValue = (v ? "true" : "false"); }

Option::Option(OnChange f) : type("button"), min(0), max(0), on_change(f)
{}

Option::Option(int v, int minv, int maxv, OnChange f) : type("spin"), min(minv), max(maxv), on_change(f)
{ defaultValue = currentValue = std::to_string(v); }

Option::operator int() const {
  assert(type == "check" || type == "spin");
  return (type == "spin" ? stoi(currentValue) : currentValue == "true");
}

Option::operator std::string() const {
  assert(type == "string");
  return currentValue;
}


/// operator<<() inits options and assigns idx in the correct printing order

void Option::operator<<(const Option& o) {

  static size_t insert_order = 0;

  *this = o;
  idx = insert_order++;
}


/// operator=() updates currentValue and triggers on_change() action. It's up to
/// the GUI to check for option's limits, but we could receive the new value from
/// the user by console window, so let's check the bounds anyway.

Option& Option::operator=(const string& v) {

  assert(!type.empty());

  if (   (type != "button" && v.empty())
      || (type == "check" && v != "true" && v != "false")
      || (type == "spin" && (stoi(v) < min || stoi(v) > max)))
      return *this;

  if (type != "button")
      currentValue = v;

  if (on_change)
      on_change(*this);

  return *this;
}

} // namespace UCI
