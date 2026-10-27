#pragma once
#include <string>
#include <vector>

enum class Token : unsigned char {
    None, Keyword, Type, Function, Number, String, Comment,
    Preprocessor, Constant, Variable, Property, Attribute,
    Class,        // class and namespace names (C++)
    Identifier,   // QML object ids
    Modifier      // Rust pub
};

// Everything the scanner needs to know about one language.
struct LanguageDef {
    std::string name;
    std::vector<std::string> extensions;   // lower case, no dot: "cpp"
    std::vector<std::string> fileNames;    // whole file names: "CMakeLists.txt"

    std::string lineComment;               // "//"  ("" = none)
    bool lineCommentAtBoundary = false;    // shell style: '#' only starts a comment after whitespace or ; | & (
    std::string blockOpen, blockClose;     // "/*" "*/"  ("" = none)
    bool nestedBlockComments = false;      // Rust

    std::string quotes;                    // characters that start a string: "\"'"
    std::string multiLineQuotes;           // quotes whose strings may continue on the next line
    std::string rawQuotes;                 // quotes without backslash escapes (shell '...')
    bool rawStrings = false;               // Rust r"..." r#"..."#
    bool lifetimes = false;                // Rust 'a and 'x' (handled instead of quotes for ')
    bool longBrackets = false;             // Lua: [[ ... ]] strings and --[[ ... ]] comments, also [==[ ... ]==]

    char preprocessor = 0;                 // '#' for C/C++
    std::vector<std::string> attributePrefixes;   // Rust: "#![" and "#["
    char variableSigil = 0;                // '$' for shell, fish and CMake
    std::string identExtra;                // extra identifier characters, e.g. "$" for JS
    char digitSeparator = 0;               // '\'' for C++
    bool shebang = false;                  // a first line starting with #! is highlighted
    bool caseInsensitive = false;          // CMake
    bool capitalizedIsType = false;        // Foo -> type, FOO -> constant
    bool callsAreFunctions = true;         // name( -> function
    bool macroBang = false;                // Rust println!
    bool propertyColon = false;            // name: -> property (JS objects, QML)
    bool capitalizedIsClass = false;       // Foo -> class (like capitalizedIsType, but with the Class colour)
    bool scopeQualifierIsClass = false;    // name:: -> class or namespace (C++)
    bool templateCalls = false;            // name<T>( -> function (C++)
    bool plainWordsAreVariables = false;   // any other word -> variable (JavaScript)
    bool fieldAccess = false;              // obj.name -> property, or function when a '(' follows (Lua)
    bool propertyEquals = false;           // name = value at the start of a line or after { , ; -> property (Lua tables)
    bool foldByIndent = false;             // blocks are made by indentation (Python, YAML): folding follows indentation, not brackets

    std::string idKeyword;                 // QML: "id" - the name in "id: name" is coloured everywhere in the file
    std::vector<std::string> classIntroducers;   // words after which the next word is a class name
    std::vector<std::string> modifiers;    // words with their own colour: Rust pub
    std::vector<std::string> keywords, types, constants, builtins;
};

// Called by each language header. Returns true so it can initialise a variable.
bool registerLanguage(const LanguageDef& def);

// The registered language for a file name: whole file names first ("CMakeLists.txt"), then the extension.
// nullptr when no language matches. The highlighter and folding both use this, so a language is described once.
const LanguageDef* findLanguage(const std::string& fileName);
