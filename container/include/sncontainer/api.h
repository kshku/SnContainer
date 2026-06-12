#pragma once

#include <sncore/api_common.h>

#if defined(SN_CONTAINER_STATIC)
    #define SN_CONTAINER_API
#elif defined(SN_EXPORT)
    #define SN_CONTAINER_API SN_API_HELPER_EXPORT
#else
    #define SN_CONTAINER_API SN_API_HELPER_IMPORT
#endif
