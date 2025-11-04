#include "ZeroBounce/ZBApiURL.h"

char* base_url_string_from_zb_api_url(ZBApiURL zbApiUrl) {
    switch (zbApiUrl) {
    case Default:
        return "https://api.zerobounce.net/v2";

    case USA:
        return "https://api-us.zerobounce.net/v2";

    case EU:
        return "https://api-eu.zerobounce.net/v2";

    default:
        return "https://api.zerobounce.net/v2";
    }
}