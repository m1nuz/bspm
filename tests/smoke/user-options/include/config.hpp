#pragma once

#ifndef BSPM_DEFINE_VALUE
#error "BSPM_DEFINE_VALUE should be provided by --define"
#endif

#ifndef BSPM_CXXFLAG_VALUE
#error "BSPM_CXXFLAG_VALUE should be provided by --cxxflag"
#endif

inline constexpr int configured_value = BSPM_DEFINE_VALUE + BSPM_CXXFLAG_VALUE;
