// Copyright (c) 2024 Cecilia Norevik Bratlie, Nils Petter Skålerud, Eimen Oueslati
// SPDX-License-Identifier: MIT

// Qt header files.
#include <QFile>
#include <QRegularExpression>
#include <QtMath>
#include <QSpan>

// Other header files.
#include "LayerStyle.h"

#include "Evaluator.h"

// This function assumes the stops have already been sorted.
template<class T>
T interpolateStops(QSpan<std::pair<int, T> const> list, int currentZoom)
{
    if (list.empty()) {
        qFatal("");
    }

    if (currentZoom <= list.begin()->first) {
        return list.begin()->second;
    }

    for(int i = 0; i < list.size(); i++)
    {
        if (currentZoom <= list[i].first) {
            return list[i-1].second;
        }
    }
    return list.back().second;
}

static std::optional<QColor> parseColorFromString(QString colorString)
{
    colorString.remove(" ");
    //All parameters for QColor::fromHslF need to be between 0 and 1.
    if (colorString.startsWith("hsl(")) {
        static QRegularExpression re { ".*\\((\\d+),(\\d+)%,(\\d+)%\\)" };
        auto match = re.match(colorString);
        auto capturedTexts = match.capturedTexts();
        if (capturedTexts.length() >= 4) {
            auto tempColor = QColor::fromHslF(
                match.capturedTexts().at(1).toInt()/359.,
                match.capturedTexts().at(2).toInt()/100.,
                match.capturedTexts().at(3).toInt()/100.);
            if (tempColor.isValid()) {
                return tempColor;
            } else {
                return std::nullopt;
            }
        }
    }
    if (colorString.startsWith("hsla(")) {
        static QRegularExpression re { ".*\\((\\d+),(\\d+)%,(\\d+)%,(\\d?\\.?\\d*)\\)" };
        auto match = re.match(colorString);
        auto capturedTexts = match.capturedTexts();
        if (capturedTexts.length() >= 5) {
            auto tempColor = QColor::fromHslF(
                capturedTexts.at(1).toInt()/359.,
                capturedTexts.at(2).toInt()/100.,
                capturedTexts.at(3).toInt()/100.,
                capturedTexts.at(4).toFloat());
            if (!tempColor.isValid()) {
                return std::nullopt;
            } else
                return tempColor;
        }
    }

    // In case the color has a different format than expected.
    // If the format cannot be handeled by QColor, we return black as a default color.
    QColor returnColor = QColor::fromString(colorString);
    if (!returnColor.isValid()) {
        return std::nullopt;
    } else {
        return returnColor;
    }
}

void StyleSheet::AbstractLayerStyle::initAbstractMembers(
    AbstractLayerStyle& layer,
    const QJsonObject &json)
{
    layer.m_id = json.value("id").toString();
    layer.m_source = json.value("source").toString();
    layer.m_sourceLayer = json.value("source-layer").toString();
    layer.m_minZoom = json.value("minzoom").toInt(0);
    layer.m_maxZoom = json.value("maxzoom").toInt(24);

    // If the layout object is not present, it means the
    // layer defaults to being visible.

    auto const& layoutRefIt = json.find("layout");
    if (layoutRefIt == json.end()) {
        layer.m_visibility = true;
    } else {
        if (!layoutRefIt->isObject()) {
            qFatal("layout was present but not of type object");
        }
        auto const& layout = layoutRefIt->toObject();
        auto const& visibilityRefIt = layout.find("visibility");
        if (visibilityRefIt == layout.end()) {
            layer.m_visibility = true;
        } else {
            if (!visibilityRefIt->isString()) {
                qFatal("");
            }
            auto const& visibilityString = visibilityRefIt->toString();
            if (visibilityString == "visible") {
                layer.m_visibility = true;
            } else if (visibilityString == "none") {
                layer.m_visibility = false;
            } else {
                qFatal("");
            }
        }
    }

    if(json.contains("filter"))
        layer.m_filter = json.value("filter").toArray();
}

std::unique_ptr<StyleSheet::AbstractLayerStyle>
StyleSheet::AbstractLayerStyle::fromJson(const QJsonObject &json)
{
    auto const& layerTypeIt = json.find("type");
    if (layerTypeIt == json.end()) {
        qFatal("Bleh");
    }
    auto const& layerTypeRef = *layerTypeIt;
    if (!layerTypeRef.isString()) {
        qFatal("Bleh");
    }
    auto const& layerType = layerTypeRef.toString();

    std::unique_ptr<AbstractLayerStyle> returnLayerPtr;

    if (layerType == "background") {
        returnLayerPtr = BackgroundLayerStyle::fromJson(json);
    }
    else if (layerType == "fill") {
        returnLayerPtr = FillLayerStyle::fromJson(json);
    }
    else if (layerType == "line")
        returnLayerPtr = NotImplementedStyle::fromJson(json);
    else if (layerType == "symbol")
        returnLayerPtr = NotImplementedStyle::fromJson(json);
    else
        returnLayerPtr = NotImplementedStyle::fromJson(json);
    return returnLayerPtr;
}

std::optional<StyleSheet> StyleSheet::fromJson(const QJsonDocument &styleSheetJsonDoc)
{
    StyleSheet out;

    auto const& styleSheetJson = styleSheetJsonDoc.object();
    out.m_id = styleSheetJson.value("id").toString();
    out.m_version = styleSheetJson.value("version").toInt();
    out.m_name = styleSheetJson.value("name").toString();

    auto const& layersIt = styleSheetJson.find("layers");
    if (layersIt == styleSheetJson.end()) {
        qFatal("Stylesheet is missing layers.");
        return std::nullopt;
    }
    if (!layersIt->isArray()) {
        qFatal("Stylesheet 'layer' property is not of type array.");
        return std::nullopt;
    }
    auto const& layers = layersIt->toArray();

    for (const auto &layerRef : layers) {
        if (!layerRef.isObject()) {
            qFatal("Bleh");
        }
        auto const& layer = layerRef.toObject();

        out.m_layerStyles.push_back(AbstractLayerStyle::fromJson(layer));
    }

    return out;
}

std::optional<StyleSheet> StyleSheet::fromJsonBytes(const QByteArray& input)
{
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(input, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return std::nullopt;
    }
    return fromJson(jsonDoc);
}

std::optional<StyleSheet> StyleSheet::fromJsonFile(const QString& path)
{
    QFile file{ path };
    bool openSuccess = file.open(QFile::ReadOnly);
    if (!openSuccess)
        return std::nullopt;

    return fromJsonBytes(file.readAll());
}

std::unique_ptr<BackgroundLayerStyle> BackgroundLayerStyle::fromJson(const QJsonObject &json)
{
    auto returnLayerPtr = std::make_unique<BackgroundLayerStyle>();
    auto& returnLayer = *returnLayerPtr;
    initAbstractMembers(returnLayer, json);

    auto const& paintJsonIt = json.find("paint");
    if (paintJsonIt == json.end()) {
        qFatal("Couldn't find paint object within background layer");
    }
    if (!paintJsonIt->isObject()) {
        qFatal("Background layer property 'paint' is not an object");
    }
    auto const& paintJson = paintJsonIt->toObject();

    auto const& colorIt = paintJson.find("background-color");
    if (colorIt == paintJson.end()) {
        qFatal("Backgrould layer member 'paint' does not contain background-color");
    }
    if (colorIt->isObject()) {
        auto const& color = colorIt->toObject();
        auto const& colorStopsIt = color.find("stops");
        if (colorStopsIt == color.end()) {
            qFatal("Bleh");
        }
        if (!colorStopsIt->isArray()) {
            qFatal("Bleh");
        }
        auto const& colorStops = colorStopsIt->toArray();
        for (auto const& item : colorStops) {
            if (!item.isArray()) {
                qFatal("Bleh");
            }
            auto const& stop = item.toArray();
            if (stop.size() != 2) {
                qFatal("Bleh");
            }
            std::pair<int, QColor> outStop;
            auto const& stopIndex = stop[0];
            if (!stopIndex.isDouble()) {
                qFatal("Bleh");
            }
            outStop.first = stopIndex.toInt();

            // Parse the color
            auto const& stopColor = stop[1];
            if (!stopColor.isString()) {
                qFatal("Bleh");
            }
            auto parsedColorOpt = parseColorFromString(stopColor.toString());
            if (!parsedColorOpt.has_value() || !parsedColorOpt.value().isValid()) {
                qFatal("Bleh");
            }
            outStop.second = parsedColorOpt.value();

            returnLayer.colorStops.push_back(outStop);
        }
    } else if (colorIt->isString()) {
        // Try to parse the string as a color.
        auto parsedColorOpt = parseColorFromString(colorIt->toString());
        if (!parsedColorOpt.has_value()) {
            qFatal("Unable to parse background color from string.");
        }
        returnLayer.colorStops.push_back({0, parsedColorOpt.value()});
    } else {
        qFatal("Unknown background-color type.");
    }

    return returnLayerPtr;
}

QColor BackgroundLayerStyle::getColor(
    int mapZoom) const
{
    return interpolateStops(
        QSpan{ colorStops.data(), (qsizetype)colorStops.size()},
        mapZoom);
}

void FillLayerStyle::loadFillColor(QJsonObject const& paintJson) {
    auto const& jsonIt = paintJson.find("fill-color");
    if (jsonIt == paintJson.end()) {
        qFatal("Fill layer member 'paint' does not contain fill-color");
    }
    if (jsonIt->isObject()) {
        // The opacity is expressed as an object containing stops.

        auto const& json = jsonIt->toObject();
        auto const& stopsIt = json.find("stops");
        if (stopsIt == json.end()) {
            qFatal("Bleh");
        }
        if (!stopsIt->isArray()) {
            qFatal("Bleh");
        }
        auto const& stops = stopsIt->toArray();
        for (auto const& stopJsonVal : stops) {
            if (!stopJsonVal.isArray()) {
                qFatal("Bleh");
            }
            auto const& stopJsonArr = stopJsonVal.toArray();
            if (stopJsonArr.size() != 2) {
                qFatal("Bleh");
            }

            std::pair<int, std::array<float, 4>> outStop;

            auto const& stopIndex = stopJsonArr[0];
            if (!stopIndex.isDouble()) {
                qFatal("Bleh");
            }
            outStop.first = stopIndex.toInt();

            // Parse the color
            auto const& stopColor = stopJsonArr[1];
            if (!stopColor.isString()) {
                qFatal("Bleh");
            }
            auto parsedColorOpt = parseColorFromString(stopColor.toString());
            if (!parsedColorOpt.has_value() || !parsedColorOpt.value().isValid()) {
                qFatal("Bleh");
            }
            parsedColorOpt.value().getRgbF(
                &outStop.second[0],
                &outStop.second[1],
                &outStop.second[2],
                &outStop.second[3]);

            m_fillColorStops.push_back(outStop);
        }
    } else if (jsonIt->isString()) {
        // Try to parse the string as a color.
        auto parsedColorOpt = parseColorFromString(jsonIt->toString());
        if (!parsedColorOpt.has_value()) {
            qFatal("Unable to parse background color from string.");
        }
        std::pair<int, std::array<float, 4>> outStop = { 0, { } };
        parsedColorOpt.value().getRgbF(
            &outStop.second[0],
            &outStop.second[1],
            &outStop.second[2],
            &outStop.second[3]);

        m_fillColorStops.push_back(outStop);
    } else {
        qFatal("Unknown color type.");
    }
}

void FillLayerStyle::loadOpacity(QJsonObject const& paintJson) {
    auto const& jsonIt = paintJson.find("fill-opacity");
    if (jsonIt == paintJson.end()) {
        // Since it's optional, we just insert the default value of one into the
        // stops.
        m_opacityFound = false;
        return;
    }

    m_opacityFound = true;
    if (jsonIt->isObject()) {
        // The opacity is expressed as an object containing stops.

        m_usingOpacityStops = true;

        auto const& json = jsonIt->toObject();
        auto const& stopsIt = json.find("stops");
        if (stopsIt == json.end()) {
            qFatal("Bleh");
        }
        if (!stopsIt->isArray()) {
            qFatal("Bleh");
        }
        auto const& stops = stopsIt->toArray();
        for (auto const& stopJsonVal : stops) {
            if (!stopJsonVal.isArray()) {
                qFatal("Bleh");
            }
            auto const& stopJsonArr = stopJsonVal.toArray();
            if (stopJsonArr.size() != 2) {
                qFatal("Bleh");
            }

            std::pair<int, float> outStop;

            auto const& stopIndexJsonVal = stopJsonArr[0];
            if (!stopIndexJsonVal.isDouble()) {
                qFatal("Bleh");
            }
            outStop.first = stopIndexJsonVal.toInt();

            // Parse the color
            auto const& stopValueJsonVal = stopJsonArr[1];
            if (!stopValueJsonVal.isDouble()) {
                qFatal("Bleh");
            }
            outStop.second = stopValueJsonVal.toDouble();

            m_opacityStops.push_back(outStop);
        }
    } else if (jsonIt->isDouble()) {
        m_usingOpacityStops = true;
        m_opacityStops.push_back({
            0,
            jsonIt->toDouble()});
    } else if (jsonIt->isArray()){
        // We expect this to be an expression. It should return a double with
        // a default value of 1.

        m_usingOpacityStops = false;

        // We should ordinary validate that it's a valid expression
        // that we can actually resolve.
        m_opacityExpression = jsonIt->toArray();
    }
}

std::unique_ptr<FillLayerStyle> FillLayerStyle::fromJson(const QJsonObject &json)
{
    auto returnLayerPtr = std::make_unique<FillLayerStyle>();
    auto& layer = *returnLayerPtr;
    initAbstractMembers(layer, json);

    auto const& paintJsonIt = json.find("paint");
    if (paintJsonIt == json.end()) {
        qFatal("Couldn't find paint object within background layer");
    }
    if (!paintJsonIt->isObject()) {
        qFatal("Background layer property 'paint' is not an object");
    }
    auto const& paintJson = paintJsonIt->toObject();

    layer.loadFillColor(paintJson);

    layer.loadOpacity(paintJson);

    auto const& fillTranslateIt = paintJson.find("fill-translate");
    if (fillTranslateIt == paintJson.end()) {
        layer.m_translateStops.push_back({ 0, {} });
    } else {
        if (!fillTranslateIt->isObject()) {
            qFatal("");
        }
        auto const& fillTranslate = fillTranslateIt->toObject();

        auto const& stopsIt = fillTranslate.find("stops");
        if (stopsIt == fillTranslate.end()) {
            qFatal("Bleh");
        }
        if (!stopsIt->isArray()) {
            qFatal("Bleh");
        }
        auto const& stops = stopsIt->toArray();
        for (auto const& item : stops) {
            if (!item.isArray()) {
                qFatal("Bleh");
            }
            auto const& stop = item.toArray();
            if (stop.size() != 2) {
                qFatal("Bleh");
            }
            std::pair<int, QVector2D> outStop;
            auto const& stopIndex = stop[0];
            if (!stopIndex.isDouble()) {
                qFatal("Bleh");
            }
            outStop.first = stopIndex.toInt();

            // Parse the 2d vec
            auto const& stopValue = stop[1];
            if (!stopValue.isArray()) {
                qFatal("Bleh");
            }
            auto const& stopValueArr = stopValue.toArray();
            if (stopValueArr.size() != 2) {
                qFatal("");
            }
            // Iterate over the array and grab the floats.)
            for (int i = 0; i < stopValueArr.size(); i++) {
                auto const& arrVal = stopValueArr[i];
                if (!arrVal.isDouble()) {
                    qFatal("");
                }
                outStop.second[i] = arrVal.toDouble();
            }

            layer.m_translateStops.push_back(outStop);
        }
    }

    return returnLayerPtr;
}

QColor FillLayerStyle::getFillColor(
    Evaluator::FeatureGeometryType featGeomType,
    const std::map<QString, QVariant>& featureMetaData,
    int mapZoom,
    double vpZoom) const
{
    auto tempColor = interpolateStops(
        QSpan{ m_fillColorStops.data(), (int)m_fillColorStops.size()},
        mapZoom);


    if (m_opacityFound) {
        float opacity = 1;

        if (m_usingOpacityStops) {
            opacity = interpolateStops(
                QSpan{ m_opacityStops.data(), (qsizetype)m_opacityStops.size()},
                mapZoom);
        } else {
            auto const& expressionResult = Evaluator::resolveExpression(
                m_opacityExpression,
                featGeomType,
                featureMetaData,
                mapZoom,
                vpZoom);
            if (!expressionResult.isValid()) {
                qFatal("");
            }

            if (expressionResult.typeId() == QMetaType::LongLong) {
                opacity = (float)expressionResult.toLongLong();
            } else {
                qFatal("");
            }
        }

        tempColor[3] = opacity;
    }

    return QColor::fromRgbF(
        tempColor[0],
        tempColor[1],
        tempColor[2],
        tempColor[3]);
}

QVector2D FillLayerStyle::getTranslation(
    const std::map<QString, QVariant>& featureMetaData,
    int mapZoom,
    double vpZoom) const
{
    return interpolateStops(
        QSpan{ m_translateStops.data(), (int)m_translateStops.size()},
        mapZoom);
}

std::unique_ptr<NotImplementedStyle> NotImplementedStyle::fromJson(const QJsonObject &json)
{
    std::unique_ptr<NotImplementedStyle> returnLayerPtr = std::make_unique<NotImplementedStyle>();
    return returnLayerPtr;
}


