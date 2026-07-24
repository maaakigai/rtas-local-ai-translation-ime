#include "bilingual_loader.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace ime::dictionary {
namespace {

std::string TrimCopy(std::string_view view) {
  const auto begin = view.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = view.find_last_not_of(" \t\r\n");
  return std::string(view.substr(begin, end - begin + 1));
}

std::vector<std::string> Split(std::string_view input, char delimiter) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= input.size()) {
    const std::size_t pos = input.find(delimiter, start);
    if (pos == std::string_view::npos) {
      parts.emplace_back(input.substr(start));
      break;
    }
    parts.emplace_back(input.substr(start, pos - start));
    start = pos + 1;
  }
  return parts;
}

std::vector<std::string> SplitAndClean(const std::string& token, char delim) {
  std::vector<std::string> result;
  if (token.empty()) {
    return result;
  }
  for (const auto& raw_part : Split(token, delim)) {
    std::string value = TrimCopy(raw_part);
    if (!value.empty()) {
      result.push_back(std::move(value));
    }
  }
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

void IndexStrings(const std::vector<std::string>& values,
                  std::size_t entry_index,
                  std::unordered_map<std::string, std::vector<std::size_t>>&
                      index) {
  for (const auto& value : values) {
    index[value].push_back(entry_index);
  }
}

}  // namespace

BilingualLoadStats BilingualDictionaryLoader::Load(
    const std::filesystem::path& path,
    BilingualDictionary& out_dictionary,
    const BilingualLoaderOptions& options) const {
  BilingualLoadStats stats;

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open bilingual dictionary: " +
                             path.string());
  }

  out_dictionary = BilingualDictionary();
  out_dictionary.entries_.reserve(
      options.max_entries > 0 ? options.max_entries : 4096);

  std::string line;
  if (!std::getline(input, line)) {
    return stats;  // Empty file.
  }

  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    stats.total_rows += 1;

    auto columns = Split(line, '\t');
    if (columns.size() < 8) {
      stats.skipped_bad_columns += 1;
      if (!options.skip_invalid_lines) {
        throw std::runtime_error("Malformed bilingual TSV row: " + line);
      }
      continue;
    }

    BilingualEntry entry;
    entry.headword = TrimCopy(columns[0]);
    entry.kanji_forms = SplitAndClean(TrimCopy(columns[1]), '|');
    entry.kana_forms = SplitAndClean(TrimCopy(columns[2]), '|');
    entry.english_glosses = SplitAndClean(TrimCopy(columns[3]), '|');
    entry.part_of_speech = SplitAndClean(TrimCopy(columns[4]), '|');
    entry.domains = SplitAndClean(TrimCopy(columns[5]), '|');
    entry.misc = SplitAndClean(TrimCopy(columns[6]), '|');
    entry.priority = SplitAndClean(TrimCopy(columns[7]), '|');

    if (entry.headword.empty() || entry.english_glosses.empty()) {
      stats.skipped_empty += 1;
      if (!options.skip_invalid_lines) {
        throw std::runtime_error("Incomplete bilingual row: " + line);
      }
      continue;
    }

    if (entry.kana_forms.empty() && entry.kanji_forms.empty()) {
      entry.kana_forms.push_back(entry.headword);
    }

    const std::size_t entry_index = out_dictionary.entries_.size();
    out_dictionary.entries_.push_back(std::move(entry));
    IndexEntry(entry_index, out_dictionary.entries_.back(), out_dictionary);
    stats.parsed_rows += 1;

    if (options.max_entries > 0 &&
        out_dictionary.entries_.size() >= options.max_entries) {
      break;
    }
  }

  return stats;
}

void BilingualDictionaryLoader::IndexEntry(
    std::size_t entry_index,
    const BilingualEntry& entry,
    BilingualDictionary& dictionary) {
  dictionary.headword_index_[entry.headword].push_back(entry_index);
  IndexStrings(entry.kanji_forms, entry_index, dictionary.kanji_index_);
  IndexStrings(entry.kana_forms, entry_index, dictionary.kana_index_);
}

std::vector<const BilingualEntry*> BilingualDictionary::LookupHeadword(
    const std::string& headword) const {
  std::vector<const BilingualEntry*> hits;
  const auto it = headword_index_.find(headword);
  if (it == headword_index_.end()) {
    return hits;
  }
  hits.reserve(it->second.size());
  for (std::size_t index : it->second) {
    hits.push_back(&entries_[index]);
  }
  return hits;
}

std::vector<const BilingualEntry*> BilingualDictionary::LookupKanji(
    const std::string& kanji) const {
  std::vector<const BilingualEntry*> hits;
  const auto it = kanji_index_.find(kanji);
  if (it == kanji_index_.end()) {
    return hits;
  }
  hits.reserve(it->second.size());
  for (std::size_t index : it->second) {
    hits.push_back(&entries_[index]);
  }
  return hits;
}

std::vector<const BilingualEntry*> BilingualDictionary::LookupKana(
    const std::string& kana) const {
  std::vector<const BilingualEntry*> hits;
  const auto it = kana_index_.find(kana);
  if (it == kana_index_.end()) {
    return hits;
  }
  hits.reserve(it->second.size());
  for (std::size_t index : it->second) {
    hits.push_back(&entries_[index]);
  }
  return hits;
}

}  // namespace ime::dictionary
