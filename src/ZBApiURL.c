#include "ZeroBounce/ZBApiURL.h"

char* base_url_string_from_zb_api_url(ZBApiURL zbApiUrl) {
    switch (zbApiUrl) {
    case ZB_Api_URL_Default:
        return "https://api.zerobounce.net/v2";

    case ZB_Api_URL_USA:
        return "https://api-us.zerobounce.net/v2";

    case ZB_Api_URL_EU:
        return "https://api-eu.zerobounce.net/v2";

    default:
        return "https://api.zerobounce.net/v2";
    }
}