#ifndef ZBAPIURL_H
#define ZBAPIURL_H

#include <string.h>

/**
 * Enum that lists all the possible API base URLs.
 */
typedef enum {
    Default,
    USA,
    EU
} ZBApiURL;

/**
 * @brief Function used to return base url string corresponding to a ZBApiURL.
 *
 * @param zbApiUrl ZBApiURL enum value
 * @return string corresponding base url string
 */
char* base_url_string_from_zb_api_url(ZBApiURL zbApiUrl);

#endif