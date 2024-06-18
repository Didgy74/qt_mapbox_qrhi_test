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
    QVariant m_fillColor;
    QVariant m_fillOpacity;
    QVariant m_fillOutlineColor;

public:
    static std::unique_ptr<FillLayerStyle> fromJson(const QJsonObject &json);

    StyleSheet::LayerType type() const override
    {
        return StyleSheet::LayerType::fill;
    }

    QColor getFillColor(
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
