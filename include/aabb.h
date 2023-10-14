#ifndef INCLUDE_AABB_H
#define INCLUDE_AABB_H

#include <stdbool.h>


typedef struct Vec2
{
    int x; // The y-value of the boxes northern edge.
    int y; // The x-value of the boxes eastern edge.
} Vec2;

typedef struct AABB
{
    unsigned short w; // The x-value of the boxes western edge.
    unsigned short n; // The y-value of the boxes northern edge.
    unsigned short e; // The x-value of the boxes eastern edge.
    unsigned short s; // The y-value of the boxes southern edge.
} AABB;


bool point_intersect(Vec2 point, AABB box);

bool AABB_intersect(AABB box1, AABB box2);

#endif