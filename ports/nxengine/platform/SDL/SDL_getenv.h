/* graphics.cpp includes this unconditionally (non-shim builds); nothing
   in the reachable code this port compiles actually calls an SDL_getenv
   function, so this shim is intentionally empty. */
#ifndef SDL_DEMONOS_SDL_GETENV_H
#define SDL_DEMONOS_SDL_GETENV_H
#endif
