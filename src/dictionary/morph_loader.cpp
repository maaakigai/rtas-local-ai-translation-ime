#include "morph_loader.h"

#include <algorithm>
#include <fstream>
#include <sstream>
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

bool ParseCost(const std::string& token, int& out_cost) {
  try {
    size_t processed = 0;
    out_cost = std::stoi(token, &processed, 10);
    return processed == token.size();
  } catch (const std::exception&) {
    return false;
  }
}

std::vector<MorphFeature> ParseFeatures(const std::string& token) {
  std::vector<MorphFeature> features;
  if (token.empty()) {
    return features;
  }
  for (const auto& feature_token : Split(token, ';')) {
    const auto equal_pos = feature_token.find('=');
    if (equal_pos == std::string::npos) {
      features.push_back({TrimCopy(feature_token), {}});
      continue;
    }
    std::string key = TrimCopy(
        std::string_view(feature_token).substr(0, equal_pos));
    std::string value =
        TrimCopy(std::string_view(feature_token).substr(equal_pos + 1));
    features.push_back({std::move(key), std::move(value)});
  }
  return features;
}

}  // namespace

MorphLoadStats MorphDictionaryLoader::Load(
    const std::filesystem::path& path,
    MorphDictionary& out_dictionary,
    const MorphLoaderOptions& options) const {
  MorphLoadStats stats;

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open morph dictionary: " +
                             path.string());
  }

  out_dictionary = MorphDictionary();  // Reset.
  out_dictionary.records_.reserve(
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
    if (columns.size() < 6) {
      stats.skipped_bad_columns += 1;
      if (!options.skip_invalid_lines) {
        throw std::runtime_error("Malformed TSV row: " + line);
      }
      continue;
    }

    MorphRecord record;
    record.surface = TrimCopy(columns[0]);
    record.reading = TrimCopy(columns[1]);
    record.base_form = TrimCopy(columns[2]);
    record.pos = TrimCopy(columns[3]);

    if (!ParseCost(TrimCopy(columns[4]), record.cost)) {
      stats.skipped_bad_columns += 1;
      if (!options.skip_invalid_lines) {
        throw std::runtime_error("Invalid cost value: " + columns[4]);
      }
      continue;
    }

    record.features = ParseFeatures(TrimCopy(columns[5]));

    const std::size_t record_index = out_dictionary.records_.size();
    out_dictionary.records_.push_back(std::move(record));
    IndexRecord(record_index, out_dictionary.records_.back(), out_dictionary);
    stats.parsed_rows += 1;

    if (options.max_entries > 0 &&
        out_dictionary.records_.size() >= options.max_entries) {
      stats.skipped_overflow += 1;
      break;
    }
  }

  return stats;
}

void MorphDictionaryLoader::IndexRecord(std::size_t record_index,
                                        const MorphRecord& record,
                                        MorphDictionary& dictionary) {
  dictionary.surface_index_[record.surface].push_back(record_index);
  dictionary.reading_index_[record.reading].push_back(record_index);
}

std::vector<const MorphRecord*> MorphDictionary::LookupSurface(
    const std::string& surface) const {
  std::vector<const MorphRecord*> hits;
  auto it = surface_index_.find(surface);
  if (it == surface_index_.end()) {
    return hits;
  }
  hits.reserve(it->second.size());
  for (std::size_t index : it->second) {
    hits.push_back(&records_[index]);
  }
  return hits;
}

std::vector<const MorphRecord*> MorphDictionary::LookupReading(
    const std::string& reading) const {
  std::vector<const MorphRecord*> hits;
  auto it = reading_index_.find(reading);
  if (it == reading_index_.end()) {
    return hits;
  }
  hits.reserve(it->second.size());
  for (std::size_t index : it->second) {
    hits.push_back(&records_[index]);
  }
  return hits;
}

}  // namespace ime::dictionary
