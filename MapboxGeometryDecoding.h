#ifndef MAPBOXGEOMETRYDECODING_H
#define MAPBOXGEOMETRYDECODING_H

#include <QSpan>

#include <vector>

struct Point {
    qint32 x;
    qint32 y;

    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
};

std::pair<std::vector<Point>, std::vector<qint32>> ProtobufFeatureToPolygon(
    QSpan<unsigned int const> encodedGeometry);

#endif // MAPBOXGEOMETRYDECODING_H
