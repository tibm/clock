#include "clk/status.hpp"

namespace clk {

const char* name(Status s) noexcept {
    switch (s) {
        case Status::Ok:
            return "ok";
        case Status::BadArg:
            return "bad-arg";
        case Status::Denied:
            return "denied";
        case Status::Busy:
            return "busy";
        case Status::NotReady:
            return "not-ready";
        case Status::Failed:
            return "failed";
        case Status::NotPresent:
            return "not-present";
    }
    return "?";
}

}  // namespace clk
