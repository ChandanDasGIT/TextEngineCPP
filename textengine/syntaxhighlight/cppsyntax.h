#pragma once
#include "languagedef.h"

const LanguageDef& cppSyntax();
inline const bool cppSyntaxRegistered = registerLanguage(cppSyntax());
