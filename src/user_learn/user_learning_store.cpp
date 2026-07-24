#include "user_learning_store.h"

#include <fstream>
#include <mutex>
#include <vector>

namespace ime::learning {
namespace {

class FileUserLearningStore final : public UserLearningStore {
 public:
  explicit FileUserLearningStore(std::filesystem::path profile_root)
      : profile_root_(std::move(profile_root)) {}

  bool AppendEvent(const LearningEvent& event) override {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(event);
    return true;
  }

  bool Flush() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.empty()) {
      return true;
    }

    const auto log_path = profile_root_ / "events.log";
    std::filesystem::create_directories(log_path.parent_path());

    std::ofstream out(log_path, std::ios::app | std::ios::binary);
    if (!out) {
      return false;
    }

    for (const auto& entry : pending_) {
      out << "{\"type\":\"" << entry.type << "\",\"payload\":"
          << entry.payload_json << "}\n";
    }
    pending_.clear();
    return true;
  }

 private:
  std::filesystem::path profile_root_;
  std::vector<LearningEvent> pending_;
  std::mutex mutex_;
};

}  // namespace

std::unique_ptr<UserLearningStore> CreateFileStore(
    const std::filesystem::path& profile_root) {
  return std::make_unique<FileUserLearningStore>(profile_root);
}

}  // namespace ime::learning
