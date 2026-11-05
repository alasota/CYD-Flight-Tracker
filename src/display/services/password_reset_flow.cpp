#include "display/services/password_reset_flow.h"

// TODO(spec 12): stub from test-author
bool shouldOfferPasswordReset(bool /*passwordSet*/, bool /*pressed*/) { return false; }

// TODO(spec 12): stub from test-author
PasswordResetFlow::PasswordResetFlow(uint32_t /*nowMs*/) {}

// TODO(spec 12): stub from test-author
ResetStep PasswordResetFlow::step(const ResetInput & /*in*/) { return {}; }

// TODO(spec 12): stub from test-author
PasswordResetFlow::Phase PasswordResetFlow::phase() const { return Phase::Hold; }
