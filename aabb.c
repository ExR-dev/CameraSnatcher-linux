
#include "include/aabb.h"

#include <stdbool.h>


bool point_intersect(Vec2 point, AABB box)
{
    if (point.x < box.w) return false;
    if (point.x > box.e) return false;
    if (point.y < box.n) return false;
    if (point.y > box.s) return false;
    return true;
}

bool AABB_intersect(AABB box1, AABB box2)
{
    if (box1.e < box2.w) return false;
    if (box1.w > box2.e) return false;
    if (box1.s < box2.n) return false;
    if (box1.n > box2.s) return false;
    return true;
}
