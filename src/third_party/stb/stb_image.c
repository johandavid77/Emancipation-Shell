/* Single translation unit that instantiates stb_image. The header is public
 * domain (see stb_image.h); we only need the decoder, so keep it isolated here
 * instead of defining the implementation macro in every user. */
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"
