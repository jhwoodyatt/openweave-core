#ifndef WEAVE_PLATFORM_ERROR_H__
#define WEAVE_PLATFORM_ERROR_H__


#define WEAVE_PLATFORM_ERROR_MIN 11000000
#define WEAVE_PLATFORM_ERROR_MAX 11000999
#define _WEAVE_PLATFORM_ERROR(e) (WEAVE_PLATFORM_ERROR_MIN + (e))

/**
 *  @def WEAVE_PLATFORM_ERROR_CONFIG_VALUE_NOT_FOUND
 *
 *  @brief
 *    The requested configuration value was not found.
 *
 */
#define WEAVE_PLATFORM_ERROR_CONFIG_NOT_FOUND                   _WEAVE_PLATFORM_ERROR(1)


#endif // WEAVE_PLATFORM_ERROR_H__