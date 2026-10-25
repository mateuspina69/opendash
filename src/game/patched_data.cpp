#include "game/patched_data.hpp"

namespace od::port {

std::vector<PatchedData*>& patchedRecords() {
    static std::vector<PatchedData*> r;
    return r;
}

std::vector<std::unique_ptr<PatchedData>>& patchedRecordsAt() {
    static std::vector<std::unique_ptr<PatchedData>> r;
    return r;
}

} // namespace od::port
