#include "qmlsyntax.h"

const LanguageDef& qmlSyntax() {
    static const LanguageDef d = [] {
        LanguageDef l;
        l.name = "QML";
        l.extensions = {"qml"};
        l.lineComment = "//";
        l.blockOpen = "/*"; l.blockClose = "*/";
        l.quotes = "\"'`";
        l.multiLineQuotes = "`";
        l.capitalizedIsType = true;
        l.propertyColon = true;
        l.idKeyword = "id";
        l.keywords = {"import","pragma","property","readonly","required","default","signal","function","alias",
            "component","on","as","if","else","for","while","do","switch","case","break","continue","return",
            "var","let","const","new","delete","typeof","in","of","try","catch","finally","throw","this","id","enum"};
        l.types = {"int","bool","real","double","string","url","color","date","list","point","size","rect","font"};
        l.constants = {"true", "false", "null", "undefined", "NaN", "Infinity", "parent"};
        l.builtins = {"console"};
        return l;
    }();
    return d;
}
