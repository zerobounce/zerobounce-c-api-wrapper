#ifndef ZBAPIURL_H
#define ZBAPIURL_H

#include <string.h>

/**
 * Enum that lists all the possible API base URLs.
 */
typedef enum {
    ZB_Api_URL_Default,
    ZB_Api_URL_USA,
    ZB_Api_URL_EU
} ZBApiURL;

/**
 * @brief Function used to return base url string corresponding to a ZBApiURL.
 *
 * @param zbApiUrl ZBApiURL enum value
 * @return string corresponding base url string
 */
char* base_url_string_from_zb_api_url(ZBApiURL zbApiUrl);

#endif