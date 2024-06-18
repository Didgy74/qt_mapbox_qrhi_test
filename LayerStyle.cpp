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
QColor interpolateStops(QSpan<std::pair<int, QColor> const> list, int currentZoom)
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

std::unique_ptr<StyleSheet::AbstractLayerStyle> StyleSheet::AbstractLayerStyle::fromJson(const QJsonObject &json)
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

    // Set layer properties.
    AbstractLayerStyle *newLayer = returnLayerPtr.get();
    newLayer->m_id = json.value("id").toString();
    newLayer->m_source = json.value("source").toString();
    newLayer->m_sourceLayer = json.value("source-layer").toString();
    newLayer->m_minZoom = json.value("minzoom").toInt(0);
    newLayer->m_maxZoom = json.value("maxzoom").toInt(24);

    auto const& layoutRefIt = json.find("layout");
    if (layoutRefIt == json.end()) {
        qFatal("");
    }
    if (!layoutRefIt->isObject()) {
        qFatal("");
    }
    auto const& layout = layoutRefIt->toObject();
    auto const& visibilityRefIt = layout.find("visibility");
    if (visibilityRefIt == layout.end()) {
        newLayer->m_visibility = true;
    } else {
        if (!visibilityRefIt->isString()) {
            qFatal("");
        }
        auto const& visibilityString = visibilityRefIt->toString();
        if (visibilityString == "visible") {
            newLayer->m_visibility = true;
        } else if (visibilityString == "none") {
            newLayer->m_visibility = false;
        } else {
            qFatal("");
        }
    }

    if(json.contains("filter"))
        newLayer->m_filter = json.value("filter").toArray();

    return returnLayerPtr;
}

std::optional<StyleSheet> StyleSheet::fromJson(const QJsonDocument &styleSheetJson)
{
    StyleSheet out;

    QJsonObject styleSheetObject = styleSheetJson.object();
    out.m_id = styleSheetObject.value("id").toString();
    out.m_version = styleSheetObject.value("version").toInt();
    out.m_name = styleSheetObject.value("name").toString();

    QJsonArray layers = styleSheetObject.value("layers").toArray();
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
    auto* returnLayer = returnLayerPtr.get();

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
    if (!colorIt->isObject()) {
        qFatal("Bleh");
    }
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

        returnLayer->colorStops.push_back(outStop);
    }

    return returnLayerPtr;
}

QColor BackgroundLayerStyle::getColor(
    int mapZoom) const
{
    return interpolateStops(
        { colorStops.data(), (qsizetype)colorStops.size()},
        mapZoom);
}

std::unique_ptr<FillLayerStyle> FillLayerStyle::fromJson(const QJsonObject &jsonObj)
{
    std::unique_ptr<FillLayerStyle> returnLayerPtr = std::make_unique<FillLayerStyle>();
    FillLayerStyle* returnLayer = returnLayerPtr.get();

    //Parsing layout properties.
    QJsonObject layout = jsonObj.value("layout").toObject();
    //visibility property is parsed in:
    // AbstractLayerStyle* AbstractLayerStyle::fromJson(const QJsonObject &jsonObj)

    //Parsing paint properties.
    QJsonObject paint = jsonObj.value("paint").toObject();

    // Get the antialiasing property from the style sheet or set it to true as a default.
    /*
    returnLayer->m_antialias = paint.contains("fill-antialias")
                                   ? paint.value("fill-antialias").toBool() : true;
    */

    if (paint.contains("fill-color")) {
        const QJsonValue& fillColor = paint.value("fill-color");
        if (fillColor.isObject()) {
            // Case where the property is an object that has "stops".
            QList<QPair<int, QColor>> stops;
            QJsonArray arr = fillColor.toObject().value("stops").toArray();

            // Loop over all stops and append a pair of <zoomStop, colorStop> data to `stops`.
            for (QJsonValueConstRef stop : arr) {
                int zoomStop = stop.toArray().first().toInt();

                auto colorOpt = parseColorFromString(stop.toArray().last().toString());
                if (!colorOpt.has_value()) {
                    qFatal("");
                }
                stops.append(QPair<int, QColor>(zoomStop, colorOpt.value()));
            }
            returnLayer->m_fillColor.setValue(stops);
        } else if (fillColor.isArray()) {
            // Case where the property is an expression.
            returnLayer->m_fillColor.setValue(fillColor.toArray());
        } else {
            // Case where the property is a color value.
            auto colorOpt = parseColorFromString(fillColor.toString());
            returnLayer->m_fillColor.setValue(colorOpt.value());
        }
    }

    if (paint.contains("fill-opacity")) {
        QJsonValue fillOpacity = paint.value("fill-opacity");
        if (fillOpacity.isObject()) {
            // Case where the property is an object that has "stops".
            QList<QPair<int, float>> stops;
            QJsonArray arr = fillOpacity.toObject().value("stops").toArray();

            // Loop over all stops and append a pair of <zoomStop, opacityStop> to `stops`.
            for (QJsonValueConstRef stop : arr) {
                int zoomStop = stop.toArray().first().toInt();
                float opacityStop = stop.toArray().last().toDouble();
                stops.append(QPair<int, float>(zoomStop, opacityStop));
            }
            returnLayer->m_fillOpacity.setValue(stops);
        } else if (fillOpacity.isArray()) {
            // Case where the property is an expression.
            returnLayer->m_fillOpacity.setValue(fillOpacity.toArray());
        } else {
            // Case where the property is a numeric value.
            returnLayer->m_fillOpacity.setValue(fillOpacity.toDouble());
        }
    }

    if (paint.contains("fill-outline-color")) {
        QJsonValue fillOutlineColor = paint.value("fill-outline-color");
        if (fillOutlineColor.isObject()) {
            // Case where the property is an object that has "stops".
            QList<QPair<int, QColor>> stops;
            QJsonArray arr =fillOutlineColor.toObject().value("stops").toArray();

            // Loop over all stops and append a pair of <zoomStop, colorStop> to `stops`.
            for (QJsonValueConstRef stop: arr) {
                int zoomStop = stop.toArray().first().toInt();

                QColor colorStop = parseColorFromString(stop.toArray().last().toString()).value();
                stops.append(QPair<int, QColor>(zoomStop, colorStop));
            }
            returnLayer->m_fillOutlineColor.setValue(stops);
        } else if (fillOutlineColor.isArray()) {
            // Case where the property is an expression.
            returnLayer->m_fillOutlineColor.setValue(fillOutlineColor.toArray());
        } else {
            // Case where the property is a color value.
            returnLayer->m_fillOutlineColor.setValue(parseColorFromString(fillOutlineColor.toString()).value());
        }
    }

    return returnLayerPtr;
}

QColor FillLayerStyle::getFillColor(
    const std::map<QString, QVariant>& featureMetaData,
    int mapZoom,
    double vpZoom) const
{
    QVariant temp;
    if (m_fillColor.isNull()) {
        // This should be considered an error.
        // The default color in case no color is provided by the style sheet.
        temp = QColor(Qt::GlobalColor::black);
    } else if (m_fillColor.typeId() != QMetaType::Type::QColor &&
               m_fillColor.typeId() != QMetaType::Type::QJsonArray) {
        QList<QPair<int, QColor>> stops = m_fillColor.value<QList<QPair<int, QColor>>>();
        if (stops.size() == 0) {
            temp = QColor(Qt::GlobalColor::black);
        } else {
            temp = QVariant(getStopOutput(stops, mapZoom));
        }
    } else {
        temp = m_fillColor;
    }


    QVariant colorVariant = temp;
    QColor color;
    // The layer style might return an expression, that must be resolved.
    if (colorVariant.typeId() == QMetaType::Type::QJsonArray){
        color = Evaluator::resolveExpression(
            colorVariant.toJsonArray(),
            featureMetaData,
            mapZoom,
            vpZoom)
            .value<QColor>();
    } else {
        color = colorVariant.value<QColor>();
    }
    return color;
}

std::unique_ptr<NotImplementedStyle> NotImplementedStyle::fromJson(const QJsonObject &json)
{
    std::unique_ptr<NotImplementedStyle> returnLayerPtr = std::make_unique<NotImplementedStyle>();
    return returnLayerPtr;
}


