#include "jssyntax.h"

const LanguageDef& jsSyntax() {
    static const LanguageDef d = [] {
        LanguageDef l;
        l.name = "JavaScript";
        l.extensions = {"js", "mjs", "cjs", "jsx"};
        l.lineComment = "//";
        l.blockOpen = "/*"; l.blockClose = "*/";
        l.quotes = "\"'`";
        l.multiLineQuotes = "`";
        l.identExtra = "$";
        l.shebang = true;
        l.capitalizedIsType = true;
        l.propertyColon = true;
        l.plainWordsAreVariables = true;
        l.keywords = {"async","await","break","case","catch","class","const","continue","debugger","default",
            "delete","do","else","export","extends","finally","for","function","if","import","in","instanceof",
            "let","new","of","return","static","super","switch","this","throw","try","typeof","var","void",
            "while","with","yield"};
        l.types = {"JSON", "Math"};
        l.constants = {"true", "false", "null", "undefined", "NaN", "Infinity"};
        l.builtins = {"console","require","module","exports","process","window","document","globalThis",
            "setTimeout","setInterval","clearTimeout","clearInterval","parseInt","parseFloat","fetch"};
        return l;
    }();
    return d;
}
