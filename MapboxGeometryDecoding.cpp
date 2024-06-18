#include "MapboxGeometryDecoding.h"

#include <QScopeGuard>

#include "CDT.h"

qint32 decodeZigZag(qint32 input) {
    return (input >> 1) ^ -(input & 1);
}

std::pair<std::vector<Point>, std::vector<qint32>> ProtobufFeatureToPolygon(
    QSpan<unsigned int const> encodedGeometry)
{
    constexpr quint32 moveToCommand = 1;
    constexpr quint32 lineToCommand = 2;
    constexpr quint32 closePathCommand = 7;

    auto isValidCommandId = [](quint32 commandId, quint32 count) {
        if (commandId != moveToCommand && commandId != lineToCommand && commandId != closePathCommand) {
            return false;
        }
        if (commandId != closePathCommand && count == 0) {
            return false;
        }
        return true;
    };

    Point pen = {};

    int cursor = 0;
    int lastPathStartIndex = 0;
    std::vector<CDT::V2d<float>> pointList{};
    // We need to track all boundary line-segments forming this polygon
    // so that we may triangulate the polygon later.
    std::vector<CDT::Edge> boundaryEdges;

    while (cursor < encodedGeometry.size()) {
        int commandIndex = cursor;
        quint32 encodedCommandInteger = encodedGeometry[commandIndex];
        quint32 commandId = encodedCommandInteger & 0x7;
        quint32 count = encodedCommandInteger >> 3;
        if (!isValidCommandId(commandId, count)) {
            qFatal("Failure during geometry decoding");
        }

        // If the first command is NOT a move-to command, we need to start
        // by inserting a point at the origin.
        // This basically never happens with most data???
        if (pointList.size() == lastPathStartIndex && commandId != moveToCommand) {
            pointList.push_back({});
        }

        // Calculate how much we need to increment
        // to find the next command integer. Then apply it
        // deferredly.
        int amountToIncrement = 0;
        if (commandId == moveToCommand || commandId == lineToCommand) {
            amountToIncrement = 1 + count * 2;
        } else if (commandId == closePathCommand) {
            amountToIncrement = 1;
        }
        if (commandIndex + amountToIncrement > encodedGeometry.size()) {
            qFatal("Invalid geometry stream");
        }
        QScopeGuard incrementor{ [&]() {
            cursor += amountToIncrement;
        }};


        // Perform the command.
        if (commandId == closePathCommand) {
            // Reuse the first point of this path.
            boundaryEdges.push_back({
                (unsigned int)pointList.size() - 1,
                (unsigned int)lastPathStartIndex });

            lastPathStartIndex = pointList.size();
        } else {
            for (int repeatTracker = 0; repeatTracker < count; repeatTracker++) {
                auto pointIndex = (commandIndex + 1) + (2 * repeatTracker);
                pen.x += decodeZigZag(encodedGeometry[pointIndex]);
                pen.y += decodeZigZag(encodedGeometry[pointIndex + 1]);

                if (commandId == moveToCommand) {
                    lastPathStartIndex = pointList.size();
                    pointList.push_back({ (float)pen.x, (float)pen.y });
                } else if (commandId == lineToCommand) {
                    // We can safely use size() - 1 because
                    // we are guaranteed to have set the origin at the
                    // pen position before this command comes.
                    boundaryEdges.push_back({
                        (unsigned int)pointList.size() - 1,
                        (unsigned int)pointList.size() });
                    pointList.push_back({ (float)pen.x, (float)pen.y });
                }
            }
        }
    }

    // Triangulate our polygon.
    CDT::Triangulation<float> cdt;

    CDT::RemoveDuplicatesAndRemapEdges(pointList, boundaryEdges);
    cdt.insertVertices(pointList);
    cdt.insertEdges(boundaryEdges);

    cdt.eraseOuterTrianglesAndHoles();
    if (!cdt.isFinalized()) {
        qFatal("Triangulation failure.");
    }
    auto const& verts = cdt.vertices;
    auto const& triangles = cdt.triangles;

    std::vector<Point> vertexBuffer{};
    for (auto const& item : verts) {
        vertexBuffer.push_back({ (qint32)item.x, (qint32)item.y });
    }
    std::vector<qint32> indexBuffer{};
    for (auto const& item : triangles) {
        indexBuffer.push_back(item.vertices[0]);
        indexBuffer.push_back(item.vertices[1]);
        indexBuffer.push_back(item.vertices[2]);
    }


    return std::make_pair(vertexBuffer, indexBuffer);
}
