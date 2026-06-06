/* map.h - selected runtime map. 1 = wall, 0 = empty. */
#ifndef MAP_H_INCLUDED
#define MAP_H_INCLUDED

#if DOOM_SIMPLE_MAP
#include "simple_map.h"
#else
#include "doom_map_generated.h"
#define ACTIVE_MAP_W MAP_W
#define ACTIVE_MAP_H MAP_H
#define ACTIVE_MAP_CELL_BYTES MAP_RUNTIME_OPEN_BYTES
#endif

#endif /* MAP_H_INCLUDED */
