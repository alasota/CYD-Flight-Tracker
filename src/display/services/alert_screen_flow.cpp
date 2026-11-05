#include "display/services/alert_screen_flow.h"

// TODO(spec 12): stub from test-author
uint32_t AlertScreenFlow::open(uint32_t /*nowMs*/, const AlertSet & /*alerts*/) { return 0; }

// TODO(spec 12): stub from test-author
AlertFlowStep AlertScreenFlow::step(const AlertScreenInput & /*in*/, const AlertSet & /*alerts*/) {
  return {};
}

// TODO(spec 12): stub from test-author
void AlertScreenFlow::closeQuietly() {}

// TODO(spec 12): stub from test-author
bool AlertScreenFlow::isOpen() const { return false; }

// TODO(spec 12): stub from test-author
uint32_t AlertScreenFlow::msToNextSecond(uint32_t /*nowMs*/) const { return 0; }
