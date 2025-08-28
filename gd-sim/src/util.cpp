#include <util.hpp>
#include <cmath>

Vec2D Vec2D::rotate(float angle, Vec2D const& pivot) const {
    if (angle == 0) return *this;

    Vec2D tmp = *this - pivot;

    float rad = deg2rad(angle);
    float s = std::sin(rad);
    float c = std::cos(rad);

    tmp = {tmp.x * c - tmp.y * s, tmp.x * s + tmp.y * c};
    tmp += pivot;

    return tmp;
}

// Very complicated (but very fast) way to check intersection
bool intersectOneWay(Entity const& a, Entity const& b) {
    float big = std::max(a.size.x, a.size.y) + std::max(b.size.x, b.size.y);
    if (std::abs(a.pos.x - b.pos.x) > big || std::abs(a.pos.y - b.pos.y) > big) {
        return false;
    }

    Entity tmp = b;

    tmp.rotation -= a.rotation;
    tmp.pos = tmp.pos.rotate(-a.rotation, a.pos);

    Vec2D corners[4] = {
        Vec2D(tmp.getLeft(), tmp.getBottom()).rotate(tmp.rotation, tmp.pos),
        Vec2D(tmp.getRight(), tmp.getBottom()).rotate(tmp.rotation, tmp.pos),
        Vec2D(tmp.getRight(), tmp.getTop()).rotate(tmp.rotation, tmp.pos),
        Vec2D(tmp.getLeft(), tmp.getTop()).rotate(tmp.rotation, tmp.pos)
    };

    float lastDiffX = 0;
    bool overlapX = false;

    float lastDiffY = 0;
    bool overlapY = false;

    for (auto vert : corners) {
        if (!overlapX) {
            float diffX = vert.x - a.pos.x;
            if ((vert.x >= a.getLeft() && vert.x <= a.getRight()) || (lastDiffX != 0 && std::signbit(lastDiffX) != std::signbit(diffX))) {
                overlapX = true;
            }
            lastDiffX = diffX;
        }
        if (!overlapY) {
            float diffY = vert.y - a.pos.y;
            if ((vert.y >= a.getBottom() && vert.y <= a.getTop()) || (lastDiffY != 0 && std::signbit(lastDiffY) != std::signbit(diffY))) {
                overlapY = true;
            }
            lastDiffY = diffY;
        }
    }

    return overlapX && overlapY;
}

bool Entity::intersects(Entity const& b) const {
    return intersectOneWay(*this, b) && intersectOneWay(b, *this);
}
