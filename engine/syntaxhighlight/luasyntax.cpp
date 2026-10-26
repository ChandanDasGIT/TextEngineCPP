#include "luasyntax.h"

const LanguageDef& luaSyntax() {
    static const LanguageDef d = [] {
        LanguageDef l;
        l.name = "Lua";
        l.extensions = {"lua", "rockspec"};
        l.lineComment = "--";
        l.quotes = "\"'";
        l.longBrackets = true;                 // [[ ... ]] strings and --[[ ... ]] comments
        l.shebang = true;
        l.plainWordsAreVariables = true;       // every other word gets the variable colour
        l.fieldAccess = true;                  // obj.name is a property, obj.name( is a function
        l.propertyEquals = true;               // name = value in a table is a property
        l.keywords = {"and","break","do","else","elseif","end","for","function","goto","if","in","local",
            "not","or","repeat","return","then","until","while"};
        l.types = {"string","table","math","io","os","coroutine","utf8","debug","package"};
        l.constants = {"true", "false", "nil", "self", "_G", "_VERSION", "_ENV"};
        l.builtins = {"print","pairs","ipairs","type","tostring","tonumber","require","pcall","xpcall","error",
            "assert","setmetatable","getmetatable","select","next","rawget","rawset","rawequal","rawlen",
            "unpack","load","loadfile","dofile","collectgarbage"};
        return l;
    }();
    return d;
}
