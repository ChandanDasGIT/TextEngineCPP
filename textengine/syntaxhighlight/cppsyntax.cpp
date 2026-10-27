#include "cppsyntax.h"

const LanguageDef& cppSyntax() {
    static const LanguageDef d = [] {
        LanguageDef l;
        l.name = "C/C++";
        l.extensions = {"c", "h", "cpp", "cc", "cxx", "hpp", "hh", "hxx", "ino"};
        l.lineComment = "//";
        l.blockOpen = "/*"; l.blockClose = "*/";
        l.quotes = "\"'";
        l.preprocessor = '#';
        l.digitSeparator = '\'';
        l.capitalizedIsClass = true;
        l.scopeQualifierIsClass = true;
        l.templateCalls = true;
        l.classIntroducers = {"class", "struct", "enum", "union", "namespace", "typename"};
        l.keywords = {"alignas","alignof","and","asm","auto","break","case","catch","class","compl","concept",
            "const","consteval","constexpr","constinit","const_cast","continue","co_await","co_return","co_yield",
            "decltype","default","delete","do","dynamic_cast","else","enum","explicit","export","extern","final",
            "for","friend","goto","if","inline","mutable","namespace","new","noexcept","not","operator","or",
            "override","private","protected","public","register","reinterpret_cast","requires","return","sizeof",
            "static","static_assert","static_cast","struct","switch","template","this","thread_local","throw",
            "try","typedef","typeid","typename","union","using","virtual","volatile","while","xor"};
        l.types = {"void","bool","char","char8_t","char16_t","char32_t","wchar_t","short","int","long","float",
            "double","signed","unsigned","size_t","ssize_t","ptrdiff_t","intptr_t","int8_t","int16_t","int32_t",
            "int64_t","uint8_t","uint16_t","uint32_t","uint64_t","string","vector","unique_ptr","shared_ptr","optional"};
        l.constants = {"true", "false", "nullptr", "NULL"};
        return l;
    }();
    return d;
}
