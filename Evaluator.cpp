// Copyright (c) 2024 Cecilia Norevik Bratlie, Nils Petter Skålerud, Eimen Oueslati
// SPDX-License-Identifier: MIT

#include "Evaluator.h"

Evaluator::ExpressionOpFnT getExpressonFunction(const QString& op) {
    using namespace Evaluator;
    if (op == "==") { return compare; }
    if (op == "!=") { return compare; }
    if (op == "in") { return in; }
    /*


    if (op == "all") { return all; }
    if (op == "case") { return case_; }
    if (op == "coalesce") { return coalesce; }
    if (op == "get") { return get; }
    if (op == ">") { return greater; }
    if (op == "has") { return has; }
    if (op == "in") { return in; }
    if (op == "interpolate") { return interpolate; }
    if (op == "match") { return match; }
    */
    return nullptr;
}

QVariant Evaluator::resolveExpression(
    const QJsonArray &expression,
    const std::map<QString, QVariant>& metaData,
    int mapZoomLevel,
    float vpZoomLevel)
{
    // Check for valid expression.
    if (expression.empty()) {
        qFatal("");
        return {};
    }


    // Extract the operation keyword from the expression.
    auto operation = expression.begin()->toString();
    // All expressions (except !=) can start with an optional "!"
    // Let's check for it.
    bool negated = operation.startsWith("!") && operation != "!=";
    if (negated) {
        operation = operation.sliced(1);
    }

    auto opFn = getExpressonFunction(operation);
    if (opFn == nullptr) {
        // Didn't find the right expression, can't parse.
        qFatal("");
        return {};
    }
    return opFn(expression, metaData, mapZoomLevel, vpZoomLevel);

    // Return an invalid QVariant in case the expression was invalid or not supported.
    qFatal("");
    return {};
}

QVariant Evaluator::compare(
    const QJsonArray &array,
    const std::map<QString, QVariant>& metaData,
    int mapZoomLevel,
    float vpZoomLevel)
{
    if (array.size() != 3) {
        qFatal("");
        return {};
    }

    QVariant operand1;
    QVariant operand2;

    if (array.at(1).isArray()) {
        // Check if the operation opperand is a simple value or an expression in itself
        // that will need resolving to extract the value.
        static QJsonArray operand1Arr = array.at(1).toArray();
        QString temp = resolveExpression(
           operand1Arr,
           metaData,
           mapZoomLevel,
           vpZoomLevel).toString();
        if (temp == "$type") {
            qFatal("Bleh");
            return {};
            // "type" is not a part of the feature's metadata so it is a special case.
            //operand1 = getType(feature);
        }
        else {
            auto tempValueIt = metaData.find(temp);
            operand1 = tempValueIt != metaData.end() ? tempValueIt->second : QVariant();
        }
    } else {
        QString temp = array.at(1).toString();
        if (temp == "$type") {
            qFatal("Bleh");
            return {};
            // Type is not a part of the feature's metadata so it is a special case.
            //operand1 = getType(feature);
        } else {
            auto tempValueIt = metaData.find(temp);
            operand1 = tempValueIt != metaData.end() ? tempValueIt->second : QVariant();
        }
    }

    operand2 = array.at(2).toVariant();
    //Check which operation this expression contains, and return the result of the comparison.
    if(array.at(0).toString() == "!=")
        return operand1 != operand2;
    else
        return operand1 == operand2;
}

QVariant Evaluator::in(
    const QJsonArray &array,
    const std::map<QString, QVariant>& metaData,
    int mapZoomLevel,
    float vpZoomLevel)
{
    QString keyword = array.at(1).toString();

    auto const& keywordValueIt = metaData.find(keyword);
    if (keywordValueIt == metaData.end()) {
        return false;
    }
    auto const& value = keywordValueIt->second;
    // The range of values to be checked is in the array from elemet 2 to n.
    bool temp = array.toVariantList().sliced(2).contains(value);
    //Check for negation.
    bool startsWithNot = array.first().toString().startsWith("!");
    if (startsWithNot)
        temp = !temp;

    return temp;
}

/*
QVariant Evaluator::get(
    const QJsonArray& array,
    const std::map<QString, QVariant>& metaData,
    int mapZoomLevel,
    float vpZoomLevel)
{
    QString property = array.at(1).toString();
    auto valueIt = metaData.find(property);
    if (valueIt != metaData.end()) {
        return valueIt->second;
    }
    else {
        return {};
    }
}

QVariant Evaluator::has(
    const QJsonArray &array,
    const std::map<QString, QVariant>& metaData,
    int mapZoomLevel,
    float vpZoomLevel)
{
    QString property = array.at(1).toString();
    return metaData.find(property) != metaData.end();
}



QVariant Evaluator::greater(
    const QJsonArray &array,
    const QMap<QString, QVariant>& metaData,
    int mapZoomLevel,
    float vpZoomLevel)
{
    QVariant operand1;
    QVariant operand2;
    if (array.at(1).isArray()) {
        //If the operand is an expression, resolve it to get the operand value.
        static QJsonArray operand1Arr = array.at(1).toArray();
        operand1 = resolveExpression(operand1Arr, metaData, mapZoomLevel, vpZoomLevel);
    } else {
        operand1 = array.at(1).toVariant();
    }

    if (array.at(2).isArray()){
        // If the operand is an expression, resolve it to get the operand value.
        static QJsonArray operand2Arr = array.at(2).toArray();
        operand2 = resolveExpression(
            operand2Arr,
            metaData,
            mapZoomLevel,
            vpZoomLevel);
    } else {
        operand2 = array.at(2).toVariant();
    }

    // The operand types that can be compared are numeric values or strings.
    if (operand1.typeId() == QMetaType::QString)
        return operand1.toString() > operand2.toString();
    else
        return operand1.toDouble() > operand2.toDouble();
}

QVariant Evaluator::all(
    const QJsonArray &array,
    const QMap<QString, QVariant>& metaData,
    int mapZoomLevel,
    float vpZoomLevel)
{
    // Loop over all the expressions and check that they evaluate to true.
    for (int i = 1; i <= array.size() - 1; i++){
        QJsonArray expressionArray = array.at(i).toArray();
        bool matches = !resolveExpression(
            expressionArray,
            metaData,
            mapZoomLevel,
            vpZoomLevel)
            .toBool();
        if (matches) {
            return false;
        }
    }
    return true;
}

QVariant Evaluator::case_(
    const QJsonArray &array,
    const QMap<QString, QVariant>& metaData,
    int mapZoomLevel,
    float vpZoomLevel)
{
    // Loop over the array elements from 1 to n - 1
    // (element 0 contains the operation keyword and element n contains the fallback value)
    for (int i = 1; i < array.size() - 2; i += 2){
        if (array.at(i).isArray()){
            QJsonArray expression = array.at(i).toArray();
            // If the current expression being resolved evaluated to true,
            // return its corresponding output (the values right after it).
            if (resolveExpression(expression, metaData, mapZoomLevel, vpZoomLevel).toBool()) {
                return array.at(i + 1).toVariant();
            }
        }
    }
    // If the loop ends without returning, return the fallback value at index n.
    return array.last().toVariant();
}

QVariant Evaluator::coalesce(
    const QJsonArray &array,
    const QMap<QString, QVariant>& metaData,
    int mapZoomLevel,
    float vpZoomLevel)
{
    // Loop over the expression array and return the first valid QVariant.
    for(int i = 1; i <= array.size() - 1; i++){
        QJsonArray expression = array.at(i).toArray();
        auto returnVariant = resolveExpression(
            expression,
            metaData,
            mapZoomLevel,
            vpZoomLevel);
        if (returnVariant.isValid()) {
            return returnVariant;
        }
    }
    return {};
}

QVariant Evaluator::match(
    const QJsonArray &array,
    const QMap<QString, QVariant>& metaData,
    int mapZoomLevel,
    float vpZoomLevel)
{
    // Extract the label to be used for the checks.
    QJsonArray expression = array.at(1).toArray();
    QVariant input = resolveExpression(
        expression,
        metaData,
        mapZoomLevel,
        vpZoomLevel);

    // Loop over the array checking which value matches the input and return its corresponding output.
    // The elements from 2 to n-2 are looped over because the first two elements contain the expression keyword and
    // the operation label, and the last element contains the fallback value.
    for (int i = 2; i < array.size() - 2; i += 2) {
        if (array.at(i).isArray()) {
            if (array.at(i).toArray().toVariantList().contains(input)) {
                if (array.at(i + 1).isArray())
                    return resolveExpression(array.at(i + 1).toArray(), metaData, mapZoomLevel, vpZoomLevel);
                else
                    return array.at(i + 1).toVariant();      
            }
        } else if (input == array.at(i).toVariant()) {
            if(array.at(i + 1).isArray())
                return resolveExpression(array.at(i + 1).toArray(), metaData, mapZoomLevel, vpZoomLevel);
            else
                return array.at(i + 1).toVariant();
        }
    }
    // If the loop ends without returning, return the fallback value at index n.
    return array.last().toVariant();
}

static float lerp(QPair<float, float> stop1, QPair<float, float> stop2, int currentZoom)
{
    float lerpedValue = stop1.second + (currentZoom - stop1.first)*(stop2.second - stop1.second)/(stop2.first - stop1.first);
    return lerpedValue;
}

QVariant Evaluator::interpolate(
    const QJsonArray &array,
    const QMap<QString, QVariant>& metaData,
    int mapZoomLevel,
    float vpZoomLevel)
{
    // Loop over the values array starting at index 3 and find the two pairs that the value falls between.
    // Start at index 3 because element 0 contains the operation keyword, element 1 contains the type of interpolation,
    // and element 2 contains the name of the value for the interpolation.
    if (mapZoomLevel <= array.at(3).toDouble()) {
        // In case the value is less that the smallest element
        if (array.at(4).isArray())
            return resolveExpression(array.at(4).toArray(), metaData, mapZoomLevel, vpZoomLevel);
        else
            return array.at(4).toDouble();
    } else if (mapZoomLevel >= array.at(array.size()-2).toDouble()) {
        // In case the value is greated than the largest element.
        if (array.last().isArray())
            return resolveExpression(array.last().toArray(), metaData, mapZoomLevel, vpZoomLevel);
        else
            return array.last().toDouble();
    } else {
        // In case the value falls between two elements.
        int index = 3;
        while(mapZoomLevel > array.at(index).toDouble() && index < array.size())
            index += 2;

        // Set the input values to lerp from and declare output values.
        float stopInput1 = array.at(index-2).toDouble();
        float stopInput2 = array.at(index).toDouble();
        float stopOutput1;
        float stopOutput2;

        // Update the stop values to use for lerping.
        if (array.at(index - 1).isArray())
            stopOutput1 = resolveExpression(array.at(index - 1).toArray(), metaData, mapZoomLevel, vpZoomLevel).toFloat();
        else
            stopOutput1 = array.at(index - 1).toDouble();

        if (array.at(index + 1).isArray())
            stopOutput2 = resolveExpression(array.at(index + 1).toArray(), metaData, mapZoomLevel, vpZoomLevel).toFloat();
        else
            stopOutput2 = array.at(index + 1).toDouble();

        return lerp(QPair<float, float>(stopInput1,stopOutput1), QPair<float, float>(stopInput2,stopOutput2), mapZoomLevel);
    }
}
*/









