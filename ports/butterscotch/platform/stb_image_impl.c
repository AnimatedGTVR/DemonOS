/* Butterscotch vendors stb_image and uses it in image_decoder.c. Keep the
   native runner small by enabling only the in-memory PNG path needed by
   GameMaker texture pages. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(condition) do { if (!(condition)) abort(); } while (0)
#include "stb_image.h"
