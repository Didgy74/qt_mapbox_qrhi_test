// Copyright (c) 2024 Cecilia Norevik Bratlie, Nils Petter Skålerud, Eimen Oueslati
// SPDX-License-Identifier: MIT

#ifndef EVALUATOR_H
#define EVALUATOR_H

// Qt header files.
#include <QChar>
#include <QJsonArray>
#include <QMap>
#include <QString>
#include <QVariant>

// Other header files.

namespace Evaluator {
    QVariant resolveExpression(
        const QJsonArray &expression,
        const std::map<QString, QVariant>& metaData,
        int mapZoomLevel,
        float vpZoomLevel);

    using ExpressionOpFnT = QVariant(*)(
        const QJsonArray& array,
        const std::map<QString, QVariant>& metaData,
        int mapZoomLevel,
        float vpZoomeLevel);

    QVariant all(
        const QJsonArray& array,
        const std::map<QString, QVariant>& metaData,
        int mapZoomLevel,
        float vpZoomeLevel);
    QVariant case_(
        const QJsonArray& array,
        const std::map<QString, QVariant>& metaData,
        int mapZoomLevel,
        float vpZoomeLevel);
    QVariant coalesce(
        const QJsonArray& array,
        const std::map<QString, QVariant>& metaData,
        int mapZoomLevel,
        float vpZoomeLevel);
    QVariant compare(
        const QJsonArray& array,
        const std::map<QString, QVariant>& metaData,
        int mapZoomLevel,
        float vpZoomeLevel);
    QVariant get(
        const QJsonArray& array,
        const std::map<QString, QVariant>& metaData,
        int mapZoomLevel,
        float vpZoomeLevel);
    QVariant greater(
        const QJsonArray& array,
        const std::map<QString, QVariant>& metaData,
        int mapZoomLevel,
        float vpZoomeLevel);
    QVariant has(
        const QJsonArray& array,
        const std::map<QString, QVariant>& metaData,
        int mapZoomLevel,
        float vpZoomeLevel);
    QVariant in(
        const QJsonArray& array,
        const std::map<QString, QVariant>& metaData,
        int mapZoomLevel,
        float vpZoomeLevel);
    QVariant interpolate(
        const QJsonArray& array,
        const std::map<QString, QVariant>& metaData,
        int mapZoomLevel,
        float vpZoomeLevel);
    QVariant match(
        const QJsonArray& array,
        const std::map<QString, QVariant>& metaData,
        int mapZoomLevel,
        float vpZoomeLevel);
}
#endif // EVALUATOR_H
