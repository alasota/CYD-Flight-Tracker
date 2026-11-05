#include "display/services/update_prompt_flow.h"

// TODO(spec 12): stub from test-author
bool UpdatePromptFlow::tryOpen(PromptGate /*gate*/, const std::string & /*tag*/,
                               uint32_t /*nowMs*/) {
  return false;
}

// TODO(spec 12): stub from test-author
PromptStep UpdatePromptFlow::step(const PromptInput & /*in*/) { return {}; }

// TODO(spec 12): stub from test-author
PromptStep UpdatePromptFlow::supersededByInstall() { return {}; }

// TODO(spec 12): stub from test-author
bool UpdatePromptFlow::active() const { return false; }

// TODO(spec 12): stub from test-author
const std::string &UpdatePromptFlow::tag() const { return tag_; }

// TODO(spec 12): stub from test-author
uint32_t UpdatePromptFlow::msUntilTimer(uint32_t /*nowMs*/) const { return 0; }
