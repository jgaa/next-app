
#include "absl/log/absl_log.h"

// https://github.com/llvm/llvm-project/issues/102443#issuecomment-2275698190

namespace absl {
    ABSL_NAMESPACE_BEGIN namespace log_internal {
        template LogMessage& LogMessage::operator << (unsigned long const&);
    } ABSL_NAMESPACE_END
}
