#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ime::dictionary {

struct BilingualEntry {
  std::string headword;
  std::vector<std::string> kanji_forms;
  std::vector<std::string> kana_forms;
  std::vector<std::string> english_glosses;
  std::vector<std::string> part_of_speech;
  std::vector<std::string> domains;
  std::vector<std::string> misc;
  std::vector<std::string> priority;
};

struct BilingualLoadStats {
  std::size_t total_rows = 0;
  std::size_t parsed_rows = 0;
  std::size_t skipped_bad_columns = 0;
  std::size_t skipped_empty = 0;
};

struct BilingualLoaderOptions {
  bool skip_invalid_lines = true;
  std::size_t max_entries = 0;  // 0 disables the cap.
};

class BilingualDictionary {
 public:
  const std::vector<BilingualEntry>& entries() const { return entries_; }

  std::vector<const BilingualEntry*> LookupHeadword(
      const std::string& headword) const;
  std::vector<const BilingualEntry*> LookupKanji(
      const std::string& kanji) const;
  std::vector<const BilingualEntry*> LookupKana(
      const std::string& kana) const;

 private:
  friend class BilingualDictionaryLoader;

  std::vector<BilingualEntry> entries_;
  std::unordered_map<std::string, std::vector<std::size_t>> headword_index_;
  std::unordered_map<std::string, std::vector<std::size_t>> kanji_index_;
  std::unordered_map<std::string, std::vector<std::size_t>> kana_index_;
};

class BilingualDictionaryLoader {
 public:
  BilingualDictionaryLoader() = default;

  BilingualLoadStats Load(const std::filesystem::path& path,
                          BilingualDictionary& out_dictionary,
                          const BilingualLoaderOptions& options = {}) const;

 private:
  static void IndexEntry(std::size_t entry_index,
                         const BilingualEntry& entry,
                         BilingualDictionary& dictionary);
};

}  // namespace ime::dictionary

