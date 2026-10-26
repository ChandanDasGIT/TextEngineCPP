#pragma once
#include "languagedef.h"

const LanguageDef& fishSyntax();
inline const bool fishSyntaxRegistered = registerLanguage(fishSyntax());
