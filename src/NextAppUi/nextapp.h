#pragma once

// Thank you again Microsoft for making it so exiting to make
// Standard portable C++ code work with you amazing compiler
#ifdef min
#   undef min
#endif

#ifdef max
#   undef max
#endif

namespace nextapp {

// The epoch for the NextApp data. Used to force a full re-sync after an upgrade if required.
static constexpr unsigned int app_data_epoc = NEXTAPP_DATA_EPOCH;

} // namespace nextapp
