// Copyright (c) 2024 Cecilia Norevik Bratlie, Nils Petter Skålerud, Eimen Oueslati
// SPDX-License-Identifier: MIT

#ifndef EVALUATOR_H
#define EVALUATOR_H

#include <QChar>
#include <QJsonArray>
#include <QMap>
#include <QString>
#include <QVariant>

// Other header files.

namespace Evaluator {
    // This should probably be pulled into a different header entirely.
    enum class FeatureGeometryType {
        Point,
        LineString,
        Polygon,
    };

    QVariant resolveExpression(
        const QJsonArray &expression,
        FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& metaData,
        int mapZoom,
        float vpZoom);

    using ExpressionOpFnT = QVariant(*)(
        const QJsonArray& array,
        FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& metaData,
        int mapZoom,
        float vpZoom);

    QVariant all(
        const QJsonArray& array,
        FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& metaData,
        int mapZoom,
        float vpZoom);
    QVariant case_(
        const QJsonArray& array,
        FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& metaData,
        int mapZoom,
        float vpZoom);
    QVariant coalesce(
        const QJsonArray& array,
        FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& metaData,
        int mapZoom,
        float vpZoom);
    QVariant compare(
        const QJsonArray& array,
        FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& metaData,
        int mapZoom,
        float vpZoom);
    QVariant get(
        const QJsonArray& array,
        FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& metaData,
        int mapZoom,
        float vpZoom);
    QVariant greater(
        const QJsonArray& array,
        FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& metaData,
        int mapZoom,
        float vpZoom);
    QVariant has(
        const QJsonArray& array,
        FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& metaData,
        int mapZoom,
        float vpZoom);
    QVariant in(
        const QJsonArray& array,
        FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& metaData,
        int mapZoom,
        float vpZoom);
    QVariant interpolate(
        const QJsonArray& array,
        FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& metaData,
        int mapZoom,
        float vpZoom);
    QVariant match(
        const QJsonArray& array,
        FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& metaData,
        int mapZoom,
        float vpZoom);
}
#endif // EVALUATOR_H
