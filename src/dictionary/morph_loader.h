#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ime::dictionary {

struct MorphFeature {
  std::string key;
  std::string value;
};

struct MorphRecord {
  std::string surface;
  std::string reading;
  std::string base_form;
  std::string pos;
  int cost = 0;
  std::vector<MorphFeature> features;
};

struct MorphLoadStats {
  std::size_t total_rows = 0;
  std::size_t parsed_rows = 0;
  std::size_t skipped_bad_columns = 0;
  std::size_t skipped_overflow = 0;
};

struct MorphLoaderOptions {
  bool skip_invalid_lines = true;
  std::size_t max_entries = 0;  // 0 means no limit.
};

class MorphDictionary {
 public:
  const std::vector<MorphRecord>& records() const { return records_; }

  std::vector<const MorphRecord*> LookupSurface(
      const std::string& surface) const;
  std::vector<const MorphRecord*> LookupReading(
      const std::string& reading) const;

  std::optional<std::size_t> size() const { return records_.size(); }

 private:
  friend class MorphDictionaryLoader;

  std::vector<MorphRecord> records_;
  std::unordered_map<std::string, std::vector<std::size_t>> surface_index_;
  std::unordered_map<std::string, std::vector<std::size_t>> reading_index_;
};

class MorphDictionaryLoader {
 public:
  MorphDictionaryLoader() = default;

  MorphLoadStats Load(const std::filesystem::path& path,
                      MorphDictionary& out_dictionary,
                      const MorphLoaderOptions& options = {}) const;

 private:
  static void IndexRecord(std::size_t record_index,
                          const MorphRecord& record,
                          MorphDictionary& dictionary);
};

}  // namespace ime::dictionary

