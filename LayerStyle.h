// Copyright (c) 2024 Cecilia Norevik Bratlie, Nils Petter Skålerud, Eimen Oueslati
// SPDX-License-Identifier: MIT

#ifndef LAYERSTYLE_H
#define LAYERSTYLE_H

// Qt header files.
#include <QColor>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPen>
#include <QString>
#include <QtTypes>
#include <vector>
#include <optional>
#include <QVector2D>

#include "Evaluator.h"

class StyleSheet
{
public:
    enum class LayerType {
        Background,
        fill,
        notImplemented,
    };
    class AbstractLayerStyle
    {
    public:
        virtual ~AbstractLayerStyle() {}

        static std::unique_ptr<AbstractLayerStyle> fromJson(const QJsonObject &json);
        [[nodiscard]] virtual LayerType type() const = 0;
        QString m_id;
        QString m_sourceLayer;
        QString m_source;
        int m_minZoom = 0;
        int m_maxZoom = 24;
        bool m_visibility;
        QJsonArray m_filter;

    protected:
        static void initAbstractMembers(AbstractLayerStyle& layer, const QJsonObject &json);
    };

    StyleSheet() = default;
    StyleSheet(StyleSheet&&) = default;
    // Explicitly marked as deleted in order to prevent
    // accidental copies.
    StyleSheet(const StyleSheet&) = delete;
    // Explicitly marked as deleted in order to prevent
    // accidental copies.
    StyleSheet& operator=(const StyleSheet&) = delete;
    StyleSheet& operator=(StyleSheet&&) = default;

    static std::optional<StyleSheet> fromJson(const QJsonDocument&);
    static std::optional<StyleSheet> fromJsonBytes(const QByteArray& input);
    static std::optional<StyleSheet> fromJsonFile(const QString& path);

    QString m_id;
    int m_version;
    QString m_name;
    std::vector<std::unique_ptr<AbstractLayerStyle>> m_layerStyles;

    [[nodiscard]] QColor getBackgroundColor(int mapZoom) const;
};

class BackgroundLayerStyle : public StyleSheet::AbstractLayerStyle
{
public:
    static std::unique_ptr<BackgroundLayerStyle> fromJson(const QJsonObject &json);

    StyleSheet::LayerType type() const override
    {
        return StyleSheet::LayerType::Background;
    }

    std::vector<std::pair<int, QColor>> colorStops;
    [[nodiscard]] QColor getColor(int mapZoom) const;
};

class FillLayerStyle : public StyleSheet::AbstractLayerStyle
{
private:
    // A color can either be an object containing stops,
    // or a regular color value?
    // Nils chose not to use a QColor for this type
    // because it was extremely annoying to debug
    // when not able to read color-values.
    std::vector<std::pair<int, std::array<float, 4>>> m_fillColorStops;
    void loadFillColor(QJsonObject const& paintJson);

    // The opacity can either be not present at all, in which case
    // we use the default opacity set by the fill-color.
    // Or it can be a single scalar value
    // Or it can be a list of stops.
    // Or it can be a full-blown expression.
    bool m_opacityFound = false;
    bool m_usingOpacityStops = true;
    bool usingOpacityExpression() const { return !m_usingOpacityStops; }
    QJsonArray m_opacityExpression;
    std::vector<std::pair<int, float>> m_opacityStops;
    void loadOpacity(QJsonObject const& paintJson);

    // Measured in pixels
    std::vector<std::pair<int, QVector2D>> m_translateStops;

public:
    static std::unique_ptr<FillLayerStyle> fromJson(const QJsonObject &json);

    StyleSheet::LayerType type() const override
    {
        return StyleSheet::LayerType::fill;
    }

    QColor getFillColor(
        Evaluator::FeatureGeometryType featGeomType,
        const std::map<QString, QVariant>& featureMetaData,
        int mapZoom,
        double vpZoom) const;
    QVector2D getTranslation(
        const std::map<QString, QVariant>& featureMetaData,
        int mapZoom,
        double vpZoom) const;


};

class NotImplementedStyle : public StyleSheet::AbstractLayerStyle
{
public:
    static std::unique_ptr<NotImplementedStyle> fromJson(const QJsonObject &json);
    StyleSheet::LayerType type() const override
    {
        return StyleSheet::LayerType::notImplemented;
    }
};



template <class T>
inline T getStopOutput(QList<QPair<int, T>> list, int currentZoom)
{
    if (currentZoom <= list.begin()->first) {
        return list.begin()->second;
    }

    for(int i = 0; i < list.size(); i++)
    {
        if (currentZoom <= list[i].first) {
            return list[i-1].second;
        }
    }
    return list.last().second;
}

#endif // LAYERSTYLE_H
