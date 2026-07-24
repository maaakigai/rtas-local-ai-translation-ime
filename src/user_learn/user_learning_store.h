#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace ime::learning {

struct LearningEvent {
  std::string type;
  std::string payload_json;
};

class UserLearningStore {
 public:
  virtual ~UserLearningStore() = default;

  virtual bool AppendEvent(const LearningEvent& event) = 0;
  virtual bool Flush() = 0;
};

std::unique_ptr<UserLearningStore> CreateFileStore(
    const std::filesystem::path& profile_root);

}  // namespace ime::learning

