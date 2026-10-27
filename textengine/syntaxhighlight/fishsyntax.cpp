#include "fishsyntax.h"

const LanguageDef& fishSyntax() {
    static const LanguageDef d = [] {
        LanguageDef l;
        l.name = "fish";
        l.extensions = {"fish"};
        l.lineComment = "#";
        l.lineCommentAtBoundary = true;
        l.quotes = "\"'";
        l.multiLineQuotes = "\"'";
        l.variableSigil = '$';
        l.shebang = true;
        l.callsAreFunctions = false;
        l.keywords = {"if","else","end","for","while","function","switch","case","begin","and","or","not",
            "in","return","break","continue"};
        l.builtins = {"echo","set","test","string","math","contains","count","command","builtin","status",
            "abbr","alias","cd","read","source","printf","exit","eval","exec","type","set_color","functions",
            "argparse","commandline","complete","emit","bind","jobs","wait","random","true","false"};
        return l;
    }();
    return d;
}
