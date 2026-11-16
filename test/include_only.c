/* Compiles the public headers on their own, with warnings as errors (see
 * CMakeLists.txt). It calls nothing, in particular not the deprecated
 * find_email: including a header must not warn, only a call can. */
#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/ZeroBounceWarmup.h"

int main(void) {
    return 0;
}
